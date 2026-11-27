// SPDX-License-Identifier: MIT
#pragma once
#include "esp_zigbee_core.h"
#define DEVICE_DIAGNOSTICS_CLUSTER 0x0b05
#define DEVICE_ATTR_UPTIME 0x0030
#define DEVICE_ATTR_RESET_REASON 0x0031
#define DEVICE_ATTR_TX_REQUEST_ERRORS 0x0032
void device_services_init(void);
esp_err_t device_services_add_clusters(esp_zb_cluster_list_t *clusters, uint8_t endpoint);
void device_services_add_attributes(esp_zb_attribute_list_t *custom);
void device_services_poll(uint64_t now, bool joined);
void device_services_tx_result(esp_err_t result);
