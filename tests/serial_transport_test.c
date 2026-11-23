// SPDX-License-Identifier: MIT
#include <assert.h>
#include <setjmp.h>
#include <stdio.h>
#include <string.h>
#include "serial_transport.h"
#include "transport_stubs.h"
// Execute production transport with deterministic queues and one UART task slice.
struct mock_queue {unsigned depth,size,count,head;unsigned char data[8][SERIAL_REPLY_MAX];};
static struct mock_queue queues[2];
static unsigned created;
static bool zigbee_context;
static uint8_t priorities[CHANNELS]={1,1,1,1,1,1,1,1};
static void (*task_entry)(void *);
static jmp_buf task_yield;
static const char *incoming="";
static char outgoing[4096];
QueueHandle_t xQueueCreate(unsigned depth,unsigned size) {
    assert(created<2 && depth==8 && size<=SERIAL_REPLY_MAX);
    struct mock_queue *q=&queues[created++];q->depth=depth;q->size=size;return q;
}
BaseType_t xQueueSend(QueueHandle_t q,const void *item,TickType_t wait) {
    assert(wait==0);if(q->count==q->depth)return 0;
    memcpy(q->data[(q->head+q->count)%q->depth],item,q->size);++q->count;return pdTRUE;
}
BaseType_t xQueueReceive(QueueHandle_t q,void *item,TickType_t wait) {
    assert(wait==0);if(!q->count)return 0;
    memcpy(item,q->data[q->head],q->size);q->head=(q->head+1)%q->depth;--q->count;return pdTRUE;
}
UBaseType_t uxQueueSpacesAvailable(QueueHandle_t q) {return q->depth-q->count;}
BaseType_t xTaskCreate(void (*task)(void *),const char *name,unsigned stack,void *arg,unsigned priority,void *handle) {
    (void)name;(void)stack;(void)arg;(void)handle;assert(priority<5);task_entry=task;return pdPASS;
}
void vTaskDelay(TickType_t ticks) {assert(ticks>0);longjmp(task_yield,1);}
int uart_param_config(int port,const uart_config_t *c) {assert(port==1 && c->baud_rate==115200);return 0;}
int uart_set_pin(int port,int tx,int rx,int rts,int cts) {assert(port==1 && tx==16 && rx==17 && rts==-1 && cts==-1);return 0;}
int uart_driver_install(int port,int rx,int tx,int count,void *queue,int flags) {
    (void)count;(void)queue;(void)flags;assert(port==1 && rx==1024 && tx==1024);return 0;
}
int uart_read_bytes(int port,void *data,unsigned size,TickType_t wait) {
    (void)port;(void)wait;assert(!zigbee_context);
    size_t n=strlen(incoming);if(n>size)n=size;memcpy(data,incoming,n);incoming+=n;return (int)n;
}
int uart_write_bytes(int port,const void *data,size_t size) {
    (void)port;assert(!zigbee_context);size_t length=strlen(outgoing);assert(length+size<sizeof(outgoing));
    memcpy(outgoing+length,data,size);outgoing[length+size]=0;return (int)size;
}
static void uart_slice(const char *text) {
    incoming=text;assert(!zigbee_context);
    if(!setjmp(task_yield))task_entry(NULL);
    assert(!*incoming);
}
static void poll(serial_input_t *s,channel_config_t *cfg,bool ready,uint64_t now) {
    zigbee_context=true;serial_transport_poll(s,cfg,priorities,ready,now);zigbee_context=false;
}
static void enqueue(serial_operation_t op,unsigned channel) {
    serial_command_t c={.operation=op,.channel=channel,.duration=100};assert(xQueueSend(&queues[0],&c,0));
}
int main(void) {
    serial_input_t state={0};channel_config_t cfg[CHANNELS];
    for(unsigned i=0;i<CHANNELS;++i)channel_defaults(&cfg[i]);
    serial_transport_start();assert(created==2 && task_entry);
    uart_slice("PING\n");assert(!strcmp(outgoing,"OK PING\n"));outgoing[0]=0;
    uart_slice("PRESS 1\n");assert(!state.buttons[0].down && queues[0].count==1 && !*outgoing);
    poll(&state,cfg,true,100);assert(state.buttons[0].down && queues[1].count==1 && !*outgoing);
    uart_slice("");assert(!strcmp(outgoing,"OK PRESS 1\n"));outgoing[0]=0;
    uart_slice("RELEASE 1\n");poll(&state,cfg,true,200);uart_slice("");
    assert(!state.buttons[0].down && !strcmp(outgoing,"OK RELEASE 1\n"));outgoing[0]=0;
    // Command flood: queue bounded at eight; rejected command has no state effect.
    uart_slice("PRESS 1\nPRESS 2\nPRESS 3\nPRESS 4\nPRESS 5\nPRESS 6\nPRESS 7\nPRESS 8\n");
    uart_slice("RELEASE ALL\n");assert(!strcmp(outgoing,"ERR QUEUE\n") && queues[0].count==8);outgoing[0]=0;
    for(unsigned i=0;i<8;++i) {
        poll(&state,cfg,true,300+i*10);assert(queues[0].count==7-i && queues[1].count==i+1);
    }
    for(unsigned i=0;i<8;++i)assert(state.buttons[i].down);
    // Full response queue: RELEASE is deferred rather than executed without an ACK.
    enqueue(SERIAL_RELEASE_ALL,0);poll(&state,cfg,true,500);
    assert(queues[0].count==1);for(unsigned i=0;i<8;++i)assert(state.buttons[i].down);
    uart_slice("");assert(queues[1].count==0);
    poll(&state,cfg,true,510);assert(queues[0].count==0 && queues[1].count==1);
    for(unsigned i=0;i<8;++i)assert(!state.buttons[i].down);
    outgoing[0]=0;uart_slice("");assert(!strcmp(outgoing,"OK RELEASE ALL\n"));outgoing[0]=0;
    enqueue(SERIAL_PRESS,2);poll(&state,cfg,false,600);uart_slice("");
    assert(!state.buttons[2].down && !strcmp(outgoing,"ERR NOT_READY\n"));outgoing[0]=0;
    uart_slice("WRONG\n");assert(!strcmp(outgoing,"ERR SYNTAX\n") && queues[0].count==0);
    outgoing[0]=0;
    uart_slice("GET 3\n");assert(queues[0].count==1 && !*outgoing);
    // Change settings after enqueue: GET must use the snapshot at execution, not RX.
    cfg[2].function=MODE_LEVEL_DOWN_NO_ONOFF;priorities[2]=0;
    state.buttons[2].running=true;
    poll(&state,cfg,false,700);assert(!*outgoing && state.buttons[2].running);
    uart_slice("");assert(strstr(outgoing,"\"channel\":3"));
    assert(strstr(outgoing,"\"switch_type\":\"brightness_level_down_no_onoff\""));
    assert(strstr(outgoing,"\"binding_priority\":\"OFF\""));
    assert(strlen(outgoing)>48 && outgoing[strlen(outgoing)-1]=='\n');
    puts("PASS: production UART task/queues, no UART calls in Zigbee, flood/backpressure and deferred ACKs");
}
