#ifndef GLASS_H
#define GLASS_H

#include <stdint.h>
#include <stdbool.h>
#include "lvgl.h"

/* The Glass skin.
 *
 * Two layers live here. The home surface: a full-screen wallpaper with a
 * user-chosen set of frosted widgets, sheets for choosing widgets, layout and
 * wallpaper. And the chrome every other screen borrows under Glass: a
 * wallpapered screen, frosted panes, the tap-to-reveal bottom drawer that
 * replaces the classic nav bar, and widget styling that stays legible over a
 * wallpaper.
 *
 * home.c hands off to glass_home_create() when theme_get_skin() is Glass; the
 * other screens branch on glass_active() inside their own create functions and
 * keep their labels and update paths, so data flow is identical in both skins. */

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
    GLASS_SHEET_ICONS,
} glass_sheet_t;

/* Which screen a glass screen is, for the drawer's highlight and navigation. */
typedef enum {
    GLASS_SCREEN_HOME = 0,
    GLASS_SCREEN_BLOCK,
    GLASS_SCREEN_MEMPOOL,
    GLASS_SCREEN_CLOCK,
    GLASS_SCREEN_PRICE,
    GLASS_SCREEN_WIFI,
    GLASS_SCREEN_SETTINGS,
    GLASS_SCREEN_NIGHT,
    GLASS_SCREEN_COUNT
} glass_screen_t;

/* ---- home surface ---- */
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

/* ---- chrome for the other screens ---- */

/* True when the Glass skin is selected. */
bool glass_active(void);

/* A screen with the wallpaper behind it and the drawer one tap away. `dim`
 * is for the night screen: wallpaper at a fifth of its brightness over black,
 * and panes made on it are dark slabs rather than bright frost. */
lv_obj_t *glass_screen_create(glass_screen_t kind, bool dim);

/* Must run before a glass screen is deleted: closes the drawer or sheet if it
 * lives there and forgets its panes. Safe to call on a classic screen. */
void glass_screen_detach(lv_obj_t *scr);

/* A frosted pane: wallpaper crop, tint, specular edge, border, shadow. Taps on
 * it fall through to the screen, so the drawer opens from anywhere. */
lv_obj_t *glass_pane(lv_obj_t *parent, int w, int h, int radius);

/* Make taps on a scrollable container (which must stay clickable to scroll)
 * open the drawer like taps on the screen do. */
void glass_attach_drawer_toggle(lv_obj_t *obj);

/* A screen may claim the tap that would otherwise toggle the drawer: the
 * callback returns true to swallow it (e.g. the Wi-Fi keyboard dismissing on
 * the first outside tap). Interceptors form a small chain asked in the order
 * registered; the first to return true wins. All are cleared when the screen
 * is detached. Waking from a dark display is not an interceptor: it is gated
 * below LVGL in display_control_filter_touch(), so it always wins. */
void glass_set_tap_interceptor(bool (*cb)(void));

/* Keep pane crops aimed while `obj` scrolls (panes are children of it). */
void glass_track_scroll(lv_obj_t *obj);

/* Call once a glass screen's panes are built: lays out, aims every crop and
 * derives each pane's material from the wallpaper under it. */
void glass_screen_ready(lv_obj_t *scr);

/* Give a label that sits straight on the wallpaper (not on a pane) a small
 * dark pill so it always has a substrate; `accent` tints it with the vibrant
 * accent instead of white. */
void glass_pill_label(lv_obj_t *label, bool accent);

/* Restyle stock widgets for the material. */
void glass_style_button(lv_obj_t *btn, bool filled);
void glass_style_dropdown(lv_obj_t *dd);
void glass_style_slider(lv_obj_t *slider);
void glass_style_textarea(lv_obj_t *ta);
void glass_style_keyboard(lv_obj_t *kb);
void glass_style_checkbox(lv_obj_t *cb);
void glass_style_bar(lv_obj_t *bar);

/* ---- drawer and sheets (also driven by the simulator) ---- */
void glass_drawer_open(void);
void glass_drawer_close(void);
bool glass_drawer_is_open(void);
void glass_sheet_open(glass_sheet_t sheet);
void glass_sheet_close(void);
void glass_scroll_to(int y);

#endif /* GLASS_H */
