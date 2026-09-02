#pragma once
#include "esp_lcd_types.h"
typedef struct { int dummy; } esp_lcd_panel_io_i2c_config_t_alias;
static inline esp_err_t esp_lcd_new_panel_io_i2c(void *bus, const void *cfg, esp_lcd_panel_io_handle_t *out)
{ (void) bus; (void) cfg; if (out) *out = NULL; return ESP_OK; }
