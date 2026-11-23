// SPDX-License-Identifier: MIT
#include "serial_input.h"
#include <stdio.h>
#include <string.h>

void serial_input_cancel(serial_input_t *s, unsigned channel) {
    if(channel<CHANNELS)memset(&s->buttons[channel],0,sizeof(s->buttons[channel]));
}
void serial_input_tick(serial_input_t *s, uint64_t now) {
    for(unsigned i=0;i<CHANNELS;++i) {
        serial_button_t *b=&s->buttons[i];
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
    unsigned n=0;
    for(;*s;++s) {
        if(*s<'0'||*s>'9'||n>10000)return false;
        n=n*10+(unsigned)(*s-'0');
    }
    *value=n;return true;
}
static serial_parse_result_t parse(serial_parser_t *s,serial_command_t *command,char *reply,size_t cap) {
    char *parts[4]={0},*save=NULL;
    unsigned count=0;
    for(char *p=strtok_r(s->line," \t",&save);p;p=strtok_r(NULL," \t",&save)) {
        if(count==4) {snprintf(reply,cap,"ERR SYNTAX\n");return SERIAL_PARSE_ERROR;}
        parts[count++]=p;
    }
    if(!count)return SERIAL_PARSE_NONE;
    const char *op=parts[0];
    *command=(serial_command_t){.duration=100};
    if(count==1 && !strcmp(op,"PING")) {command->operation=SERIAL_PING;return SERIAL_PARSE_COMMAND;}
    if(count==2 && !strcmp(op,"RELEASE") && !strcmp(parts[1],"ALL")) {
        command->operation=SERIAL_RELEASE_ALL;return SERIAL_PARSE_COMMAND;
    }
    bool get=!strcmp(op,"GET");
    bool press=!strcmp(op,"PRESS"),release=!strcmp(op,"RELEASE"),set=!strcmp(op,"SET");
    bool click=!strcmp(op,"CLICK"),dbl=!strcmp(op,"DOUBLE"),triple=!strcmp(op,"TRIPLE"),hold=!strcmp(op,"HOLD");
    if(!(get||press||release||set||click||dbl||triple||hold) || count!=(unsigned)((set||hold)?3:2)) {
        snprintf(reply,cap,"ERR SYNTAX\n");return SERIAL_PARSE_ERROR;
    }
    unsigned channel;
    if(!number(parts[1],&channel)||channel<1||channel>CHANNELS) {
        snprintf(reply,cap,"ERR CHANNEL\n");return SERIAL_PARSE_ERROR;
    }
    command->channel=channel-1;
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
// JSON values are fixed strings and validated numeric configuration, never input text.
static void describe(unsigned channel,const channel_config_t *cfg,bool priority,char *reply,size_t cap) {
    static const char *modes[MODE_COUNT]={
        "toggle","momentary","multifunction","brightness_level","brightness_level_up",
        "brightness_level_down","move_to_color_temperature","move_to_color_temperature_up",
        "move_to_color_temperature_down","scene","brightness_level_no_onoff",
        "brightness_level_up_no_onoff","brightness_level_down_no_onoff"
    };
    static const char *gestures[MODE_COUNT]={
        "\"single\"", "\"single\",\"release\"", "\"single\",\"double\",\"triple\",\"hold\",\"release\"",
        "\"single\",\"hold\",\"release\"", "\"single\",\"double\",\"hold\",\"release\"",
        "\"single\",\"double\",\"hold\",\"release\"", "\"hold\",\"release\"",
        "\"single\",\"hold\",\"release\"", "\"single\",\"hold\",\"release\"", "\"single\"",
        "\"hold\",\"release\"", "\"double\",\"hold\",\"release\"", "\"double\",\"hold\",\"release\""
    };
    static const char *actions[]={"off","on","toggle"};
    if(!channel_valid(cfg)) {snprintf(reply,cap,"ERR CONFIG\n");return;}
    bool rocker=cfg->input_mode==INPUT_ROCKER;
    int length=snprintf(reply,cap,
        "{\"channel\":%u,\"input_mode\":\"%s\",\"switch_type\":\"%s\","
        "\"binding_priority\":\"%s\",\"invert\":%s,\"switch_actions\":\"%s\","
        "\"min_level\":%u,\"max_level\":%u,\"transition_time\":%u,"
        "\"color_temp_min\":%u,\"color_temp_max\":%u,\"scene_id\":%u,\"group_id\":%u,"
        "\"commands\":[%s],\"gestures\":[%s]}\n",
        channel+1,rocker?"rocker":"push",modes[cfg->function],priority?"ON":"OFF",
        cfg->invert?"true":"false",actions[cfg->switch_action],
        cfg->min_level,cfg->max_level,cfg->transition_time,cfg->color_min,cfg->color_max,
        cfg->scene_id,cfg->scene_group,
        rocker?"\"GET\",\"PRESS\",\"RELEASE\",\"SET\"":
            "\"GET\",\"PRESS\",\"RELEASE\",\"CLICK\",\"DOUBLE\",\"TRIPLE\",\"HOLD\"",
        rocker?"\"close\",\"open\"":gestures[cfg->function]);
    if(length<0 || (size_t)length>=cap)snprintf(reply,cap,"ERR RESPONSE_SIZE\n");
}
void serial_input_execute(serial_input_t *s,const serial_command_t *c,uint64_t now,
                          const channel_config_t *cfg,const uint8_t *binding_priority,bool ready,char *reply,size_t cap) {
    serial_operation_t op=c->operation;
    if(op==SERIAL_PING) {snprintf(reply,cap,"OK PING\n");return;}
    if(op==SERIAL_RELEASE_ALL) {
        for(unsigned i=0;i<CHANNELS;++i)serial_input_cancel(s,i);
        snprintf(reply,cap,"OK RELEASE ALL\n");return;
    }
    if(c->channel>=CHANNELS) {snprintf(reply,cap,"ERR CHANNEL\n");return;}
    if(op==SERIAL_GET) {describe(c->channel,&cfg[c->channel],binding_priority[c->channel]!=0,reply,cap);return;}
    bool up=op==SERIAL_RELEASE||op==SERIAL_SET_OFF;
    bool set=op==SERIAL_SET_ON||op==SERIAL_SET_OFF;
    if(!up && !ready) {snprintf(reply,cap,"ERR NOT_READY\n");return;}
    if(set && cfg[c->channel].input_mode!=INPUT_ROCKER) {snprintf(reply,cap,"ERR MODE\n");return;}
    if(op>=SERIAL_CLICK && op<=SERIAL_HOLD && cfg[c->channel].input_mode!=INPUT_PUSH) {
        snprintf(reply,cap,"ERR MODE\n");return;
    }
    serial_button_t *b=&s->buttons[c->channel];
    if(up)serial_input_cancel(s,c->channel);
    else if(op==SERIAL_PRESS||set) {
        if(b->running) {snprintf(reply,cap,"ERR BUSY\n");return;}
        b->down=true;b->deadline=now+SERIAL_HOLD_MAX_MS;
    } else {
        if(b->down||b->running) {snprintf(reply,cap,"ERR BUSY\n");return;}
        *b=(serial_button_t){.down=true,.running=true,.remaining=op==SERIAL_TRIPLE?3:(op==SERIAL_DOUBLE?2:1),
            .duration=c->duration,.deadline=now+c->duration};
    }
    static const char *names[]={"PING","PRESS","RELEASE","CLICK","DOUBLE","TRIPLE","HOLD","SET","SET","RELEASE"};
    snprintf(reply,cap,"OK %s %u\n",names[op],c->channel+1);
}
serial_parse_result_t serial_parser_feed(serial_parser_t *s,char byte,serial_command_t *command,char *reply,size_t cap) {
    if(byte=='\n') {
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
