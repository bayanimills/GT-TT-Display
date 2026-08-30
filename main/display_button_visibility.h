#pragma once

#include <stdbool.h>

typedef enum {
    DISPLAY_POWER_BUTTON_TOP_RIGHT = 0,
    DISPLAY_POWER_BUTTON_TOP_LEFT = 1,
    DISPLAY_POWER_BUTTON_HIDDEN = 2,
} display_power_button_corner_t;

bool display_button_visibility_should_show(display_power_button_corner_t placement,
                                           bool screen_allows_button);
