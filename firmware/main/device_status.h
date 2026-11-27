// SPDX-License-Identifier: MIT
#pragma once
#include <stdbool.h>
#include <stdint.h>

typedef struct {
    uint16_t resets;
    uint8_t reset_reason;
    uint32_t uptime, tx_request_errors;
    bool identifying;
    uint64_t identify_started;
} device_status_t;
uint16_t device_reset_count(uint16_t previous, bool stored);
void device_status_init(device_status_t *s, uint16_t resets, uint8_t reason);
// Active-low GPIO15 level; starts ON, changes every 250 ms, OFF immediately on stop.
int device_status_poll(device_status_t *s, uint64_t now, bool identify);
void device_status_tx_result(device_status_t *s, bool accepted);
