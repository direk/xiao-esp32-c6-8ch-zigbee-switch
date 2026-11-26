// SPDX-License-Identifier: MIT
#include <inttypes.h>
#include <string.h>
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_app_desc.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "ha/esp_zigbee_ha_standard.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "control.h"
#include "serial_input.h"
#include "serial_transport.h"

#define CLUSTER 0xfc00
#define ATTR_MODE 0x0001
#define ATTR_INVERT 0x0002
#define ATTR_FUNCTION 0x0005
#define ATTR_ACTION 0x0006
#define ATTR_MIN 0x0007
#define ATTR_MAX 0x0008
#define ATTR_TRANSITION 0x0009
#define ATTR_COLOR_MIN 0x000a
#define ATTR_COLOR_MAX 0x000b
#define ATTR_SCENE 0x000c
#define ATTR_SCENE_GROUP 0x000d
#define ATTR_BINDING_PRIORITY 0x000e
#define ATTR_BINDING_CLUSTERS 0x0014
#define ATTR_CHANNEL_ROUTE 0x0012
#define ATTR_BINDING_STATUS 0x0013
#define ATTR_EVENT 0x0020
#define COORDINATOR_EP 1
#ifndef XIAO_EXTERNAL_ANTENNA
#error "Antenna selection must be supplied by CMake"
#endif

// XIAO D0..D5, D8/D9. UART D6/D7 (GPIO16/17) handles serial commands.
// USB (GPIO12/13), antenna (3/14) and BOOT (9) remain separate.
static const gpio_num_t pins[CHANNELS] = {GPIO_NUM_0, GPIO_NUM_1, GPIO_NUM_2, GPIO_NUM_21, GPIO_NUM_22, GPIO_NUM_23, GPIO_NUM_19, GPIO_NUM_20};
static const char *TAG = "xiao8";
static struct { uint8_t version; channel_config_t channels[CHANNELS]; } cfg = {.version=2};
static nvs_handle_t nv;
// Separate key preserves the binary layout of existing config2 installations.
static uint8_t binding_priority[CHANNELS];
static button_t inputs[CHANNELS];
static serial_input_t serial_input;
static bool joined, dirty, rebase[CHANNELS];
static uint64_t dirty_at, boot_pressed_at;
static bool boot_held,reset_pending;
static uint64_t reset_requested_at;
static uint32_t serial_boot_id;
static uint32_t events[CHANNELS];
static uint8_t channel_route[CHANNELS] = {1,1,1,1,1,1,1,1};
static uint8_t binding_status[CHANNELS]; // 0=unknown, 1=unbound, 2=bound.
static uint8_t bound_mask, cluster_masks[4], pending_masks[4], binding_clusters[CHANNELS];
static uint8_t aps_sequence;
static bool bindings_known, scan_active;
static binding_scan_t binding_scan;
static esp_zb_zdo_mgmt_bind_param_t binding_request;
static uintptr_t scan_generation;
static uint64_t scan_started_ms, next_scan_ms;
static esp_zb_ieee_addr_t coordinator_ieee;
static bool coordinator_known;

static uint64_t now_ms(void) { return (uint64_t)esp_timer_get_time() / 1000; }
static bool physical(unsigned i) { return (gpio_get_level(pins[i]) == 0) ^ (cfg.channels[i].invert != 0); }
static bool active(unsigned i) { return physical(i) || serial_input.buttons[i].down; }

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
        uint8_t mask=0;
        for (unsigned j=0;j<4;++j) if (cluster_masks[j] & (1U<<i)) mask|=1U<<j;
        uint8_t function=cfg.channels[i].function;
        uint8_t needed=function<=MODE_MOMENTARY ? 1 : function==MODE_MULTIFUNCTION ? 0 :
            function==MODE_SCENE ? 8 : (function>=MODE_COLOR && function<=MODE_COLOR_DOWN) ? 4 :
            function>=MODE_LEVEL_NO_ONOFF ? 2 : 3;
        bool group_scene=function==MODE_SCENE && cfg.channels[i].scene_group!=0;
        bool bound=(bound_mask & (1U<<i))!=0;
        uint8_t available=mask & needed;
        uint8_t path=group_scene || (needed && available==needed) ? 0 : available ? 2 : 1;
        if (binding_clusters[i]!=mask) {
            binding_clusters[i]=mask;
            set_attr(i+1,ATTR_BINDING_CLUSTERS,&binding_clusters[i]);
            report(i+1,ATTR_BINDING_CLUSTERS);
        }
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
        bool local=!memcmp(record->src_address,own,sizeof(own));
        mask=binding_mask_add(mask,record->src_endp,record->cluster_id,record->dst_addr_mode,local,excluded);
        int ci=binding_cluster_index(record->cluster_id);
        if (ci>=0) pending_masks[ci]=binding_mask_add(pending_masks[ci],record->src_endp,record->cluster_id,record->dst_addr_mode,local,excluded);
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
        memcpy(cluster_masks,pending_masks,sizeof(cluster_masks));
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
    memset(pending_masks,0,sizeof(pending_masks));
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

