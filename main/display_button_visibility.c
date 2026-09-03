#include "display_button_visibility.h"

display_button_visibility_t display_button_visibility_resolve(bool screen_allows_button,
                                                              bool configured_show_visuals)
{
    return (display_button_visibility_t) {
        .interactive = screen_allows_button,
        .show_visuals = configured_show_visuals,
    };
}

display_power_button_mode_t display_button_mode_from_config(display_power_button_corner_t corner,
                                                            bool show_visuals)
{
    if (show_visuals) {
        return corner == DISPLAY_POWER_BUTTON_TOP_LEFT
                   ? DISPLAY_POWER_BUTTON_MODE_VISIBLE_LEFT
                   : DISPLAY_POWER_BUTTON_MODE_VISIBLE_RIGHT;
    }

    return corner == DISPLAY_POWER_BUTTON_TOP_LEFT
               ? DISPLAY_POWER_BUTTON_MODE_HIDDEN_LEFT
               : DISPLAY_POWER_BUTTON_MODE_HIDDEN_RIGHT;
}

display_power_button_corner_t display_button_mode_corner(display_power_button_mode_t mode)
{
    return mode == DISPLAY_POWER_BUTTON_MODE_VISIBLE_LEFT ||
                   mode == DISPLAY_POWER_BUTTON_MODE_HIDDEN_LEFT
               ? DISPLAY_POWER_BUTTON_TOP_LEFT
               : DISPLAY_POWER_BUTTON_TOP_RIGHT;
}

bool display_button_mode_shows_visuals(display_power_button_mode_t mode)
{
    return mode == DISPLAY_POWER_BUTTON_MODE_VISIBLE_RIGHT ||
           mode == DISPLAY_POWER_BUTTON_MODE_VISIBLE_LEFT;
}
