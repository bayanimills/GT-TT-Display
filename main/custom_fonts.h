#ifndef CUSTOM_FONTS_H
#define CUSTOM_FONTS_H

#include "lvgl.h" 

// Declare all your custom fonts here
LV_FONT_DECLARE(angelwish_96);
LV_FONT_DECLARE(untyped_96);
LV_FONT_DECLARE(Nevan_RUS_96);
LV_FONT_DECLARE(montserrat_120);
LV_FONT_DECLARE(montserrat_140);
/* DSEG7 Classic Bold, the seven-segment face, digits and colon only - it has
 * no letters and not even a space, so it can be used for a time and nothing
 * else. Monospace: every digit advances 159.94px at 196 and 65.25 at 80, which
 * is what lets the row be sized to fill its box exactly.
 *
 * 196 puts four digits and a colon in 680 of the 700px box; 80 puts them in
 * 280 of the twin card's 298. Ten pixels a side, and no more. */
LV_FONT_DECLARE(dseg7_196);
LV_FONT_DECLARE(dseg7_80);
// LV_FONT_DECLARE(angelwish_48);
// LV_FONT_DECLARE(angelwish_24);


// Custom images
LV_IMG_DECLARE(clock_solid_full);
LV_IMG_DECLARE(cube_solid_full);
LV_IMG_DECLARE(cubes_solid_full);

#endif // CUSTOM_FONTS_H
