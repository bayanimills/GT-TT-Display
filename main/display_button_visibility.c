#include "display_button_visibility.h"

bool display_button_visibility_should_show(display_power_button_corner_t placement,
                                           bool screen_allows_button)
{
    return screen_allows_button && placement != DISPLAY_POWER_BUTTON_HIDDEN;
}
