#pragma once
#include "esp_lcd_touch.h"
#define ESP_LCD_TOUCH_IO_I2C_GT911_CONFIG() { 0 }
#define ESP_LCD_TOUCH_IO_I2C_GT911_ADDRESS 0x5D
typedef struct { int dummy; } esp_lcd_panel_io_i2c_config_t;
static inline esp_err_t esp_lcd_touch_new_i2c_gt911(void *io, const void *cfg, esp_lcd_touch_handle_t *out)
{ (void) io; (void) cfg; if (out) *out = NULL; return ESP_OK; }
