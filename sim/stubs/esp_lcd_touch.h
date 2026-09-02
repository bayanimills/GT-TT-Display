#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "esp_lcd_types.h"
typedef struct esp_lcd_touch_s *esp_lcd_touch_handle_t;
static inline esp_err_t esp_lcd_touch_read_data(esp_lcd_touch_handle_t t) { (void)t; return ESP_OK; }
static inline bool esp_lcd_touch_get_coordinates(esp_lcd_touch_handle_t t, uint16_t *x, uint16_t *y, uint16_t *s, uint8_t *n, uint8_t max)
{ (void)t;(void)x;(void)y;(void)s;(void)max; if (n) *n = 0; return false; }
