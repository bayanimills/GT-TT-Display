#ifndef CUSTOM_FONTS_H
#define CUSTOM_FONTS_H

#include "lvgl.h" 

// Declare all your custom fonts here
LV_FONT_DECLARE(angelwish_96);
LV_FONT_DECLARE(untyped_96);
LV_FONT_DECLARE(Nevan_RUS_96);
LV_FONT_DECLARE(montserrat_120);
LV_FONT_DECLARE(montserrat_140);
/* DejaVu Sans Mono at 220px, digits and colon only. Genuinely monospace -
 * every glyph advances 132.44px - so the clock cannot shift as the time
 * changes, and the digits stand ~166px against Montserrat's ~100px at 140. */
LV_FONT_DECLARE(dejavu_mono_220);
/* The same face at 96px for the twin card, so both digital layouts read as
 * the same clock rather than two different ones. */
LV_FONT_DECLARE(dejavu_mono_96);
// LV_FONT_DECLARE(angelwish_48);
// LV_FONT_DECLARE(angelwish_24);


// Custom images
LV_IMG_DECLARE(clock_solid_full);
LV_IMG_DECLARE(cube_solid_full);
LV_IMG_DECLARE(cubes_solid_full);

#endif // CUSTOM_FONTS_H
