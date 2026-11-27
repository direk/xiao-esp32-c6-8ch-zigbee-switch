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
#include "binding_table.h"
#include "ota_client.h"
#include "device_services.h"
#include "input_router.h"
#include <stdlib.h>
#include "zboss_api.h"
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
#define ATTR_SCENE_LIST 0x000f
#define ATTR_SOURCE 0x0010
#define ATTR_GESTURE 0x0011
#define ATTR_PIN_MODE 0x0040
#define ATTR_PIN_INVERT 0x0041
#define ATTR_PIN_HOLD 0x0042
#define ATTR_PIN_MULTI 0x0043
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
static endpoint_config_t cfg, write_candidate;
static input_router_t input_router;
typedef struct { uint8_t version; channel_config_v2_t channels[CHANNELS]; } legacy_config_t;
_Static_assert(sizeof(legacy_config_t)==130,"Legacy NVS blob layout changed");
static nvs_handle_t nv;
// Separate key preserves the binary layout of existing config2 installations.
#define binding_priority cfg.priorities
static button_t inputs[ENDPOINTS];
static serial_input_t serial_input;
static bool joined, dirty, rebase[ENDPOINTS];
static uint64_t dirty_at, boot_pressed_at;
static bool boot_held,reset_pending;
static uint64_t reset_requested_at;
static uint32_t serial_boot_id;
static uint32_t events[ENDPOINTS];
static uint8_t channel_route[ENDPOINTS] = {1,1,1,1,1,1,1,1};
static uint8_t binding_status[ENDPOINTS]; // 0=unknown, 1=unbound, 2=bound.
static uint32_t bound_mask, cluster_masks[4], pending_masks[4];
static uint8_t binding_clusters[ENDPOINTS];
static uint8_t aps_sequence;
static bool bindings_known, scan_active;
static binding_scan_t binding_scan;
static uint8_t binding_tsn;
static bool binding_page_pending;
static uint64_t scan_started_ms, next_scan_ms;
static esp_zb_ieee_addr_t coordinator_ieee;
static bool coordinator_known;

