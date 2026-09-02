#ifndef GLASS_H
#define GLASS_H

#include <stdint.h>
#include <stdbool.h>
#include "lvgl.h"

/* The Glass skin's home surface: a full-screen wallpaper with a user-chosen
 * set of frosted widgets, a tap-to-reveal bottom drawer for navigation, and
 * glass sheets for choosing widgets, layout and wallpaper.
 *
 * home.c hands off to these when theme_get_skin() == THEME_SKIN_GLASS, so
 * every other screen keeps calling home_screen_create() / home_get_screen()
 * exactly as before. */

typedef enum {
    GLASS_WIDGET_HASHRATE = 0,
    GLASS_WIDGET_TEMPERATURE,
    GLASS_WIDGET_POWER,
    GLASS_WIDGET_SHARES,
    GLASS_WIDGET_BEST_DIFF,
    GLASS_WIDGET_FAN,
    GLASS_WIDGET_POOL,
    GLASS_WIDGET_BLOCK,
    GLASS_WIDGET_PRICE,
    GLASS_WIDGET_MEMPOOL,
    GLASS_WIDGET_CLOCK,
    GLASS_WIDGET_COUNT
} glass_widget_t;

typedef enum {
    GLASS_LAYOUT_SINGLE = 0,
    GLASS_LAYOUT_TWIN,
    GLASS_LAYOUT_COUNT
} glass_layout_t;

typedef enum {
    GLASS_SHEET_NONE = 0,
    GLASS_SHEET_WIDGETS,
    GLASS_SHEET_LAYOUT,
    GLASS_SHEET_WALLPAPER,
    GLASS_SHEET_POOL,
} glass_sheet_t;

void      glass_home_create(void);
void      glass_home_destroy(void);
lv_obj_t *glass_home_get_screen(void);
bool      glass_home_is_active(void);

/* Preferences. Setters persist to NVS and, when the surface is on screen,
 * rebuild it (deferred through lv_async_call, so they are safe from events). */
uint32_t       glass_get_widget_mask(void);
void           glass_set_widget_mask(uint32_t mask);
glass_layout_t glass_get_layout(void);
void           glass_set_layout(glass_layout_t layout);
int            glass_get_wallpaper(void);
void           glass_set_wallpaper(int index);
const char    *glass_widget_name(glass_widget_t id);

/* Drawer and sheets, exposed so the simulator can drive them directly. */
void glass_drawer_open(void);
void glass_drawer_close(void);
bool glass_drawer_is_open(void);
void glass_sheet_open(glass_sheet_t sheet);
void glass_sheet_close(void);
void glass_scroll_to(int y);

#endif /* GLASS_H */
