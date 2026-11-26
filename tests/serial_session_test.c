// SPDX-License-Identifier: MIT
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "serial_session.h"
static serial_session_t session;
static serial_input_t state;
static serial_runtime_t runtime;
static channel_config_t cfg[CHANNELS];
static uint8_t priority[CHANNELS];
static char reply[SERIAL_WIRE_MAX];
static unsigned resets;
static bool reset_fails;
static bool reset_callback(void *unused) {(void)unused;++resets;return !reset_fails;}
static void init(void) {
    memset(&session,0,sizeof(session));memset(&state,0,sizeof(state));resets=0;reset_fails=false;
    runtime=(serial_runtime_t){.firmware_version="2.2.0",.antenna="internal",.ota_enabled=true,.ota_manufacturer=0x131b,.ota_image_type=0xc600,
        .ota_file_version=0x02020000,.ota_slot="ota_1",.ota_state="downloading",.ota_received=UINT32_MAX,.ota_total=UINT32_MAX,.boot_id=UINT32_MAX,.joined=true,.bindings_known=true,.short_address=0xabcd};
    for(unsigned i=0;i<CHANNELS;++i) {channel_defaults(&cfg[i]);priority[i]=1;}
}
static bool request(const char *line) {
    serial_parser_t parser={0};serial_command_t command={0};bool started=false;
    for(const char *p=line;*p;++p) {
        serial_parse_result_t result=serial_parser_feed(&parser,*p,&command,reply,sizeof(reply));
        if(result==SERIAL_PARSE_COMMAND)started=serial_session_execute(&session,&state,&command,123456,cfg,priority,&runtime,reset_callback,NULL,reply,sizeof(reply));
        else if(result==SERIAL_PARSE_ERROR)serial_response_tag(&command,reply,sizeof(reply));
    }
    return started;
}
int main(int argc,char **argv) {
    (void)argv;init();
    if(argc>1) {
        runtime.joined=false;runtime.bindings_known=false;runtime.factory_new=true;
        request("INFO\n");fputs(reply,stdout);
        request("STATUS\n");fputs(reply,stdout);
        runtime.joined=runtime.bindings_known=true;runtime.factory_new=false;
        runtime.log_dropped=UINT32_MAX;runtime.physical_mask=0x81;runtime.reset_pending=true;
        for(unsigned i=0;i<CHANNELS;++i) {
            runtime.binding_clusters[i]=15;runtime.control_path[i]=i%3;
            state.buttons[i].down=true;state.buttons[i].running=true;
        }
        runtime.uart=(serial_uart_stats_t){UINT32_MAX,UINT32_MAX,UINT32_MAX,UINT32_MAX,
            UINT32_MAX,UINT32_MAX,UINT32_MAX,UINT32_MAX,UINT32_MAX,UINT32_MAX,UINT32_MAX,UINT32_MAX};
        session.requests=session.executed=session.duplicates=session.id_conflicts=session.id_expired=UINT32_MAX-1;
        request("@4294967295 STATUS\n");assert(reply[0]=='@' && strstr(reply,"}}\n"));fputs(reply,stdout);
        return 0;
    }
    request("@42 CLICK 2\n");assert(!strcmp(reply,"@42 OK CLICK 2\n") && state.buttons[1].running);
    uint64_t deadline=state.buttons[1].deadline;
    request("@42 CLICK 2\n");assert(!strcmp(reply,"@42 OK CLICK 2\n") && state.buttons[1].deadline==deadline && session.duplicates==1);
    serial_input_cancel(&state,1);request("@42 CLICK 2\n");assert(!state.buttons[1].running && session.duplicates==2);
    request("@42 CLICK 3\n");assert(!strcmp(reply,"@42 ERR ID_CONFLICT\n") && !state.buttons[2].down);
    request("@43 GET 2\n");char snapshot[SERIAL_WIRE_MAX];strcpy(snapshot,reply);
    cfg[1].function=MODE_LEVEL_UP_NO_ONOFF;
    request("@43 GET 2\n");assert(!strcmp(snapshot,reply));request("@44 GET 2\n");assert(strstr(reply,"brightness_level_up_no_onoff"));
    request("@45 CLICK 1\n");request("@46 CLICK 1\n");assert(!strcmp(reply,"@46 ERR BUSY\n"));
    serial_input_cancel(&state,0);request("@46 CLICK 1\n");assert(!strcmp(reply,"@46 ERR BUSY\n") && !state.buttons[0].down);
    request("@0 PING\n");assert(!strcmp(reply,"ERR ID\n"));request("@4294967296 PING\n");assert(!strcmp(reply,"ERR ID\n"));
    request("@-1 PING\n");assert(!strcmp(reply,"ERR ID\n"));request("@7 CLICK 9\n");assert(!strcmp(reply,"@7 ERR CHANNEL\n"));
    request("@7 HOLD 2 9999999999999999999\n");assert(!strcmp(reply,"@7 ERR DURATION\n"));
    request("@7\n");assert(!strcmp(reply,"@7 ERR SYNTAX\n"));request("RESET ALL\n");assert(!strcmp(reply,"ERR SYNTAX\n") && !resets);
    runtime.joined=false;runtime.bindings_known=false;
    request("INFO\n");assert(strstr(reply,"\"protocol\":2") && strstr(reply,"\"firmware\":\"2.2.0\""));
    request("STATUS\n");assert(strstr(reply,"\"joined\":false") && strstr(reply,"\"short_address\":null"));
    channel_config_t saved[CHANNELS];memcpy(saved,cfg,sizeof(saved));uint8_t saved_priority[CHANNELS];memcpy(saved_priority,priority,sizeof(priority));
    reset_fails=true;assert(!request("@100 RESET\n") && !strcmp(reply,"@100 ERR STORAGE\n"));
    reset_fails=false;assert(request("@101 RESET\n") && !strcmp(reply,"@101 OK RESET\n"));
    assert(resets==2);runtime.reset_pending=true;
    assert(!request("@101 RESET\n") && !strcmp(reply,"@101 OK RESET\n") && resets==2);
    request("@102 PRESS 1\n");assert(!strcmp(reply,"@102 ERR RESET_PENDING\n"));
    request("@103 RESET\n");assert(!strcmp(reply,"@103 ERR RESET_PENDING\n") && resets==2);
    request("GET 1\n");assert(reply[0]=='{');request("INFO\n");assert(reply[0]=='{');request("STATUS\n");assert(strstr(reply,"\"reset_pending\":true"));
    assert(!memcmp(saved,cfg,sizeof(saved)) && !memcmp(saved_priority,priority,sizeof(priority)));
    assert(!serial_reset_ready(false,100,true,1000));assert(!serial_reset_ready(true,101,true,100));
    assert(!serial_reset_ready(true,100,true,299));assert(serial_reset_ready(true,100,true,300));
    assert(!serial_reset_ready(true,100,false,2099));assert(serial_reset_ready(true,100,false,2100));
    init();request("@1 PRESS 1\n");serial_input_cancel(&state,0);
    for(unsigned id=2;id<=17;++id) {char line[32];snprintf(line,sizeof(line),"@%u PING\n",id);request(line);}
    request("@1 PRESS 1\n");assert(!state.buttons[0].down && session.duplicates==0 && session.id_expired==1);
    assert(!strcmp(reply,"@1 ERR ID_EXPIRED\n"));
    request("@2 PRESS 1\n");assert(!strcmp(reply,"@2 ERR ID_CONFLICT\n") && !state.buttons[0].down);
    puts("PASS: session IDs, exact replay, conflicts, bounded cache, INFO/STATUS, RESET callback/ACK gating and config preservation");
}
