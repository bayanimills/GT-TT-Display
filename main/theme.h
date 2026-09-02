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
    THEME_SLOT_COUNT
} theme_slot_t;

typedef struct {
    const char *name;
    uint32_t    slot[THEME_SLOT_COUNT];   /* 0xRRGGBB */
} theme_preset_t;

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

/* The screen layer registers how to rebuild itself after a theme change. */
void theme_register_reload(void (*reload_cb)(void));

#endif /* THEME_H */
