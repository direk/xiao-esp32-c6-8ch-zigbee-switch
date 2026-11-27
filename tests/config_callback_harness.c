// SPDX-License-Identifier: MIT
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "control.h"
#include "input_router.h"
#include "serial_input.h"
#include "config_write_guard.h"
#define ESP_ZB_ZCL_STATUS_SUCCESS 0
#define ESP_ZB_ZCL_STATUS_FAIL 1
#define ESP_ZB_ZCL_STATUS_INVALID_VALUE 0x87
#define ESP_LOGI(...) ((void)0)
static endpoint_config_t cfg,write_candidate;
static input_router_t input_router;
static config_write_guard_t config_write_guard;
static serial_input_t serial_input;
static button_t inputs[ENDPOINTS];
static bool rebase[ENDPOINTS],reset_pending,dirty;
static uint64_t dirty_at;
#define binding_priority cfg.priorities
static unsigned sdk_updates,depth,stops;
static int check_value(uint16_t id,uint8_t ep,uint8_t *v);
static void write_attr(uint8_t ep,uint16_t id,uint8_t *v,uint16_t manufacturer);
static bool physical(unsigned pin){(void)pin;return false;}
static uint64_t now_ms(void){return 1234;}
static void set_attr(uint8_t ep,uint16_t id,void *value) {
    assert(++depth==1);++sdk_updates;
    // SDK 1.6.9 unconditionally invokes custom hooks for a local attribute setter.
    assert(check_value(id,ep,value)==ESP_ZB_ZCL_STATUS_SUCCESS);
    write_attr(ep,id,value,0);
    --depth;
}
static void dispatch(unsigned ep,const command_t *command) {
    (void)command;++stops;uint32_t event=123;
    set_attr(ep+1,0x20,&event); // Internal status callback must not clobber the outer candidate.
}
// MAIN_CONFIG_CALLBACKS
int main(void) {
    endpoint_config_defaults(&cfg);
    inputs[0].holding=true;inputs[0].held_cluster=CL_LEVEL;
    uint8_t single=GESTURE_SINGLE;
    assert(check_value(ATTR_GESTURE,1,&single)==ESP_ZB_ZCL_STATUS_SUCCESS);
    write_attr(1,ATTR_GESTURE,&single,0);
    assert(cfg.channels[0].input_gesture==GESTURE_SINGLE && endpoint_config_valid(&cfg));
    assert(dirty && dirty_at==1234 && stops==1 && sdk_updates==81 && !config_write_active(&config_write_guard));
    uint8_t hold=GESTURE_HOLD;
    write_attr(1,ATTR_GESTURE,&hold,0);
    assert(cfg.channels[0].input_gesture==GESTURE_HOLD && sdk_updates==161);
    uint8_t off=0;inputs[0].holding=true;unsigned previous=sdk_updates;
    write_attr(1,ATTR_BINDING_PRIORITY,&off,0);
    assert(!cfg.priorities[0] && inputs[0].holding && sdk_updates==previous);
    cfg.channels[8].input_source=SOURCE_D0; // Another decoded endpoint makes legacy invalid.
    uint8_t legacy=GESTURE_LEGACY;dirty=false;
    assert(check_value(ATTR_GESTURE,1,&legacy)==ESP_ZB_ZCL_STATUS_INVALID_VALUE);
    write_attr(1,ATTR_GESTURE,&legacy,0);
    assert(cfg.channels[0].input_gesture==GESTURE_HOLD && !dirty && sdk_updates==previous);
    uint8_t invert=1;write_attr(1,ATTR_PIN_INVERT,&invert,0);
    assert(cfg.pins[0].invert && cfg.channels[0].invert && cfg.channels[8].invert);
    assert(endpoint_config_valid(&cfg) && !config_write_active(&config_write_guard));
    puts("PASS: production CH1 write callbacks return through nested SDK setters, preserve candidates and accept subsequent writes");
}
