// SPDX-License-Identifier: MIT
#include "ota_client.h"
#include "sdkconfig.h"
#if !CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE
#error "OTA requires rollback: regenerate sdkconfig using scripts/build_firmware.py"
#endif
#include "ota_stream.h"
#include "esp_ota_ops.h"
#include "esp_app_desc.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_zigbee_ota.h"
#include <string.h>

#ifndef XIAO_OTA_FILE_VERSION
#error "OTA file version must be supplied by CMake"
#endif

static const char *TAG="xiao_ota";
static const esp_partition_t *target, *running;
static esp_ota_handle_t handle;
static ota_stream_t stream;
static bool handle_open, validated, approved, available, boot_confirmed;
static bool (*prepare_restart_cb)(void);
static uint32_t offered_version, offered_size;
static uint64_t last_data_ms, restart_at;
static const char *state="idle";
static unsigned last_progress;
static uint16_t header_length;

static uint64_t now_ms(void) { return (uint64_t)esp_timer_get_time()/1000; }
static void clear_download(void) {
    if (handle_open) esp_ota_abort(handle);
    handle_open=false; validated=false; approved=false; target=NULL;
}
void ota_client_cancel(void) {
    if (restart_at) return; // Boot selection has already been committed.
    clear_download(); state="idle";
}
static esp_err_t failed(esp_err_t error) {
    clear_download(); state="error";
    ESP_LOGW(TAG,"OTA rejected/aborted: %s",esp_err_to_name(error));
    return error;
}
void ota_client_init(bool (*prepare_restart)(void)) {
    prepare_restart_cb=prepare_restart;
    running=esp_ota_get_running_partition();
    const esp_partition_t *a=esp_partition_find_first(ESP_PARTITION_TYPE_APP,ESP_PARTITION_SUBTYPE_APP_OTA_0,NULL);
    const esp_partition_t *b=esp_partition_find_first(ESP_PARTITION_TYPE_APP,ESP_PARTITION_SUBTYPE_APP_OTA_1,NULL);
    const esp_partition_t *data=esp_partition_find_first(ESP_PARTITION_TYPE_DATA,ESP_PARTITION_SUBTYPE_DATA_OTA,NULL);
    available=a && b && data && running && (running->address==a->address || running->address==b->address);
    if (!available) ESP_LOGW(TAG,"OTA partition layout missing; install the 2.2.0 USB migration images");
}
static bool write_payload(void *unused,const uint8_t *data,size_t length) {
    (void)unused;
    return handle_open && esp_ota_write(handle,data,length)==ESP_OK;
}
static bool identity_ok(const esp_zb_ota_file_header_t *header) {
    if (header->manufacturer_code!=XIAO_OTA_MANUFACTURER || header->image_type!=XIAO_OTA_IMAGE_TYPE ||
        header->file_version!=offered_version || (header->field_control & ~4U)) return false;
    header_length=(header->field_control & 4) ? 60 : 56;
    return !(header->field_control & 4) ||
        (header->optional.minimum_hardware_version<=XIAO_OTA_HARDWARE_VERSION &&
         header->optional.maximum_hardware_version>=XIAO_OTA_HARDWARE_VERSION);
}
static esp_err_t query_image(const esp_zb_zcl_ota_upgrade_query_image_resp_message_t *m) {
    if (m->info.status!=ESP_ZB_ZCL_STATUS_SUCCESS || m->query_status!=ESP_ZB_ZCL_STATUS_SUCCESS) return ESP_OK;
    if (handle_open || validated || restart_at) return ESP_ERR_INVALID_STATE;
    const esp_partition_t *next=esp_ota_get_next_update_partition(NULL);
    if (!available || !next || m->manufacturer_code!=XIAO_OTA_MANUFACTURER || m->image_type!=XIAO_OTA_IMAGE_TYPE ||
        m->file_version<=XIAO_OTA_FILE_VERSION || m->image_size<86 || m->image_size>next->size+66) return failed(ESP_ERR_INVALID_ARG);
    offered_version=m->file_version; offered_size=m->image_size; approved=true;
    ESP_LOGI(TAG,"OTA offered: version=%08lx type=%04x bytes=%lu",(unsigned long)offered_version,m->image_type,(unsigned long)offered_size);
    return ESP_OK;
}
static esp_err_t upgrade(const esp_zb_zcl_ota_upgrade_value_message_t *m) {
    if (restart_at) return ESP_OK;
    if (m->info.status!=ESP_ZB_ZCL_STATUS_SUCCESS) return failed(ESP_FAIL);
    switch (m->upgrade_status) {
    case ESP_ZB_ZCL_OTA_UPGRADE_STATUS_START:
        if (!available || !approved || handle_open || validated || restart_at) return failed(ESP_ERR_INVALID_STATE);
        target=esp_ota_get_next_update_partition(NULL);
        if (!target || target->address==running->address) return failed(ESP_ERR_INVALID_STATE);
        ota_stream_init(&stream,target->size); header_length=0; last_progress=0;
        esp_err_t err=esp_ota_begin(target,OTA_WITH_SEQUENTIAL_WRITES,&handle);
        if (err!=ESP_OK) return failed(err);
        handle_open=true; last_data_ms=now_ms(); state="downloading";
        ESP_LOGI(TAG,"OTA start into %s",target->label);
        return ESP_OK;
    case ESP_ZB_ZCL_OTA_UPGRADE_STATUS_APPLY:
        if (!approved || !identity_ok(&m->ota_header)) return failed(ESP_ERR_INVALID_ARG);
        return ESP_OK;
    case ESP_ZB_ZCL_OTA_UPGRADE_STATUS_RECEIVE:
        if (!handle_open || !identity_ok(&m->ota_header)) return failed(ESP_ERR_INVALID_STATE);
        if (!ota_stream_feed(&stream,m->payload,m->payload_size,write_payload,NULL)) return failed(ESP_FAIL);
        last_data_ms=now_ms();
        if (stream.expected) {
            unsigned progress=(uint64_t)stream.received*100/stream.expected;
            if (progress>=last_progress+5 || progress==100) {
                last_progress=progress; ESP_LOGI(TAG,"OTA download %u%%",progress);
            }
        }
        return ESP_OK;
    case ESP_ZB_ZCL_OTA_UPGRADE_STATUS_CHECK: {
        if (!handle_open || !header_length || !ota_stream_complete(&stream) ||
            (uint64_t)stream.expected+6+header_length!=offered_size) return failed(ESP_ERR_INVALID_SIZE);
        state="validating";
        esp_err_t err=esp_ota_end(handle);handle_open=false;
        if (err!=ESP_OK) return failed(err);
        esp_app_desc_t candidate;
        err=esp_ota_get_partition_description(target,&candidate);
        if (err!=ESP_OK || strncmp(candidate.project_name,esp_app_get_description()->project_name,sizeof(candidate.project_name)) ||
            !memchr(candidate.version,0,sizeof(candidate.version)) || !ota_version_matches(candidate.version,offered_version)) return failed(ESP_ERR_INVALID_VERSION);
        validated=true;last_data_ms=now_ms();state="ready";
        return ESP_OK;
    }
    case ESP_ZB_ZCL_OTA_UPGRADE_STATUS_FINISH: {
        if (!validated || !target || restart_at || !prepare_restart_cb || !prepare_restart_cb()) return failed(ESP_ERR_INVALID_STATE);
        esp_err_t err=esp_ota_set_boot_partition(target);
        if (err!=ESP_OK) return failed(err);
        restart_at=now_ms()+250;state="reboot_pending";
        ESP_LOGI(TAG,"OTA verified; rebooting into %s",target->label);
        return ESP_OK;
    }
    case ESP_ZB_ZCL_OTA_UPGRADE_STATUS_ABORT:
    case ESP_ZB_ZCL_OTA_UPGRADE_STATUS_ERROR:
        return failed(ESP_FAIL);
    default:
        return ESP_OK;
    }
}
esp_err_t ota_client_action(esp_zb_core_action_callback_id_t id,const void *message) {
    if (!message) return ESP_ERR_INVALID_ARG;
    if (id==ESP_ZB_CORE_OTA_UPGRADE_QUERY_IMAGE_RESP_CB_ID) return query_image(message);
    if (id==ESP_ZB_CORE_OTA_UPGRADE_VALUE_CB_ID) return upgrade(message);
    return ESP_OK;
}
static void first_query(uint8_t unused) {
    (void)unused;
    if (available && esp_zb_bdb_dev_joined() && !handle_open && !restart_at) {
        esp_zb_ota_upgrade_client_query_interval_set(XIAO_OTA_ENDPOINT,24*60);
        // SDK 1.x declaration swaps the parameter names; types/order are address, endpoint.
        esp_zb_ota_upgrade_client_query_image_req(0x0000,1);
    }
}
void ota_client_joined(void) { esp_zb_scheduler_alarm(first_query,0,10000); }
void ota_client_poll(uint64_t now) {
    if (!boot_confirmed && now>=10000) {
        esp_ota_img_states_t image_state;
        esp_err_t err=esp_ota_get_state_partition(running,&image_state);
        if (err==ESP_OK && image_state==ESP_OTA_IMG_PENDING_VERIFY) {
            if (esp_ota_mark_app_valid_cancel_rollback()!=ESP_OK) return;
            ESP_LOGI(TAG,"OTA boot confirmed");
        }
        boot_confirmed=true;
    }
    if ((handle_open || validated) && now-last_data_ms>180000) failed(ESP_ERR_TIMEOUT);
    if (restart_at && now>=restart_at) esp_restart();
}
bool ota_client_reboot_pending(void) { return restart_at!=0; }
void ota_client_runtime(serial_runtime_t *r) {
    r->antenna=XIAO_EXTERNAL_ANTENNA ? "external" : "internal";
    r->ota_enabled=available;r->ota_state=state;r->ota_slot=running ? running->label : "unknown";
    r->ota_file_version=XIAO_OTA_FILE_VERSION;r->ota_image_type=XIAO_OTA_IMAGE_TYPE;r->ota_manufacturer=XIAO_OTA_MANUFACTURER;
    r->ota_received=stream.received;r->ota_total=stream.expected;
}
esp_err_t ota_client_add_cluster(esp_zb_cluster_list_t *clusters) {
    esp_zb_ota_cluster_cfg_t config={.ota_upgrade_file_version=XIAO_OTA_FILE_VERSION,
        .ota_upgrade_downloaded_file_ver=XIAO_OTA_FILE_VERSION,.ota_upgrade_manufacturer=XIAO_OTA_MANUFACTURER,
        .ota_upgrade_image_type=XIAO_OTA_IMAGE_TYPE};
    esp_zb_attribute_list_t *ota=esp_zb_ota_cluster_create(&config);
    if (!ota) return ESP_ERR_NO_MEM;
    static esp_zb_zcl_ota_upgrade_client_variable_t variables={.timer_query=24*60,.hw_version=XIAO_OTA_HARDWARE_VERSION,.max_data_size=64};
    static uint16_t server_address=0x0000;
    static uint8_t server_endpoint=1;
    esp_err_t err=esp_zb_ota_cluster_add_attr(ota,ESP_ZB_ZCL_ATTR_OTA_UPGRADE_CLIENT_DATA_ID,&variables);
    if (err==ESP_OK) err=esp_zb_ota_cluster_add_attr(ota,ESP_ZB_ZCL_ATTR_OTA_UPGRADE_SERVER_ADDR_ID,&server_address);
    if (err==ESP_OK) err=esp_zb_ota_cluster_add_attr(ota,ESP_ZB_ZCL_ATTR_OTA_UPGRADE_SERVER_ENDPOINT_ID,&server_endpoint);
    if (err==ESP_OK) err=esp_zb_cluster_list_add_ota_cluster(clusters,ota,ESP_ZB_ZCL_CLUSTER_CLIENT_ROLE);
    return err;
}
