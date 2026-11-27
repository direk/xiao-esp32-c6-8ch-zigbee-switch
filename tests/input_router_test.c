// SPDX-License-Identifier: MIT
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "input_router.h"
#include "serial_input.h"
static endpoint_config_t config;
static input_router_t router;
static serial_input_t serial;
static button_t buttons[ENDPOINTS];
static unsigned events[ENDPOINTS][ACT_COUNT];
static command_t last[ENDPOINTS];
static uint64_t now;
static uint8_t levels;
static void init(uint8_t boot) {
    now=0;levels=boot;memset(&serial,0,sizeof(serial));memset(events,0,sizeof(events));
    input_router_init(&router,boot,0);
    for(unsigned i=0;i<ENDPOINTS;++i)button_init(&buttons[i],input_router_active(&router,&config,i,boot),0);
}
static void advance(unsigned ms) {
    for(unsigned t=0;t<ms;t+=10) {
        now+=10;serial_input_tick(&serial,now);
        uint8_t mask=levels;
        for(unsigned p=0;p<CHANNELS;++p)if(serial.buttons[p].down)mask|=1U<<p;
        input_router_poll(&router,&config,mask,now);
        for(unsigned i=0;i<ENDPOINTS;++i) {
            if(router.endpoints[i].hold && router.endpoints[i].down && buttons[i].stable && buttons[i].armed && !buttons[i].holding)
                buttons[i].pressed_ms=now>=GESTURE_MS?now-GESTURE_MS:0;
            command_t command;
            if(button_poll(&buttons[i],input_router_active(&router,&config,i,mask)||serial.endpoint_buttons[i].down,&config.channels[i],now,&command)) {
                ++events[i][command.action];last[i]=command;
            }
        }
    }
}
static void contact(bool down,unsigned ms){levels=down?2:0;advance(ms);}
static void click(void){contact(true,100);contact(false,100);}
static void decoded(void) {
    endpoint_config_defaults(&config);
    config.channels[1].input_gesture=GESTURE_SINGLE;
    config.channels[1].switch_action=SWITCH_ACTION_ON_OFF;
    config.channels[9].input_source=SOURCE_D1;config.channels[9].function=MODE_SCENE_CYCLE;
    config.channels[9].scene_group=77;memcpy(config.channels[9].scene_list,"\0053,0,1",6);
    config.channels[17].input_source=SOURCE_D1;
    assert(endpoint_config_valid(&config));
}
int main(void) {
    endpoint_config_defaults(&config);assert(endpoint_config_valid(&config));init(0);
    contact(true,100);assert(events[1][ACT_TOGGLE]==1);contact(false,100);assert(events[1][ACT_TOGGLE]==1);
    // Mixed raw/decoded mappings are rejected, then explicit opt-in enables the new route.
    config.channels[9].input_source=SOURCE_D1;assert(!endpoint_config_valid(&config));
    decoded();init(0);click();advance(600);
    assert(events[1][ACT_ON]==1 && !events[9][ACT_SCENE] && !events[17][ACT_LEVEL_UP]);
    click();click();advance(600);
    assert(events[1][ACT_ON]==1 && !events[1][ACT_OFF] && events[9][ACT_SCENE]==1 && last[9].data[2]==3 && last[9].group==77);
    click();click();advance(600);assert(events[9][ACT_SCENE]==2 && last[9].data[2]==0);
    contact(true,700);assert(events[17][ACT_LEVEL_UP]==1);contact(false,200);
    assert(events[17][ACT_LEVEL_STOP]==1 && last[17].cluster==CL_LEVEL);
    advance(700);assert(events[1][ACT_ON]==1 && events[9][ACT_SCENE]==2);
    contact(true,700);contact(false,200);assert(events[17][ACT_LEVEL_DOWN]==1 && events[17][ACT_LEVEL_STOP]==2);
    // The second raw edge at the deadline is reserved through debounce; outside it gives two singles.
    decoded();init(0);click();advance(440);contact(true,100);contact(false,100);advance(600);
    assert(events[9][ACT_SCENE]==1 && !events[1][ACT_ON]);
    decoded();init(0);click();advance(450);contact(true,100);contact(false,100);advance(600);
    assert(!events[9][ACT_SCENE] && events[1][ACT_ON]==1 && events[1][ACT_OFF]==1);
    decoded();init(0);
    // Boot held produces neither click nor hold, then the next real press works.
    init(2);advance(2000);contact(false,200);advance(600);assert(!events[1][ACT_ON] && !events[17][ACT_LEVEL_UP]);
    click();advance(600);assert(events[1][ACT_ON]==1);
    // Release-all/config rebase suppresses held input until it is released.
    contact(true,700);input_router_cancel(&router,levels,now);
    for(unsigned i=0;i<ENDPOINTS;++i){command_t command;button_cancel(&buttons[i],&command);button_rebase(&buttons[i],input_router_active(&router,&config,i,levels),now);}
    unsigned holds=events[17][ACT_LEVEL_UP];advance(1500);assert(events[17][ACT_LEVEL_UP]==holds);
    contact(false,200);advance(600);
    // A delayed scheduler must retain a short mapped press through endpoint debounce.
    decoded();init(0);click();now+=600;input_router_poll(&router,&config,0,now);
    assert(router.endpoints[1].down);now+=300;input_router_poll(&router,&config,0,now);
    assert(router.endpoints[1].down);advance(10);assert(!router.endpoints[1].down);
    // Shared pin settings reject rocker for decoded gestures and propagate inversion.
    pin_config_t pin=config.pins[1];pin.mode=INPUT_ROCKER;
    endpoint_config_t candidate=config;assert(!endpoint_config_pin(&candidate,1,&pin));
    pin.mode=INPUT_PUSH;pin.invert=1;assert(endpoint_config_pin(&config,1,&pin));
    assert(config.channels[1].invert && config.channels[9].invert && config.channels[17].invert);
    // Mapping is independent of endpoint number; all 24 can be routed from one pin.
    endpoint_config_defaults(&config);
    for(unsigned i=0;i<ENDPOINTS;++i){config.channels[i].input_source=SOURCE_D1;config.channels[i].input_gesture=GESTURE_SINGLE;config.channels[i].function=MODE_TOGGLE;}
    assert(endpoint_config_valid(&config));init(0);click();advance(200);
    for(unsigned i=0;i<ENDPOINTS;++i)assert(events[i][ACT_TOGGLE]==1);
    for(unsigned i=0;i<ENDPOINTS;++i)config.channels[i].switch_action=SWITCH_ACTION_ON_OFF;
    init(0);click();advance(200);
    for(unsigned i=0;i<ENDPOINTS;++i)assert(events[i][ACT_ON]==1 && !events[i][ACT_OFF]);
    click();advance(200);for(unsigned i=0;i<ENDPOINTS;++i)assert(events[i][ACT_OFF]==1);
    serial.pins=config.pins;
    serial_command_t get={.operation=SERIAL_GET,.channel=1};char reply[SERIAL_REPLY_MAX];
    serial_input_execute(&serial,&get,now,config.channels,config.priorities,false,reply,sizeof(reply));
    assert(strstr(reply,"\"input_source\":\"D1\"") && strstr(reply,"\"endpoints\":[1,2,3,4"));
    endpoint_config_t saved=config;memset(&config,0,sizeof(config));memcpy(&config,&saved,sizeof(config));assert(endpoint_config_valid(&config));
    config.channels[23].input_source=9;assert(!endpoint_config_valid(&config));
    puts("PASS: 24 routed endpoints, legacy opt-in, single/double/hold isolation, Stop, boot/rebase, shared pins and stored configuration");
}