static bool config_value(channel_config_t *c, uint16_t id, const uint8_t *v) {
    uint16_t word=0;
    switch (id) {
    case ATTR_MODE:c->input_mode=*v;break;
    case ATTR_INVERT:c->invert=*v;break;
    case ATTR_FUNCTION:c->function=*v;break;
    case ATTR_ACTION:c->switch_action=*v;break;
    case ATTR_MIN:c->min_level=*v;break;
    case ATTR_MAX:c->max_level=*v;break;
    case ATTR_SCENE:c->scene_id=*v;break;
    case ATTR_TRANSITION:case ATTR_COLOR_MIN:case ATTR_COLOR_MAX:case ATTR_SCENE_GROUP:
        memcpy(&word,v,sizeof(word));
        if (id==ATTR_TRANSITION)c->transition_time=word;
        else if(id==ATTR_COLOR_MIN)c->color_min=word;
        else if(id==ATTR_COLOR_MAX)c->color_max=word;
        else c->scene_group=word;
        break;
    default:return false;
    }
    return channel_valid(c);
}
static int check_value(uint16_t id, uint8_t ep, uint8_t *v) {
    if(reset_pending)return ESP_ZB_ZCL_STATUS_FAIL;
    if (!v || ep<1 || ep>CHANNELS) return ESP_ZB_ZCL_STATUS_INVALID_VALUE;
    if (id==ATTR_BINDING_PRIORITY) return *v<=1 ? ESP_ZB_ZCL_STATUS_SUCCESS : ESP_ZB_ZCL_STATUS_INVALID_VALUE;
    channel_config_t candidate=cfg.channels[ep-1];
    return config_value(&candidate,id,v) ? ESP_ZB_ZCL_STATUS_SUCCESS : ESP_ZB_ZCL_STATUS_INVALID_VALUE;
}
static void write_attr(uint8_t ep,uint16_t id,uint8_t *v,uint16_t manufacturer) {
    (void)manufacturer;
    if (check_value(id,ep,v)!=ESP_ZB_ZCL_STATUS_SUCCESS)return;
    if(id==ATTR_BINDING_PRIORITY) binding_priority[ep-1]=*v;
    else {
        serial_input_cancel(&serial_input,ep-1);
        config_value(&cfg.channels[ep-1],id,v);
        rebase[ep-1]=true;
    }
    dirty=true;dirty_at=now_ms();
    ESP_LOGI(TAG,"Configuration write CH%u attr=0x%04x",ep,id);
}
static void send_action(unsigned i,uint8_t action) {
    uint32_t seq=((events[i]>>8)+1)&0xffffff;
    if(!seq)seq=1;
    events[i]=(seq<<8)|action;
    set_attr(i+1,ATTR_EVENT,&events[i]);report(i+1,ATTR_EVENT);
}
static void dispatch(unsigned i,const command_t *c) {
    if(!joined)return;
    int ci=binding_cluster_index(c->cluster);
    bool bound=ci>=0 && (cluster_masks[ci] & (1U<<i));
    bool direct=command_direct(c,bound);
    if(command_reports(c,bound,binding_priority[i])) {
        send_action(i,c->action);
        ESP_LOGI(TAG,"CH%u action=%u to Z2M",i+1,c->action);
    }
    if(!direct)return;
    uint8_t frame[16];
    size_t length=command_frame(c,aps_sequence++,frame,sizeof(frame));
    if(!length)return;
    esp_zb_apsde_data_req_t req={
        .dst_addr_mode=c->group ? ESP_ZB_APS_ADDR_MODE_16_GROUP_ENDP_NOT_PRESENT : ESP_ZB_APS_ADDR_MODE_DST_ADDR_ENDP_NOT_PRESENT,
        .src_endpoint=i+1,.profile_id=ESP_ZB_AF_HA_PROFILE_ID,.cluster_id=c->cluster,
        .asdu_length=length,.asdu=frame,.radius=30,
    };
    if(c->group)req.dst_addr.addr_short=c->group;
    esp_err_t err=esp_zb_aps_data_request(&req);
    ESP_LOGI(TAG,"CH%u cluster=0x%04x command=0x%02x direct: %s",i+1,c->cluster,c->id,esp_err_to_name(err));
}

