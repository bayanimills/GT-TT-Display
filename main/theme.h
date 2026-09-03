#ifndef THEME_H
#define THEME_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "lvgl.h"

/* Theme colour slots.
 * Every screen paints through these; nothing hardcodes a colour. */
typedef enum {
    THEME_BACKGROUND = 0,
    THEME_CARD_BG,
    THEME_ACCENT,
    THEME_RED,
    THEME_TEXT_PRIMARY,
    THEME_TEXT_SECONDARY,
    THEME_TEXT_ON_ACCENT,
    THEME_BORDER,
    THEME_NAV_BG,
    THEME_ICON,          /* icon tint; presets leave it equal to the accent */
    THEME_SLOT_COUNT
} theme_slot_t;

typedef struct {
    const char *name;
    uint32_t    slot[THEME_SLOT_COUNT];   /* 0xRRGGBB */
} theme_preset_t;

/* A skin is the second, orthogonal axis of a theme: the palette says which
 * colours, the skin says which layout and surface treatment. Classic is the
 * original card-and-nav-bar UI painted through the nine slots; Glass is the
 * full-screen wallpaper-and-widgets surface in glass.c, which borrows only the
 * palette's accent. Keeping the two separate means every existing preset
 * still works untouched in either skin. */
typedef enum {
    THEME_SKIN_CLASSIC = 0,
    THEME_SKIN_GLASS,
    THEME_SKIN_COUNT
} theme_skin_t;

/* Load persisted theme (NVS on device, defaults elsewhere). Safe to call once at boot. */
void theme_init(void);

/* Current colour for a slot. Cheap: plain array read. */
lv_color_t theme_color(theme_slot_t slot);

/* Raw 0xRRGGBB for a slot, for serialising / the sim. */
uint32_t theme_color_hex(theme_slot_t slot);

/* Built-in presets. */
const theme_preset_t *theme_presets(size_t *count);
int         theme_preset_count(void);
int         theme_get_index(void);
const char *theme_get_name(void);

/* Select a preset by index. Persists and rebuilds the active screen. */
void theme_set_index(int index);

/* Override a single slot (custom themes / live sim editing).
 * Marks the theme custom; call theme_commit() to persist and repaint. */
void theme_set_slot(theme_slot_t slot, uint32_t rgb);
void theme_commit(void);

/* Icon tint override (0 = follow the preset, otherwise 0xRRGGBB). Persists.
 * Lets the Glass skin pick an icon colour that suits a wallpaper without
 * abandoning the palette. */
void     theme_set_icon_override(uint32_t rgb);
uint32_t theme_get_icon_override(void);

/* Skin selection. Persists; takes effect the next time the home screen is built
 * (the settings screen that hosts the picker is rebuilt through the reload). */
theme_skin_t theme_get_skin(void);
const char  *theme_skin_name(theme_skin_t skin);
void         theme_set_skin(theme_skin_t skin);

/* The screen layer registers how to rebuild itself after a theme change. */
void theme_register_reload(void (*reload_cb)(void));

/* Black or white, whichever reads better on `bg`.
 *
 * For a glyph sitting on a filled accent shape. The themable
 * THEME_TEXT_ON_ACCENT slot cannot answer this on its own: one value has
 * to serve nine presets plus whatever the user picks, and on the Bitaxe
 * Red accent it produced a near-black glyph on a dark red disc, about
 * 2.4:1, under the 3:1 a UI component needs. Deriving it from the actual
 * background is right for every palette including custom ones. */
lv_color_t theme_ink_on(lv_color_t bg);
#endif /* THEME_H */
