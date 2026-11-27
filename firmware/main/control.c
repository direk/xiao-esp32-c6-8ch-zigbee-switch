// SPDX-License-Identifier: MIT
#include "control.h"
#include <string.h>

bool scene_list_parse(const uint8_t *text, uint8_t *ids, uint8_t *count) {
    *count=0;
    unsigned length=text[0];
    if(length>=SCENE_LIST_BYTES)return false;
    for(unsigned pos=1;pos<=length;) {
        unsigned value=0,digits=0;
        while(pos<=length && text[pos]>='0' && text[pos]<='9') {
            value=value*10+(text[pos++]-'0');
            if(++digits>3 || value>255)return false;
        }
        if(!digits || *count>=SCENE_LIST_MAX)return false;
        for(unsigned i=0;i<*count;++i)if(ids[i]==value)return false;
        ids[(*count)++]=(uint8_t)value;
        if(pos<=length && (text[pos++]!=',' || pos>length))return false;
    }
    return true;
}
uint8_t channel_legacy_store(const channel_config_t *channels, channel_config_v2_t *saved) {
    uint8_t mask=0;
    for(unsigned i=0;i<CHANNELS;++i) {
        memcpy(&saved[i],&channels[i],sizeof(saved[i]));
        if(saved[i].function==MODE_SCENE_CYCLE) {mask|=1U<<i;saved[i].function=MODE_SCENE;}
        if(saved[i].switch_action==SWITCH_ACTION_ON_OFF)saved[i].switch_action=ACT_TOGGLE;
    }
    return mask;
}
void channel_legacy_restore(channel_config_t *channels, const channel_config_v2_t *saved,
                           const uint8_t lists[CHANNELS][SCENE_LIST_BYTES], uint8_t cycle_mask) {
    for(unsigned i=0;i<CHANNELS;++i) {
        channel_defaults(&channels[i]);
        memcpy(&channels[i],&saved[i],sizeof(saved[i]));
        if(lists)memcpy(channels[i].scene_list,lists[i],SCENE_LIST_BYTES);
        if((cycle_mask & (1U<<i)) && channels[i].function==MODE_SCENE && channels[i].input_mode==INPUT_PUSH)
            channels[i].function=MODE_SCENE_CYCLE;
    }
}
void channel_defaults(channel_config_t *c) {
    *c = (channel_config_t){.switch_action=ACT_TOGGLE, .min_level=1, .max_level=254,
        .transition_time=25, .color_min=153, .color_max=500};
}
bool channel_valid(const channel_config_t *c) {
    uint8_t ids[SCENE_LIST_MAX],count;
    return scene_list_parse(c->scene_list,ids,&count) && c->input_source<=SOURCE_D9 && c->input_gesture<=GESTURE_HOLD && c->input_mode <= INPUT_ROCKER && c->invert <= 1 && c->function < MODE_COUNT &&
        c->switch_action <= SWITCH_ACTION_ON_OFF && c->min_level >= 1 && c->max_level <= 254 &&
        c->min_level <= c->max_level && c->transition_time <= 600 &&
        c->color_min >= 1 && c->color_min <= c->color_max && c->color_max <= 1000 &&
        c->scene_group <= 0xfff7 && (c->input_mode != INPUT_ROCKER || c->function <= MODE_MOMENTARY);
}
uint8_t channel_control_path(const channel_config_t *c, uint8_t mask) {
    uint8_t function=c->function;
    if(function==MODE_SCENE_CYCLE)return c->scene_group && c->scene_list[0] ? 0 : 1;
    uint8_t needed=function<=MODE_MOMENTARY ? 1 : function==MODE_MULTIFUNCTION ? 0 :
        function==MODE_SCENE ? 8 : (function>=MODE_COLOR && function<=MODE_COLOR_DOWN) ? 4 :
        function>=MODE_LEVEL_NO_ONOFF ? 2 : 3;
    uint8_t available=mask & needed;
    return (function==MODE_SCENE && c->scene_group) || (needed && available==needed) ? 0 : available ? 2 : 1;
}
void button_init(button_t *s, bool active, uint64_t now) {
    *s = (button_t){.raw=active, .stable=active, .changed_ms=now};
}
void button_rebase(button_t *s, bool active, uint64_t now) {
    bool phase=s->on_off_next_off;
    uint8_t scene_index=s->scene_index;
    button_init(s,active,now);
    s->on_off_next_off=phase;
    s->scene_index=scene_index;
}
uint8_t channel_actions_store(const channel_config_t *channels, channel_config_t *saved) {
    memcpy(saved,channels,CHANNELS*sizeof(*saved));
    uint8_t mask=0;
    for(unsigned i=0;i<CHANNELS;++i)if(saved[i].switch_action==SWITCH_ACTION_ON_OFF) {
        mask|=1U<<i;
        saved[i].switch_action=ACT_TOGGLE;
    }
    return mask;
}
void channel_actions_restore(channel_config_t *channels, uint8_t mask) {
    for(unsigned i=0;i<CHANNELS;++i)
        if((mask & (1U<<i)) && channels[i].switch_action==ACT_TOGGLE)
            channels[i].switch_action=SWITCH_ACTION_ON_OFF;
}
static void base(command_t *o, uint16_t cluster, uint8_t id, uint8_t action) {
    *o = (command_t){.cluster=cluster, .id=id, .action=action};
}
static void u16(uint8_t *p, uint16_t v) { p[0]=v; p[1]=v>>8; }
static bool no_onoff(uint8_t mode) { return mode >= MODE_LEVEL_NO_ONOFF && mode <= MODE_LEVEL_DOWN_NO_ONOFF; }
static bool is_level(uint8_t mode) { return (mode>=MODE_LEVEL && mode<=MODE_LEVEL_DOWN) || no_onoff(mode); }
static bool alternate(uint8_t mode) { return mode==MODE_LEVEL || mode==MODE_LEVEL_NO_ONOFF || mode==MODE_COLOR; }
static bool fixed_down(uint8_t mode) { return mode==MODE_LEVEL_DOWN || mode==MODE_LEVEL_DOWN_NO_ONOFF || mode==MODE_COLOR_DOWN; }
static void report(command_t *o, uint8_t action) { base(o,0,0,action); o->report_only=true; }
static void onoff(command_t *o, uint8_t id) { base(o,CL_ONOFF,id,id); }
static uint8_t push_action(button_t *s, const channel_config_t *c) {
    if(c->switch_action!=SWITCH_ACTION_ON_OFF)return c->switch_action;
    uint8_t action=s->on_off_next_off ? ACT_OFF : ACT_ON;
    s->on_off_next_off=!s->on_off_next_off;
    return action;
}
static void options(command_t *o, bool nooff) {
    if (nooff) { o->data[o->length++]=1; o->data[o->length++]=0; } // ExecuteIfOff mask=1, override=0.
}
static void stop(command_t *o, uint16_t cluster, bool nooff) {
    base(o,cluster,cluster==CL_LEVEL ? 0x03 : 0x47,cluster==CL_LEVEL ? ACT_LEVEL_STOP : ACT_COLOR_STOP);
    if (cluster==CL_LEVEL) options(o,nooff);
}
bool button_cancel(button_t *s, command_t *o) {
    if (!s->holding) return false;
    s->holding=false; s->clicks=0;
    if (!s->held_cluster) report(o,ACT_RELEASE);
    else stop(o,s->held_cluster,s->held_no_onoff);
    return true;
}
static bool hold(button_t *s, const channel_config_t *c, command_t *o) {
    if (c->function==MODE_MULTIFUNCTION) {
        report(o,ACT_HOLD); s->held_cluster=0;
    } else if (is_level(c->function)) {
        bool down = alternate(c->function) ? s->direction_down : fixed_down(c->function);
        if (alternate(c->function)) s->direction_down=!s->direction_down;
        bool safe = no_onoff(c->function);
        // Ordinary brightening turns lamps on; ordinary dimming does not switch them off on hold.
        base(o,CL_LEVEL,(!safe && !down) ? 0x04 : 0x00,down ? ACT_LEVEL_DOWN : ACT_LEVEL_UP);
        o->data[0]=down ? c->min_level : c->max_level;
        u16(o->data+1,c->transition_time); o->length=3; options(o,safe);
        s->held_cluster=CL_LEVEL; s->held_no_onoff=safe;
    } else if (c->function>=MODE_COLOR && c->function<=MODE_COLOR_DOWN) {
        bool down = alternate(c->function) ? s->direction_down : fixed_down(c->function);
        if (alternate(c->function)) s->direction_down=!s->direction_down;
        base(o,CL_COLOR,0x0a,down ? ACT_COLOR_DOWN : ACT_COLOR_UP);
        u16(o->data,down ? c->color_min : c->color_max);
        u16(o->data+2,c->transition_time); o->length=4;
        s->held_cluster=CL_COLOR;
    } else return false;
    s->holding=true; s->clicks=0;
    return true;
}
static bool clicks(const channel_config_t *c, uint8_t count, command_t *o) {
    if (c->function==MODE_MULTIFUNCTION && count<=3) { report(o,ACT_SINGLE+count-1); return true; }
    if (is_level(c->function)) {
        bool down=fixed_down(c->function), safe=no_onoff(c->function);
        if (count==1 && !safe) { onoff(o,alternate(c->function) ? ACT_TOGGLE : (down ? ACT_OFF : ACT_ON)); return true; }
        if (count==2 && !alternate(c->function)) {
            base(o,CL_LEVEL,safe ? 0x02 : 0x06,down ? ACT_LEVEL_STEP_DOWN : ACT_LEVEL_STEP_UP);
            o->data[0]=down; o->data[1]=25; u16(o->data+2,1); o->length=4;
            options(o,safe); return true;
        }
    } else if (c->function>=MODE_COLOR && c->function<=MODE_COLOR_DOWN && count==1 && !alternate(c->function)) {
        bool down=fixed_down(c->function);
        base(o,CL_COLOR,0x4c,down ? ACT_COLOR_STEP_DOWN : ACT_COLOR_STEP_UP);
        o->data[0]=down ? 3 : 1; // Color temperature step modes are 1=up, 3=down.
        u16(o->data+1,87); u16(o->data+3,1);
        u16(o->data+5,c->color_min); u16(o->data+7,c->color_max);
        o->length=9; return true;
    }
    return false;
}
bool button_poll(button_t *s, bool active, const channel_config_t *c, uint64_t now, command_t *o) {
    if (active!=s->raw) { s->raw=active; s->changed_ms=now; }
    bool edge=s->stable!=s->raw && now-s->changed_ms>=DEBOUNCE_MS;
    if (edge) {
        s->stable=s->raw;
        if (c->input_mode==INPUT_ROCKER) {
            // Toggle must follow edges, not the contact position or a cached lamp state.
            onoff(o,c->function==MODE_TOGGLE ? ACT_TOGGLE : (s->stable ? ACT_ON : ACT_OFF));
            return true;
        }
        if (s->stable) {
            s->pressed_ms=now; s->armed=true;
            if (c->function<=MODE_MOMENTARY) { onoff(o,push_action(s,c)); return true; }
            if (c->function==MODE_SCENE || c->function==MODE_SCENE_CYCLE) {
                uint8_t scene=c->scene_id;
                if(c->function==MODE_SCENE_CYCLE) {
                    uint8_t ids[SCENE_LIST_MAX],count;
                    if(!c->scene_group || !scene_list_parse(c->scene_list,ids,&count) || !count)return false;
                    scene=ids[s->scene_index%count];s->scene_index=(s->scene_index+1)%count;
                }
                base(o,CL_SCENES,0x05,ACT_SCENE); o->group=c->scene_group;
                u16(o->data,c->scene_group); o->data[2]=scene; o->length=3; return true;
            }
        } else {
            if (!s->armed) return false;
            s->armed=false;
            if (c->function==MODE_MOMENTARY) {
                onoff(o,c->switch_action==SWITCH_ACTION_ON_OFF ? push_action(s,c) :
                        c->switch_action==ACT_TOGGLE ? ACT_TOGGLE : (c->switch_action^1));
                return true;
            }
            if (s->holding) return button_cancel(s,o);
            if (c->function>=MODE_MULTIFUNCTION && c->function!=MODE_SCENE && c->function!=MODE_SCENE_CYCLE && s->clicks<4) ++s->clicks;
        }
    }
    if (s->stable && s->armed && !s->holding && now-s->pressed_ms>=GESTURE_MS && c->function>=MODE_MULTIFUNCTION) {
        if (hold(s,c,o)) return true;
    }
    if (!s->stable && s->clicks && now-s->pressed_ms>=GESTURE_MS) {
        uint8_t n=s->clicks; s->clicks=0;
        return clicks(c,n,o);
    }
    return false;
}
size_t command_frame(const command_t *c, uint8_t sequence, uint8_t *out, size_t capacity) {
    if (c->report_only || c->length>sizeof(c->data) || capacity<(size_t)c->length+3) return 0;
    out[0]=0x11; out[1]=sequence; out[2]=c->id;
    memcpy(out+3,c->data,c->length);
    return c->length+3;
}
int binding_cluster_index(uint16_t cluster) {
    switch (cluster) { case CL_ONOFF:return 0; case CL_LEVEL:return 1; case CL_COLOR:return 2; case CL_SCENES:return 3; default:return -1; }
}
uint32_t binding_mask_add(uint32_t mask, uint8_t endpoint, uint16_t cluster, uint8_t mode, bool local, bool excluded) {
    if (local && !excluded && binding_cluster_index(cluster)>=0 && endpoint>=1 && endpoint<=ENDPOINTS && (mode==1 || mode==3)) mask|=1U<<(endpoint-1);
    return mask;
}
int binding_scan_page(binding_scan_t *s, uint8_t index, uint8_t total, uint8_t count, uint32_t page_mask) {
    if (index!=s->next || total>BINDING_CAPACITY || index+count>total || (index && total!=s->total) || (!count && index<total)) return -1;
    s->total=total; s->next=index+count; s->mask|=page_mask;
    return s->next==total ? 1 : 0;
}

bool command_direct(const command_t *cmd, bool bound) {
    return !cmd->report_only && (bound || cmd->group!=0);
}
bool command_reports(const command_t *cmd, bool bound, bool binding_priority) {
    return !command_direct(cmd,bound) || !binding_priority;
}
