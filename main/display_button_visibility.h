#pragma once

#include <stdbool.h>

typedef enum {
    DISPLAY_POWER_BUTTON_TOP_RIGHT = 0,
    DISPLAY_POWER_BUTTON_TOP_LEFT = 1,
    DISPLAY_POWER_BUTTON_HIDDEN_LEGACY = 2,
} display_power_button_corner_t;

typedef struct {
    bool interactive;
    bool show_visuals;
} display_button_visibility_t;

typedef enum {
    DISPLAY_POWER_BUTTON_MODE_VISIBLE_RIGHT = 0,
    DISPLAY_POWER_BUTTON_MODE_VISIBLE_LEFT = 1,
    DISPLAY_POWER_BUTTON_MODE_HIDDEN_RIGHT = 2,
    DISPLAY_POWER_BUTTON_MODE_HIDDEN_LEFT = 3,
} display_power_button_mode_t;

display_button_visibility_t display_button_visibility_resolve(bool screen_allows_button,
                                                              bool configured_show_visuals);
display_power_button_mode_t display_button_mode_from_config(display_power_button_corner_t corner,
                                                            bool show_visuals);
display_power_button_corner_t display_button_mode_corner(display_power_button_mode_t mode);
bool display_button_mode_shows_visuals(display_power_button_mode_t mode);
