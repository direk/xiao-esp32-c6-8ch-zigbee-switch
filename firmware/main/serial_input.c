// SPDX-License-Identifier: MIT
#include "serial_input.h"
#include <stdio.h>
#include <inttypes.h>
#include <string.h>

void serial_input_cancel(serial_input_t *s, unsigned channel) {
    if(channel<CHANNELS)memset(&s->buttons[channel],0,sizeof(s->buttons[channel]));
}
void serial_endpoint_cancel(serial_input_t *s,unsigned endpoint) {
    if(endpoint<ENDPOINTS)memset(&s->endpoint_buttons[endpoint],0,sizeof(s->endpoint_buttons[endpoint]));
}
void serial_input_tick(serial_input_t *s, uint64_t now) {
    for(unsigned i=0;i<CHANNELS+ENDPOINTS;++i) {
        serial_button_t *b=i<CHANNELS?&s->buttons[i]:&s->endpoint_buttons[i-CHANNELS];
        if(!b->down && !b->running)continue;
        if(now<b->deadline)continue;
        // Never compress overdue transitions into one poll: debounce must see each phase.
        if(b->running) {
            if(b->down) { b->down=false; if(b->remaining)--b->remaining; }
            else b->down=true;
            if(!b->remaining)b->running=false;
            b->deadline=now+(b->down?b->duration:100);
        } else b->down=false; // PRESS/SET watchdog.
    }
}
static bool number(const char *s,unsigned *value) {
    if(!s || !*s)return false;
    uint32_t n=0;
    for(;*s;++s) {
        if(*s<'0'||*s>'9')return false;
        uint32_t digit=(unsigned)(*s-'0');
        if(n>(UINT32_MAX-digit)/10)return false;
        n=n*10+digit;
    }
    *value=n;return true;
}
static serial_parse_result_t parse(serial_parser_t *s,serial_command_t *command,char *reply,size_t cap) {
    *command=(serial_command_t){.duration=100};
    char *save=NULL,*first=strtok_r(s->line," \t",&save);
    if(!first)return SERIAL_PARSE_NONE;
    if(first[0]=='@') {
        unsigned id;
        if(!number(first+1,&id)||!id) {snprintf(reply,cap,"ERR ID\n");return SERIAL_PARSE_ERROR;}
        command->has_id=true;command->request_id=id;
        first=strtok_r(NULL," \t",&save);
        if(!first) {snprintf(reply,cap,"ERR SYNTAX\n");return SERIAL_PARSE_ERROR;}
    }
    char *parts[4]={first};unsigned count=1;
    for(char *p=strtok_r(NULL," \t",&save);p;p=strtok_r(NULL," \t",&save)) {
        if(count==4) {snprintf(reply,cap,"ERR SYNTAX\n");return SERIAL_PARSE_ERROR;}
        parts[count++]=p;
    }
    const char *op=parts[0];
    if(count==1) {
        if(!strcmp(op,"PING"))command->operation=SERIAL_PING;
        else if(!strcmp(op,"INFO"))command->operation=SERIAL_INFO;
        else if(!strcmp(op,"STATUS"))command->operation=SERIAL_STATUS;
        else if(!strcmp(op,"RESET"))command->operation=SERIAL_RESET;
        else {snprintf(reply,cap,"ERR SYNTAX\n");return SERIAL_PARSE_ERROR;}
        return SERIAL_PARSE_COMMAND;
    }
    if(!strcmp(op,"LOG")) {
        if(count!=2 || (strcmp(parts[1],"0") && strcmp(parts[1],"1"))) {
            snprintf(reply,cap,"ERR SYNTAX\n");return SERIAL_PARSE_ERROR;
        }
        command->operation=SERIAL_LOG;command->duration=parts[1][0]=='1';
        return SERIAL_PARSE_COMMAND;
    }
    if(count==2 && !strcmp(op,"RELEASE") && !strcmp(parts[1],"ALL")) {
        command->operation=SERIAL_RELEASE_ALL;return SERIAL_PARSE_COMMAND;
    }
    bool get=!strcmp(op,"GET"),status=!strcmp(op,"STATUS");
    bool press=!strcmp(op,"PRESS"),release=!strcmp(op,"RELEASE"),set=!strcmp(op,"SET");
    bool click=!strcmp(op,"CLICK"),dbl=!strcmp(op,"DOUBLE"),triple=!strcmp(op,"TRIPLE"),hold=!strcmp(op,"HOLD");
    if(!(get||status||press||release||set||click||dbl||triple||hold) || count!=(unsigned)((set||hold)?3:2)) {
        snprintf(reply,cap,"ERR SYNTAX\n");return SERIAL_PARSE_ERROR;
    }
    unsigned channel;
    command->endpoint_target=!strncmp(parts[1],"EP",2);
    const char *target=parts[1]+(command->endpoint_target?2:0);
    if(status && !command->endpoint_target){snprintf(reply,cap,"ERR SYNTAX\n");return SERIAL_PARSE_ERROR;}
    if(!number(target,&channel)||channel<1||channel>(command->endpoint_target?ENDPOINTS:CHANNELS)) {
        snprintf(reply,cap,"ERR CHANNEL\n");return SERIAL_PARSE_ERROR;
    }
    command->channel=channel-1;
    if(status){command->operation=SERIAL_STATUS;return SERIAL_PARSE_COMMAND;}
    if(get) {command->operation=SERIAL_GET;return SERIAL_PARSE_COMMAND;}
    if(hold && (!number(parts[2],&command->duration)||command->duration<600||command->duration>SERIAL_HOLD_MAX_MS)) {
        snprintf(reply,cap,"ERR DURATION\n");return SERIAL_PARSE_ERROR;
    }
    if(set && strcmp(parts[2],"ON") && strcmp(parts[2],"OFF")) {
        snprintf(reply,cap,"ERR SYNTAX\n");return SERIAL_PARSE_ERROR;
    }
    command->operation=press?SERIAL_PRESS:release?SERIAL_RELEASE:click?SERIAL_CLICK:
        dbl?SERIAL_DOUBLE:triple?SERIAL_TRIPLE:hold?SERIAL_HOLD:
        !strcmp(parts[2],"ON")?SERIAL_SET_ON:SERIAL_SET_OFF;
    return SERIAL_PARSE_COMMAND;
}
void serial_response_tag(const serial_command_t *command,char *reply,size_t cap) {
    if(!command->has_id)return;
    char prefix[16];int n=snprintf(prefix,sizeof(prefix),"@%"PRIu32" ",command->request_id);
    size_t length=strlen(reply);
    if(n<0 || (size_t)n+length>=cap) {snprintf(reply,cap,"ERR RESPONSE_SIZE\n");return;}
    memmove(reply+n,reply,length+1);memcpy(reply,prefix,(size_t)n);
}
// JSON values are fixed strings and validated numeric configuration, never input text.
static void describe(unsigned channel,bool endpoint,const channel_config_t *cfg,bool priority,char *reply,size_t cap) {
    static const char *modes[MODE_COUNT]={
        "toggle","momentary","multifunction","brightness_level","brightness_level_up",
        "brightness_level_down","move_to_color_temperature","move_to_color_temperature_up",
        "move_to_color_temperature_down","scene","brightness_level_no_onoff",
        "brightness_level_up_no_onoff","brightness_level_down_no_onoff","scene_cycle"
    };
    static const char *gestures[MODE_COUNT]={
        "\"single\"", "\"single\",\"release\"", "\"single\",\"double\",\"triple\",\"hold\",\"release\"",
        "\"single\",\"hold\",\"release\"", "\"single\",\"double\",\"hold\",\"release\"",
        "\"single\",\"double\",\"hold\",\"release\"", "\"hold\",\"release\"",
        "\"single\",\"hold\",\"release\"", "\"single\",\"hold\",\"release\"", "\"single\"",
        "\"hold\",\"release\"", "\"double\",\"hold\",\"release\"", "\"double\",\"hold\",\"release\"", "\"single\""
    };
    static const char *actions[]={"off","on","toggle","on_off"};
    if(!channel_valid(cfg)) {snprintf(reply,cap,"ERR CONFIG\n");return;}
    bool rocker=cfg->input_mode==INPUT_ROCKER;
    int length=snprintf(reply,cap,
        "{\"%s\":%u,\"input_mode\":\"%s\",\"switch_type\":\"%s\","
        "\"binding_priority\":\"%s\",\"invert\":%s,\"switch_actions\":\"%s\","
        "\"min_level\":%u,\"max_level\":%u,\"transition_time\":%u,"
        "\"color_temp_min\":%u,\"color_temp_max\":%u,\"scene_id\":%u,\"group_id\":%u,"
        "\"scene_list\":\"%.*s\",\"input_source\":\"%s\",\"input_gesture\":\"%s\",\"commands\":[%s],\"gestures\":[%s]}\n",
        endpoint?"endpoint":"channel",channel+1,rocker?"rocker":"push",modes[cfg->function],priority?"ON":"OFF",
        cfg->invert?"true":"false",actions[cfg->switch_action],
        cfg->min_level,cfg->max_level,cfg->transition_time,cfg->color_min,cfg->color_max,
        cfg->scene_id,cfg->scene_group,
        cfg->scene_list[0],(const char *)cfg->scene_list+1,
        (const char *[]) {"none","D0","D1","D2","D3","D4","D5","D8","D9"}[cfg->input_source],
        (const char *[]) {"legacy","single","double","hold"}[cfg->input_gesture],
        rocker?"\"GET\",\"PRESS\",\"RELEASE\",\"SET\"":
            "\"GET\",\"PRESS\",\"RELEASE\",\"CLICK\",\"DOUBLE\",\"TRIPLE\",\"HOLD\"",
        rocker?"\"close\",\"open\"":gestures[cfg->function]);
    if(length<0 || (size_t)length>=cap)snprintf(reply,cap,"ERR RESPONSE_SIZE\n");
}
void serial_input_execute(serial_input_t *s,const serial_command_t *c,uint64_t now,
                          const channel_config_t *cfg,const uint8_t *binding_priority,bool ready,char *reply,size_t cap) {
    serial_operation_t op=c->operation;
    if(op>SERIAL_GET) {snprintf(reply,cap,"ERR SYNTAX\n");return;}
    if(op==SERIAL_PING) {snprintf(reply,cap,"OK PING\n");return;}
    if(op==SERIAL_RELEASE_ALL) {
        for(unsigned i=0;i<CHANNELS;++i)serial_input_cancel(s,i);
        for(unsigned i=0;i<ENDPOINTS;++i)serial_endpoint_cancel(s,i);
        s->release_all=true;
        snprintf(reply,cap,"OK RELEASE ALL\n");return;
    }
    if(c->channel>=(c->endpoint_target?ENDPOINTS:CHANNELS)) {snprintf(reply,cap,"ERR CHANNEL\n");return;}
    if(op==SERIAL_GET) {
        if(!c->endpoint_target && s->pins) {
            unsigned pin=c->channel;const pin_config_t *p=&s->pins[pin];
            int n=snprintf(reply,cap,"{\"channel\":%u,\"input_source\":\"%s\",\"input_mode\":\"%s\",\"invert\":%s,\"hold_time\":%u,\"multi_click_time\":%u,\"commands\":[%s],\"endpoints\":[",
                pin+1,(const char *[]) {"D0","D1","D2","D3","D4","D5","D8","D9"}[pin],p->mode==INPUT_ROCKER?"rocker":"push",p->invert?"true":"false",p->hold_ms,p->multi_ms,p->mode==INPUT_ROCKER?"\"GET\",\"PRESS\",\"RELEASE\",\"SET\"":"\"GET\",\"PRESS\",\"RELEASE\",\"CLICK\",\"DOUBLE\",\"TRIPLE\",\"HOLD\"");
            if(n<0 || (size_t)n>=cap){snprintf(reply,cap,"ERR RESPONSE_SIZE\n");return;}
            size_t used=(size_t)n;bool first=true;
            for(unsigned ep=0;ep<ENDPOINTS;++ep)if(cfg[ep].input_source==pin+1) {
                n=snprintf(reply+used,cap-used,"%s%u",first?"":",",ep+1);
                if(n<0 || (size_t)n>=cap-used){snprintf(reply,cap,"ERR RESPONSE_SIZE\n");return;}
                used+=(size_t)n;first=false;
            }
            if(cap-used<4){snprintf(reply,cap,"ERR RESPONSE_SIZE\n");return;}
            memcpy(reply+used,"]}\n",4);
        } else describe(c->channel,c->endpoint_target,&cfg[c->channel],binding_priority[c->channel]!=0,reply,cap);
        return;
    }
    uint8_t mode=!c->endpoint_target && s->pins ? s->pins[c->channel].mode : cfg[c->channel].input_mode;
    if(op==SERIAL_HOLD && !c->endpoint_target && s->pins && c->duration<s->pins[c->channel].hold_ms+100U) {
        for(unsigned ep=0;ep<ENDPOINTS;++ep)if(cfg[ep].input_source==c->channel+1 && cfg[ep].input_gesture==GESTURE_HOLD) {
            snprintf(reply,cap,"ERR DURATION\n");return;
        }
    }
    bool up=op==SERIAL_RELEASE||op==SERIAL_SET_OFF;
    bool set=op==SERIAL_SET_ON||op==SERIAL_SET_OFF;
    if(!up && !ready) {snprintf(reply,cap,"ERR NOT_READY\n");return;}
    if(set && mode!=INPUT_ROCKER) {snprintf(reply,cap,"ERR MODE\n");return;}
    if(op>=SERIAL_CLICK && op<=SERIAL_HOLD && mode!=INPUT_PUSH) {
        snprintf(reply,cap,"ERR MODE\n");return;
    }
    serial_button_t *b=c->endpoint_target?&s->endpoint_buttons[c->channel]:&s->buttons[c->channel];
    if(up){if(c->endpoint_target)serial_endpoint_cancel(s,c->channel);else serial_input_cancel(s,c->channel);}
    else if(op==SERIAL_PRESS||set) {
        if(b->running) {snprintf(reply,cap,"ERR BUSY\n");return;}
        b->down=true;b->deadline=now+SERIAL_HOLD_MAX_MS;
    } else {
        if(b->down||b->running) {snprintf(reply,cap,"ERR BUSY\n");return;}
        *b=(serial_button_t){.down=true,.running=true,.remaining=op==SERIAL_TRIPLE?3:(op==SERIAL_DOUBLE?2:1),
            .duration=c->duration,.deadline=now+c->duration};
    }
    static const char *names[]={"PING","PRESS","RELEASE","CLICK","DOUBLE","TRIPLE","HOLD","SET","SET","RELEASE"};
    snprintf(reply,cap,"OK %s %s%u\n",names[op],c->endpoint_target?"EP":"",c->channel+1);
}
serial_parse_result_t serial_parser_feed(serial_parser_t *s,char byte,serial_command_t *command,char *reply,size_t cap) {
    if(s->discard_until_lf) {
        if(byte=='\n')s->discard_until_lf=false;
        return SERIAL_PARSE_NONE;
    }
    if(byte=='\n') {
        *command=(serial_command_t){0};
        s->line[s->length]=0;
        serial_parse_result_t result;
        if(s->overflow) {snprintf(reply,cap,"ERR LINE\n");result=SERIAL_PARSE_ERROR;}
        else result=parse(s,command,reply,cap);
        s->length=0;s->overflow=false;
        return result;
    }
    if(byte=='\r')return SERIAL_PARSE_NONE;
    if((byte<32 && byte!='\t') || (unsigned char)byte>126)s->overflow=true;
    if(s->length+1<sizeof(s->line))s->line[s->length++]=byte;
    else s->overflow=true;
    return SERIAL_PARSE_NONE;
}

void serial_parser_abort(serial_parser_t *s) {
    s->length=0;s->overflow=false;s->discard_until_lf=true;
}
