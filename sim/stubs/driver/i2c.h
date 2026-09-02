#pragma once
#include <stdint.h>
#include <stddef.h>
#include "esp_err.h"
#include "driver/gpio.h"
typedef int i2c_port_t;
#define I2C_NUM_0 0
#define I2C_NUM_1 1
typedef enum { I2C_MODE_SLAVE = 0, I2C_MODE_MASTER } i2c_mode_t;
typedef struct {
    i2c_mode_t mode; int sda_io_num, scl_io_num;
    gpio_pullup_t sda_pullup_en, scl_pullup_en;
    union { struct { uint32_t clk_speed; } master; };
    uint32_t clk_flags;
} i2c_config_t;
static inline esp_err_t i2c_param_config(i2c_port_t p, const i2c_config_t *c) { (void) p; (void) c; return ESP_OK; }
static inline esp_err_t i2c_driver_install(i2c_port_t p, i2c_mode_t m, size_t rx, size_t tx, int f) { (void) p; (void) m; (void) rx; (void) tx; (void) f; return ESP_OK; }
static inline esp_err_t i2c_master_write_to_device(i2c_port_t p, uint8_t a, const uint8_t *d, size_t n, uint32_t t) { (void) p; (void) a; (void) d; (void) n; (void) t; return ESP_OK; }
static inline esp_err_t i2c_master_read_from_device(i2c_port_t p, uint8_t a, uint8_t *d, size_t n, uint32_t t) { (void) p; (void) a; (void) d; (void) n; (void) t; return ESP_OK; }
