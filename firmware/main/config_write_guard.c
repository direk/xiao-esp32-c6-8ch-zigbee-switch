// SPDX-License-Identifier: MIT
#include "config_write_guard.h"

bool config_write_active(const config_write_guard_t *guard){return guard->active;}
bool config_write_run(config_write_guard_t *guard,void (*apply)(void *),void *context) {
    if(guard->active || !apply)return false;
    guard->active=true;
    apply(context);
    guard->active=false;
    return true;
}
