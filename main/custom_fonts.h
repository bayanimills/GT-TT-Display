#ifndef CUSTOM_FONTS_H
#define CUSTOM_FONTS_H

#include "lvgl.h" 

// Declare all your custom fonts here
LV_FONT_DECLARE(angelwish_96);
LV_FONT_DECLARE(untyped_96);
LV_FONT_DECLARE(Nevan_RUS_96);
LV_FONT_DECLARE(montserrat_120);
LV_FONT_DECLARE(montserrat_140);
/* Montserrat at 200px for the large clock face. Its figures are proportional,
 * so the per-character cells in clock.c are what keeps the time from shifting
 * as digits change; the font does not do it for us. */
LV_FONT_DECLARE(montserrat_200);
// LV_FONT_DECLARE(angelwish_48);
// LV_FONT_DECLARE(angelwish_24);


// Custom images
LV_IMG_DECLARE(clock_solid_full);
LV_IMG_DECLARE(cube_solid_full);
LV_IMG_DECLARE(cubes_solid_full);

#endif // CUSTOM_FONTS_H
