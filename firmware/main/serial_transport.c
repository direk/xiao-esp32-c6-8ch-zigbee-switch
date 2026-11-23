// SPDX-License-Identifier: MIT
#include "serial_transport.h"
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdatomic.h>
#include "driver/uart.h"
#include "driver/usb_serial_jtag.h"
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#define LOG_DEPTH 8
#define LOG_SIZE 256
#define UART_FIELDS(X) X(rx_bytes) X(rx_lines) X(parse_errors) X(queue_rejected) \
    X(fifo_overflows) X(buffer_full) X(frame_errors) X(parity_errors) X(breaks) \
    X(event_queue_full) X(rx_resyncs) X(tx_errors)
typedef struct {bool reset_after_send;char text[SERIAL_WIRE_MAX];} serial_reply_t;
typedef struct {uint32_t generation;char text[LOG_SIZE];} serial_log_t;
typedef struct {
    serial_port_id_t id;
    QueueHandle_t commands,replies,logs;
    serial_session_t session; // Only Zigbee accesses sessions and channel state.
    serial_parser_t parser;   // Only this port's task accesses the parser/device.
    atomic_bool logs_enabled;
    atomic_uint_least32_t log_generation,log_dropped;
    bool needs_newline;
#define FIELD(name) atomic_uint_least32_t name;
    struct {UART_FIELDS(FIELD)} stats;
#undef FIELD
} serial_port_t;
static serial_port_t ports[SERIAL_PORT_COUNT];
static QueueHandle_t uart_events;
static atomic_bool reset_acknowledged;
static unsigned next_port;
#define INC(p,name) atomic_fetch_add_explicit(&(p)->stats.name,1,memory_order_relaxed)

