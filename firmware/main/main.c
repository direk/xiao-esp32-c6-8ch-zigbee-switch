// SPDX-License-Identifier: MIT
#include <inttypes.h>
#include <string.h>
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "ha/esp_zigbee_ha_standard.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "control.h"

#define CLUSTER 0xfc00
#define ATTR_MODE 0x0001
#define ATTR_INVERT 0x0002
#define ATTR_CHANNEL_ROUTE 0x0012
#define ATTR_BINDING_STATUS 0x0013
#define ATTR_EVENT 0x0020
#define CHANNELS 4
#define COORDINATOR_EP 1
#define EXTERNAL_ANTENNA 1

// XIAO D0, D1, D2, D3. Contacts close to GND, never to mains.
static const gpio_num_t pins[CHANNELS] = {GPIO_NUM_0, GPIO_NUM_1, GPIO_NUM_2, GPIO_NUM_21};
static const char *TAG = "xiao4";
static struct {
    uint8_t version;
    uint8_t mode[CHANNELS];
    uint8_t invert[CHANNELS];
    uint16_t reserved; // Retain the v1 NVS layout; this field is unused.
} cfg = {.version = 1};
static nvs_handle_t nv;
static input_t inputs[CHANNELS];
static bool joined, dirty, rebase[CHANNELS];
static uint64_t dirty_at, boot_pressed_at;
static bool boot_held;
static uint32_t events[CHANNELS];
static uint8_t channel_route[CHANNELS] = {1, 1, 1, 1};
static uint8_t binding_status[CHANNELS]; // 0=unknown, 1=unbound, 2=bound.
static uint8_t bound_mask;
static bool bindings_known, scan_active;
static binding_scan_t binding_scan;
static esp_zb_zdo_mgmt_bind_param_t binding_request;
static uintptr_t scan_generation;
static uint64_t scan_started_ms, next_scan_ms;
static esp_zb_ieee_addr_t coordinator_ieee;
static bool coordinator_known;

static uint64_t now_ms(void) { return (uint64_t)esp_timer_get_time() / 1000; }
static bool active(unsigned i) { return (gpio_get_level(pins[i]) == 0) ^ (cfg.invert[i] != 0); }

static void set_attr(uint8_t ep, uint16_t id, void *value) {
    esp_zb_zcl_status_t s = esp_zb_zcl_set_attribute_val(ep, CLUSTER, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE, id, value, false);
    if (s != ESP_ZB_ZCL_STATUS_SUCCESS) ESP_LOGE(TAG, "Set attribute %04x failed: %d", id, s);
}

static void report(uint8_t ep, uint16_t id) {
    if (!joined) return;
    esp_zb_zcl_report_attr_cmd_t cmd = {
        .zcl_basic_cmd = {.dst_addr_u.addr_short = 0x0000, .dst_endpoint = COORDINATOR_EP, .src_endpoint = ep},
        .address_mode = ESP_ZB_APS_ADDR_MODE_16_ENDP_PRESENT,
        .clusterID = CLUSTER,
        .direction = ESP_ZB_ZCL_CMD_DIRECTION_TO_CLI,
        .dis_default_resp = 1,
        .attributeID = id,
    };
    esp_zb_zcl_report_attr_cmd_req(&cmd);
}

static void update_route(void) {
    for (unsigned i = 0; i < CHANNELS; ++i) {
        bool bound = (bound_mask & (1U << i)) != 0;
        uint8_t path = channel_uses_binding(bound) ? 0 : 1;
        uint8_t status = bindings_known ? (bound ? 2 : 1) : 0;
        if (channel_route[i] != path) {
            channel_route[i] = path;
            set_attr(i + 1, ATTR_CHANNEL_ROUTE, &channel_route[i]);
            report(i + 1, ATTR_CHANNEL_ROUTE);
        }
        if (binding_status[i] != status) {
            binding_status[i] = status;
            set_attr(i + 1, ATTR_BINDING_STATUS, &binding_status[i]);
            report(i + 1, ATTR_BINDING_STATUS);
            ESP_LOGI(TAG, "CH%u binding: %s", i + 1, bound ? "bound" : "unbound");
        }
    }
}

