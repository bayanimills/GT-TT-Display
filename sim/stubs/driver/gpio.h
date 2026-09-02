#pragma once
#include <stdint.h>
#include "esp_err.h"
typedef int gpio_num_t;
#define GPIO_NUM_NC (-1)
typedef enum { GPIO_MODE_DISABLE = 0, GPIO_MODE_INPUT, GPIO_MODE_OUTPUT, GPIO_MODE_OUTPUT_OD } gpio_mode_t;
typedef enum { GPIO_PULLUP_DISABLE = 0, GPIO_PULLUP_ENABLE } gpio_pullup_t;
typedef enum { GPIO_PULLDOWN_DISABLE = 0, GPIO_PULLDOWN_ENABLE } gpio_pulldown_t;
typedef enum { GPIO_INTR_DISABLE = 0, GPIO_INTR_POSEDGE, GPIO_INTR_NEGEDGE, GPIO_INTR_ANYEDGE } gpio_int_type_t;
typedef struct {
    uint64_t pin_bit_mask; gpio_mode_t mode; gpio_pullup_t pull_up_en;
    gpio_pulldown_t pull_down_en; gpio_int_type_t intr_type;
} gpio_config_t;
static inline esp_err_t gpio_config(const gpio_config_t *c) { (void) c; return ESP_OK; }
static inline esp_err_t gpio_set_level(gpio_num_t p, uint32_t l) { (void) p; (void) l; return ESP_OK; }
static inline int       gpio_get_level(gpio_num_t p) { (void) p; return 1; }
static inline esp_err_t gpio_set_direction(gpio_num_t p, gpio_mode_t m) { (void) p; (void) m; return ESP_OK; }
static inline esp_err_t gpio_install_isr_service(int f) { (void) f; return ESP_OK; }
static inline esp_err_t gpio_isr_handler_add(gpio_num_t p, void (*fn)(void *), void *a) { (void) p; (void) fn; (void) a; return ESP_OK; }
static inline esp_err_t gpio_reset_pin(gpio_num_t p) { (void) p; return ESP_OK; }
