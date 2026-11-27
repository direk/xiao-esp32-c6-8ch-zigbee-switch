// SPDX-License-Identifier: MIT
#pragma once
#include <stdbool.h>

typedef struct {bool active;} config_write_guard_t;
// Zigbee-context only: SDK setters invoke the custom write hook even with check=false.
// Internal writes must update SDK storage without replaying configuration side effects.
bool config_write_active(const config_write_guard_t *guard);
bool config_write_run(config_write_guard_t *guard,void (*apply)(void *),void *context);
