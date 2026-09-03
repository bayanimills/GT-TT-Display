#include <assert.h>

#include "display_button_visibility.h"

int main(void)
{
    assert(display_button_mode_from_config(DISPLAY_POWER_BUTTON_TOP_RIGHT, true) ==
           DISPLAY_POWER_BUTTON_MODE_VISIBLE_RIGHT);
    assert(display_button_mode_from_config(DISPLAY_POWER_BUTTON_TOP_LEFT, true) ==
           DISPLAY_POWER_BUTTON_MODE_VISIBLE_LEFT);
    assert(display_button_mode_from_config(DISPLAY_POWER_BUTTON_TOP_RIGHT, false) ==
           DISPLAY_POWER_BUTTON_MODE_HIDDEN_RIGHT);
    assert(display_button_mode_from_config(DISPLAY_POWER_BUTTON_TOP_LEFT, false) ==
           DISPLAY_POWER_BUTTON_MODE_HIDDEN_LEFT);

    assert(display_button_mode_corner(DISPLAY_POWER_BUTTON_MODE_VISIBLE_RIGHT) ==
           DISPLAY_POWER_BUTTON_TOP_RIGHT);
    assert(display_button_mode_corner(DISPLAY_POWER_BUTTON_MODE_VISIBLE_LEFT) ==
           DISPLAY_POWER_BUTTON_TOP_LEFT);
    assert(display_button_mode_corner(DISPLAY_POWER_BUTTON_MODE_HIDDEN_RIGHT) ==
           DISPLAY_POWER_BUTTON_TOP_RIGHT);
    assert(display_button_mode_corner(DISPLAY_POWER_BUTTON_MODE_HIDDEN_LEFT) ==
           DISPLAY_POWER_BUTTON_TOP_LEFT);
    assert(display_button_mode_shows_visuals(DISPLAY_POWER_BUTTON_MODE_VISIBLE_RIGHT));
    assert(display_button_mode_shows_visuals(DISPLAY_POWER_BUTTON_MODE_VISIBLE_LEFT));
    assert(!display_button_mode_shows_visuals(DISPLAY_POWER_BUTTON_MODE_HIDDEN_RIGHT));
    assert(!display_button_mode_shows_visuals(DISPLAY_POWER_BUTTON_MODE_HIDDEN_LEFT));

    display_button_visibility_t visible = display_button_visibility_resolve(true, true);
    assert(visible.interactive);
    assert(visible.show_visuals);

    display_button_visibility_t invisible = display_button_visibility_resolve(true, false);
    assert(invisible.interactive);
    assert(!invisible.show_visuals);

    display_button_visibility_t suppressed = display_button_visibility_resolve(false, true);
    assert(!suppressed.interactive);
    assert(suppressed.show_visuals);

    display_button_visibility_t suppressed_invisible = display_button_visibility_resolve(false, false);
    assert(!suppressed_invisible.interactive);
    assert(!suppressed_invisible.show_visuals);
    return 0;
}