static void binding_table_received(const esp_zb_zdo_binding_table_info_t *table, void *context) {
    if (!scan_active || (uintptr_t)context != scan_generation) return;
    if (!table || table->status != ESP_ZB_ZDP_STATUS_SUCCESS) {
        scan_active = false; // Retain last complete snapshot on failure.
        ESP_LOGW(TAG, "Local binding table query failed");
        return;
    }
    esp_zb_ieee_addr_t own;
    esp_zb_get_long_address(own);
    uint8_t mask = 0;
    const esp_zb_zdo_binding_table_record_t *record = table->record;
    for (unsigned n = 0; n < table->count; ++n) {
        if (!record) { scan_active = false; return; }
        bool excluded = record->dst_addr_mode == 3 &&
            (!memcmp(record->dst_address.addr_long, own, sizeof(own)) ||
             (coordinator_known && !memcmp(record->dst_address.addr_long, coordinator_ieee, sizeof(own))));
        mask = binding_mask_add(mask, record->src_endp, record->cluster_id, record->dst_addr_mode,
                                !memcmp(record->src_address, own, sizeof(own)), excluded);
        record = record->next;
    }
    int state = binding_scan_page(&binding_scan, table->index, table->total, table->count, mask);
    if (state < 0) {
        scan_active = false;
        ESP_LOGW(TAG, "Inconsistent binding table snapshot; retrying");
    } else if (state == 0) {
        binding_request.start_index = binding_scan.next;
        esp_zb_zdo_binding_table_req(&binding_request, binding_table_received, (void *)scan_generation);
    } else {
        bound_mask = binding_scan.mask;
        bindings_known = true;
        scan_active = false;
        update_route();
    }
}

static void refresh_bindings(uint64_t now) {
    if (!joined) return;
    if (scan_active && now - scan_started_ms >= 6000) {
        scan_active = false;
        ++scan_generation; // Late callbacks cannot replace a newer snapshot.
    }
    if (scan_active || now < next_scan_ms) return;
    if (esp_zb_ieee_address_by_short(0, coordinator_ieee) == ESP_OK) coordinator_known = true;
    binding_scan = (binding_scan_t){0};
    binding_request = (esp_zb_zdo_mgmt_bind_param_t){.dst_addr = esp_zb_get_short_address(), .start_index = 0};
    scan_active = true;
    scan_started_ms = now;
    next_scan_ms = now + 1000;
    ++scan_generation;
    esp_zb_zdo_binding_table_req(&binding_request, binding_table_received, (void *)scan_generation);
}

static bool aps_indication(esp_zb_apsde_data_ind_t ind) {
    // Observe Bind/Unbind without consuming them; let the stack apply the change.
    if (ind.profile_id == 0 && (ind.cluster_id == 0x0021 || ind.cluster_id == 0x0022)) {
        next_scan_ms = now_ms() + 100;
    }
    return false;
}

static int check_value(uint16_t id, uint8_t ep, uint8_t *v) {
    if (!v || ep < 1 || ep > CHANNELS) return ESP_ZB_ZCL_STATUS_INVALID_VALUE;
    if (id == ATTR_MODE || id == ATTR_INVERT) return *v <= 1 ? ESP_ZB_ZCL_STATUS_SUCCESS : ESP_ZB_ZCL_STATUS_INVALID_VALUE;
    return ESP_ZB_ZCL_STATUS_READ_ONLY;
}

static void write_attr(uint8_t ep, uint16_t id, uint8_t *v, uint16_t manufacturer) {
    (void)manufacturer;
    if (check_value(id, ep, v) != ESP_ZB_ZCL_STATUS_SUCCESS) return;
    // Attribute storage can already contain the new value when this callback runs.
    // Never use comparison against that same storage to decide whether to persist.
    if (id == ATTR_MODE) {
        cfg.mode[ep - 1] = *v; rebase[ep - 1] = true;
    } else if (id == ATTR_INVERT) {
        cfg.invert[ep - 1] = *v; rebase[ep - 1] = true;
    }
    dirty = true; dirty_at = now_ms();
    ESP_LOGI(TAG, "Configuration write CH%u attr=0x%04x; persistence pending", ep, id);
}