// ESP_LOG may call from different tasks. Format locally and enqueue without waiting.
// All physical writes happen later in the destination port task, never in this hook.
static int log_vprintf(const char *format,va_list args) {
    bool enabled[SERIAL_PORT_COUNT];bool any=false;
    for(unsigned i=0;i<SERIAL_PORT_COUNT;++i) {
        enabled[i]=atomic_load(&ports[i].logs_enabled);any|=enabled[i];
    }
    if(!any)return 0;
    serial_log_t record;
    memcpy(record.text,"#LOG ",5);
    int n=vsnprintf(record.text+5,sizeof(record.text)-6,format,args);
    if(n<0)return n;
    size_t length=strlen(record.text);
    while(length>5 && (record.text[length-1]=='\n'||record.text[length-1]=='\r'))--length;
    for(size_t i=5;i<length;++i)if(record.text[i]=='\n'||record.text[i]=='\r')record.text[i]=' ';
    record.text[length++]='\n';record.text[length]=0;
    for(unsigned i=0;i<SERIAL_PORT_COUNT;++i)if(enabled[i]) {
        serial_port_t *p=&ports[i];record.generation=atomic_load(&p->log_generation);
        if(p->logs && atomic_load(&p->logs_enabled) && xQueueSend(p->logs,&record,0)!=pdTRUE)
            atomic_fetch_add(&p->log_dropped,1);
    }
    return n;
}
void serial_transport_logging_init(void) {
    esp_log_set_vprintf(log_vprintf);
    // Compiled INFO logs are available, but the router starts with both ports OFF.
    esp_log_level_set("*",ESP_LOG_INFO);
}
bool serial_transport_reset_acknowledged(void) {return atomic_load(&reset_acknowledged);}
void serial_transport_poll(serial_input_t *state,const channel_config_t *config,
    const uint8_t *priority,const serial_runtime_t *runtime,uint64_t now,
    bool (*reset)(void *),void *reset_context) {
    for(unsigned tried=0;tried<SERIAL_PORT_COUNT;++tried) {
        unsigned index=(next_port+tried)%SERIAL_PORT_COUNT;
        serial_port_t *p=&ports[index];
        // A stalled/full USB port must not prevent UART progress (and vice versa).
        if(!uxQueueSpacesAvailable(p->replies))continue;
        serial_command_t command;
        if(xQueueReceive(p->commands,&command,0)!=pdTRUE)continue;
        serial_runtime_t snapshot=*runtime;
        snapshot.port=p->id;snapshot.logs_enabled=atomic_load(&p->logs_enabled);
        snapshot.log_dropped=atomic_load(&p->log_dropped);
#define SNAP(name) snapshot.uart.name=atomic_load_explicit(&p->stats.name,memory_order_relaxed);
        UART_FIELDS(SNAP)
#undef SNAP
        snapshot.command_queue_used=uxQueueMessagesWaiting(p->commands);
        snapshot.reply_queue_used=uxQueueMessagesWaiting(p->replies);
        serial_reply_t reply={0};
        serial_effect_t effect=serial_session_execute(&p->session,state,&command,now,config,priority,
            &snapshot,reset,reset_context,reply.text,sizeof(reply.text));
        reply.reset_after_send=effect==SERIAL_EFFECT_RESET;
        if(effect==SERIAL_EFFECT_LOG_ON || effect==SERIAL_EFFECT_LOG_OFF) {
            atomic_fetch_add(&p->log_generation,1);
            atomic_store(&p->logs_enabled,effect==SERIAL_EFFECT_LOG_ON);
        }
        BaseType_t sent=xQueueSend(p->replies,&reply,0);configASSERT(sent==pdTRUE);
        next_port=(index+1)%SERIAL_PORT_COUNT;
        break; // Preserve the total budget: one command per Zigbee tick.
    }
}
static int device_write(serial_port_t *p,const char *text,size_t length) {
    return p->id==SERIAL_PORT_UART ? uart_write_bytes(UART_NUM_1,text,length) :
        usb_serial_jtag_write_bytes(text,length,pdMS_TO_TICKS(100));
}
static bool write_reply(serial_port_t *p,const char *text) {
    // After a partial USB write, terminate the incomplete line before a new one.
    if(p->needs_newline) {
        if(device_write(p,"\n",1)!=1) {INC(p,tx_errors);return false;}
        p->needs_newline=false;
    }
    size_t length=strlen(text);
    int written=device_write(p,text,length);
    if(written!=(int)length) {
        if(written>0)p->needs_newline=true;
        INC(p,tx_errors);return false;
    }
    return true;
}
static bool receive_errors(serial_port_t *p) {
    if(p->id!=SERIAL_PORT_UART)return false;
    bool failed=uxQueueSpacesAvailable(uart_events)==0;
    if(failed)INC(p,event_queue_full);
    uart_event_t event;
    for(unsigned i=0;i<SERIAL_EVENT_DEPTH && xQueueReceive(uart_events,&event,0)==pdTRUE;++i) {
        switch(event.type) {
        case UART_FIFO_OVF:INC(p,fifo_overflows);failed=true;break;
        case UART_BUFFER_FULL:INC(p,buffer_full);failed=true;break;
        case UART_FRAME_ERR:INC(p,frame_errors);failed=true;break;
        case UART_PARITY_ERR:INC(p,parity_errors);failed=true;break;
        case UART_BREAK:INC(p,breaks);failed=true;break;
        default:break;
        }
    }
    if(failed) {
        uart_flush_input(UART_NUM_1);serial_parser_abort(&p->parser);INC(p,rx_resyncs);
        write_reply(p,"ERR UART RX\n");
    }
    return failed;
}
static void serial_task(void *arg) {
    serial_port_t *p=arg;
    for(;;) {
        serial_reply_t reply;
        for(unsigned i=0;i<SERIAL_QUEUE_DEPTH && xQueueReceive(p->replies,&reply,0)==pdTRUE;++i) {
            bool written=write_reply(p,reply.text);
            if(reply.reset_after_send && written) {
                int result=p->id==SERIAL_PORT_UART ? uart_wait_tx_done(UART_NUM_1,pdMS_TO_TICKS(1000)) :
                    usb_serial_jtag_wait_tx_done(pdMS_TO_TICKS(1000));
                if(result==0)atomic_store(&reset_acknowledged,true);else INC(p,tx_errors);
            }
        }
        receive_errors(p);
        uint8_t bytes[64];
        int received=p->id==SERIAL_PORT_UART ? uart_read_bytes(UART_NUM_1,bytes,sizeof(bytes),pdMS_TO_TICKS(10)) :
            usb_serial_jtag_read_bytes(bytes,sizeof(bytes),pdMS_TO_TICKS(10));
        if(received>0)atomic_fetch_add_explicit(&p->stats.rx_bytes,(uint32_t)received,memory_order_relaxed);
        if(receive_errors(p))received=0;
        for(int i=0;i<received;++i) {
            if(bytes[i]=='\n')INC(p,rx_lines);
            serial_command_t command;
            serial_parse_result_t result=serial_parser_feed(&p->parser,(char)bytes[i],&command,reply.text,sizeof(reply.text));
            if(result==SERIAL_PARSE_COMMAND) {
                if(command.operation==SERIAL_PING && !command.has_id)strcpy(reply.text,"OK PING\n");
                else if(xQueueSend(p->commands,&command,0)!=pdTRUE) {
                    INC(p,queue_rejected);strcpy(reply.text,"ERR QUEUE\n");serial_response_tag(&command,reply.text,sizeof(reply.text));
                } else continue;
            } else if(result==SERIAL_PARSE_ERROR) {
                INC(p,parse_errors);serial_response_tag(&command,reply.text,sizeof(reply.text));
            } else continue;
            write_reply(p,reply.text);
        }
        serial_log_t log;
        // Responses get priority. No task other than this one writes this port.
        for(unsigned i=0;i<2 && !uxQueueMessagesWaiting(p->replies) && xQueueReceive(p->logs,&log,0)==pdTRUE;++i)
            if(atomic_load(&p->logs_enabled) && log.generation==atomic_load(&p->log_generation))write_reply(p,log.text);
        vTaskDelay(1);
    }
}
void serial_transport_start(void) {
    serial_port_t *uart=&ports[SERIAL_PORT_UART],*usb=&ports[SERIAL_PORT_USB];
    uart->id=SERIAL_PORT_UART;usb->id=SERIAL_PORT_USB;
    uart->commands=xQueueCreate(SERIAL_QUEUE_DEPTH,sizeof(serial_command_t));
    uart->replies=xQueueCreate(SERIAL_QUEUE_DEPTH,sizeof(serial_reply_t));
    configASSERT(uart->commands && uart->replies);
    const uart_config_t config={.baud_rate=115200,.data_bits=UART_DATA_8_BITS,
        .parity=UART_PARITY_DISABLE,.stop_bits=UART_STOP_BITS_1,
        .flow_ctrl=UART_HW_FLOWCTRL_DISABLE,.source_clk=UART_SCLK_DEFAULT};
    ESP_ERROR_CHECK(uart_param_config(UART_NUM_1,&config));
    ESP_ERROR_CHECK(uart_set_pin(UART_NUM_1,16,17,UART_PIN_NO_CHANGE,UART_PIN_NO_CHANGE));
    ESP_ERROR_CHECK(uart_driver_install(UART_NUM_1,1024,1024,SERIAL_EVENT_DEPTH,&uart_events,0));
    configASSERT(uart_events);
    uart->logs=xQueueCreate(LOG_DEPTH,sizeof(serial_log_t));
    usb->commands=xQueueCreate(SERIAL_QUEUE_DEPTH,sizeof(serial_command_t));
    usb->replies=xQueueCreate(SERIAL_QUEUE_DEPTH,sizeof(serial_reply_t));
    usb->logs=xQueueCreate(LOG_DEPTH,sizeof(serial_log_t));
    configASSERT(uart->logs && usb->commands && usb->replies && usb->logs);
    usb_serial_jtag_driver_config_t usb_config={.tx_buffer_size=4096,.rx_buffer_size=1024};
    ESP_ERROR_CHECK(usb_serial_jtag_driver_install(&usb_config));
    for(unsigned i=0;i<SERIAL_PORT_COUNT;++i) {
        BaseType_t started=xTaskCreate(serial_task,i==SERIAL_PORT_UART?"serial_uart":"serial_usb",6144,&ports[i],4,NULL);
        configASSERT(started==pdPASS);
    }
}
