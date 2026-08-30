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

display_button_visibility_t display_button_visibility_resolve(bool screen_allows_button,
                                                              bool configured_show_visuals);