static void button_action(unsigned i, int action) {
    if (!joined) { ESP_LOGW(TAG, "Not joined; channel %u ignored", i + 1); return; }
    // Decide once per edge. No retry through the alternative path and no replay.
    bool direct = channel_uses_binding((bound_mask & (1U << i)) != 0);
    if (!direct) {
        uint32_t sequence = ((events[i] >> 8) + 1) & 0xffffff;
        if (!sequence) sequence = 1;
        events[i] = (sequence << 8) | (uint8_t)action;
        set_attr(i + 1, ATTR_EVENT, &events[i]);
        report(i + 1, ATTR_EVENT); // Explicit coordinator unicast, never binding-addressed.
    } else {
        esp_zb_zcl_on_off_cmd_t cmd = {
            .zcl_basic_cmd.src_endpoint = i + 1,
            .address_mode = ESP_ZB_APS_ADDR_MODE_DST_ADDR_ENDP_NOT_PRESENT,
            .on_off_cmd_id = action,
        };
        esp_zb_zcl_on_off_cmd_req(&cmd); // Stack uses (source EP, cluster) binding entries.
    }
    ESP_LOGI(TAG, "CH%u command=%d via %s", i + 1, action, direct ? "binding" : "Z2M action");
}

// Runs on the Zigbee scheduler: all app state and stack calls share one context.
static void tick(uint8_t unused) {
    (void)unused;
    uint64_t now = now_ms();
    refresh_bindings(now);
    update_route();
    for (unsigned i = 0; i < CHANNELS; ++i) {
        if (rebase[i]) { input_init(&inputs[i], active(i), now); rebase[i] = false; }
        if (!bindings_known) continue; // Wait for the first local snapshot before choosing a path.
        int action = input_sample(&inputs[i], active(i), cfg.mode[i], now);
        if (action != ACTION_NONE) button_action(i, action);
    }
    if (dirty && now - dirty_at >= 1000) {
        esp_err_t err = nvs_set_blob(nv, "config", &cfg, sizeof(cfg));
        if (err == ESP_OK) err = nvs_commit(nv);
        if (err == ESP_OK) { dirty = false; ESP_LOGI(TAG, "Configuration saved to NVS"); }
        else { ESP_LOGE(TAG, "Configuration persistence failed: %s", esp_err_to_name(err)); dirty_at = now; }
    }
    if (!gpio_get_level(GPIO_NUM_9)) {
        if (!boot_held) { boot_held = true; boot_pressed_at = now; }
        if (now - boot_pressed_at >= 8000) {
            ESP_LOGW(TAG, "Factory reset: configuration, network and bindings");
            esp_err_t err = nvs_erase_key(nv, "config");
            ESP_ERROR_CHECK(err == ESP_ERR_NVS_NOT_FOUND ? ESP_OK : err);
            ESP_ERROR_CHECK(nvs_commit(nv));
            esp_zb_factory_reset();
            return;
        }
    } else boot_held = false;
    esp_zb_scheduler_alarm(tick, 0, 10);
}

static void commission(uint8_t mode) {
    esp_err_t err = esp_zb_bdb_start_top_level_commissioning(mode);
    if (err != ESP_OK) esp_zb_scheduler_alarm(commission, mode, 5000);
}

