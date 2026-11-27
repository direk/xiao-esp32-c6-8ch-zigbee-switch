// SPDX-License-Identifier: MIT
#include "input_router.h"
#include <string.h>
void endpoint_config_defaults(endpoint_config_t *c) {
    memset(c,0,sizeof(*c));c->version=3;
    for(unsigned i=0;i<CHANNELS;++i)c->pins[i]=(pin_config_t){.hold_ms=500,.multi_ms=500};
    for(unsigned i=0;i<ENDPOINTS;++i) {
        channel_defaults(&c->channels[i]);c->priorities[i]=1;
        c->channels[i].input_source=i<CHANNELS?i+1:SOURCE_NONE;
        c->channels[i].input_gesture=i<CHANNELS?GESTURE_LEGACY:i<16?GESTURE_DOUBLE:GESTURE_HOLD;
        if(i>=16)c->channels[i].function=MODE_LEVEL;
    }
}
bool endpoint_config_valid(const endpoint_config_t *c) {
    if(c->version!=3)return false;
    for(unsigned p=0;p<CHANNELS;++p) {
        const pin_config_t *pin=&c->pins[p];
        bool legacy=false,decoded=false;
        for(unsigned i=0;i<ENDPOINTS;++i)if(c->channels[i].input_source==p+1) {
            if(c->channels[i].input_gesture==GESTURE_LEGACY)legacy=true;else decoded=true;
        }
        if(legacy && decoded)return false;
        if(pin->mode>INPUT_ROCKER || pin->invert>1 || pin->hold_ms<250 || pin->hold_ms>5000 || pin->multi_ms<150 || pin->multi_ms>1000)return false;
    }
    for(unsigned i=0;i<ENDPOINTS;++i) {
        const channel_config_t *e=&c->channels[i];
        if(!channel_valid(e) || c->priorities[i]>1)return false;
        if(e->input_source) {
            const pin_config_t *p=&c->pins[e->input_source-1];
            if(e->input_mode!=p->mode || e->invert!=p->invert)return false;
            if(e->input_gesture!=GESTURE_LEGACY && p->mode!=INPUT_PUSH)return false;
        }
    }
    return true;
}
bool endpoint_config_pin(endpoint_config_t *c,unsigned pin,const pin_config_t *value) {
    if(pin>=CHANNELS)return false;
    c->pins[pin]=*value;
    for(unsigned i=0;i<ENDPOINTS;++i)if(c->channels[i].input_source==pin+1) {
        c->channels[i].input_mode=value->mode;c->channels[i].invert=value->invert;
    }
    return endpoint_config_valid(c);
}
void input_router_init(input_router_t *r,uint8_t active,uint64_t now) {
    memset(r,0,sizeof(*r));
    for(unsigned p=0;p<CHANNELS;++p)r->pins[p]=(pin_state_t){.raw=(active>>p)&1,.stable=(active>>p)&1,.changed=now};
}
void input_router_cancel(input_router_t *r,uint8_t active,uint64_t now){input_router_init(r,active,now);}
static bool listens(const endpoint_config_t *c,unsigned pin,unsigned gesture) {
    for(unsigned i=0;i<ENDPOINTS;++i)if(c->channels[i].input_source==pin+1 && c->channels[i].input_gesture==gesture)return true;
    return false;
}
static void fire(input_router_t *r,const endpoint_config_t *c,unsigned pin,unsigned gesture,uint64_t now) {
    for(unsigned i=0;i<ENDPOINTS;++i)if(c->channels[i].input_source==pin+1 && c->channels[i].input_gesture==gesture) {
        r->endpoints[i]=(routed_button_t){.down=true,.hold=gesture==GESTURE_HOLD,.until=now+100,.started=now};
    }
}
void input_router_poll(input_router_t *r,const endpoint_config_t *c,uint8_t active,uint64_t now) {
    for(unsigned i=0;i<ENDPOINTS;++i) {
        routed_button_t *b=&r->endpoints[i];
        if(!b->down || b->hold)continue;
        if(!b->observed && now-b->started>=DEBOUNCE_MS) {
            b->observed=true;
            if(now>=b->until)b->until=now+10; // Let the endpoint observe the delayed stable press.
        } else if(b->observed && now>=b->until)b->down=false;
    }
    for(unsigned p=0;p<CHANNELS;++p) {
        pin_state_t *s=&r->pins[p];bool value=(active>>p)&1;
        if(value!=s->raw){s->raw=value;s->changed=now;}
        if(s->stable!=s->raw && now-s->changed>=DEBOUNCE_MS) {
            s->stable=s->raw;
            if(s->stable){
                if(s->clicks && s->changed-s->released>c->pins[p].multi_ms){fire(r,c,p,GESTURE_SINGLE,now);s->clicks=0;}
                s->armed=true;s->pressed=now;
            }
            else if(s->armed) {
                s->armed=false;
                if(s->holding) {
                    for(unsigned i=0;i<ENDPOINTS;++i)if(c->channels[i].input_source==p+1 && c->channels[i].input_gesture==GESTURE_HOLD)r->endpoints[i].down=false;
                    s->holding=false;s->clicks=0;
                } else {
                    if(s->clicks<2)++s->clicks;
                    s->released=now;
                    if(s->clicks==2 && listens(c,p,GESTURE_DOUBLE)){fire(r,c,p,GESTURE_DOUBLE,now);s->clicks=0;}
                    else if(!listens(c,p,GESTURE_DOUBLE)){fire(r,c,p,GESTURE_SINGLE,now);s->clicks=0;}
                }
            }
        }
        if(s->stable && s->armed && !s->holding && now-s->pressed>=c->pins[p].hold_ms && listens(c,p,GESTURE_HOLD)) {
            s->holding=true;s->clicks=0;fire(r,c,p,GESTURE_HOLD,now);
        }
        if(!s->stable && !s->raw && s->clicks && now-s->released>=c->pins[p].multi_ms){fire(r,c,p,GESTURE_SINGLE,now);s->clicks=0;}
    }
}
bool input_router_active(const input_router_t *r,const endpoint_config_t *c,unsigned ep,uint8_t active) {
    if(ep>=ENDPOINTS)return false;
    const channel_config_t *e=&c->channels[ep];
    return e->input_gesture==GESTURE_LEGACY ? e->input_source && (active & (1U<<(e->input_source-1))) : r->endpoints[ep].down;
}
