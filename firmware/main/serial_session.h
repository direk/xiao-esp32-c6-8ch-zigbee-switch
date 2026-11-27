// SPDX-License-Identifier: MIT
#pragma once
#include "serial_input.h"
#define SERIAL_PROTOCOL_VERSION 3
#define SERIAL_DEDUP_DEPTH 16

typedef struct {
    uint32_t rx_bytes, rx_lines, parse_errors, queue_rejected, fifo_overflows;
    uint32_t buffer_full, frame_errors, parity_errors, breaks, event_queue_full;
    uint32_t rx_resyncs, tx_errors;
} serial_uart_stats_t;
typedef struct {
    const char *firmware_version;
    const char *antenna, *ota_state, *ota_slot;
    bool ota_enabled;
    uint16_t ota_manufacturer, ota_image_type;
    uint32_t ota_file_version, ota_received, ota_total;
    serial_port_id_t port;
    bool logs_enabled;
    uint32_t log_dropped;
    uint32_t boot_id;
    bool joined, bindings_known, factory_new, reset_pending;
    uint16_t short_address;
    uint8_t physical_mask, binding_clusters[ENDPOINTS], control_path[ENDPOINTS];
    serial_uart_stats_t uart;
    unsigned command_queue_used, reply_queue_used;
} serial_runtime_t;
typedef struct {
    bool valid;
    serial_command_t command;
    char reply[SERIAL_WIRE_MAX];
} serial_cached_reply_t;
typedef struct {
    serial_cached_reply_t cache[SERIAL_DEDUP_DEPTH];
    unsigned next;
    uint32_t requests, executed, duplicates, id_conflicts, id_expired, last_id;
} serial_session_t;
typedef enum { SERIAL_EFFECT_NONE, SERIAL_EFFECT_RESET, SERIAL_EFFECT_LOG_OFF, SERIAL_EFFECT_LOG_ON } serial_effect_t;
// Only called from Zigbee. Effects are returned only for newly accepted requests.
// reset callback stops active operations and saves pending channel settings.
serial_effect_t serial_session_execute(serial_session_t *session, serial_input_t *state,
    const serial_command_t *command, uint64_t now, const channel_config_t *config,
    const uint8_t *priority, const serial_runtime_t *runtime,
    bool (*reset)(void *), void *reset_context, char *reply, size_t capacity);
// Allow pending Stop frames to leave; ACK is drained by UART or a 2s fallback expires.
bool serial_reset_ready(bool pending, uint64_t requested_at, bool ack_sent, uint64_t now);
