// SPDX-License-Identifier: MIT
#pragma once
#include "esp_zigbee_core.h"
#include "serial_session.h"

#define XIAO_OTA_MANUFACTURER 0x131b
#define XIAO_OTA_IMAGE_BASE 0xc600
#define XIAO_OTA_HARDWARE_VERSION 1
#define XIAO_OTA_IMAGE_TYPE (XIAO_OTA_IMAGE_BASE + XIAO_EXTERNAL_ANTENNA)
#define XIAO_OTA_ENDPOINT 1

void ota_client_init(bool (*prepare_restart)(void));
esp_err_t ota_client_add_cluster(esp_zb_cluster_list_t *clusters);
esp_err_t ota_client_action(esp_zb_core_action_callback_id_t id, const void *message);
void ota_client_joined(void);
void ota_client_poll(uint64_t now);
void ota_client_cancel(void);
bool ota_client_reboot_pending(void);
void ota_client_runtime(serial_runtime_t *runtime);
