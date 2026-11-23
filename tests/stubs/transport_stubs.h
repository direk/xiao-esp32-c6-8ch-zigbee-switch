#pragma once
#include <assert.h>
#include <stddef.h>
#include <stdint.h>
typedef int BaseType_t;
typedef unsigned UBaseType_t;
typedef unsigned TickType_t;
typedef struct mock_queue *QueueHandle_t;
#define pdTRUE 1
#define pdPASS 1
#define configASSERT(x) assert(x)
#define pdMS_TO_TICKS(x) (x)
#define ESP_ERROR_CHECK(x) assert((x)==0)
#define UART_NUM_1 1
#define UART_DATA_8_BITS 8
#define UART_PARITY_DISABLE 0
#define UART_STOP_BITS_1 1
#define UART_HW_FLOWCTRL_DISABLE 0
#define UART_SCLK_DEFAULT 0
#define UART_PIN_NO_CHANGE (-1)
typedef struct {int baud_rate,data_bits,parity,stop_bits,flow_ctrl,source_clk;} uart_config_t;
QueueHandle_t xQueueCreate(unsigned count,unsigned size);
BaseType_t xQueueReceive(QueueHandle_t q,void *item,TickType_t wait);
BaseType_t xQueueSend(QueueHandle_t q,const void *item,TickType_t wait);
UBaseType_t uxQueueSpacesAvailable(QueueHandle_t q);
BaseType_t xTaskCreate(void (*task)(void *),const char *name,unsigned stack,void *arg,unsigned priority,void *handle);
void vTaskDelay(TickType_t ticks);
int uart_param_config(int port,const uart_config_t *config);
int uart_set_pin(int port,int tx,int rx,int rts,int cts);
int uart_driver_install(int port,int rx,int tx,int count,void *queue,int flags);
int uart_read_bytes(int port,void *data,unsigned length,TickType_t wait);
int uart_write_bytes(int port,const void *data,size_t length);
