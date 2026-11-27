// SPDX-License-Identifier: MIT
#include "device_services.h"
#include "device_status.h"
#include "control.h"
#include "driver/gpio.h"
#include "esp_system.h"
#include "nvs.h"
#include <string.h>

#define CONTROL_CLUSTER 0xfc00
#define LED_GPIO GPIO_NUM_15
#define READ_REPORT (ESP_ZB_ZCL_ATTR_ACCESS_READ_ONLY | ESP_ZB_ZCL_ATTR_ACCESS_REPORTING)
static device_status_t status;
static bool online;
static uint64_t next_report;
static int led_level=1;

void device_services_init(void) {
    // Separate namespace: network RESET and button configuration reset retain this counter.
    nvs_handle_t handle;
    ESP_ERROR_CHECK(nvs_open("xiao_diag",NVS_READWRITE,&handle));
    uint16_t previous=0;
    esp_err_t result=nvs_get_u16(handle,"resets",&previous);
    if(result!=ESP_OK && result!=ESP_ERR_NVS_NOT_FOUND)ESP_ERROR_CHECK(result);
    uint16_t resets=device_reset_count(previous,result==ESP_OK);
    ESP_ERROR_CHECK(nvs_set_u16(handle,"resets",resets));
    ESP_ERROR_CHECK(nvs_commit(handle));nvs_close(handle);
    device_status_init(&status,resets,(uint8_t)esp_reset_reason());
    ESP_ERROR_CHECK(gpio_set_level(LED_GPIO,1));
    gpio_config_t output={.pin_bit_mask=1ULL<<LED_GPIO,.mode=GPIO_MODE_OUTPUT};
    ESP_ERROR_CHECK(gpio_config(&output));
}
esp_err_t device_services_add_clusters(esp_zb_cluster_list_t *clusters,uint8_t endpoint) {
    // The standard switch factory already supplies Identify; ensure it exists on each endpoint.
    if(!esp_zb_cluster_list_get_cluster(clusters,ESP_ZB_ZCL_CLUSTER_ID_IDENTIFY,ESP_ZB_ZCL_CLUSTER_SERVER_ROLE)) {
        esp_zb_identify_cluster_cfg_t identify={.identify_time=0};
        esp_err_t result=esp_zb_cluster_list_add_identify_cluster(clusters,
            esp_zb_identify_cluster_create(&identify),ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);
        if(result!=ESP_OK)return result;
    }
    if(endpoint!=1)return ESP_OK;
    esp_zb_diagnostics_cluster_cfg_t config={};
    esp_zb_attribute_list_t *diag=esp_zb_diagnostics_cluster_create(&config);
    if(!diag)return ESP_ERR_NO_MEM;
    esp_err_t result=esp_zb_cluster_add_attr(diag,DEVICE_DIAGNOSTICS_CLUSTER,0x0000,
        ESP_ZB_ZCL_ATTR_TYPE_U16,READ_REPORT,&status.resets);
    if(result!=ESP_OK)return result;
    return esp_zb_cluster_list_add_diagnostics_cluster(clusters,diag,ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);
}
void device_services_add_attributes(esp_zb_attribute_list_t *custom) {
    ESP_ERROR_CHECK(esp_zb_custom_cluster_add_custom_attr(custom,DEVICE_ATTR_UPTIME,
        ESP_ZB_ZCL_ATTR_TYPE_U32,READ_REPORT,&status.uptime));
    ESP_ERROR_CHECK(esp_zb_custom_cluster_add_custom_attr(custom,DEVICE_ATTR_RESET_REASON,
        ESP_ZB_ZCL_ATTR_TYPE_U8,READ_REPORT,&status.reset_reason));
    ESP_ERROR_CHECK(esp_zb_custom_cluster_add_custom_attr(custom,DEVICE_ATTR_TX_REQUEST_ERRORS,
        ESP_ZB_ZCL_ATTR_TYPE_U32,READ_REPORT,&status.tx_request_errors));
}
static void publish(uint16_t cluster,uint16_t attr,void *value) {
    esp_zb_zcl_status_t result=esp_zb_zcl_set_attribute_val(1,cluster,ESP_ZB_ZCL_CLUSTER_SERVER_ROLE,attr,value,false);
    if(result!=ESP_ZB_ZCL_STATUS_SUCCESS)return;
    esp_zb_zcl_report_attr_cmd_t report={
        .zcl_basic_cmd={.dst_addr_u.addr_short=0,.dst_endpoint=1,.src_endpoint=1},
        .address_mode=ESP_ZB_APS_ADDR_MODE_16_ENDP_PRESENT,.clusterID=cluster,
        .direction=ESP_ZB_ZCL_CMD_DIRECTION_TO_CLI,.dis_default_resp=1,.attributeID=attr};
    esp_zb_zcl_report_attr_cmd_req(&report);
}
void device_services_poll(uint64_t now,bool joined) {
    bool identify=false;
    for(uint8_t ep=1;ep<=ENDPOINTS;++ep) {
        esp_zb_zcl_attr_t *attr=esp_zb_zcl_get_attribute(ep,ESP_ZB_ZCL_CLUSTER_ID_IDENTIFY,
            ESP_ZB_ZCL_CLUSTER_SERVER_ROLE,ESP_ZB_ZCL_ATTR_IDENTIFY_IDENTIFY_TIME_ID);
        uint16_t remaining=0;
        if(attr && attr->data_p)memcpy(&remaining,attr->data_p,sizeof(remaining));
        identify |= remaining!=0;
    }
    uint32_t previous_uptime=status.uptime;
    int level=device_status_poll(&status,now,identify);
    if(level!=led_level){gpio_set_level(LED_GPIO,level);led_level=level;}
    // Refresh the readable uptime even while offline. Counters point at live backing storage.
    if(status.uptime!=previous_uptime)esp_zb_zcl_set_attribute_val(1,CONTROL_CLUSTER,ESP_ZB_ZCL_CLUSTER_SERVER_ROLE,
        DEVICE_ATTR_UPTIME,&status.uptime,false);
    if(!joined){online=false;return;}
    if(!online){online=true;next_report=now+1000;}
    if(now<next_report)return;
    next_report=now+60000;
    publish(DEVICE_DIAGNOSTICS_CLUSTER,0x0000,&status.resets);
    publish(CONTROL_CLUSTER,DEVICE_ATTR_UPTIME,&status.uptime);
    publish(CONTROL_CLUSTER,DEVICE_ATTR_RESET_REASON,&status.reset_reason);
    publish(CONTROL_CLUSTER,DEVICE_ATTR_TX_REQUEST_ERRORS,&status.tx_request_errors);
}
void device_services_tx_result(esp_err_t result) {
    device_status_tx_result(&status,result==ESP_OK);
    esp_zb_zcl_set_attribute_val(1,CONTROL_CLUSTER,ESP_ZB_ZCL_CLUSTER_SERVER_ROLE,
        DEVICE_ATTR_TX_REQUEST_ERRORS,&status.tx_request_errors,false);
}