static esp_err_t save_config(void) {
    if(!dirty)return ESP_OK;
    esp_err_t err=nvs_set_blob(nv,"config2",&cfg,sizeof(cfg));
    uint8_t priorities=0;
    for(unsigned i=0;i<CHANNELS;++i)priorities|=(binding_priority[i]!=0)<<i;
    if(err==ESP_OK)err=nvs_set_u8(nv,"bind_priority",priorities);
    if(err==ESP_OK)err=nvs_commit(nv);
    if(err==ESP_OK) {dirty=false;ESP_LOGI(TAG,"Configuration saved to NVS");}
    return err;
}
static bool request_network_reset(void *unused) {
    (void)unused;
    if(save_config()!=ESP_OK)return false;
    // Stop direct holds while old bindings/network still exist, then release UART states.
    for(unsigned i=0;i<CHANNELS;++i) {
        command_t command;
        if(button_cancel(&inputs[i],&command))dispatch(i,&command);
        serial_input_cancel(&serial_input,i);
        button_init(&inputs[i],active(i),now_ms());
    }
    reset_pending=true;reset_requested_at=now_ms();
    return true;
}

// Runs on the Zigbee scheduler: all app state and stack calls share one context.
static void tick(uint8_t unused) {
    (void)unused;
    uint64_t now = now_ms();
    refresh_bindings(now);
    update_route();
    serial_input_tick(&serial_input,now);
    serial_runtime_t runtime={.firmware_version=esp_app_get_description()->version,
        .boot_id=serial_boot_id,.joined=joined,.bindings_known=bindings_known,
        .factory_new=esp_zb_bdb_is_factory_new(),.reset_pending=reset_pending,
        .short_address=joined?esp_zb_get_short_address():0xffff};
    for(unsigned i=0;i<CHANNELS;++i) {
        if(physical(i))runtime.physical_mask|=1U<<i;
        runtime.binding_clusters[i]=binding_clusters[i];runtime.control_path[i]=channel_route[i];
    }
    serial_transport_poll(&serial_input,cfg.channels,binding_priority,&runtime,now,request_network_reset,NULL);
    if(serial_reset_ready(reset_pending,reset_requested_at,serial_transport_reset_acknowledged(),now)) {
        ESP_LOGW(TAG,"UART RESET: clearing Zigbee network/bindings; channel settings retained");
        esp_zb_factory_reset(); // Erases only zb_storage, then reboots into network steering.
        return;
    }
    for (unsigned i = 0; i < CHANNELS; ++i) {
        command_t command;
        if (rebase[i]) {
            if(button_cancel(&inputs[i],&command))dispatch(i,&command);
            button_init(&inputs[i],active(i),now);rebase[i]=false;
        }
        if (!bindings_known || reset_pending) continue;
        if(button_poll(&inputs[i],active(i),&cfg.channels[i],now,&command))dispatch(i,&command);
    }
    if (dirty && now - dirty_at >= 1000) {
        esp_err_t err=save_config();
        if(err!=ESP_OK) {ESP_LOGE(TAG,"Configuration persistence failed: %s",esp_err_to_name(err));dirty_at=now;}
    }
    if (!gpio_get_level(GPIO_NUM_9)) {
        if (!boot_held) { boot_held = true; boot_pressed_at = now; }
        if (now - boot_pressed_at >= 8000) {
            ESP_LOGW(TAG, "Factory reset: configuration, network and bindings");
            esp_err_t err=nvs_erase_all(nv);
            ESP_ERROR_CHECK(err);
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
        memset(cluster_masks,0,sizeof(cluster_masks));
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
    static char model[] = "\x0b" "XIAO-C6-8CH";
    const uint8_t rw = ESP_ZB_ZCL_ATTR_ACCESS_READ_WRITE;
    const uint8_t rr = ESP_ZB_ZCL_ATTR_ACCESS_READ_ONLY | ESP_ZB_ZCL_ATTR_ACCESS_REPORTING;
    for (uint8_t ep = 1; ep <= CHANNELS; ++ep) {
        esp_zb_on_off_switch_cfg_t defaults = ESP_ZB_DEFAULT_ON_OFF_SWITCH_CONFIG();
        esp_zb_cluster_list_t *clusters = esp_zb_on_off_switch_clusters_create(&defaults);
        esp_zb_attribute_list_t *basic = esp_zb_cluster_list_get_cluster(clusters, ESP_ZB_ZCL_CLUSTER_ID_BASIC, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);
        ESP_ERROR_CHECK(esp_zb_basic_cluster_add_attr(basic, ESP_ZB_ZCL_ATTR_BASIC_MANUFACTURER_NAME_ID, manufacturer));
        ESP_ERROR_CHECK(esp_zb_basic_cluster_add_attr(basic, ESP_ZB_ZCL_ATTR_BASIC_MODEL_IDENTIFIER_ID, model));
        // Empty client attribute lists advertise the clusters used by outgoing bound commands.
        ESP_ERROR_CHECK(esp_zb_cluster_list_add_level_cluster(clusters,esp_zb_zcl_attr_list_create(CL_LEVEL),ESP_ZB_ZCL_CLUSTER_CLIENT_ROLE));
        ESP_ERROR_CHECK(esp_zb_cluster_list_add_color_control_cluster(clusters,esp_zb_zcl_attr_list_create(CL_COLOR),ESP_ZB_ZCL_CLUSTER_CLIENT_ROLE));
        if(!esp_zb_cluster_list_get_cluster(clusters,CL_SCENES,ESP_ZB_ZCL_CLUSTER_CLIENT_ROLE))
            ESP_ERROR_CHECK(esp_zb_cluster_list_add_scenes_cluster(clusters,esp_zb_zcl_attr_list_create(CL_SCENES),ESP_ZB_ZCL_CLUSTER_CLIENT_ROLE));
        esp_zb_attribute_list_t *custom = esp_zb_zcl_attr_list_create(CLUSTER);
        channel_config_t *c=&cfg.channels[ep-1];
        add_custom(custom, ATTR_MODE, ESP_ZB_ZCL_ATTR_TYPE_U8, rw, &c->input_mode);
        add_custom(custom, ATTR_INVERT, ESP_ZB_ZCL_ATTR_TYPE_BOOL, rw, &c->invert);
        add_custom(custom,ATTR_FUNCTION,ESP_ZB_ZCL_ATTR_TYPE_U8,rw,&c->function);
        add_custom(custom,ATTR_ACTION,ESP_ZB_ZCL_ATTR_TYPE_U8,rw,&c->switch_action);
        add_custom(custom,ATTR_MIN,ESP_ZB_ZCL_ATTR_TYPE_U8,rw,&c->min_level);
        add_custom(custom,ATTR_MAX,ESP_ZB_ZCL_ATTR_TYPE_U8,rw,&c->max_level);
        add_custom(custom,ATTR_TRANSITION,ESP_ZB_ZCL_ATTR_TYPE_U16,rw,&c->transition_time);
        add_custom(custom,ATTR_COLOR_MIN,ESP_ZB_ZCL_ATTR_TYPE_U16,rw,&c->color_min);
        add_custom(custom,ATTR_COLOR_MAX,ESP_ZB_ZCL_ATTR_TYPE_U16,rw,&c->color_max);
        add_custom(custom,ATTR_SCENE,ESP_ZB_ZCL_ATTR_TYPE_U8,rw,&c->scene_id);
        add_custom(custom,ATTR_SCENE_GROUP,ESP_ZB_ZCL_ATTR_TYPE_U16,rw,&c->scene_group);
        add_custom(custom,ATTR_BINDING_PRIORITY,ESP_ZB_ZCL_ATTR_TYPE_BOOL,rw,&binding_priority[ep-1]);
        add_custom(custom,ATTR_BINDING_CLUSTERS,ESP_ZB_ZCL_ATTR_TYPE_U8,rr,&binding_clusters[ep-1]);
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
    serial_transport_logging_init();
    esp_err_t err = nvs_flash_init();
    // Do not silently erase persisted network/configuration on NVS errors.
    ESP_ERROR_CHECK(err);
    ESP_ERROR_CHECK(nvs_open("xiao4", NVS_READWRITE, &nv));
    for(unsigned i=0;i<CHANNELS;++i)channel_defaults(&cfg.channels[i]);
    size_t size=sizeof(cfg);
    typeof(cfg) loaded;
    err=nvs_get_blob(nv,"config2",&loaded,&size);
    if(err==ESP_OK && size==sizeof(cfg) && loaded.version==2) {
        bool valid=true;
        for(unsigned i=0;i<CHANNELS;++i)valid &= channel_valid(&loaded.channels[i]);
        if(valid)cfg=loaded;
    } else if(err==ESP_ERR_NVS_NOT_FOUND) {
        struct {uint8_t version,mode[4],invert[4];uint16_t reserved;} old;
        size=sizeof(old);
        if(nvs_get_blob(nv,"config",&old,&size)==ESP_OK && size==sizeof(old) && old.version==1) {
            for(unsigned i=0;i<4;++i)if(old.mode[i]<=1 && old.invert[i]<=1) {
                cfg.channels[i].input_mode=old.mode[i];cfg.channels[i].invert=old.invert[i];
            }
            dirty=true;dirty_at=now_ms();
            ESP_LOGI(TAG,"Migrated first four input settings from firmware 1.x");
        }
    }
    uint8_t priorities=0xff; // New and upgraded devices default to ON on all eight channels.
    err=nvs_get_u8(nv,"bind_priority",&priorities);
    if(err!=ESP_OK && err!=ESP_ERR_NVS_NOT_FOUND) ESP_ERROR_CHECK(err);
    for(unsigned i=0;i<CHANNELS;++i)binding_priority[i]=(priorities>>i)&1;
    gpio_config_t output = {.pin_bit_mask = (1ULL << 3) | (1ULL << 14), .mode = GPIO_MODE_INPUT_OUTPUT};
    ESP_ERROR_CHECK(gpio_config(&output));
    gpio_set_level(GPIO_NUM_3, 0); // Enable RF switch on XIAO.
    gpio_set_level(GPIO_NUM_14, XIAO_EXTERNAL_ANTENNA);
    ESP_LOGI(TAG, "Antenna: %s (RF enable=%d, select=%d)", XIAO_EXTERNAL_ANTENNA ? "external" : "internal",
             gpio_get_level(GPIO_NUM_3), gpio_get_level(GPIO_NUM_14));
    uint64_t input_mask=1ULL<<9; // BOOT plus the eight channel pins.
    for(unsigned i=0;i<CHANNELS;++i)input_mask |= 1ULL<<pins[i];
    gpio_config_t input = {
        .pin_bit_mask = input_mask,
        .mode = GPIO_MODE_INPUT, .pull_up_en = GPIO_PULLUP_ENABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&input));
    for (unsigned i = 0; i < CHANNELS; ++i) {
        button_init(&inputs[i], active(i), now_ms());
        events[i] = (esp_random() & 0xffffff) << 8; // Avoid reusing event counters after a reboot.
    }
    serial_boot_id=esp_random();
    serial_transport_start();
    esp_zb_platform_config_t platform = {
        .radio_config.radio_mode = ZB_RADIO_MODE_NATIVE,
        .host_config.host_connection_mode = ZB_HOST_CONNECTION_MODE_NONE,
    };
    ESP_ERROR_CHECK(esp_zb_platform_config(&platform));
    BaseType_t started = xTaskCreate(zigbee_task, "zigbee", 8192, NULL, 5, NULL);
    configASSERT(started == pdPASS);
}
