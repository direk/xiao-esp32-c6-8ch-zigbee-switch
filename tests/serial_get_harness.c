// SPDX-License-Identifier: MIT
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "serial_input.h"
int main(void) {
    channel_config_t cfg[CHANNELS];
    uint8_t priorities[CHANNELS];
    for(unsigned i=0;i<CHANNELS;++i) {
        channel_defaults(&cfg[i]);priorities[i]=i%2;
        memcpy(cfg[i].scene_list,"\077240,241,242,243,244,245,246,247,248,249,250,251,252,253,254,255",64);
        cfg[i].invert=i%2;cfg[i].switch_action=i%4;
        cfg[i].min_level=254;cfg[i].max_level=254;cfg[i].transition_time=600;
        cfg[i].color_min=1000;cfg[i].color_max=1000;cfg[i].scene_id=255;cfg[i].scene_group=0xfff7;
    }
    for(unsigned input=0;input<2;++input)
    for(unsigned mode=0;mode<(input?2:MODE_COUNT);++mode)
    for(unsigned ep=0;ep<CHANNELS;++ep) {
        cfg[ep].input_mode=input;cfg[ep].function=mode;
        serial_input_t state={0};state.buttons[ep].down=true;state.buttons[ep].running=true;
        serial_input_t before=state;
        serial_parser_t parser={0};serial_command_t command;
        char request[32],reply[SERIAL_REPLY_MAX];
        snprintf(request,sizeof(request),"GET %u\r\n",ep+1);
        for(const char *p=request;*p;++p) {
            serial_parse_result_t result=serial_parser_feed(&parser,*p,&command,reply,sizeof(reply));
            assert(result!=SERIAL_PARSE_ERROR);
            if(result==SERIAL_PARSE_COMMAND) {
                assert(command.operation==SERIAL_GET && command.channel==ep);
                serial_input_execute(&state,&command,0,cfg,priorities,false,reply,sizeof(reply));
                assert(reply[0]=='{' && strlen(reply)<sizeof(reply)-1 && reply[strlen(reply)-1]=='\n');
                fputs(reply,stdout);
            }
        }
        assert(!memcmp(&before,&state,sizeof(state)));
        char small[32];serial_input_execute(&state,&command,0,cfg,priorities,false,small,sizeof(small));
        assert(!strcmp(small,"ERR RESPONSE_SIZE\n"));
    }
}