void esp_zb_app_signal_handler(esp_zb_app_signal_t *s) {
    esp_zb_app_signal_type_t type = *s->p_app_signal;
    switch (type) {
    case ESP_ZB_ZDO_SIGNAL_SKIP_STARTUP:
        commission(ESP_ZB_BDB_MODE_INITIALIZATION);
        break;
    case ESP_ZB_BDB_SIGNAL_DEVICE_FIRST_START:
    case ESP_ZB_BDB_SIGNAL_DEVICE_REBOOT:
        if (s->esp_err_status != ESP_OK) {
            esp_zb_scheduler_alarm(commission, ESP_ZB_BDB_MODE_INITIALIZATION, 5000);
        } else if (esp_zb_bdb_is_factory_new()) {
            commission(ESP_ZB_BDB_MODE_NETWORK_STEERING);
        } else {
            joined = true;
            ESP_LOGI(TAG, "Restored Zigbee network; bindings retained");
        }
        break;
    case ESP_ZB_BDB_SIGNAL_STEERING:
        if (s->esp_err_status == ESP_OK) {
            joined = true;
            ESP_LOGI(TAG, "Joined network, short address 0x%04x", esp_zb_get_short_address());
        } else {
            esp_zb_scheduler_alarm(commission, ESP_ZB_BDB_MODE_NETWORK_STEERING, 10000);
        }
        break;
    case ESP_ZB_ZDO_SIGNAL_LEAVE:
        joined = false;
        bindings_known = scan_active = coordinator_known = false;
        bound_mask = 0;
        ++scan_generation;
        next_scan_ms = 0;
        esp_zb_scheduler_alarm(commission, ESP_ZB_BDB_MODE_NETWORK_STEERING, 3000);
        break;
    default:
        ESP_LOGD(TAG, "Zigbee signal %s: %s", esp_zb_zdo_signal_to_string(type), esp_err_to_name(s->esp_err_status));
        break;
    }
}

static void add_custom(esp_zb_attribute_list_t *a, uint16_t id, uint8_t type, uint8_t access, void *value) {
    ESP_ERROR_CHECK(esp_zb_custom_cluster_add_custom_attr(a, id, type, access, value));
}

static void zigbee_task(void *arg) {
    (void)arg;
    ESP_ERROR_CHECK(esp_zb_aps_src_binding_table_size_set(32));
    esp_zb_cfg_t network = {
        .esp_zb_role = ESP_ZB_DEVICE_TYPE_ROUTER,
        .install_code_policy = false,
        .nwk_cfg.zczr_cfg.max_children = 10,
    };
    esp_zb_init(&network);
    esp_zb_ep_list_t *eps = esp_zb_ep_list_create();
    static char manufacturer[] = "\x08" "DIY-XIAO";
    static char model[] = "\x11" "XIAO-C6-4CH-FAIL1";
    const uint8_t rw = ESP_ZB_ZCL_ATTR_ACCESS_READ_WRITE;
    const uint8_t rr = ESP_ZB_ZCL_ATTR_ACCESS_READ_ONLY | ESP_ZB_ZCL_ATTR_ACCESS_REPORTING;
    for (uint8_t ep = 1; ep <= CHANNELS; ++ep) {
        esp_zb_on_off_switch_cfg_t defaults = ESP_ZB_DEFAULT_ON_OFF_SWITCH_CONFIG();
        esp_zb_cluster_list_t *clusters = esp_zb_on_off_switch_clusters_create(&defaults);
        esp_zb_attribute_list_t *basic = esp_zb_cluster_list_get_cluster(clusters, ESP_ZB_ZCL_CLUSTER_ID_BASIC, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);
        ESP_ERROR_CHECK(esp_zb_basic_cluster_add_attr(basic, ESP_ZB_ZCL_ATTR_BASIC_MANUFACTURER_NAME_ID, manufacturer));
        ESP_ERROR_CHECK(esp_zb_basic_cluster_add_attr(basic, ESP_ZB_ZCL_ATTR_BASIC_MODEL_IDENTIFIER_ID, model));
        esp_zb_attribute_list_t *custom = esp_zb_zcl_attr_list_create(CLUSTER);
        add_custom(custom, ATTR_MODE, ESP_ZB_ZCL_ATTR_TYPE_U8, rw, &cfg.mode[ep - 1]);
        add_custom(custom, ATTR_INVERT, ESP_ZB_ZCL_ATTR_TYPE_BOOL, rw, &cfg.invert[ep - 1]);
        add_custom(custom, ATTR_EVENT, ESP_ZB_ZCL_ATTR_TYPE_U32, rr, &events[ep - 1]);
        add_custom(custom, ATTR_CHANNEL_ROUTE, ESP_ZB_ZCL_ATTR_TYPE_U8, rr, &channel_route[ep - 1]);
        add_custom(custom, ATTR_BINDING_STATUS, ESP_ZB_ZCL_ATTR_TYPE_U8, rr, &binding_status[ep - 1]);
        ESP_ERROR_CHECK(esp_zb_cluster_list_add_custom_cluster(clusters, custom, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE));
        esp_zb_endpoint_config_t desc = {
            .endpoint = ep, .app_profile_id = ESP_ZB_AF_HA_PROFILE_ID,
            .app_device_id = ESP_ZB_HA_ON_OFF_SWITCH_DEVICE_ID, .app_device_version = 1,
        };
        ESP_ERROR_CHECK(esp_zb_ep_list_add_ep(eps, clusters, desc));
    }
    ESP_ERROR_CHECK(esp_zb_device_register(eps));
    ESP_ERROR_CHECK(esp_zb_zcl_custom_cluster_handlers_update((esp_zb_zcl_custom_cluster_handlers_t){
        .cluster_id = CLUSTER, .cluster_role = ESP_ZB_ZCL_CLUSTER_SERVER_ROLE,
        .check_value_cb = check_value, .write_attr_cb = write_attr,
    }));
    esp_zb_aps_data_indication_handler_register(aps_indication);
    esp_zb_set_primary_network_channel_set(ESP_ZB_TRANSCEIVER_ALL_CHANNELS_MASK);
    ESP_ERROR_CHECK(esp_zb_start(false));
    esp_zb_scheduler_alarm(tick, 0, 10);
    esp_zb_stack_main_loop();
}

