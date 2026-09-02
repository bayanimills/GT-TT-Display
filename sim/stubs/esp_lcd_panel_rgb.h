#pragma once
#include <stdbool.h>
#include "esp_lcd_types.h"
typedef struct { bool (*on_vsync)(esp_lcd_panel_handle_t, const void *, void *); } esp_lcd_rgb_panel_event_callbacks_t;
static inline esp_err_t esp_lcd_rgb_panel_get_frame_buffer(esp_lcd_panel_handle_t p, uint32_t n, void **fb0, ...) { (void)p;(void)n;(void)fb0; return ESP_OK; }
static inline esp_err_t esp_lcd_rgb_panel_restart(esp_lcd_panel_handle_t p) { (void)p; return ESP_OK; }
