#include <assert.h>

#include "display_button_visibility.h"

int main(void)
{
    assert(display_button_visibility_should_show(DISPLAY_POWER_BUTTON_TOP_RIGHT, true));
    assert(display_button_visibility_should_show(DISPLAY_POWER_BUTTON_TOP_LEFT, true));
    assert(!display_button_visibility_should_show(DISPLAY_POWER_BUTTON_HIDDEN, true));

    assert(!display_button_visibility_should_show(DISPLAY_POWER_BUTTON_TOP_RIGHT, false));
    assert(!display_button_visibility_should_show(DISPLAY_POWER_BUTTON_TOP_LEFT, false));
    assert(!display_button_visibility_should_show(DISPLAY_POWER_BUTTON_HIDDEN, false));
    return 0;
}
