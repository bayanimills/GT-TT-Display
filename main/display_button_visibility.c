#include "display_button_visibility.h"

display_button_visibility_t display_button_visibility_resolve(bool screen_allows_button,
                                                              bool configured_show_visuals)
{
    return (display_button_visibility_t) {
        .interactive = screen_allows_button,
        .show_visuals = configured_show_visuals,
    };
}
