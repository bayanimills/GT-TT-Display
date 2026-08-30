#include <assert.h>

#include "display_button_visibility.h"

int main(void)
{
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
