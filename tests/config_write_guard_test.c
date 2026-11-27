// SPDX-License-Identifier: MIT
#include <assert.h>
#include <stdio.h>
#include "config_write_guard.h"
#include "input_router.h"
static config_write_guard_t guard;
static endpoint_config_t config;
static unsigned applications,internal_writes;
static void apply(void *value) {
    assert(config_write_active(&guard));++applications;
    config.channels[0].input_gesture=*(unsigned *)value;
    assert(endpoint_config_valid(&config));
    // Reproduce SDK behavior: each local set_attribute_val invokes the same hook,
    // including the 48 endpoint aliases and 32 shared pin attributes in main.c.
    for(unsigned i=0;i<ENDPOINTS*2+CHANNELS*4;++i) {
        ++internal_writes;
        assert(config_write_active(&guard)); // nested validation must not replace candidate
        assert(!config_write_run(&guard,apply,value));
    }
}
int main(void) {
    endpoint_config_defaults(&config);
    unsigned single=GESTURE_SINGLE,hold=GESTURE_HOLD;
    assert(config_write_run(&guard,apply,&single));
    assert(applications==1 && internal_writes==80 && config.channels[0].input_gesture==GESTURE_SINGLE);
    assert(!config_write_active(&guard));
    assert(config_write_run(&guard,apply,&hold));
    assert(applications==2 && internal_writes==160 && config.channels[0].input_gesture==GESTURE_HOLD);
    assert(!config_write_active(&guard));
    assert(!config_write_run(&guard,NULL,&single) && !config_write_active(&guard));
    puts("PASS: SDK nested write hooks cannot recurse, replace a pending configuration or block subsequent writes");
}
