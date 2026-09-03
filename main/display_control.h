#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "display_button_visibility.h"
#include "esp_err.h"

typedef struct {
    bool schedule_enabled;
    uint16_t off_minute;
    uint16_t on_minute;
    display_power_button_corner_t power_button_corner;
    bool power_button_visuals_visible;
} display_control_config_t;

esp_err_t display_control_init(void);
void display_control_create_power_button(void);
void display_control_set_power_button_visible(bool visible);

/* Restyle the corner control for the active skin; screens call it as they
 * are built so a skin change is reflected wherever the user lands next. */
void display_control_refresh_skin(void);

/* While a modal surface (drawer, sheet) is up the corner control must not be
 * live above it. Nested: every push needs a pop. */
void display_control_push_overlay(void);
void display_control_pop_overlay(void);

/* Night screen: render the control as a barely-there dark disc. */
void display_control_set_power_button_dim(bool dim);
void display_control_get_config(display_control_config_t *config);
esp_err_t display_control_set_config(const display_control_config_t *config);
bool display_control_is_backlight_on(void);
bool display_control_handle_touch_wake(void);

/* Feed every raw touch sample through here from the indev read callback and
 * deliver the returned state to LVGL. While the backlight is off the first
 * touch wakes the display and is reported as released until the finger
 * lifts, so waking can never activate whatever is under the finger. This
 * sits below LVGL, which is why it wins over every on-screen tap consumer
 * (the Glass drawer, the Wi-Fi keyboard) without coordinating with them. */
bool display_control_filter_touch(bool pressed);
void display_control_turn_off(void);
