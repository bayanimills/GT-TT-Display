#ifndef WALLPAPER_H
#define WALLPAPER_H

#include <stdint.h>
#include <stdbool.h>
#include "lvgl.h"

/* Procedural wallpapers for the Glass skin.
 *
 * Photographic wallpapers do not fit: one 800x480 RGB565 frame is 768KB of
 * flash and there are three of them, against a 4MB app partition. Instead each
 * wallpaper is a small scene description (gradient, glows, discs, bands) that
 * is rasterised into PSRAM at boot, so flash cost is a few hundred bytes.
 *
 * Every wallpaper renders in two variants. SHARP is what shows between the
 * widgets. FROST is the same scene drawn with soft edges and lifted, slightly
 * desaturated colour: LVGL 8 has no backdrop blur, so a glass panel instead
 * shows a crop of FROST offset to its own screen position, which reads as a
 * frosted pane over the sharp wallpaper behind it. */

typedef enum {
    WALLPAPER_SHARP = 0,
    WALLPAPER_FROST,
    WALLPAPER_VARIANT_COUNT
} wallpaper_variant_t;

int         wallpaper_count(void);
const char *wallpaper_name(int index);

/* Rasterise wallpaper `index` at full screen size into PSRAM (both variants).
 * Re-renders only when the index changes. Returns false if memory ran out, in
 * which case wallpaper_image() returns NULL and callers must fall back to a
 * flat colour. Synchronous: blocks the caller for the whole render. */
bool wallpaper_prepare(int index);

/* Same, on a worker task that never holds the LVGL lock while it renders.
 * `done` is called on the worker under the LVGL lock once the new buffers are
 * installed (ok=false if allocation failed). If the wallpaper is already
 * current, `done(true)` is called synchronously before this returns. A
 * request that arrives while a render is in flight is queued and run after. */
typedef void (*wallpaper_done_cb_t)(bool ok);
void wallpaper_prepare_async(int index, wallpaper_done_cb_t done);
int  wallpaper_current(void);

const lv_img_dsc_t *wallpaper_image(wallpaper_variant_t variant);

/* Render a preview of wallpaper `index` into a caller-owned RGB565 buffer of
 * w*h pixels and fill in `dsc` to describe it. Used by the wallpaper picker. */
void wallpaper_render_thumb(int index, lv_img_dsc_t *dsc, uint16_t *buf, int w, int h);

/* Free the full-size buffers (e.g. when leaving the Glass skin). */
void wallpaper_release(void);

#endif /* WALLPAPER_H */