static uint64_t now_ms(void) { return (uint64_t)esp_timer_get_time() / 1000; }
static bool physical(unsigned i) { return (gpio_get_level(pins[i]) == 0) ^ (cfg.pins[i].invert != 0); }
static uint8_t physical_inputs(void) {
    uint8_t mask=0;for(unsigned i=0;i<CHANNELS;++i)if(physical(i) || serial_input.buttons[i].down)mask|=1U<<i;
    return mask;
}
static bool active(unsigned i) {
    return input_router_active(&input_router,&cfg,i,physical_inputs()) || serial_input.endpoint_buttons[i].down;
}

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
    for (unsigned i = 0; i < ENDPOINTS; ++i) {
        uint8_t mask=0;
        for (unsigned j=0;j<4;++j) if (cluster_masks[j] & (1U<<i)) mask|=1U<<j;
        bool bound=(bound_mask & (1U<<i))!=0;
        uint8_t path=channel_control_path(&cfg.channels[i],mask);
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

// esp-zigbee-lib 1.6.9 advances by 21 bytes even for 14-byte group records.
// Use the pinned ZBOSS raw-response API and decode the wire format ourselves.
static void binding_table_received(zb_uint8_t buffer) {
    if (buffer == ZB_BUF_INVALID) return;
    const uint8_t *data = zb_buf_begin(buffer);
    size_t length = zb_buf_len(buffer);
    if (!scan_active || !binding_page_pending || !length || data[0] != binding_tsn) {
        zb_buf_free(buffer);
        return;
    }
    binding_page_pending = false;
    esp_zb_ieee_addr_t own;
    esp_zb_get_long_address(own);
    binding_page_t page;
    bool valid = binding_page_decode(data, length, own, coordinator_known ? coordinator_ieee : NULL, &page);
    zb_buf_free(buffer); // No pointers into the stack buffer survive this callback.
    if (!valid) {
        scan_active = false; // Retain the last complete snapshot.
        ESP_LOGW(TAG, "Invalid or unsuccessful binding table response");
        return;
    }
    int state = binding_scan_page(&binding_scan, page.index, page.total, page.count, page.mask);
    if (state < 0) {
        scan_active = false;
        ESP_LOGW(TAG, "Inconsistent binding table snapshot; retrying");
        return;
    }
    for (unsigned i=0; i<4; ++i) pending_masks[i] |= page.clusters[i];
    ESP_LOGI(TAG, "Binding page index=%u total=%u count=%u mask=%06lx", page.index,page.total,page.count,(unsigned long)page.mask);
    if (state > 0) {
        bound_mask = binding_scan.mask;
        memcpy(cluster_masks, pending_masks, sizeof(cluster_masks));
        bindings_known = true;
        scan_active = false;
        update_route();
    }
    // The next page is requested by tick(), after the callback has returned.
}

static void refresh_bindings(uint64_t now) {
    if (!joined) return;
    if (scan_active && now - scan_started_ms >= 6000) {
        scan_active = binding_page_pending = false;
        ESP_LOGW(TAG, "Binding table scan timed out; retaining previous snapshot");
    }
    if (!scan_active) {
        if (now < next_scan_ms) return;
        if (esp_zb_ieee_address_by_short(0, coordinator_ieee) == ESP_OK) coordinator_known = true;
        binding_scan = (binding_scan_t){0};
        memset(pending_masks, 0, sizeof(pending_masks));
        scan_active = true;
        binding_page_pending = false;
        scan_started_ms = now;
        next_scan_ms = now + 1000;
    }
    if (binding_page_pending) return;
    zb_uint8_t buffer = zb_buf_get_out();
    if (buffer == ZB_BUF_INVALID) {
        scan_active = false;
        return;
    }
    zb_zdo_mgmt_bind_param_t *request = ZB_BUF_GET_PARAM(buffer, zb_zdo_mgmt_bind_param_t);
    request->dst_addr = esp_zb_get_short_address();
    request->start_index = binding_scan.next;
    binding_page_pending = true;
    // The request transfers buffer ownership to ZBOSS, just as the SDK wrapper does.
    binding_tsn = zb_zdo_mgmt_bind_req(buffer, binding_table_received);
    if (binding_tsn == 0xff) scan_active = binding_page_pending = false;
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
    case ATTR_SCENE_LIST: {
        uint8_t ids[SCENE_LIST_MAX],count;
        if(!scene_list_parse(v,ids,&count))return false;
        uint8_t text[SCENE_LIST_BYTES]={0};
        memcpy(text,v,(size_t)v[0]+1);
        memcpy(c->scene_list,text,sizeof(text));break;
    }
    case ATTR_SOURCE:c->input_source=*v;break;
    case ATTR_GESTURE:c->input_gesture=*v;break;
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
static void dispatch(unsigned i,const command_t *c);
static int check_value(uint16_t id,uint8_t ep,uint8_t *v) {
    if(reset_pending)return ESP_ZB_ZCL_STATUS_FAIL;
    if(!v || ep<1 || ep>ENDPOINTS)return ESP_ZB_ZCL_STATUS_INVALID_VALUE;
    write_candidate=cfg;
    channel_config_t *e=&write_candidate.channels[ep-1];
    bool valid=true;
    if(id==ATTR_BINDING_PRIORITY)write_candidate.priorities[ep-1]=*v;
    else if(id>=ATTR_PIN_MODE && id<=ATTR_PIN_MULTI) {
        if(ep>CHANNELS)return ESP_ZB_ZCL_STATUS_INVALID_VALUE;
        pin_config_t pin=write_candidate.pins[ep-1];uint16_t word=0;
        if(id==ATTR_PIN_MODE)pin.mode=*v;
        else if(id==ATTR_PIN_INVERT)pin.invert=*v;
        else {memcpy(&word,v,2);if(id==ATTR_PIN_HOLD)pin.hold_ms=word;else pin.multi_ms=word;}
        valid=endpoint_config_pin(&write_candidate,ep-1,&pin);
    } else {
        valid=config_value(e,id,v);
        if(valid && e->input_source) {
            unsigned pin=e->input_source-1;
            if(id==ATTR_MODE || id==ATTR_INVERT) {
                pin_config_t value=write_candidate.pins[pin];
                if(id==ATTR_MODE)value.mode=e->input_mode;else value.invert=e->invert;
                valid=endpoint_config_pin(&write_candidate,pin,&value);
            } else {
                e->input_mode=write_candidate.pins[pin].mode;e->invert=write_candidate.pins[pin].invert;
            }
        }
    }
    return valid && endpoint_config_valid(&write_candidate) ? ESP_ZB_ZCL_STATUS_SUCCESS : ESP_ZB_ZCL_STATUS_INVALID_VALUE;
}
static void write_attr(uint8_t ep,uint16_t id,uint8_t *v,uint16_t manufacturer) {
    (void)manufacturer;
    if(check_value(id,ep,v)!=ESP_ZB_ZCL_STATUS_SUCCESS)return;
    if(id==ATTR_BINDING_PRIORITY)binding_priority[ep-1]=*v;
    else {
        bool pin_setting=id>=ATTR_PIN_MODE && id<=ATTR_PIN_MULTI;
        uint8_t affected=pin_setting ? 1U<<(ep-1) : 0;
        if(!pin_setting) {
            if(cfg.channels[ep-1].input_source)affected|=1U<<(cfg.channels[ep-1].input_source-1);
            if(write_candidate.channels[ep-1].input_source)affected|=1U<<(write_candidate.channels[ep-1].input_source-1);
        }
        for(unsigned i=0;i<ENDPOINTS;++i) {
            uint8_t source=cfg.channels[i].input_source;
            if((!pin_setting && i==ep-1) || (source && (affected & (1U<<(source-1))))) {
                command_t command;if(button_cancel(&inputs[i],&command))dispatch(i,&command);
                serial_endpoint_cancel(&serial_input,i);input_router.endpoints[i]=(routed_button_t){0};rebase[i]=true;
            }
        }
        cfg=write_candidate;
        for(unsigned p=0;p<CHANNELS;++p)if(affected & (1U<<p)) {
            serial_input_cancel(&serial_input,p);bool value=physical(p);
            input_router.pins[p]=(pin_state_t){.raw=value,.stable=value,.changed=now_ms()};
        }
        if(id==ATTR_ACTION)inputs[ep-1].on_off_next_off=false;
        if(id==ATTR_SCENE_LIST || id==ATTR_SCENE_GROUP || id==ATTR_FUNCTION)inputs[ep-1].scene_index=0;
        for(unsigned i=0;i<ENDPOINTS;++i) {
            set_attr(i+1,ATTR_MODE,&cfg.channels[i].input_mode);set_attr(i+1,ATTR_INVERT,&cfg.channels[i].invert);
        }
        for(unsigned p=0;p<CHANNELS;++p) {
            set_attr(p+1,ATTR_PIN_MODE,&cfg.pins[p].mode);set_attr(p+1,ATTR_PIN_INVERT,&cfg.pins[p].invert);
            set_attr(p+1,ATTR_PIN_HOLD,&cfg.pins[p].hold_ms);set_attr(p+1,ATTR_PIN_MULTI,&cfg.pins[p].multi_ms);
        }
    }
    dirty=true;dirty_at=now_ms();
    ESP_LOGI(TAG,"Configuration write EP%u attr=0x%04x",ep,id);
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
    device_services_tx_result(err);
    ESP_LOGI(TAG,"CH%u cluster=0x%04x command=0x%02x direct: %s",i+1,c->cluster,c->id,esp_err_to_name(err));
}

static esp_err_t save_config(void) {
    if(!dirty)return ESP_OK;
    typeof(cfg) saved=cfg;
    uint8_t on_off_mask=channel_actions_store(cfg.channels,saved.channels);
    legacy_config_t legacy={.version=2};
    uint8_t cycle_mask=channel_legacy_store(cfg.channels,legacy.channels);
    uint8_t lists[CHANNELS][SCENE_LIST_BYTES];
    for(unsigned i=0;i<CHANNELS;++i)memcpy(lists[i],cfg.channels[i].scene_list,SCENE_LIST_BYTES);
    esp_err_t err=nvs_set_blob(nv,"config3",&cfg,sizeof(cfg));
    if(err==ESP_OK)err=nvs_set_blob(nv,"config2",&legacy,sizeof(legacy));
    if(err==ESP_OK)err=nvs_set_blob(nv,"scene_lists",lists,sizeof(lists));
    if(err==ESP_OK)err=nvs_set_u8(nv,"scene_cycle",cycle_mask);
    if(err==ESP_OK)err=nvs_set_u8(nv,"onoff_action",on_off_mask);
    uint8_t priorities=0;
    for(unsigned i=0;i<CHANNELS;++i)priorities|=(binding_priority[i]!=0)<<i;
    if(err==ESP_OK)err=nvs_set_u8(nv,"bind_priority",priorities);
    if(err==ESP_OK)err=nvs_commit(nv);
    if(err==ESP_OK) {dirty=false;ESP_LOGI(TAG,"Configuration saved to NVS");}
    return err;
}
static bool prepare_ota_restart(void) {
    if(save_config()!=ESP_OK)return false;
    for(unsigned i=0;i<ENDPOINTS;++i) {
        command_t command;
        if(button_cancel(&inputs[i],&command))dispatch(i,&command);
        serial_endpoint_cancel(&serial_input,i);
        button_init(&inputs[i],active(i),now_ms());
    }
    for(unsigned i=0;i<CHANNELS;++i)serial_input_cancel(&serial_input,i);
    input_router_cancel(&input_router,physical_inputs(),now_ms());
    return true;
}
static bool request_network_reset(void *unused) {
    (void)unused;
    if(ota_client_reboot_pending())return false;
    ota_client_cancel();
    if(save_config()!=ESP_OK)return false;
    // Stop direct holds while old bindings/network still exist, then release UART states.
    for(unsigned i=0;i<ENDPOINTS;++i) {
        command_t command;
        if(button_cancel(&inputs[i],&command))dispatch(i,&command);
        serial_endpoint_cancel(&serial_input,i);
        button_init(&inputs[i],active(i),now_ms());
    }
    for(unsigned i=0;i<CHANNELS;++i)serial_input_cancel(&serial_input,i);
    input_router_cancel(&input_router,physical_inputs(),now_ms());
    reset_pending=true;reset_requested_at=now_ms();
    return true;
}

// Runs on the Zigbee scheduler: all app state and stack calls share one context.
static void tick(uint8_t unused) {
    (void)unused;
    uint64_t now = now_ms();
    ota_client_poll(now);
    device_services_poll(now,joined);
    refresh_bindings(now);
    update_route();
    serial_input_tick(&serial_input,now);
    serial_runtime_t runtime={.firmware_version=esp_app_get_description()->version,
        .boot_id=serial_boot_id,.joined=joined,.bindings_known=bindings_known,
        .factory_new=esp_zb_bdb_is_factory_new(),.reset_pending=reset_pending || ota_client_reboot_pending(),
        .short_address=joined?esp_zb_get_short_address():0xffff};
    for(unsigned i=0;i<ENDPOINTS;++i) {
        if(i<CHANNELS && physical(i))runtime.physical_mask|=1U<<i;
        runtime.binding_clusters[i]=binding_clusters[i];runtime.control_path[i]=channel_route[i];
    }
    ota_client_runtime(&runtime);
    serial_transport_poll(&serial_input,cfg.channels,binding_priority,&runtime,now,request_network_reset,NULL);
    if(serial_reset_ready(reset_pending,reset_requested_at,serial_transport_reset_acknowledged(),now)) {
        ESP_LOGW(TAG,"UART RESET: clearing Zigbee network/bindings; channel settings retained");
        esp_zb_factory_reset(); // Erases only zb_storage, then reboots into network steering.
        return;
    }
    if(serial_input.release_all) {
        serial_input.release_all=false;
        for(unsigned i=0;i<ENDPOINTS;++i){command_t command;if(button_cancel(&inputs[i],&command))dispatch(i,&command);rebase[i]=true;}
        input_router_cancel(&input_router,physical_inputs(),now);
    }
    if(bindings_known && !reset_pending && !ota_client_reboot_pending())input_router_poll(&input_router,&cfg,physical_inputs(),now);
    else input_router_cancel(&input_router,physical_inputs(),now);
    for (unsigned i = 0; i < ENDPOINTS; ++i) {
        command_t command;
        if (rebase[i]) {
            if(button_cancel(&inputs[i],&command))dispatch(i,&command);
            button_rebase(&inputs[i],active(i),now);rebase[i]=false;
        }
        if (!bindings_known || reset_pending || ota_client_reboot_pending()) continue;
        if(input_router.endpoints[i].down && input_router.endpoints[i].hold && inputs[i].stable && inputs[i].armed && !inputs[i].holding)
            inputs[i].pressed_ms=now>=GESTURE_MS ? now-GESTURE_MS : 0;
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
            ota_client_joined();
        }
        break;
    case ESP_ZB_BDB_SIGNAL_STEERING:
        if (s->esp_err_status == ESP_OK) {
            joined = true;
            ESP_LOGI(TAG, "Joined network, short address 0x%04x", esp_zb_get_short_address());
            ota_client_joined();
        } else {
            esp_zb_scheduler_alarm(commission, ESP_ZB_BDB_MODE_NETWORK_STEERING, 10000);
        }
        break;
    case ESP_ZB_ZDO_SIGNAL_LEAVE:
        joined = false;
        ota_client_cancel();
        bindings_known = scan_active = coordinator_known = false;
        bound_mask = 0;
        memset(cluster_masks,0,sizeof(cluster_masks));
        binding_page_pending = false;
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
    ESP_ERROR_CHECK(esp_zb_io_buffer_size_set(160));
    ESP_ERROR_CHECK(esp_zb_aps_src_binding_table_size_set(BINDING_CAPACITY));
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
    static uint8_t build_id[33];
    size_t build_length=strnlen(esp_app_get_description()->version,31);
    build_id[0]=build_length;memcpy(build_id+1,esp_app_get_description()->version,build_length);
    for (uint8_t ep = 1; ep <= ENDPOINTS; ++ep) {
        esp_zb_on_off_switch_cfg_t defaults = ESP_ZB_DEFAULT_ON_OFF_SWITCH_CONFIG();
        esp_zb_cluster_list_t *clusters = esp_zb_on_off_switch_clusters_create(&defaults);
        ESP_ERROR_CHECK(device_services_add_clusters(clusters,ep));
        esp_zb_attribute_list_t *basic = esp_zb_cluster_list_get_cluster(clusters, ESP_ZB_ZCL_CLUSTER_ID_BASIC, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);
        ESP_ERROR_CHECK(esp_zb_basic_cluster_add_attr(basic, ESP_ZB_ZCL_ATTR_BASIC_MANUFACTURER_NAME_ID, manufacturer));
        ESP_ERROR_CHECK(esp_zb_basic_cluster_add_attr(basic, ESP_ZB_ZCL_ATTR_BASIC_MODEL_IDENTIFIER_ID, model));
        ESP_ERROR_CHECK(esp_zb_basic_cluster_add_attr(basic, ESP_ZB_ZCL_ATTR_BASIC_SW_BUILD_ID, build_id));
        if(ep==XIAO_OTA_ENDPOINT)ESP_ERROR_CHECK(ota_client_add_cluster(clusters));
        // Empty client attribute lists advertise the clusters used by outgoing bound commands.
        ESP_ERROR_CHECK(esp_zb_cluster_list_add_level_cluster(clusters,esp_zb_zcl_attr_list_create(CL_LEVEL),ESP_ZB_ZCL_CLUSTER_CLIENT_ROLE));
        ESP_ERROR_CHECK(esp_zb_cluster_list_add_color_control_cluster(clusters,esp_zb_zcl_attr_list_create(CL_COLOR),ESP_ZB_ZCL_CLUSTER_CLIENT_ROLE));
        if(!esp_zb_cluster_list_get_cluster(clusters,CL_SCENES,ESP_ZB_ZCL_CLUSTER_CLIENT_ROLE))
            ESP_ERROR_CHECK(esp_zb_cluster_list_add_scenes_cluster(clusters,esp_zb_zcl_attr_list_create(CL_SCENES),ESP_ZB_ZCL_CLUSTER_CLIENT_ROLE));
        esp_zb_attribute_list_t *custom = esp_zb_zcl_attr_list_create(CLUSTER);
        if(ep==1)device_services_add_attributes(custom);
        channel_config_t *c=&cfg.channels[ep-1];
        add_custom(custom,ATTR_SOURCE,ESP_ZB_ZCL_ATTR_TYPE_U8,rw,&c->input_source);
        add_custom(custom,ATTR_GESTURE,ESP_ZB_ZCL_ATTR_TYPE_U8,rw,&c->input_gesture);
        if(ep<=CHANNELS) {
            pin_config_t *p=&cfg.pins[ep-1];
            add_custom(custom,ATTR_PIN_MODE,ESP_ZB_ZCL_ATTR_TYPE_U8,rw,&p->mode);
            add_custom(custom,ATTR_PIN_INVERT,ESP_ZB_ZCL_ATTR_TYPE_BOOL,rw,&p->invert);
            add_custom(custom,ATTR_PIN_HOLD,ESP_ZB_ZCL_ATTR_TYPE_U16,rw,&p->hold_ms);
            add_custom(custom,ATTR_PIN_MULTI,ESP_ZB_ZCL_ATTR_TYPE_U16,rw,&p->multi_ms);
        }
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
        add_custom(custom,ATTR_SCENE_LIST,ESP_ZB_ZCL_ATTR_TYPE_CHAR_STRING,rw,c->scene_list);
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
    esp_zb_core_action_handler_register(ota_client_action);
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
    device_services_init();
    ESP_ERROR_CHECK(nvs_open("xiao4", NVS_READWRITE, &nv));
    endpoint_config_defaults(&cfg);
    legacy_config_t loaded;
    size_t size=sizeof(loaded);
    err=nvs_get_blob(nv,"config2",&loaded,&size);
    if(err==ESP_OK && size==sizeof(loaded) && loaded.version==2) {
        channel_config_t candidate[CHANNELS];
        channel_legacy_restore(candidate,loaded.channels,NULL,0);
        bool valid=true;
        for(unsigned i=0;i<CHANNELS;++i)valid &= channel_valid(&candidate[i]);
        if(valid)memcpy(cfg.channels,candidate,sizeof(candidate));
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
    uint8_t lists[CHANNELS][SCENE_LIST_BYTES]={0},cycle_mask=0;
    size=sizeof(lists);err=nvs_get_blob(nv,"scene_lists",lists,&size);
    if(err!=ESP_OK && err!=ESP_ERR_NVS_NOT_FOUND)ESP_ERROR_CHECK(err);
    if(err==ESP_OK && size==sizeof(lists)) {
        for(unsigned i=0;i<CHANNELS;++i) {
            uint8_t ids[SCENE_LIST_MAX],count;
            if(scene_list_parse(lists[i],ids,&count))memcpy(cfg.channels[i].scene_list,lists[i],SCENE_LIST_BYTES);
        }
    }
    err=nvs_get_u8(nv,"scene_cycle",&cycle_mask);
    if(err!=ESP_OK && err!=ESP_ERR_NVS_NOT_FOUND)ESP_ERROR_CHECK(err);
    for(unsigned i=0;i<CHANNELS;++i)
        if((cycle_mask & (1U<<i)) && cfg.channels[i].function==MODE_SCENE && cfg.channels[i].input_mode==INPUT_PUSH)
            cfg.channels[i].function=MODE_SCENE_CYCLE;
    uint8_t priorities=0xff; // New and upgraded devices default to ON on all eight channels.
    err=nvs_get_u8(nv,"bind_priority",&priorities);
    if(err!=ESP_OK && err!=ESP_ERR_NVS_NOT_FOUND) ESP_ERROR_CHECK(err);
    for(unsigned i=0;i<CHANNELS;++i)binding_priority[i]=(priorities>>i)&1;
    uint8_t on_off_mask=0;
    err=nvs_get_u8(nv,"onoff_action",&on_off_mask);
    if(err!=ESP_OK && err!=ESP_ERR_NVS_NOT_FOUND) ESP_ERROR_CHECK(err);
    channel_actions_restore(cfg.channels,on_off_mask);
    for(unsigned i=0;i<CHANNELS;++i) {
        cfg.channels[i].input_source=i+1;cfg.channels[i].input_gesture=GESTURE_LEGACY;
        cfg.pins[i].mode=cfg.channels[i].input_mode;cfg.pins[i].invert=cfg.channels[i].invert;
    }
    endpoint_config_t *stored=malloc(sizeof(*stored));ESP_ERROR_CHECK(stored?ESP_OK:ESP_ERR_NO_MEM);
    size=sizeof(*stored);err=nvs_get_blob(nv,"config3",stored,&size);
    if(err==ESP_OK && size==sizeof(*stored) && endpoint_config_valid(stored))cfg=*stored;
    else if(err==ESP_ERR_NVS_NOT_FOUND){dirty=true;dirty_at=now_ms();}
    free(stored);serial_input.pins=cfg.pins;
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
    input_router_init(&input_router,physical_inputs(),now_ms());
    for (unsigned i = 0; i < ENDPOINTS; ++i) {
        button_init(&inputs[i], active(i), now_ms());
        events[i] = (esp_random() & 0xffffff) << 8; // Avoid reusing event counters after a reboot.
    }
    ota_client_init(prepare_ota_restart);
    serial_boot_id=esp_random();
    serial_transport_start();
    esp_zb_platform_config_t platform = {
        .radio_config.radio_mode = ZB_RADIO_MODE_NATIVE,
        .host_config.host_connection_mode = ZB_HOST_CONNECTION_MODE_NONE,
    };
    ESP_ERROR_CHECK(esp_zb_platform_config(&platform));
    BaseType_t started = xTaskCreate(zigbee_task, "zigbee", 12288, NULL, 5, NULL);
    configASSERT(started == pdPASS);
}