void app_main(void) {
    esp_err_t err = nvs_flash_init();
    // Do not silently erase persisted network/configuration on NVS errors.
    ESP_ERROR_CHECK(err);
    ESP_ERROR_CHECK(nvs_open("xiao4", NVS_READWRITE, &nv));
    size_t size = sizeof(cfg);
    typeof(cfg) loaded;
    err = nvs_get_blob(nv, "config", &loaded, &size);
    if (err == ESP_OK && size == sizeof(cfg) && loaded.version == 1) {
        bool valid = true;
        for (unsigned i = 0; i < CHANNELS; ++i) valid &= loaded.mode[i] <= 1 && loaded.invert[i] <= 1;
        if (valid) cfg = loaded;
    }
    gpio_config_t output = {.pin_bit_mask = (1ULL << 3) | (1ULL << 14), .mode = GPIO_MODE_INPUT_OUTPUT};
    ESP_ERROR_CHECK(gpio_config(&output));
    gpio_set_level(GPIO_NUM_3, 0); // Enable RF switch on XIAO.
    gpio_set_level(GPIO_NUM_14, EXTERNAL_ANTENNA);
    ESP_LOGI(TAG, "Antenna: %s (RF enable=%d, select=%d)", EXTERNAL_ANTENNA ? "external" : "internal",
             gpio_get_level(GPIO_NUM_3), gpio_get_level(GPIO_NUM_14));
    gpio_config_t input = {
        .pin_bit_mask = (1ULL << 0) | (1ULL << 1) | (1ULL << 2) | (1ULL << 21) | (1ULL << 9),
        .mode = GPIO_MODE_INPUT, .pull_up_en = GPIO_PULLUP_ENABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&input));
    for (unsigned i = 0; i < CHANNELS; ++i) {
        input_init(&inputs[i], active(i), now_ms());
        events[i] = (esp_random() & 0xffffff) << 8; // Avoid reusing event counters after a reboot.
    }
    esp_zb_platform_config_t platform = {
        .radio_config.radio_mode = ZB_RADIO_MODE_NATIVE,
        .host_config.host_connection_mode = ZB_HOST_CONNECTION_MODE_NONE,
    };
    ESP_ERROR_CHECK(esp_zb_platform_config(&platform));
    BaseType_t started = xTaskCreate(zigbee_task, "zigbee", 8192, NULL, 5, NULL);
    configASSERT(started == pdPASS);
}
