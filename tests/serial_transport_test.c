// SPDX-License-Identifier: MIT
#include <assert.h>
#include <setjmp.h>
#include <stdio.h>
#include <string.h>
#include "serial_transport.h"
#include "transport_stubs.h"
// Execute production transport with deterministic queues and one UART task slice.
struct mock_queue {unsigned depth,size,count,head;unsigned char data[SERIAL_EVENT_DEPTH][SERIAL_WIRE_MAX+8];};
static struct mock_queue queues[7];
static unsigned created;
static bool zigbee_context;
static uint8_t priorities[ENDPOINTS]={1,1,1,1,1,1,1,1};
static void (*task_entry[2])(void *);
static void *task_args[2];
static vprintf_like_t log_hook;
static const char *usb_incoming="";
static char usb_outgoing[16384];
static bool usb_blocked;
static unsigned usb_waits;
static size_t usb_partial;
static jmp_buf task_yield;
static const char *incoming="";
static char outgoing[16384];
static unsigned reset_calls,flushes;
static bool fail_reset;
static int error_during_read;
static bool reset_callback(void *unused) {(void)unused;++reset_calls;return !fail_reset;}
int uart_flush_input(int port) {(void)port;assert(!zigbee_context);++flushes;return 0;}
int uart_wait_tx_done(int port,TickType_t wait) {(void)port;(void)wait;assert(!zigbee_context);return 0;}
QueueHandle_t xQueueCreate(unsigned depth,unsigned size) {
    assert(created<7 && depth<=SERIAL_EVENT_DEPTH && size<=SERIAL_WIRE_MAX+8);
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
UBaseType_t uxQueueMessagesWaiting(QueueHandle_t q) {return q->count;}
BaseType_t xTaskCreate(void (*task)(void *),const char *name,unsigned stack,void *arg,unsigned priority,void *handle) {
    (void)stack;(void)handle;assert(priority<5);unsigned port=!strcmp(name,"serial_usb");
    task_entry[port]=task;task_args[port]=arg;assert(arg);return pdPASS;
}
void vTaskDelay(TickType_t ticks) {assert(ticks>0);longjmp(task_yield,1);}
int uart_param_config(int port,const uart_config_t *c) {assert(port==1 && c->baud_rate==115200);return 0;}
int uart_set_pin(int port,int tx,int rx,int rts,int cts) {assert(port==1 && tx==16 && rx==17 && rts==-1 && cts==-1);return 0;}
int uart_driver_install(int port,int rx,int tx,int count,void *queue,int flags) {
    (void)flags;assert(port==1 && rx==1024 && tx==1024 && count==SERIAL_EVENT_DEPTH);
    *(QueueHandle_t *)queue=xQueueCreate(count,sizeof(uart_event_t));return 0;
}
int uart_read_bytes(int port,void *data,unsigned size,TickType_t wait) {
    (void)port;(void)wait;assert(!zigbee_context);
    size_t n=strlen(incoming);if(n>size)n=size;memcpy(data,incoming,n);incoming+=n;
    if(error_during_read) {uart_event_t event={.type=error_during_read};assert(xQueueSend(&queues[2],&event,0));error_during_read=0;}
    return (int)n;
}
int uart_write_bytes(int port,const void *data,size_t size) {
    (void)port;assert(!zigbee_context);size_t length=strlen(outgoing);assert(length+size<sizeof(outgoing));
    memcpy(outgoing+length,data,size);outgoing[length+size]=0;return (int)size;
}
vprintf_like_t esp_log_set_vprintf(vprintf_like_t callback) {log_hook=callback;return NULL;}
void esp_log_level_set(const char *tag,int level) {assert(!strcmp(tag,"*") && level==ESP_LOG_INFO);}
static void emit_log(const char *format,...) {
    va_list args;va_start(args,format);log_hook(format,args);va_end(args);
}
int usb_serial_jtag_driver_install(usb_serial_jtag_driver_config_t *c) {assert(c->tx_buffer_size==4096 && c->rx_buffer_size==1024);return 0;}
int usb_serial_jtag_read_bytes(void *data,uint32_t size,TickType_t wait) {
    (void)wait;assert(!zigbee_context);size_t n=strlen(usb_incoming);if(n>size)n=size;
    memcpy(data,usb_incoming,n);usb_incoming+=n;return (int)n;
}
int usb_serial_jtag_write_bytes(const void *data,size_t size,TickType_t wait) {
    (void)wait;assert(!zigbee_context);if(usb_blocked)return 0;
    if(usb_partial && size>usb_partial) {size=usb_partial;usb_partial=0;}
    size_t length=strlen(usb_outgoing);assert(length+size<sizeof(usb_outgoing));
    memcpy(usb_outgoing+length,data,size);usb_outgoing[length+size]=0;return (int)size;
}
int usb_serial_jtag_wait_tx_done(TickType_t wait) {(void)wait;assert(!zigbee_context);++usb_waits;return usb_blocked?-1:0;}
static void usb_slice(const char *text) {
    usb_incoming=text;if(!setjmp(task_yield))task_entry[1](task_args[1]);assert(!*usb_incoming);
}
static void uart_slice(const char *text) {
    incoming=text;assert(!zigbee_context);
    if(!setjmp(task_yield))task_entry[0](task_args[0]);
    assert(!*incoming);
}
static void poll(serial_input_t *s,channel_config_t *cfg,bool ready,uint64_t now) {
    serial_runtime_t runtime={.firmware_version="2.1.0",.boot_id=123,.joined=ready,.bindings_known=ready};
    zigbee_context=true;serial_transport_poll(s,cfg,priorities,&runtime,now,reset_callback,NULL);zigbee_context=false;
}
static void enqueue(serial_operation_t op,unsigned channel) {
    serial_command_t c={.operation=op,.channel=channel,.duration=100};assert(xQueueSend(&queues[0],&c,0));
}
int main(void) {
    serial_input_t state={0};channel_config_t cfg[ENDPOINTS];
    for(unsigned i=0;i<ENDPOINTS;++i)channel_defaults(&cfg[i]);
    serial_transport_logging_init();serial_transport_start();assert(created==7 && task_entry[0] && task_entry[1]);
    emit_log("off by default\n");usb_slice("");assert(!*usb_outgoing && !*outgoing);
    usb_slice("PING\n");assert(!strcmp(usb_outgoing,"OK PING\n"));usb_outgoing[0]=0;
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
    outgoing[0]=0;
    uart_slice("@500 CLICK 4\n@500 CLICK 4\n");assert(queues[0].count==2);
    poll(&state,cfg,true,800);poll(&state,cfg,true,810);uart_slice("");
    assert(!strcmp(outgoing,"@500 OK CLICK 4\n@500 OK CLICK 4\n"));
    serial_input_cancel(&state,3);outgoing[0]=0;
    uart_slice("@500 CLICK 4\n");poll(&state,cfg,true,820);uart_slice("");
    assert(!state.buttons[3].down && !strcmp(outgoing,"@500 OK CLICK 4\n"));
    outgoing[0]=0;uart_slice("@500 CLICK 5\n");poll(&state,cfg,true,830);uart_slice("");
    assert(!strcmp(outgoing,"@500 ERR ID_CONFLICT\n") && !state.buttons[4].down);
    // Corruption in a partial RESET must not turn its surviving suffix into a command.
    outgoing[0]=0;uart_slice("RE");uart_event_t event={.type=UART_FRAME_ERR};
    assert(xQueueSend(&queues[2],&event,0));uart_slice("SET\n");
    assert(!strcmp(outgoing,"ERR UART RX\n") && !queues[0].count && !reset_calls);
    outgoing[0]=0;uart_slice("PING\n");assert(!strcmp(outgoing,"OK PING\n"));
    // Error arriving during RX read discards the entire staged batch, then resyncs at LF.
    outgoing[0]=0;error_during_read=UART_FIFO_OVF;uart_slice("RESET\n");
    assert(!strcmp(outgoing,"ERR UART RX\n") && !queues[0].count && !reset_calls);
    uart_slice("\n");
    const int errors[]={UART_BUFFER_FULL,UART_PARITY_ERR,UART_BREAK};
    for(unsigned i=0;i<3;++i) {
        outgoing[0]=0;event.type=errors[i];assert(xQueueSend(&queues[2],&event,0));uart_slice("\n");
        assert(!strcmp(outgoing,"ERR UART RX\n") && !queues[0].count);
    }
    // Full event queue is treated as a possibly lost error even when all entries are DATA.
    event.type=UART_DATA;
    for(unsigned i=0;i<SERIAL_EVENT_DEPTH;++i)assert(xQueueSend(&queues[2],&event,0));
    outgoing[0]=0;uart_slice("\n");assert(!strcmp(outgoing,"ERR UART RX\n"));
    outgoing[0]=0;uart_slice("STATUS\n");poll(&state,cfg,false,900);uart_slice("");
    assert(strstr(outgoing,"\"frame_errors\":1") && strstr(outgoing,"\"fifo_overflows\":1"));
    assert(strstr(outgoing,"\"buffer_full\":1") && strstr(outgoing,"\"parity_errors\":1"));
    assert(strstr(outgoing,"\"breaks\":1") && strstr(outgoing,"\"event_queue_full\":1"));
    assert(strstr(outgoing,"\"rx_resyncs\":6") && flushes==6);
    assert(strstr(outgoing,"\"duplicates\":2") && strstr(outgoing,"\"id_conflicts\":1"));
    outgoing[0]=0;uart_slice("@99 HOLD 3 nope\n");assert(!strcmp(outgoing,"@99 ERR DURATION\n"));
    // Accepted RESET waits for its response to be drained by the UART task.
    outgoing[0]=0;uart_slice("@1000 RESET\n");poll(&state,cfg,true,1000);
    assert(reset_calls==1 && !serial_transport_reset_acknowledged() && !*outgoing);
    uart_slice("");assert(!strcmp(outgoing,"@1000 OK RESET\n") && serial_transport_reset_acknowledged());
    outgoing[0]=0;uart_slice("@1000 RESET\n");poll(&state,cfg,true,1010);uart_slice("");
    assert(reset_calls==1 && !strcmp(outgoing,"@1000 OK RESET\n"));
    // A partial line on UART must not consume USB input.
    outgoing[0]=0;usb_outgoing[0]=0;uart_slice("GE");usb_slice("GET 2\n");
    poll(&state,cfg,true,1100);usb_slice("");assert(strstr(usb_outgoing,"\"channel\":2") && !*outgoing);
    uart_slice("T 1\n");poll(&state,cfg,true,1110);uart_slice("");assert(strstr(outgoing,"\"channel\":1"));
    // Sessions and IDs are independent, responses always return to their input port.
    outgoing[0]=0;usb_outgoing[0]=0;
    uart_slice("@3000 GET 1\n");usb_slice("@3000 GET 2\n");
    poll(&state,cfg,true,1120);poll(&state,cfg,true,1130);uart_slice("");usb_slice("");
    assert(strstr(outgoing,"@3000 {\"channel\":1") && strstr(usb_outgoing,"@3000 {\"channel\":2"));
    // Port-local logging: OFF initially, and logging from Zigbee only enqueues.
    outgoing[0]=0;usb_outgoing[0]=0;uart_slice("LOG 1\n");poll(&state,cfg,true,1140);uart_slice("");
    assert(!strcmp(outgoing,"OK LOG 1\n"));outgoing[0]=0;
    zigbee_context=true;emit_log("alpha\nbeta\r\n");zigbee_context=false;
    assert(!*outgoing && !*usb_outgoing);uart_slice("");usb_slice("");
    assert(!strcmp(outgoing,"#LOG alpha beta\n") && !*usb_outgoing);
    outgoing[0]=0;usb_slice("@3001 LOG 1\n");poll(&state,cfg,true,1150);usb_slice("");
    assert(!strcmp(usb_outgoing,"@3001 OK LOG 1\n"));usb_outgoing[0]=0;
    emit_log("both\n");uart_slice("");usb_slice("");assert(!strcmp(outgoing,"#LOG both\n") && !strcmp(usb_outgoing,"#LOG both\n"));
    outgoing[0]=0;usb_outgoing[0]=0;
    emit_log("queued before off\n");
    serial_command_t log_off={.operation=SERIAL_LOG,.duration=0};assert(xQueueSend(&queues[0],&log_off,0));
    poll(&state,cfg,true,1160);uart_slice("");usb_slice("");
    assert(!strcmp(outgoing,"OK LOG 0\n") && !strcmp(usb_outgoing,"#LOG queued before off\n"));
    outgoing[0]=0;usb_outgoing[0]=0;emit_log("only usb\n");uart_slice("");usb_slice("");
    assert(!*outgoing && !strcmp(usb_outgoing,"#LOG only usb\n"));
    usb_outgoing[0]=0;usb_slice("@3002 LOG 0\n");poll(&state,cfg,true,1170);usb_slice("");usb_outgoing[0]=0;
    usb_slice("@3001 LOG 1\n");poll(&state,cfg,true,1180);usb_slice("");
    assert(!strcmp(usb_outgoing,"@3001 OK LOG 1\n"));usb_outgoing[0]=0;
    emit_log("replayed LOG must not enable\n");usb_slice("");assert(!*usb_outgoing);
    usb_slice("STATUS\n");poll(&state,cfg,true,1190);usb_slice("");
    assert(strstr(usb_outgoing,"\"port\":\"usb\"") && strstr(usb_outgoing,"\"logs_enabled\":false"));
    // A full USB response queue cannot block execution of UART requests.
    usb_outgoing[0]=0;outgoing[0]=0;
    for(unsigned i=0;i<8;++i) {
        serial_command_t cmd={.operation=SERIAL_INFO,.duration=100};assert(xQueueSend(&queues[4],&cmd,0));poll(&state,cfg,true,1200+i*10);
    }
    assert(queues[5].count==8);
    usb_slice("INFO\n"); // Drains the replies; fill again without running USB task.
    usb_outgoing[0]=0;
    for(unsigned i=0;i<8;++i)poll(&state,cfg,true,1300+i*10);
    // One reply is currently waiting. Add the other seven with direct task-side enqueues.
    for(unsigned i=0;i<7;++i) {serial_command_t cmd={.operation=SERIAL_INFO,.duration=100};assert(xQueueSend(&queues[4],&cmd,0));poll(&state,cfg,true,1400+i*10);}
    assert(queues[5].count==8);
    serial_command_t pending={.operation=SERIAL_INFO,.duration=100};assert(xQueueSend(&queues[4],&pending,0));
    uart_slice("GET 1\n");poll(&state,cfg,true,1500);uart_slice("");
    assert(strstr(outgoing,"\"channel\":1") && queues[4].count==1);
    usb_blocked=true;usb_slice("");assert(!*usb_outgoing && !queues[5].count);
    poll(&state,cfg,true,1510);usb_slice(""); // USB transmit timeout never touches UART.
    outgoing[0]=0;uart_slice("PING\n");assert(!strcmp(outgoing,"OK PING\n"));
    usb_blocked=false;usb_slice("@4000 INFO\n");poll(&state,cfg,true,1520);usb_slice("");
    assert(strstr(usb_outgoing,"@4000 {\"firmware\":") && strstr(usb_outgoing,"\"port\":\"usb\""));
    usb_outgoing[0]=0;
    usb_slice("LOG 1\n");poll(&state,cfg,true,1530);usb_slice("");usb_outgoing[0]=0;
    for(unsigned i=0;i<20;++i)emit_log("flood %u\n",i);
    serial_command_t status_cmd={.operation=SERIAL_STATUS,.duration=100};assert(xQueueSend(&queues[4],&status_cmd,0));
    poll(&state,cfg,true,1540);usb_slice("");
    assert(usb_outgoing[0]=='{' && strstr(usb_outgoing,"\"log_dropped\":12"));
    usb_outgoing[0]=0;usb_slice("LOG 0\n");poll(&state,cfg,true,1550);usb_slice("");
    outgoing[0]=0;usb_outgoing[0]=0;
    usb_slice("@5000 RESET\n");poll(&state,cfg,true,1560);
    unsigned waits_before=usb_waits;assert(reset_calls==2 && !*usb_outgoing);
    usb_slice("");assert(!strcmp(usb_outgoing,"@5000 OK RESET\n") && usb_waits==waits_before+1 && !*outgoing);
    // Interrupted USB output is terminated before the cached response is replayed.
    usb_outgoing[0]=0;usb_slice("@6000 INFO\n");poll(&state,cfg,true,1570);
    usb_partial=20;usb_slice("");assert(strlen(usb_outgoing)==20 && !strchr(usb_outgoing,'\n'));
    usb_slice("@6000 INFO\n");poll(&state,cfg,true,1580);usb_slice("");
    assert(usb_outgoing[20]=='\n' && !strncmp(usb_outgoing+21,"@6000 {",7));
    outgoing[0]=0;usb_outgoing[0]=0;
    uart_slice("PRESS EP24\n");poll(&state,cfg,true,1600);uart_slice("");
    assert(state.endpoint_buttons[23].down && !strcmp(outgoing,"OK PRESS EP24\n"));
    usb_slice("RELEASE EP24\n");poll(&state,cfg,true,1610);usb_slice("");
    assert(!state.endpoint_buttons[23].down && !strcmp(usb_outgoing,"OK RELEASE EP24\n"));
    outgoing[0]=0;usb_outgoing[0]=0;
    usb_slice("GET EP24\n");poll(&state,cfg,true,1620);usb_slice("");assert(strstr(usb_outgoing,"\"endpoint\":24"));
    uart_slice("STATUS EP24\n");poll(&state,cfg,true,1630);uart_slice("");assert(strstr(outgoing,"\"endpoint\":24"));
    puts("PASS: production UART task/queues, no UART calls in Zigbee, flood/backpressure and deferred ACKs");
}
