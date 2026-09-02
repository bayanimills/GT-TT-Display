#pragma once
#include "esp_err.h"
#include <stdint.h>
#include <stddef.h>
typedef int uart_port_t;
#define UART_NUM_0 0
#define UART_NUM_1 1
#define UART_NUM_2 2
#define UART_PIN_NO_CHANGE (-1)
typedef enum { UART_DATA_8_BITS = 3 } uart_word_length_t;
typedef enum { UART_PARITY_DISABLE = 0 } uart_parity_t;
typedef enum { UART_STOP_BITS_1 = 1 } uart_stop_bits_t;
typedef enum { UART_HW_FLOWCTRL_DISABLE = 0 } uart_hw_flowcontrol_t;
typedef enum { UART_SCLK_DEFAULT = 0, UART_SCLK_APB = 1 } uart_sclk_t;
typedef struct {
    int baud_rate; uart_word_length_t data_bits; uart_parity_t parity;
    uart_stop_bits_t stop_bits; uart_hw_flowcontrol_t flow_ctrl;
    uint8_t rx_flow_ctrl_thresh; union { uart_sclk_t source_clk; };
} uart_config_t;
static inline esp_err_t uart_driver_install(uart_port_t p, int rx, int tx, int q, void *qh, int f) { (void)p;(void)rx;(void)tx;(void)q;(void)qh;(void)f; return ESP_OK; }
static inline esp_err_t uart_param_config(uart_port_t p, const uart_config_t *c) { (void)p;(void)c; return ESP_OK; }
static inline esp_err_t uart_set_pin(uart_port_t p, int tx, int rx, int rts, int cts) { (void)p;(void)tx;(void)rx;(void)rts;(void)cts; return ESP_OK; }
static inline int uart_write_bytes(uart_port_t p, const void *src, size_t n) { (void)p;(void)src; return (int)n; }
int uart_read_bytes(uart_port_t p, void *buf, uint32_t len, uint32_t ticks);
static inline esp_err_t uart_flush(uart_port_t p) { (void)p; return ESP_OK; }
static inline esp_err_t uart_flush_input(uart_port_t p) { (void)p; return ESP_OK; }
