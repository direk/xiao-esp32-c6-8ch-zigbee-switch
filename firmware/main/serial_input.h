// SPDX-License-Identifier: MIT
#pragma once
#include "control.h"
#define SERIAL_LINE_MAX 64
#define SERIAL_REPLY_MAX 768
#define SERIAL_HOLD_MAX_MS 10000

typedef struct {
    bool down, running;
    uint8_t remaining;
    uint16_t duration;
    uint64_t deadline;
} serial_button_t;
typedef struct { serial_button_t buttons[CHANNELS]; } serial_input_t;
typedef enum {
    SERIAL_PING, SERIAL_PRESS, SERIAL_RELEASE, SERIAL_CLICK, SERIAL_DOUBLE,
    SERIAL_TRIPLE, SERIAL_HOLD, SERIAL_SET_ON, SERIAL_SET_OFF, SERIAL_RELEASE_ALL, SERIAL_GET
} serial_operation_t;
typedef struct {
    serial_operation_t operation;
    unsigned channel; // Zero-based; unused by PING and RELEASE ALL.
    unsigned duration;
} serial_command_t;
typedef enum { SERIAL_PARSE_NONE, SERIAL_PARSE_COMMAND, SERIAL_PARSE_ERROR } serial_parse_result_t;
typedef struct {
    char line[SERIAL_LINE_MAX];
    size_t length;
    bool overflow;
} serial_parser_t;

// Parser state belongs exclusively to the UART task. No channel state/config access.
serial_parse_result_t serial_parser_feed(serial_parser_t *parser, char byte,
                                        serial_command_t *command, char *reply, size_t capacity);
// Channel state and configuration are accessed exclusively by the Zigbee scheduler.
void serial_input_execute(serial_input_t *s, const serial_command_t *command, uint64_t now,
                          const channel_config_t *config, const uint8_t *binding_priority,
                          bool ready, char *reply, size_t capacity);
void serial_input_tick(serial_input_t *s, uint64_t now);
void serial_input_cancel(serial_input_t *s, unsigned channel);
