#pragma once
#include "esp_lcd_types.h"
static inline esp_err_t esp_lcd_panel_draw_bitmap(esp_lcd_panel_handle_t p, int x1, int y1, int x2, int y2, const void *d)
{ (void)p;(void)x1;(void)y1;(void)x2;(void)y2;(void)d; return ESP_OK; }
static inline esp_err_t esp_lcd_panel_reset(esp_lcd_panel_handle_t p) { (void)p; return ESP_OK; }
static inline esp_err_t esp_lcd_panel_init(esp_lcd_panel_handle_t p) { (void)p; return ESP_OK; }
