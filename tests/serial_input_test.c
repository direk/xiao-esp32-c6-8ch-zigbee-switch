// SPDX-License-Identifier: MIT
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "serial_input.h"
static serial_input_t serial;
static serial_parser_t parser;
static channel_config_t cfg[CHANNELS];
static button_t buttons[CHANNELS];
static bool physical[CHANNELS];
static unsigned events[CHANNELS][ACT_COUNT];
static uint64_t now;
static uint8_t priorities[CHANNELS]={1,1,1,1,1,1,1,1};
static char response[SERIAL_REPLY_MAX];
static void reset(void) {
    memset(&parser,0,sizeof(parser));
    memset(&serial,0,sizeof(serial));memset(physical,0,sizeof(physical));
    memset(events,0,sizeof(events));now=0;
    for(unsigned i=0;i<CHANNELS;++i) {channel_defaults(&cfg[i]);button_init(&buttons[i],false,0);}
}
static void advance(unsigned ms) {
    for(unsigned t=0;t<ms;t+=10) {
        now+=10;serial_input_tick(&serial,now);
        for(unsigned i=0;i<CHANNELS;++i) {
            command_t command;
            if(button_poll(&buttons[i],physical[i]||serial.buttons[i].down,&cfg[i],now,&command))
                ++events[i][command.action];
        }
    }
}
static void send_ready(const char *text,const char *expected,bool ready) {
    response[0]=0;
    for(const char *p=text;*p;++p) {
        serial_command_t command;
        serial_parse_result_t result=serial_parser_feed(&parser,*p,&command,response,sizeof(response));
        if(result==SERIAL_PARSE_COMMAND)serial_input_execute(&serial,&command,now,cfg,priorities,ready,response,sizeof(response));
    }
    assert(!strcmp(response,expected));
}
static void send(const char *text,const char *expected) {send_ready(text,expected,true);}
int main(void) {
    reset();send("LOG 2\n","ERR SYNTAX\n");send("LOG ON\n","ERR SYNTAX\n");
    send("LOG\n","ERR SYNTAX\n");send("LOG 1 EXTRA\n","ERR SYNTAX\n");
    reset();send("GET 0\n","ERR CHANNEL\n");send("GET 9\n","ERR CHANNEL\n");
    send("GET -1\n","ERR CHANNEL\n");send("GET 1 EXTRA\n","ERR SYNTAX\n");
    send("GET\n","ERR SYNTAX\n");send("GET ALL\n","ERR CHANNEL\n");
    reset();send("PING\r\n","OK PING\n");
    send_ready("PRESS 1\n","ERR NOT_READY\n",false);assert(!serial.buttons[0].down);
    send("PRESS 0\n","ERR CHANNEL\n");send("PRESS 9\n","ERR CHANNEL\n");
    send("PRESS -1\n","ERR CHANNEL\n");send("PRESS 999999999999999999999\n","ERR CHANNEL\n");
    send("CLICK 1 EXTRA\n","ERR SYNTAX\n");send("SET 1 ON\n","ERR MODE\n");
    send("HOLD 1 599\n","ERR DURATION\n");send("HOLD 1 10001\n","ERR DURATION\n");
    send("HOLD 1 -1\n","ERR DURATION\n");send("ON 1\n","ERR SYNTAX\n");
    send("PING ONE TWO THREE FOUR\n","ERR SYNTAX\n");
    char long_line[100];memset(long_line,'X',98);long_line[98]='\n';long_line[99]=0;
    send(long_line,"ERR LINE\n");send("PING\n","OK PING\n");
    send("PRESS\x01 1\n","ERR LINE\n");
    reset();
    for(unsigned i=1;i<=CHANNELS;++i) {
        char cmd[32],expected[32];snprintf(cmd,sizeof(cmd),"PRESS %u\n",i);snprintf(expected,sizeof(expected),"OK PRESS %u\n",i);
        send(cmd,expected);
    }
    advance(100);
    for(unsigned i=0;i<CHANNELS;++i)assert(events[i][ACT_TOGGLE]==1);
    send("PRESS 1\n","OK PRESS 1\n");advance(100);assert(events[0][ACT_TOGGLE]==1);
    send("RELEASE ALL\n","OK RELEASE ALL\n");advance(100);
    for(unsigned i=0;i<CHANNELS;++i)assert(!serial.buttons[i].down);
    reset();cfg[0].function=MODE_MULTIFUNCTION;
    send("CLICK 1\n","OK CLICK 1\n");send("CLICK 1\n","ERR BUSY\n");advance(700);
    assert(events[0][ACT_SINGLE]==1 && !events[0][ACT_HOLD]);
    send("DOUBLE 1\n","OK DOUBLE 1\n");advance(1000);assert(events[0][ACT_DOUBLE]==1);
    send("TRIPLE 1\n","OK TRIPLE 1\n");advance(1200);assert(events[0][ACT_TRIPLE]==1);
    send("HOLD 1 1500\n","OK HOLD 1\n");advance(700);assert(events[0][ACT_HOLD]==1);
    advance(1000);assert(events[0][ACT_RELEASE]==1);
    reset();cfg[2].function=MODE_LEVEL_UP_NO_ONOFF;
    send("HOLD 3 1000\n","OK HOLD 3\n");advance(700);assert(events[2][ACT_LEVEL_UP]==1);
    advance(500);assert(events[2][ACT_LEVEL_STOP]==1);
    send("CLICK 3\n","OK CLICK 3\n");advance(700);assert(!events[2][ACT_ON]&&!events[2][ACT_TOGGLE]);
    send("DOUBLE 3\n","OK DOUBLE 3\n");advance(1000);assert(events[2][ACT_LEVEL_STEP_UP]==1);
    reset();cfg[1].input_mode=INPUT_ROCKER; // User's CH2 rocker/toggle, same planner for UART/USB.
    send("SET 2 ON\n","OK SET 2\n");advance(100);assert(events[1][ACT_TOGGLE]==1);
    send("SET 2 ON\n","OK SET 2\n");advance(100);assert(events[1][ACT_TOGGLE]==1);
    send("SET 2 OFF\n","OK SET 2\n");advance(100);assert(events[1][ACT_TOGGLE]==2);
    send("PRESS 2\n","OK PRESS 2\n");advance(100);assert(events[1][ACT_TOGGLE]==3);
    send("RELEASE 2\n","OK RELEASE 2\n");advance(100);assert(events[1][ACT_TOGGLE]==4);
    assert(!events[1][ACT_ON] && !events[1][ACT_OFF]);
    reset();cfg[0].input_mode=INPUT_ROCKER;cfg[0].function=MODE_MOMENTARY;
    send("CLICK 1\n","ERR MODE\n");send("SET 1 ON\n","OK SET 1\n");advance(100);
    assert(events[0][ACT_ON]==1);send("SET 1 ON\n","OK SET 1\n");advance(100);assert(events[0][ACT_ON]==1);
    send("SET 1 OFF\n","OK SET 1\n");advance(100);assert(events[0][ACT_OFF]==1);
    reset();cfg[0].function=MODE_MOMENTARY;cfg[0].switch_action=ACT_ON;
    physical[0]=true;advance(100);send("PRESS 1\n","OK PRESS 1\n");advance(100);
    send("RELEASE 1\n","OK RELEASE 1\n");advance(100);assert(!events[0][ACT_OFF]);
    physical[0]=false;advance(100);assert(events[0][ACT_ON]==1 && events[0][ACT_OFF]==1);
    reset();cfg[0].function=MODE_LEVEL_UP;
    send("PRESS 1\n","OK PRESS 1\n");advance(700);assert(events[0][ACT_LEVEL_UP]==1);
    advance(9500);assert(!serial.buttons[0].down && events[0][ACT_LEVEL_STOP]==1);
    send("HOLD 1 1500\n","OK HOLD 1\n");advance(700);send("RELEASE 1\n","OK RELEASE 1\n");advance(100);
    assert(events[0][ACT_LEVEL_STOP]==2);advance(2000);assert(!serial.buttons[0].down);
    reset();cfg[0].function=MODE_MOMENTARY;cfg[0].switch_action=ACT_ON;
    send("CLICK 1\n","OK CLICK 1\n");advance(300);
    assert(events[0][ACT_ON]==1 && events[0][ACT_OFF]==1);
    reset();cfg[0].function=MODE_SCENE;
    send("CLICK 1\n","OK CLICK 1\n");advance(700);assert(events[0][ACT_SCENE]==1);
    reset();cfg[0].input_mode=INPUT_ROCKER;cfg[0].function=MODE_MOMENTARY;
    send("SET 1 ON\n","OK SET 1\n");advance(6000);
    send("SET 1 ON\n","OK SET 1\n");advance(6000);
    assert(serial.buttons[0].down && events[0][ACT_ON]==1 && !events[0][ACT_OFF]);
    advance(4200);assert(!serial.buttons[0].down && events[0][ACT_OFF]==1);
    // A slow scheduler stretches phases instead of dropping edges.
    reset();send("TRIPLE 1\n","OK TRIPLE 1\n");now=1000;serial_input_tick(&serial,now);
    assert(!serial.buttons[0].down && serial.buttons[0].remaining==2);
    // Input arrives in fragments; no command is applied before LF.
    reset();send("PRESS 8","");assert(!serial.buttons[7].down);send("\r\n","OK PRESS 8\n");assert(serial.buttons[7].down);
    puts("PASS: UART parser, eight channels, gestures, rocker, watchdog, cancellation, physical OR and invalid input");
}
