// SPDX-License-Identifier: MIT
#pragma once
#include "serial_input.h"
#define SERIAL_QUEUE_DEPTH 8
#define SERIAL_COMMANDS_PER_TICK 1

// Initialize queues/UART and start its dedicated, lower-priority FreeRTOS task.
void serial_transport_start(void);
// Called only by Zigbee scheduler. Queue operations have zero timeout; no UART I/O.
void serial_transport_poll(serial_input_t *state, const channel_config_t *config, const uint8_t *binding_priority, bool ready, uint64_t now);
