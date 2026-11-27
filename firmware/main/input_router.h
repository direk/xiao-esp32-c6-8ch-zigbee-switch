// SPDX-License-Identifier: MIT
#pragma once
#include "control.h"

typedef struct {uint8_t mode,invert;uint16_t hold_ms,multi_ms;} pin_config_t;
typedef struct {
    uint8_t version;
    channel_config_t channels[ENDPOINTS];
    uint8_t priorities[ENDPOINTS];
    pin_config_t pins[CHANNELS];
} endpoint_config_t;
typedef struct {
    bool raw,stable,armed,holding,suppress;
    uint8_t clicks;
    uint64_t changed,pressed,released;
} pin_state_t;
typedef struct {bool down,hold,observed;uint64_t until,started;} routed_button_t;
typedef struct {pin_state_t pins[CHANNELS];routed_button_t endpoints[ENDPOINTS];} input_router_t;
void endpoint_config_defaults(endpoint_config_t *c);
bool endpoint_config_valid(const endpoint_config_t *c);
// Shares electrical pin settings and synchronizes corresponding endpoint aliases.
bool endpoint_config_pin(endpoint_config_t *c,unsigned pin,const pin_config_t *value);
void input_router_init(input_router_t *r,uint8_t active,uint64_t now);
void input_router_cancel(input_router_t *r,uint8_t active,uint64_t now);
void input_router_poll(input_router_t *r,const endpoint_config_t *c,uint8_t active,uint64_t now);
bool input_router_active(const input_router_t *r,const endpoint_config_t *c,unsigned ep,uint8_t active);
