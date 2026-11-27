// SPDX-License-Identifier: MIT
#include "serial_session.h"
#include <stdio.h>
#include <stdarg.h>
#include <inttypes.h>
#include <string.h>

typedef struct {char *text;size_t capacity,used;bool failed;} json_t;
static void append(json_t *j,const char *format,...) {
    if(j->failed)return;
    va_list args;va_start(args,format);
    int n=vsnprintf(j->text+j->used,j->capacity-j->used,format,args);va_end(args);
    if(n<0 || (size_t)n>=j->capacity-j->used)j->failed=true;
    else j->used+=(size_t)n;
}
static void finish(json_t *j) {if(j->failed)snprintf(j->text,j->capacity,"ERR RESPONSE_SIZE\n");}
static void info(const serial_runtime_t *r,char *reply,size_t cap) {
    json_t j={.text=reply,.capacity=cap};
    append(&j,"{\"firmware\":\"%s\",\"protocol\":%u,\"model\":\"XIAO-C6-8CH\","
        "\"boot_id\":%"PRIu32",\"channels\":8,\"endpoints\":24,\"endpoint_addressing\":true,\"baud\":115200,\"format\":\"8N1\","
        "\"port\":\"%s\",\"transports\":[\"uart\",\"usb\"],\"logs_enabled\":%s,\"request_ids\":true,\"id_order\":\"increasing\",\"dedup_depth\":%u,\"hold_timeout_ms\":%u,\"commands\":["
        "\"PING\",\"INFO\",\"STATUS\",\"GET\",\"PRESS\",\"RELEASE\",\"CLICK\",\"DOUBLE\","
        "\"TRIPLE\",\"HOLD\",\"SET\",\"RELEASE ALL\",\"RESET\",\"LOG\"]}\n",
        r->firmware_version,SERIAL_PROTOCOL_VERSION,r->boot_id,
        r->port==SERIAL_PORT_USB?"usb":"uart",r->logs_enabled?"true":"false",SERIAL_DEDUP_DEPTH,SERIAL_HOLD_MAX_MS);
    // Append OTA diagnostics only to INFO; STATUS retains its bounded wire size.
    if (!j.failed && j.used>=2) {
        j.used-=2;
        append(&j,",\"antenna\":\"%s\",\"ota\":{\"enabled\":%s,\"manufacturer\":%u,\"image_type\":%u,"
            "\"file_version\":%"PRIu32",\"slot\":\"%s\",\"state\":\"%s\",\"received\":%"PRIu32",\"total\":%"PRIu32"}}\n",
            r->antenna?r->antenna:"unknown",r->ota_enabled?"true":"false",r->ota_manufacturer,r->ota_image_type,
            r->ota_file_version,r->ota_slot?r->ota_slot:"unknown",r->ota_state?r->ota_state:"idle",r->ota_received,r->ota_total);
    }
    finish(&j);
}
static void status(const serial_session_t *s,const serial_input_t *state,const serial_runtime_t *r,
                   uint64_t now,char *reply,size_t cap) {
    json_t j={.text=reply,.capacity=cap};
    append(&j,"{\"port\":\"%s\",\"logs_enabled\":%s,\"boot_id\":%"PRIu32",\"uptime_ms\":%"PRIu64",\"zigbee\":{\"joined\":%s,"
        "\"bindings_known\":%s,\"factory_new\":%s,\"reset_pending\":%s,\"short_address\":",
        r->port==SERIAL_PORT_USB?"usb":"uart",r->logs_enabled?"true":"false",r->boot_id,now,r->joined?"true":"false",r->bindings_known?"true":"false",
        r->factory_new?"true":"false",r->reset_pending?"true":"false");
    if(r->joined)append(&j,"%u",r->short_address);else append(&j,"null");
    append(&j,"},\"queues\":{\"commands\":%u,\"replies\":%u},\"channels\":[",r->command_queue_used,r->reply_queue_used);
    static const char *paths[]={"binding","z2m","mixed"};
    for(unsigned i=0;i<CHANNELS;++i) {
        append(&j,"%s{\"channel\":%u,\"binding_status\":\"%s\",\"binding_clusters\":%u,"
            "\"control_path\":\"%s\",\"physical\":%s,\"virtual\":%s,\"sequence_active\":%s}",
            i?",":"",i+1,!r->bindings_known?"unknown":r->binding_clusters[i]?"bound":"unbound",
            r->binding_clusters[i],paths[r->control_path[i]<3?r->control_path[i]:1],
            (r->physical_mask&(1U<<i))?"true":"false",state->buttons[i].down?"true":"false",
            state->buttons[i].running?"true":"false");
    }
    append(&j,"],\"counters\":{\"requests\":%"PRIu32",\"executed\":%"PRIu32",\"duplicates\":%"PRIu32",\"id_conflicts\":%"PRIu32,
        s->requests,s->executed,s->duplicates,s->id_conflicts);
#define COUNT(name,value) append(&j,",\"" name "\":%"PRIu32,(uint32_t)(value))
    COUNT("id_expired",s->id_expired);COUNT("log_dropped",r->log_dropped);
    COUNT("rx_bytes",r->uart.rx_bytes);COUNT("rx_lines",r->uart.rx_lines);
    COUNT("parse_errors",r->uart.parse_errors);COUNT("queue_rejected",r->uart.queue_rejected);
    COUNT("fifo_overflows",r->uart.fifo_overflows);COUNT("buffer_full",r->uart.buffer_full);
    COUNT("frame_errors",r->uart.frame_errors);COUNT("parity_errors",r->uart.parity_errors);
    COUNT("breaks",r->uart.breaks);COUNT("event_queue_full",r->uart.event_queue_full);
    COUNT("rx_resyncs",r->uart.rx_resyncs);COUNT("tx_errors",r->uart.tx_errors);
#undef COUNT
    append(&j,"}}\n");finish(&j);
}
static bool same(const serial_command_t *a,const serial_command_t *b) {
    return a->endpoint_target==b->endpoint_target && a->operation==b->operation && a->channel==b->channel && a->duration==b->duration;
}
serial_effect_t serial_session_execute(serial_session_t *s,serial_input_t *state,const serial_command_t *c,
    uint64_t now,const channel_config_t *cfg,const uint8_t *priority,const serial_runtime_t *r,
    bool (*reset)(void *),void *context,char *reply,size_t cap) {
    ++s->requests;
    if(c->has_id)for(unsigned i=0;i<SERIAL_DEDUP_DEPTH;++i) {
        serial_cached_reply_t *entry=&s->cache[i];
        if(!entry->valid || entry->command.request_id!=c->request_id)continue;
        if(same(&entry->command,c)) {
            ++s->duplicates;int n=snprintf(reply,cap,"%s",entry->reply);
            if(n<0 || (size_t)n>=cap)snprintf(reply,cap,"ERR RESPONSE_SIZE\n");
        }
        else {++s->id_conflicts;snprintf(reply,cap,"ERR ID_CONFLICT\n");serial_response_tag(c,reply,cap);}
        return SERIAL_EFFECT_NONE;
    }
    if(c->has_id && c->request_id<=s->last_id) {
        ++s->id_expired;snprintf(reply,cap,"ERR ID_EXPIRED\n");serial_response_tag(c,reply,cap);
        return SERIAL_EFFECT_NONE;
    }
    if(c->has_id)s->last_id=c->request_id;
    ++s->executed;
    serial_effect_t effect=SERIAL_EFFECT_NONE;
    size_t payload_cap=cap<SERIAL_REPLY_MAX?cap:SERIAL_REPLY_MAX;
    if(c->operation==SERIAL_INFO)info(r,reply,payload_cap);
    else if(c->operation==SERIAL_STATUS) {
        if(c->endpoint_target && c->channel<ENDPOINTS) {
            unsigned ep=c->channel;
            snprintf(reply,payload_cap,"{\"endpoint\":%u,\"bindings_known\":%s,\"binding_clusters\":%u,\"control_path\":\"%s\",\"virtual\":%s,\"sequence_active\":%s}\n",
                ep+1,r->bindings_known?"true":"false",r->binding_clusters[ep],
                (const char *[]) {"binding","z2m","mixed"}[r->control_path[ep]<3?r->control_path[ep]:1],
                state->endpoint_buttons[ep].down?"true":"false",state->endpoint_buttons[ep].running?"true":"false");
        } else status(s,state,r,now,reply,payload_cap);
    }
    else if(c->operation==SERIAL_LOG) {
        snprintf(reply,cap,"OK LOG %u\n",c->duration);
        effect=c->duration?SERIAL_EFFECT_LOG_ON:SERIAL_EFFECT_LOG_OFF;
    }
    else if(r->reset_pending && c->operation!=SERIAL_GET && c->operation!=SERIAL_PING)snprintf(reply,cap,"ERR RESET_PENDING\n");
    else if(c->operation==SERIAL_RESET) {
        if(!reset || !reset(context))snprintf(reply,cap,"ERR STORAGE\n");
        else {snprintf(reply,cap,"OK RESET\n");effect=SERIAL_EFFECT_RESET;}
    } else serial_input_execute(state,c,now,cfg,priority,r->joined && r->bindings_known,reply,payload_cap);
    serial_response_tag(c,reply,cap);
    if(c->has_id) {
        serial_cached_reply_t *entry=&s->cache[s->next];
        entry->valid=true;entry->command=*c;snprintf(entry->reply,sizeof(entry->reply),"%s",reply);
        s->next=(s->next+1)%SERIAL_DEDUP_DEPTH;
    }
    return effect;
}

bool serial_reset_ready(bool pending,uint64_t requested_at,bool ack_sent,uint64_t now) {
    if(!pending || now<requested_at)return false;
    uint64_t elapsed=now-requested_at;
    return elapsed>=200 && (ack_sent || elapsed>=2000);
}
