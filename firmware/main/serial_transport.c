// SPDX-License-Identifier: MIT
#include "serial_transport.h"
#include <string.h>
#include "driver/uart.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

typedef struct {char text[SERIAL_REPLY_MAX];} serial_reply_t;
static QueueHandle_t commands, replies;

void serial_transport_poll(serial_input_t *state,const channel_config_t *config,const uint8_t *binding_priority,bool ready,uint64_t now) {
    for(unsigned i=0;i<SERIAL_COMMANDS_PER_TICK;++i) {
        // Sole reply producer; consumer can only free more space after this check.
        // Reserve response capacity BEFORE executing a command, never lose its ACK.
        if(!uxQueueSpacesAvailable(replies))break;
        serial_command_t command;
        if(xQueueReceive(commands,&command,0)!=pdTRUE)break;
        serial_reply_t reply={0};
        serial_input_execute(state,&command,now,config,binding_priority,ready,reply.text,sizeof(reply.text));
        BaseType_t sent=xQueueSend(replies,&reply,0);
        configASSERT(sent==pdTRUE);
    }
}
static void uart_task(void *unused) {
    (void)unused;
    serial_parser_t parser={0};
    for(;;) {
        serial_reply_t reply;
        for(unsigned i=0;i<SERIAL_QUEUE_DEPTH && xQueueReceive(replies,&reply,0)==pdTRUE;++i)
            uart_write_bytes(UART_NUM_1,reply.text,strlen(reply.text));
        uint8_t bytes[64];
        int received=uart_read_bytes(UART_NUM_1,bytes,sizeof(bytes),pdMS_TO_TICKS(10));
        for(int i=0;i<received;++i) {
            serial_command_t command;
            serial_parse_result_t result=serial_parser_feed(&parser,(char)bytes[i],&command,reply.text,sizeof(reply.text));
            if(result==SERIAL_PARSE_COMMAND) {
                // PING checks the transport, independently of Zigbee availability.
                if(command.operation==SERIAL_PING)strcpy(reply.text,"OK PING\n");
                else if(xQueueSend(commands,&command,0)!=pdTRUE)strcpy(reply.text,"ERR QUEUE\n");
                else continue;
            } else if(result!=SERIAL_PARSE_ERROR)continue;
            // This may wait for TX space, but only in this task, never in Zigbee.
            uart_write_bytes(UART_NUM_1,reply.text,strlen(reply.text));
        }
        // Bound CPU consumption even under a continuous stream of invalid input.
        vTaskDelay(1);
    }
}
void serial_transport_start(void) {
    commands=xQueueCreate(SERIAL_QUEUE_DEPTH,sizeof(serial_command_t));
    replies=xQueueCreate(SERIAL_QUEUE_DEPTH,sizeof(serial_reply_t));
    configASSERT(commands && replies);
    const uart_config_t config={.baud_rate=115200,.data_bits=UART_DATA_8_BITS,
        .parity=UART_PARITY_DISABLE,.stop_bits=UART_STOP_BITS_1,
        .flow_ctrl=UART_HW_FLOWCTRL_DISABLE,.source_clk=UART_SCLK_DEFAULT};
    ESP_ERROR_CHECK(uart_param_config(UART_NUM_1,&config));
    ESP_ERROR_CHECK(uart_set_pin(UART_NUM_1,16,17,UART_PIN_NO_CHANGE,UART_PIN_NO_CHANGE));
    ESP_ERROR_CHECK(uart_driver_install(UART_NUM_1,1024,1024,0,NULL,0));
    BaseType_t started=xTaskCreate(uart_task,"serial",4096,NULL,4,NULL);
    configASSERT(started==pdPASS);
}
