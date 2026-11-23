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

#define UART_DATA 1
#define UART_FIFO_OVF 2
#define UART_BUFFER_FULL 3
#define UART_FRAME_ERR 4
#define UART_PARITY_ERR 5
#define UART_BREAK 6
typedef struct {int type;size_t size;} uart_event_t;
UBaseType_t uxQueueMessagesWaiting(QueueHandle_t q);
int uart_flush_input(int port);
int uart_wait_tx_done(int port,TickType_t wait);
#include <stdarg.h>
#define ESP_LOG_INFO 3
typedef int (*vprintf_like_t)(const char *,va_list);
vprintf_like_t esp_log_set_vprintf(vprintf_like_t callback);
void esp_log_level_set(const char *tag,int level);
typedef struct {uint32_t tx_buffer_size,rx_buffer_size;} usb_serial_jtag_driver_config_t;
int usb_serial_jtag_driver_install(usb_serial_jtag_driver_config_t *config);
int usb_serial_jtag_read_bytes(void *data,uint32_t size,TickType_t wait);
int usb_serial_jtag_write_bytes(const void *data,size_t size,TickType_t wait);
int usb_serial_jtag_wait_tx_done(TickType_t wait);
