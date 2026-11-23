// SPDX-License-Identifier: MIT
#pragma once
#include "serial_session.h"
#define SERIAL_QUEUE_DEPTH 8
#define SERIAL_COMMANDS_PER_TICK 1
#define SERIAL_EVENT_DEPTH 32

// Call first in app_main: install a nonblocking ESP_LOG router, default OFF.
void serial_transport_logging_init(void);
void serial_transport_start(void);
// Zigbee context: zero-timeout queue operations only, no UART I/O.
void serial_transport_poll(serial_input_t *state, const channel_config_t *config,
    const uint8_t *priority, const serial_runtime_t *runtime, uint64_t now,
    bool (*reset)(void *), void *reset_context);
bool serial_transport_reset_acknowledged(void);
