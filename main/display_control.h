#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

typedef enum {
    DISPLAY_POWER_BUTTON_TOP_RIGHT = 0,
    DISPLAY_POWER_BUTTON_TOP_LEFT = 1,
    DISPLAY_POWER_BUTTON_HIDDEN = 2,
} display_power_button_corner_t;

typedef struct {
    bool schedule_enabled;
    uint16_t off_minute;
    uint16_t on_minute;
    display_power_button_corner_t power_button_corner;
} display_control_config_t;

esp_err_t display_control_init(void);
void display_control_create_power_button(void);
void display_control_set_power_button_visible(bool visible);
void display_control_get_config(display_control_config_t *config);
esp_err_t display_control_set_config(const display_control_config_t *config);
bool display_control_is_backlight_on(void);
bool display_control_handle_touch_wake(void);
void display_control_turn_off(void);
