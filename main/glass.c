#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include "glass.h"
#include "wallpaper.h"
#include "theme.h"
#include "home.h"
#include "block.h"
#include "price.h"
#include "mempool.h"
#include "clock.h"
#include "wifi.h"
#include "settings.h"
#include "night.h"
#include "odds.h"
#include "payout.h"
#include "chain.h"
#include "ota_update.h"
#include "display_control.h"
#include "esp_heap_caps.h"
#include "custom_fonts.h"
#include "assets/glass_icons.h"
#include "lvgl__lvgl/src/extra/libs/qrcode/lv_qrcode.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"

static const char *TAG = "glass";

#define GLASS_NVS_NS       "glass"
#define GLASS_NVS_WIDGETS  "widgets"
#define GLASS_NVS_LAYOUT   "layout"
#define GLASS_NVS_WALL     "wall"
#define GLASS_NVS_ORDER    "order"

/* Surface geometry. The whole 800x480 is the canvas; there is no chrome. */
#define EDGE_PAD      24
#define GAP           16
#define CONTENT_W     (SCREEN_WIDTH - 2 * EDGE_PAD)          /* 752 */
#define TWIN_W        ((CONTENT_W - GAP) / 2)                /* 368 */
#define HERO_H        168
#define TWIN_H        116
#define SINGLE_H      88
#define CARD_RADIUS   24

#define DRAWER_H      308
#define DRAWER_MARGIN 14
/* The drawer is a grid of destinations over a row of customise actions.
 *
 * It used to be one row of eleven 68 px buttons with 14 px captions, which is
 * as small as this panel can render and still pretend to be readable, and it
 * had no room left for another screen. Five columns of 148 px give an icon you
 * can identify across a desk and a caption at 16 px. Ten cells over two rows
 * leaves one spare for the next screen. */
#define DRAWER_COLS   5
#define CELL_W        146
#define CELL_H        102
#define CELL_DISC     62
#define GRID_W        (DRAWER_COLS * CELL_W)

/* Text tiers. Captions and sub-values each have a floor; nothing on a pane is
 * drawn fainter than the sub-value tier. Contrast itself is guaranteed by the
 * material underneath (see material_for), not by these numbers. */
#define CAPTION_OPA    LV_OPA_80
#define SUB_OPA        LV_OPA_90
#define BORDER_OPA     LV_OPA_30
#define SPECULAR_OPA   LV_OPA_60

/* Material text colours: the material is always dark glass carrying white
 * text (the three wallpapers peak at a frost luminance of 0.49, so a light
 * material never had a region to appear on and is deliberately not offered).
 * The secondary tier is a step toward the substrate. */
#define DARK_PRIMARY     0xFFFFFF
#define DARK_SECONDARY   0xD4DAE4

/* Substrate target. A pane's black tint is chosen so the wallpaper showing
 * through it lands at or below this luminance, which puts white text above
 * 4.5:1 at the caption tier on every wallpaper. */
#define DARK_TARGET_LUM   0.13f
#define TINT_MIN          0.30f
#define TINT_MAX          0.85f
#define ICON_LUM_GAP      0.30f

#define DEFAULT_MASK ((1u << GLASS_WIDGET_HASHRATE) | (1u << GLASS_WIDGET_TEMPERATURE) | \
                      (1u << GLASS_WIDGET_POWER)    | (1u << GLASS_WIDGET_SHARES) | \
                      (1u << GLASS_WIDGET_BEST_DIFF)| (1u << GLASS_WIDGET_FAN) | \
                      (1u << GLASS_WIDGET_BLOCK))

/* user_data markers on labels and images so the material walk knows which
 * ones are icons or accent text rather than body text. */
#define MARK_ICON   ((void *) 1)
#define MARK_ACCENT ((void *) 2)

typedef struct {
    glass_widget_t id;
    lv_obj_t *card;
    lv_obj_t *value;      /* main figure */
    lv_obj_t *value2;     /* hero only: fractional part */
    lv_obj_t *unit;       /* hero only */
    lv_obj_t *sub;        /* secondary line */
    lv_obj_t *aux;        /* hero only: right-hand figure */
} card_t;

typedef struct {
    float    substrate;   /* luminance the text sits on, 0..1 */
    lv_opa_t tint_opa;
} material_t;

/* Every glass pane keeps a frost crop of the wallpaper as its backdrop, and the
 * crop has to track the pane's on-screen position (scrolling, the drawer
 * sliding in), so panes register here and frost_sync() re-aims them. The same
 * pass samples the wallpaper under the pane and sets its material. Entries
 * remember which screen they belong to so a screen can be forgotten wholesale
 * when it is destroyed. */
typedef struct {
    lv_obj_t *panel;
    lv_obj_t *frost;
    lv_obj_t *tint;
    lv_obj_t *host;
    bool      dim;
    int       last_x, last_y;      /* where the material was last sampled */
    int       off_x, off_y;        /* crop offset currently applied */
    uint8_t   mat_bucket;          /* 0 = never applied */
    material_t mat;
} frost_ref_t;

/* The screen the drawer and sheets currently attach to. Only one glass screen
 * is normally alive, but during navigation the next one is created before the
 * previous is destroyed, so the host is tracked explicitly. */
static lv_obj_t      *s_host      = NULL;
static glass_screen_t s_host_kind = GLASS_SCREEN_HOME;

/* Screens are kept after you navigate away from them.
 *
 * Every screen_create() already returns early when its screen exists, so
 * caching is mostly a matter of not tearing the old one down: the next visit
 * becomes an lv_scr_load() and little else. That is the whole of the CPU
 * spike on a screen change, which measures about twice a classic screen
 * because each glass pane is four composited layers to rebuild.
 *
 * The catch is that glass_screen_create() also sets the host state every
 * drawer, crop and power button reads, and a create that no-ops sets none of
 * it. So each screen records what it established, and a cached visit adopts
 * that back rather than leaving the previous screen s state in place. */
typedef struct {
    lv_obj_t *scr;
    lv_obj_t *wall;
    bool      dim;
} host_rec_t;

static host_rec_t s_hosts[GLASS_SCREEN_COUNT];

/* Bounded, or a tour of every screen pins them all in memory. The screen on
 * display is never evicted. */
#define SCREEN_CACHE_MAX 4
static glass_screen_t s_mru[GLASS_SCREEN_COUNT];
static int            s_mru_n = 0;
static bool           s_host_dim  = false;
static lv_obj_t      *s_host_wall = NULL;
static int            s_host_wall_index = -1;   /* wallpaper the host was built with */
#define TAP_INTERCEPTOR_MAX 4
typedef struct { bool (*cb)(void); lv_obj_t *host; } tap_interceptor_t;
static tap_interceptor_t s_tap_interceptor[TAP_INTERCEPTOR_MAX];
static int               s_tap_interceptor_count = 0;

static void interceptors_drop_host(lv_obj_t *host)
{
    for (int i = s_tap_interceptor_count - 1; i >= 0; i--) {
        if (s_tap_interceptor[i].host != host) continue;
        s_tap_interceptor[i] = s_tap_interceptor[s_tap_interceptor_count - 1];
        s_tap_interceptor_count--;
    }
}

/* Home surface. */
static lv_obj_t *s_screen       = NULL;
static lv_obj_t *s_grid         = NULL;
static lv_timer_t *s_refresh    = NULL;

/* Drawer and sheets. */
static lv_obj_t *s_drawer_scrim = NULL;
static lv_obj_t *s_drawer_sheet = NULL;
static lv_obj_t *s_drawer_host  = NULL;
static lv_obj_t *s_sheet_scrim  = NULL;
static lv_obj_t *s_sheet_panel  = NULL;
static lv_obj_t *s_sheet_host   = NULL;
static glass_sheet_t s_sheet       = GLASS_SHEET_NONE;
static bool          s_drawer_open = false;
static bool          s_drawer_anim = false;

static card_t      s_cards[GLASS_WIDGET_COUNT];
static int         s_card_count = 0;

/* The registry grows as needed and every pane unregisters itself from its
 * own LV_EVENT_DELETE, so a caller that deletes a pane (lv_obj_clean on a
 * row, a dialog closing) can never leave a dangling entry behind. */
static frost_ref_t *s_frost = NULL;
static int          s_frost_count = 0;
static int          s_frost_cap = 0;

static uint32_t       s_mask   = DEFAULT_MASK;
static uint8_t        s_order[GLASS_WIDGET_COUNT];   /* widget ids, display order */
static glass_layout_t s_layout = GLASS_LAYOUT_TWIN;
static int            s_wall   = 0;
static bool           s_prefs_loaded = false;

/* Thumbnails for the wallpaper picker live only while the sheet is open. */
static uint16_t     *s_thumb_buf[3];
static lv_img_dsc_t  s_thumb_dsc[3];
#define THUMB_W 176
#define THUMB_H 106

static const char *k_widget_names[GLASS_WIDGET_COUNT] = {
    "Hashrate", "Temperature", "Power", "Shares", "Best difficulty", "Fan",
    "Pool", "Block height", "BTC price", "Mempool", "Clock", "Halving",
    "Solo odds",
};

/* Icon tint choices offered in the drawer. 0 means follow the palette. */
typedef struct { const char *name; uint32_t rgb; } icon_choice_t;
static const icon_choice_t k_icon_choices[] = {
    { "Accent", 0 },        { "White", 0xFFFFFF },  { "Amber", 0xF5B942 },
    { "Mint",   0x4DE3B0 }, { "Sky",   0x5AC8FA },  { "Lilac", 0xB48CFF },
    { "Rose",   0xFF6B9A },
};
#define ICON_CHOICE_COUNT ((int) (sizeof(k_icon_choices) / sizeof(k_icon_choices[0])))

static void build_grid(void);
static void rebuild_grid_async(void *unused);
static void frost_sync(void);
static void drawer_toggle_cb(lv_event_t *e);
static void drawer_reset(void);
static void sheet_reset(void);

/* ---------------- preferences ---------------- */

static void order_reset(void)
{
    for (int i = 0; i < GLASS_WIDGET_COUNT; i++) s_order[i] = (uint8_t) i;
}

/* The saved part of an order: every id in range, no repeats. Says nothing
 * about ids that were not in the build that wrote it. */
static bool order_prefix_valid(const uint8_t *o, size_t n)
{
    uint32_t seen = 0;
    for (size_t i = 0; i < n; i++) {
        if (o[i] >= GLASS_WIDGET_COUNT || (seen & (1u << o[i]))) return false;
        seen |= 1u << o[i];
    }
    return true;
}

static bool order_valid(const uint8_t *o)
{
    uint32_t seen = 0;
    for (int i = 0; i < GLASS_WIDGET_COUNT; i++) {
        if (o[i] >= GLASS_WIDGET_COUNT || (seen & (1u << o[i]))) return false;
        seen |= 1u << o[i];
    }
    return true;
}

static void prefs_load(void)
{
    if (s_prefs_loaded) return;
    s_prefs_loaded = true;
    order_reset();

    nvs_handle_t h;
    if (nvs_open(GLASS_NVS_NS, NVS_READONLY, &h) != ESP_OK) return;
    int32_t v;
    if (nvs_get_i32(h, GLASS_NVS_WIDGETS, &v) == ESP_OK) s_mask = (uint32_t) v;
    if (nvs_get_i32(h, GLASS_NVS_LAYOUT, &v) == ESP_OK && v >= 0 && v < GLASS_LAYOUT_COUNT) s_layout = (glass_layout_t) v;
    if (nvs_get_i32(h, GLASS_NVS_WALL, &v) == ESP_OK && v >= 0 && v < wallpaper_count()) s_wall = (int) v;
    /* A saved order from an older build is shorter than this array, because
     * adding a widget grows GLASS_WIDGET_COUNT. Requiring an exact length
     * threw the whole arrangement away every time one was added, which has
     * already happened twice. Keep what was saved, in the order it was saved,
     * and append whatever is new on the end. */
    uint8_t order[GLASS_WIDGET_COUNT];
    size_t len = sizeof(order);
    if (nvs_get_blob(h, GLASS_NVS_ORDER, order, &len) == ESP_OK && len > 0 &&
        len <= sizeof(order) && order_prefix_valid(order, len)) {
        uint32_t seen = 0;
        size_t n = 0;
        for (; n < len; n++) {
            s_order[n] = order[n];
            seen |= 1u << order[n];
        }
        for (uint8_t id = 0; id < GLASS_WIDGET_COUNT && n < GLASS_WIDGET_COUNT; id++) {
            if (!(seen & (1u << id))) {
                s_order[n++] = id;
            }
        }
    }
    nvs_close(h);
}

static void prefs_save(void)
{
    nvs_handle_t h;
    if (nvs_open(GLASS_NVS_NS, NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_i32(h, GLASS_NVS_WIDGETS, (int32_t) s_mask);
    nvs_set_i32(h, GLASS_NVS_LAYOUT, (int32_t) s_layout);
    nvs_set_i32(h, GLASS_NVS_WALL, (int32_t) s_wall);
    nvs_set_blob(h, GLASS_NVS_ORDER, s_order, sizeof(s_order));
    nvs_commit(h);
    nvs_close(h);
}

uint32_t       glass_get_widget_mask(void) { prefs_load(); return s_mask; }
glass_layout_t glass_get_layout(void)      { prefs_load(); return s_layout; }
int            glass_get_wallpaper(void)   { prefs_load(); return s_wall; }
const char    *glass_widget_name(glass_widget_t id)
{
    if (id < 0 || id >= GLASS_WIDGET_COUNT) return "?";
    return k_widget_names[id];
}
bool glass_active(void) { return theme_get_skin() == THEME_SKIN_GLASS; }

void glass_set_widget_mask(uint32_t mask)
{
    glass_screens_forget();
    prefs_load();
    s_mask = mask & ((1u << GLASS_WIDGET_COUNT) - 1);
    prefs_save();
    if (s_screen) lv_async_call(rebuild_grid_async, NULL);
}

void glass_set_layout(glass_layout_t layout)
{
    glass_screens_forget();
    prefs_load();
    if (layout < 0 || layout >= GLASS_LAYOUT_COUNT) return;
    s_layout = layout;
    prefs_save();
    if (s_screen) lv_async_call(rebuild_grid_async, NULL);
}

static void material_reapply_all(void)
{
    for (int i = 0; i < s_frost_count; i++) s_frost[i].mat_bucket = 0;
    frost_sync();
}

static void rebuild_host_async(void *unused)
{
    (void) unused;
    /* Only when the host was built against a different (or no) wallpaper;
     * a rebuilt host reports the current one and the chain stops here. */
    if (!s_host || s_host_wall_index == wallpaper_current()) return;
    glass_rebuild_host();
}

/* Runs on the wallpaper worker, under the LVGL lock, when a render lands.
 * The screen was built with flat panes (or the old wallpaper); rebuilding it
 * on the LVGL task gives every pane its crop and re-derives the materials. */
static void wallpaper_ready_cb(bool ok)
{
    if (!ok || !s_host || !glass_active()) return;
    lv_async_call(rebuild_host_async, NULL);
}

void glass_set_wallpaper(int index)
{
    glass_screens_forget();
    prefs_load();
    if (index < 0 || index >= wallpaper_count()) return;
    s_wall = index;
    prefs_save();
    if (s_host) wallpaper_prepare_async(s_wall, wallpaper_ready_cb);
}

/* ---------------- colour maths ---------------- */

static float color_lum(lv_color_t c)
{
    lv_color32_t c32;
    c32.full = lv_color_to32(c);
    return (0.2126f * c32.ch.red + 0.7152f * c32.ch.green + 0.0722f * c32.ch.blue) / 255.0f;
}

static inline float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

/* iOS does not put a brand colour on glass neat: over a dark material it is
 * lifted and desaturated, over a light one deepened. Then it is pushed until
 * it sits at least ICON_LUM_GAP of luminance away from the substrate, so an
 * icon can never sink into the pane it sits on. */
static lv_color_t vibrant(lv_color_t base, const material_t *m)
{
    lv_color_hsv_t hsv = lv_color_to_hsv(base);
    if (hsv.s > 72) hsv.s = 72;
    hsv.v = 100;
    lv_color_t out = lv_color_hsv_to_rgb(hsv.h, hsv.s, hsv.v);
    for (int i = 0; i < 8 && color_lum(out) < m->substrate + ICON_LUM_GAP && hsv.s > 8; i++) {
        hsv.s = (uint8_t) (hsv.s > 12 ? hsv.s - 12 : 0);
        out = lv_color_hsv_to_rgb(hsv.h, hsv.s, hsv.v);
    }
    return out;
}

static lv_color_t material_primary(const material_t *m)   { (void) m; return lv_color_hex(DARK_PRIMARY); }
static lv_color_t material_secondary(const material_t *m) { (void) m; return lv_color_hex(DARK_SECONDARY); }
static lv_color_t material_icon(const material_t *m)      { return vibrant(theme_color(THEME_ICON), m); }
static lv_color_t material_accent(const material_t *m)    { return vibrant(theme_color(THEME_ACCENT), m); }

/* Mean luminance of the frost wallpaper under a screen rectangle. */
static float region_lum(int x1, int y1, int x2, int y2)
{
    const lv_img_dsc_t *img = wallpaper_image(WALLPAPER_FROST);
    if (!img) return 0.10f;
    const uint16_t *px = (const uint16_t *) img->data;
    if (x1 < 0) x1 = 0;
    if (y1 < 0) y1 = 0;
    if (x2 > SCREEN_WIDTH - 1) x2 = SCREEN_WIDTH - 1;
    if (y2 > SCREEN_HEIGHT - 1) y2 = SCREEN_HEIGHT - 1;
    if (x2 <= x1 || y2 <= y1) return 0.10f;

    uint32_t sum = 0, n = 0;
    for (int y = y1; y <= y2; y += 8) {
        const uint16_t *row = px + (size_t) y * SCREEN_WIDTH;
        for (int x = x1; x <= x2; x += 8) {
            uint16_t v = row[x];
            uint32_t r = ((v >> 11) & 0x1F) << 3, g = ((v >> 5) & 0x3F) << 2, b = (v & 0x1F) << 3;
            sum += (r * 54 + g * 183 + b * 18) >> 8;
            n++;
        }
    }
    return n ? (float) sum / (255.0f * (float) n) : 0.10f;
}

/* Choose the tint for a pane over wallpaper of mean luminance `lum`. */
static material_t material_for(float lum, bool dim)
{
    material_t m;
    float a = dim ? TINT_MAX : clampf((lum - DARK_TARGET_LUM) / (lum + 0.001f), TINT_MIN, TINT_MAX);
    m.tint_opa  = (lv_opa_t) (255.0f * a);
    m.substrate = lum * (1.0f - a);
    return m;
}

static bool color_eq(lv_color_t a, lv_color_t b) { return a.full == b.full; }

static bool is_material_text(lv_color_t c, bool *secondary)
{
    static const uint32_t primaries[]   = { DARK_PRIMARY };
    static const uint32_t secondaries[] = { DARK_SECONDARY, 0xC8D0DC };
    for (size_t i = 0; i < sizeof(primaries) / sizeof(primaries[0]); i++) {
        if (color_eq(c, lv_color_hex(primaries[i]))) { *secondary = false; return true; }
    }
    for (size_t i = 0; i < sizeof(secondaries) / sizeof(secondaries[0]); i++) {
        if (color_eq(c, lv_color_hex(secondaries[i]))) { *secondary = true; return true; }
    }
    return false;
}

/* Recolour everything on a pane for its material. Labels and icons created
 * by this file carry markers; labels from the classic screen code are
 * recognised by the colour they were painted with (the theme's Glass text
 * values or the raw accent), so the same pass serves every screen. */
static void apply_material_rec(lv_obj_t *obj, const material_t *m)
{
    lv_color_t raw_accent = theme_color(THEME_ACCENT);
    lv_color_t raw_icon   = theme_color(THEME_ICON);
    uint32_t n = lv_obj_get_child_cnt(obj);
    for (uint32_t i = 0; i < n; i++) {
        lv_obj_t *c = lv_obj_get_child(obj, i);
        void *mark = lv_obj_get_user_data(c);
        if (lv_obj_check_type(c, &lv_label_class)) {
            lv_color_t cur = lv_obj_get_style_text_color(c, 0);
            bool secondary = false;
            /* A label painted with the raw accent is accent text from here
             * on: mark it, because after this pass its colour is the vibrant
             * derivative and would no longer match on the next material. */
            if (mark == NULL && (color_eq(cur, raw_accent) || color_eq(cur, raw_icon))) {
                lv_obj_set_user_data(c, MARK_ACCENT);
                mark = MARK_ACCENT;
            }
            if (mark == MARK_ICON)                              lv_obj_set_style_text_color(c, material_icon(m), 0);
            else if (mark == MARK_ACCENT)                       lv_obj_set_style_text_color(c, material_accent(m), 0);
            else if (is_material_text(cur, &secondary))         lv_obj_set_style_text_color(c, secondary ? material_secondary(m) : material_primary(m), 0);
        } else if (lv_obj_check_type(c, &lv_img_class)) {
            lv_color_t cur = lv_obj_get_style_img_recolor(c, 0);
            if (mark == NULL && (color_eq(cur, raw_accent) || color_eq(cur, raw_icon))) {
                lv_obj_set_user_data(c, MARK_ICON);
                mark = MARK_ICON;
            }
            if (mark == MARK_ICON) lv_obj_set_style_img_recolor(c, material_icon(m), 0);
        }
        apply_material_rec(c, m);
    }
}

/* ---------------- the glass material ---------------- */

static void unregister_frost(lv_obj_t *panel)
{
    for (int i = 0; i < s_frost_count; i++) {
        if (s_frost[i].panel != panel) continue;
        s_frost[i] = s_frost[s_frost_count - 1];
        s_frost_count--;
        return;
    }
}

static void frost_delete_cb(lv_event_t *e)
{
    unregister_frost(lv_event_get_target(e));
}

static void register_frost(lv_obj_t *panel, lv_obj_t *frost, lv_obj_t *tint, bool dim)
{
    if (s_frost_count == s_frost_cap) {
        int cap = s_frost_cap ? s_frost_cap * 2 : 16;
        frost_ref_t *grown = realloc(s_frost, (size_t) cap * sizeof(*grown));
        if (!grown) {
            ESP_LOGW(TAG, "frost registry full; pane will have no crop aim");
            return;
        }
        s_frost = grown;
        s_frost_cap = cap;
    }
    frost_ref_t *r = &s_frost[s_frost_count++];
    memset(r, 0, sizeof(*r));
    r->panel = panel;
    r->frost = frost;
    r->tint  = tint;
    r->host  = lv_obj_get_screen(panel);
    r->dim   = dim;
    r->last_x = r->last_y = -10000;
    r->off_x = r->off_y = 1;   /* impossible offset, so the first sync applies one */
    lv_obj_add_event_cb(panel, frost_delete_cb, LV_EVENT_DELETE, NULL);
}

static void frost_drop_host(lv_obj_t *host)
{
    for (int i = s_frost_count - 1; i >= 0; i--) {
        if (s_frost[i].host != host) continue;
        s_frost[i] = s_frost[s_frost_count - 1];
        s_frost_count--;
    }
}

/* Aim each pane's frost crop at the pane's own screen rectangle, so what shows
 * through the pane is the (softened) wallpaper directly behind it, then set
 * the pane's material from what is actually under it. The material is only
 * re-applied when it changes bucket, so scrolling does not restyle every
 * label every frame. */
static void frost_sync(void)
{
    for (int i = 0; i < s_frost_count; i++) {
        frost_ref_t *r = &s_frost[i];
        lv_area_t a;
        lv_obj_get_coords(r->panel, &a);
        /* lv_img_set_offset_* invalidate unconditionally, so a pane that has
         * not moved is left alone: scrolling one row must not repaint all. */
        if (r->frost && (r->off_x != -a.x1 || r->off_y != -a.y1)) {
            r->off_x = -a.x1;
            r->off_y = -a.y1;
            lv_img_set_offset_x(r->frost, (lv_coord_t) r->off_x);
            lv_img_set_offset_y(r->frost, (lv_coord_t) r->off_y);
        }

        bool moved = (abs(a.x1 - r->last_x) >= 4) || (abs(a.y1 - r->last_y) >= 4);
        if (!moved && r->mat_bucket) continue;
        r->last_x = a.x1;
        r->last_y = a.y1;

        float lum = region_lum(a.x1, a.y1, a.x2, a.y2);
        material_t m = material_for(lum, r->dim);
#ifndef ESP_PLATFORM
        /* SIM_MATERIAL_LOG=1 prints the sampled luminance per pane, which is
         * how the tint targets were checked against the wallpapers. */
        if (getenv("SIM_MATERIAL_LOG")) {
            ESP_LOGI(TAG, "pane %dx%d at %d,%d lum=%.2f tint=%d", (int) lv_area_get_width(&a),
                     (int) lv_area_get_height(&a), (int) a.x1, (int) a.y1, lum, (int) m.tint_opa);
        }
#endif
        uint8_t bucket = (uint8_t) (1 + (int) (m.substrate * 15.0f));
        if (bucket == r->mat_bucket && abs((int) m.tint_opa - (int) r->mat.tint_opa) < 20) continue;
        r->mat_bucket = bucket;
        r->mat = m;

        if (r->tint) {
            lv_obj_set_style_bg_color(r->tint, lv_color_black(), 0);
            lv_obj_set_style_bg_opa(r->tint, m.tint_opa, 0);
        }
        lv_obj_set_style_border_color(r->panel, lv_color_white(), 0);
        apply_material_rec(r->panel, &m);
    }
}

static void scroll_sync_cb(lv_event_t *e)
{
    (void) e;
    frost_sync();
}

void glass_track_scroll(lv_obj_t *obj)
{
    lv_obj_add_event_cb(obj, scroll_sync_cb, LV_EVENT_SCROLL, NULL);
}

void glass_screen_ready(lv_obj_t *scr)
{
    lv_obj_update_layout(scr);
    frost_sync();
}

static lv_obj_t *glass_panel_create(lv_obj_t *parent, int w, int h, int radius)
{
    bool dim = s_host_dim && lv_obj_get_screen(parent) == s_host;

    lv_obj_t *panel = lv_obj_create(parent);
    lv_obj_set_size(panel, w, h);
    lv_obj_set_style_radius(panel, radius, 0);
    lv_obj_set_style_pad_all(panel, 0, 0);
    lv_obj_set_style_bg_color(panel, lv_color_hex(0x10141F), 0);
    lv_obj_set_style_bg_opa(panel, LV_OPA_90, 0);
    lv_obj_set_style_border_width(panel, 1, 0);
    lv_obj_set_style_border_color(panel, lv_color_white(), 0);
    lv_obj_set_style_border_opa(panel, dim ? LV_OPA_10 : BORDER_OPA, 0);
    lv_obj_set_style_shadow_width(panel, 36, 0);
    lv_obj_set_style_shadow_ofs_y(panel, 12, 0);
    lv_obj_set_style_shadow_color(panel, lv_color_black(), 0);
    lv_obj_set_style_shadow_opa(panel, LV_OPA_40, 0);
    lv_obj_set_style_clip_corner(panel, true, 0);
    lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
    /* Not a hit target: a tap on a pane is a tap on the screen, which is what
     * opens the drawer. Sheets that must hold taps re-add the flag. */
    lv_obj_clear_flag(panel, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_scrollbar_mode(panel, LV_SCROLLBAR_MODE_OFF);

    const lv_img_dsc_t *frost_src = wallpaper_image(WALLPAPER_FROST);
    lv_obj_t *frost = NULL;
    if (frost_src) {
        /* The opaque bg above is only a fallback for when the wallpaper could
         * not be allocated; with a crop present the pane is the crop. */
        lv_obj_set_style_bg_opa(panel, LV_OPA_TRANSP, 0);
        frost = lv_img_create(panel);
        lv_img_set_src(frost, frost_src);
        lv_obj_set_size(frost, w, h);
        lv_obj_set_pos(frost, 0, 0);
        lv_obj_clear_flag(frost, LV_OBJ_FLAG_CLICKABLE);
    } else {
        /* No wallpaper, so there is nothing to sample a material from and
         * nothing behind the pane worth showing. Make it a solid surface:
         * left translucent, the screen underneath reads straight through the
         * captions, which is what a board without PSRAM would show. */
        lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, 0);
    }

    /* Tint: the material itself. Colour and opacity are set by frost_sync()
     * from the wallpaper under the pane; this is only the initial state. */
    lv_obj_t *tint = lv_obj_create(panel);
    lv_obj_set_size(tint, w, h);
    lv_obj_set_pos(tint, 0, 0);
    lv_obj_set_style_radius(tint, radius, 0);
    lv_obj_set_style_bg_color(tint, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(tint, LV_OPA_50, 0);
    lv_obj_set_style_border_width(tint, 0, 0);
    lv_obj_set_style_pad_all(tint, 0, 0);
    lv_obj_clear_flag(tint, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);

    if (frost) {
        register_frost(panel, frost, tint, dim);
    } else {
        /* Not registered, so frost_sync() leaves the tint alone rather than
         * deriving an opacity from a wallpaper that does not exist. */
        lv_obj_set_style_bg_opa(tint, LV_OPA_TRANSP, 0);
    }

    /* Specular edge: a 1px brighter line along the top, the cue that light
     * is catching the upper rim of a slab of glass. */
    lv_obj_t *spec = lv_obj_create(panel);
    lv_obj_set_size(spec, w - 2 * (radius / 2), 1);
    lv_obj_set_pos(spec, radius / 2, 1);
    lv_obj_set_style_radius(spec, 0, 0);
    lv_obj_set_style_bg_color(spec, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(spec, dim ? LV_OPA_20 : SPECULAR_OPA, 0);
    lv_obj_set_style_border_width(spec, 0, 0);
    lv_obj_set_style_pad_all(spec, 0, 0);
    lv_obj_clear_flag(spec, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);

    return panel;
}

lv_obj_t *glass_pane(lv_obj_t *parent, int w, int h, int radius)
{
    return glass_panel_create(parent, w, h, radius);
}

static lv_obj_t *glass_label(lv_obj_t *parent, const char *text, const lv_font_t *font, lv_opa_t opa)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_label_set_text(l, text);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(DARK_PRIMARY), 0);
    lv_obj_set_style_text_opa(l, opa, 0);
    lv_obj_clear_flag(l, LV_OBJ_FLAG_CLICKABLE);
    return l;
}

static lv_obj_t *glass_caption(lv_obj_t *parent, const char *text)
{
    lv_obj_t *l = glass_label(parent, text, &lv_font_montserrat_14, CAPTION_OPA);
    lv_obj_set_style_text_color(l, lv_color_hex(DARK_SECONDARY), 0);
    return l;
}

static lv_obj_t *glass_subvalue(lv_obj_t *parent, const char *text)
{
    lv_obj_t *l = glass_label(parent, text, &lv_font_montserrat_16, SUB_OPA);
    lv_obj_set_style_text_color(l, lv_color_hex(DARK_SECONDARY), 0);
    return l;
}

static lv_obj_t *glass_icon_img(lv_obj_t *parent, const lv_img_dsc_t *src)
{
    lv_obj_t *i = lv_img_create(parent);
    lv_img_set_src(i, src);
    lv_obj_set_style_img_recolor(i, theme_color(THEME_ICON), 0);
    lv_obj_set_style_img_recolor_opa(i, LV_OPA_COVER, 0);
    lv_obj_set_user_data(i, MARK_ICON);
    lv_obj_clear_flag(i, LV_OBJ_FLAG_CLICKABLE);
    return i;
}

static lv_obj_t *glass_icon_symbol(lv_obj_t *parent, const char *symbol, const lv_font_t *font)
{
    lv_obj_t *l = glass_label(parent, symbol, font, LV_OPA_COVER);
    lv_obj_set_style_text_color(l, theme_color(THEME_ICON), 0);
    lv_obj_set_user_data(l, MARK_ICON);
    return l;
}

/* One destination in the drawer grid: a disc with an icon, a caption under it,
 * and the whole cell as the touch target. */
static lv_obj_t *glass_grid_cell(lv_obj_t *parent, const char *symbol, const lv_img_dsc_t *img,
                                 const char *caption, lv_event_cb_t cb, void *user_data, bool active)
{
    lv_obj_t *cont = lv_obj_create(parent);
    lv_obj_set_size(cont, CELL_W, CELL_H);
    lv_obj_set_style_bg_opa(cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(cont, 0, 0);
    lv_obj_set_style_pad_all(cont, 0, 0);
    lv_obj_clear_flag(cont, LV_OBJ_FLAG_SCROLLABLE);
    if (cb) lv_obj_add_event_cb(cont, cb, LV_EVENT_CLICKED, user_data);

    lv_obj_t *btn = lv_obj_create(cont);
    lv_obj_set_size(btn, CELL_DISC, CELL_DISC);
    lv_obj_align(btn, LV_ALIGN_TOP_MID, 0, 4);
    lv_obj_set_style_radius(btn, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(btn, active ? COLOR_ACCENT : lv_color_white(), 0);
    lv_obj_set_style_bg_opa(btn, active ? LV_OPA_90 : LV_OPA_20, 0);
    lv_obj_set_style_border_width(btn, 1, 0);
    lv_obj_set_style_border_color(btn, lv_color_white(), 0);
    lv_obj_set_style_border_opa(btn, LV_OPA_30, 0);
    lv_obj_set_style_pad_all(btn, 0, 0);
    lv_obj_clear_flag(btn, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);

    /* An active button is a solid accent disc, so its glyph is the on-accent
     * colour and must not be re-tinted by the material walk: no marker. */
    if (img) {
        lv_obj_t *i = lv_img_create(btn);
        lv_img_set_src(i, img);
        lv_obj_set_style_img_recolor(i, active ? theme_ink_on(COLOR_ACCENT) : lv_color_hex(DARK_PRIMARY), 0);
        lv_obj_set_style_img_recolor_opa(i, LV_OPA_COVER, 0);
        lv_obj_center(i);
    } else {
        lv_obj_t *l = glass_label(btn, symbol, &lv_font_montserrat_26, LV_OPA_COVER);
        if (active) lv_obj_set_style_text_color(l, theme_ink_on(COLOR_ACCENT), 0);
        lv_obj_center(l);
    }

    /* A pending update should be visible without opening settings to find
     * it, so the cell that leads there wears a dot. */
    if (user_data == (void *) GLASS_SCREEN_SETTINGS && ota_update_available()) {
        lv_obj_t *dot = lv_obj_create(cont);
        lv_obj_set_size(dot, 14, 14);
        lv_obj_align(dot, LV_ALIGN_TOP_MID, CELL_DISC / 2 - 4, 2);
        lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(dot, COLOR_ACCENT, 0);
        lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(dot, 2, 0);
        lv_obj_set_style_border_color(dot, lv_color_white(), 0);
        lv_obj_set_style_border_opa(dot, LV_OPA_70, 0);
        lv_obj_clear_flag(dot, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    }

    lv_obj_t *cap = glass_label(cont, caption, &lv_font_montserrat_16, CAPTION_OPA);
    lv_obj_align(cap, LV_ALIGN_BOTTOM_MID, 0, 0);
    return cont;
}

/* A customise action: these open a sheet rather than going anywhere, so they
 * read as a separate, quieter tier under the destinations. */
static lv_obj_t *glass_pill_button(lv_obj_t *parent, const char *symbol, const char *text,
                                   lv_event_cb_t cb, void *user_data)
{
    lv_obj_t *pill = lv_obj_create(parent);
    lv_obj_set_size(pill, 176, 38);
    lv_obj_set_style_radius(pill, 19, 0);
    lv_obj_set_style_bg_color(pill, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(pill, LV_OPA_20, 0);
    lv_obj_set_style_border_width(pill, 1, 0);
    lv_obj_set_style_border_color(pill, lv_color_white(), 0);
    lv_obj_set_style_border_opa(pill, LV_OPA_30, 0);
    lv_obj_set_style_pad_all(pill, 0, 0);
    lv_obj_clear_flag(pill, LV_OBJ_FLAG_SCROLLABLE);
    if (cb) lv_obj_add_event_cb(pill, cb, LV_EVENT_CLICKED, user_data);

    lv_obj_t *ic = glass_icon_symbol(pill, symbol, &lv_font_montserrat_16);
    lv_obj_align(ic, LV_ALIGN_LEFT_MID, 16, 0);

    lv_obj_t *l = glass_label(pill, text, &lv_font_montserrat_16, LV_OPA_COVER);
    lv_obj_align(l, LV_ALIGN_LEFT_MID, 44, 0);
    return pill;
}

/* ---------------- stock widget styling ---------------- */

void glass_pill_label(lv_obj_t *label, bool accent)
{
    material_t dark = { .substrate = 0.08f, .tint_opa = 0 };
    lv_obj_set_style_bg_color(label, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(label, LV_OPA_60, 0);
    lv_obj_set_style_radius(label, 10, 0);
    lv_obj_set_style_pad_hor(label, 10, 0);
    lv_obj_set_style_pad_ver(label, 3, 0);
    lv_obj_set_style_text_color(label, accent ? vibrant(theme_color(THEME_ACCENT), &dark)
                                              : lv_color_hex(DARK_PRIMARY), 0);
}

void glass_style_button(lv_obj_t *btn, bool filled)
{
    lv_obj_set_style_bg_color(btn, filled ? COLOR_ACCENT : lv_color_white(), 0);
    lv_obj_set_style_bg_opa(btn, filled ? LV_OPA_COVER : LV_OPA_20, 0);
    lv_obj_set_style_border_width(btn, 1, 0);
    lv_obj_set_style_border_color(btn, lv_color_white(), 0);
    lv_obj_set_style_border_opa(btn, filled ? LV_OPA_TRANSP : LV_OPA_30, 0);
    lv_obj_set_style_radius(btn, 12, 0);
    lv_obj_set_style_shadow_width(btn, 0, 0);
    lv_obj_set_style_bg_color(btn, lv_color_white(), LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(btn, LV_OPA_40, LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(btn, LV_OPA_10, LV_STATE_DISABLED);
    lv_obj_t *label = lv_obj_get_child(btn, 0);
    if (label) {
        /* A filled button's label is on the accent, not on the material. */
        lv_obj_set_style_text_color(label, filled ? COLOR_TEXT_ON_ACCENT : lv_color_hex(DARK_PRIMARY), 0);
        if (filled) lv_obj_set_user_data(label, (void *) 3);
    }
}

void glass_style_dropdown(lv_obj_t *dd)
{
    lv_obj_set_style_bg_color(dd, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(dd, LV_OPA_20, 0);
    lv_obj_set_style_border_width(dd, 1, 0);
    lv_obj_set_style_border_color(dd, lv_color_white(), 0);
    lv_obj_set_style_border_opa(dd, LV_OPA_30, 0);
    lv_obj_set_style_radius(dd, 10, 0);
    lv_obj_set_style_text_color(dd, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(dd, LV_OPA_40, LV_STATE_PRESSED);
    /* The list floats over the wallpaper, so it is nearly opaque. */
    lv_obj_t *list = lv_dropdown_get_list(dd);
    if (list) {
        lv_obj_set_style_bg_color(list, lv_color_hex(0x141A2A), 0);
        lv_obj_set_style_bg_opa(list, LV_OPA_90, 0);
        lv_obj_set_style_border_width(list, 1, 0);
        lv_obj_set_style_border_color(list, lv_color_white(), 0);
        lv_obj_set_style_border_opa(list, LV_OPA_30, 0);
        lv_obj_set_style_radius(list, 12, 0);
        lv_obj_set_style_text_color(list, lv_color_white(), 0);
        lv_obj_set_style_bg_color(list, COLOR_ACCENT, LV_PART_SELECTED | LV_STATE_CHECKED);
        lv_obj_set_style_text_color(list, COLOR_TEXT_ON_ACCENT, LV_PART_SELECTED | LV_STATE_CHECKED);
    }
}

void glass_style_slider(lv_obj_t *slider)
{
    lv_obj_set_style_bg_color(slider, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(slider, LV_OPA_30, LV_PART_MAIN);
    lv_obj_set_style_bg_color(slider, COLOR_ACCENT, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(slider, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(slider, lv_color_white(), LV_PART_KNOB);
    lv_obj_set_style_bg_opa(slider, LV_OPA_COVER, LV_PART_KNOB);
    lv_obj_set_style_shadow_width(slider, 10, LV_PART_KNOB);
    lv_obj_set_style_shadow_opa(slider, LV_OPA_40, LV_PART_KNOB);
    lv_obj_set_style_shadow_color(slider, lv_color_black(), LV_PART_KNOB);
    lv_obj_set_style_bg_opa(slider, LV_OPA_10, LV_PART_MAIN | LV_STATE_DISABLED);
    lv_obj_set_style_bg_opa(slider, LV_OPA_30, LV_PART_INDICATOR | LV_STATE_DISABLED);
}

void glass_style_textarea(lv_obj_t *ta)
{
    lv_obj_set_style_bg_color(ta, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(ta, LV_OPA_20, 0);
    lv_obj_set_style_border_width(ta, 1, 0);
    lv_obj_set_style_border_color(ta, lv_color_white(), 0);
    lv_obj_set_style_border_opa(ta, LV_OPA_30, 0);
    lv_obj_set_style_border_color(ta, COLOR_ACCENT, LV_STATE_FOCUSED);
    lv_obj_set_style_border_opa(ta, LV_OPA_COVER, LV_STATE_FOCUSED);
    lv_obj_set_style_radius(ta, 10, 0);
    lv_obj_set_style_text_color(ta, lv_color_white(), 0);
    lv_obj_set_style_text_color(ta, lv_color_white(), LV_PART_TEXTAREA_PLACEHOLDER);
    lv_obj_set_style_text_opa(ta, LV_OPA_50, LV_PART_TEXTAREA_PLACEHOLDER);
    lv_obj_set_style_bg_color(ta, COLOR_ACCENT, LV_PART_CURSOR | LV_STATE_FOCUSED);
}

/* Typing needs certainty, so the keyboard is an opaque dark slab rather than
 * glass: the wallpaper must not compete with the key legends. */
void glass_style_keyboard(lv_obj_t *kb)
{
    lv_obj_set_style_bg_color(kb, lv_color_hex(0x0E1220), 0);
    lv_obj_set_style_bg_opa(kb, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(kb, 1, 0);
    lv_obj_set_style_border_color(kb, lv_color_white(), 0);
    lv_obj_set_style_border_opa(kb, LV_OPA_20, 0);
    lv_obj_set_style_radius(kb, 18, 0);
    lv_obj_set_style_pad_all(kb, 8, 0);
    lv_obj_set_style_bg_color(kb, lv_color_white(), LV_PART_ITEMS);
    lv_obj_set_style_bg_opa(kb, LV_OPA_20, LV_PART_ITEMS);
    lv_obj_set_style_border_width(kb, 0, LV_PART_ITEMS);
    lv_obj_set_style_radius(kb, 8, LV_PART_ITEMS);
    lv_obj_set_style_text_color(kb, lv_color_white(), LV_PART_ITEMS);
    lv_obj_set_style_bg_color(kb, COLOR_ACCENT, LV_PART_ITEMS | LV_STATE_CHECKED);
    lv_obj_set_style_bg_opa(kb, LV_OPA_COVER, LV_PART_ITEMS | LV_STATE_CHECKED);
    lv_obj_set_style_text_color(kb, COLOR_TEXT_ON_ACCENT, LV_PART_ITEMS | LV_STATE_CHECKED);
    lv_obj_set_style_bg_opa(kb, LV_OPA_50, LV_PART_ITEMS | LV_STATE_PRESSED);
}

void glass_style_checkbox(lv_obj_t *cb)
{
    lv_obj_set_style_text_color(cb, lv_color_white(), 0);
    lv_obj_set_style_bg_color(cb, lv_color_white(), LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(cb, LV_OPA_20, LV_PART_INDICATOR);
    lv_obj_set_style_border_color(cb, lv_color_white(), LV_PART_INDICATOR);
    lv_obj_set_style_border_opa(cb, LV_OPA_50, LV_PART_INDICATOR);
    lv_obj_set_style_radius(cb, 6, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(cb, COLOR_ACCENT, LV_PART_INDICATOR | LV_STATE_CHECKED);
    lv_obj_set_style_bg_opa(cb, LV_OPA_COVER, LV_PART_INDICATOR | LV_STATE_CHECKED);
    lv_obj_set_style_border_opa(cb, LV_OPA_TRANSP, LV_PART_INDICATOR | LV_STATE_CHECKED);
}

/* The switch that replaced the settings checkboxes: an unlit track reads as
 * frosted glass, a lit one as the accent. */
void glass_style_switch(lv_obj_t *sw)
{
    lv_obj_set_style_bg_color(sw, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(sw, LV_OPA_20, LV_PART_MAIN);
    lv_obj_set_style_border_color(sw, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_border_opa(sw, LV_OPA_40, LV_PART_MAIN);
    lv_obj_set_style_border_width(sw, 1, LV_PART_MAIN);
    lv_obj_set_style_bg_color(sw, COLOR_ACCENT, LV_PART_INDICATOR | LV_STATE_CHECKED);
    lv_obj_set_style_bg_opa(sw, LV_OPA_COVER, LV_PART_INDICATOR | LV_STATE_CHECKED);
    lv_obj_set_style_bg_color(sw, lv_color_white(), LV_PART_KNOB);
    lv_obj_set_style_bg_opa(sw, LV_OPA_COVER, LV_PART_KNOB);
}

void glass_style_bar(lv_obj_t *bar)
{
    lv_obj_set_style_bg_color(bar, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(bar, LV_OPA_20, LV_PART_MAIN);
    lv_obj_set_style_bg_color(bar, COLOR_ACCENT, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_radius(bar, 8, 0);
}

/* ---------------- widgets ---------------- */

static const lv_img_dsc_t *widget_icon_img(glass_widget_t id)
{
    switch (id) {
    case GLASS_WIDGET_TEMPERATURE: return &glass_icon_thermo;
    case GLASS_WIDGET_BEST_DIFF:   return &glass_icon_star;
    case GLASS_WIDGET_FAN:         return &glass_icon_fan;
    case GLASS_WIDGET_BLOCK:       return &cube_solid_full;
    case GLASS_WIDGET_MEMPOOL:     return &cubes_solid_full;
    case GLASS_WIDGET_CLOCK:       return &clock_solid_full;
    default:                       return NULL;
    }
}

static const char *widget_icon_symbol(glass_widget_t id)
{
    switch (id) {
    case GLASS_WIDGET_POWER:  return LV_SYMBOL_CHARGE;
    case GLASS_WIDGET_SHARES: return LV_SYMBOL_OK;
    case GLASS_WIDGET_POOL:   return LV_SYMBOL_UPLOAD;
    case GLASS_WIDGET_PRICE:  return "$";
    case GLASS_WIDGET_HALVING: return LV_SYMBOL_CUT;
    case GLASS_WIDGET_ODDS:    return "%";
    default:                  return LV_SYMBOL_DUMMY;
    }
}

/* Caption icon centred in a 24px box at (x, y). Every widget uses the same
 * treatment: a 24px glyph in the icon tint beside its caption. */
static void widget_icon(lv_obj_t *parent, glass_widget_t id, int x, int y)
{
    const lv_img_dsc_t *img = widget_icon_img(id);
    if (img) {
        lv_obj_t *i = glass_icon_img(parent, img);
        int w = (int) img->header.w, h = (int) img->header.h;
        lv_obj_set_pos(i, x + (24 - w) / 2, y + (24 - h) / 2);
    } else {
        lv_obj_t *l = glass_icon_symbol(parent, widget_icon_symbol(id), &lv_font_montserrat_20);
        lv_obj_set_pos(l, x + 2, y + 1);
    }
}

static const char *nz(const char *s, const char *fallback)
{
    return (s && s[0]) ? s : fallback;
}

static void clock_text(char *out, size_t len, char *ampm, size_t ampm_len)
{
    time_t now = time(NULL);
    int hour, minute;
    if (now < 946684800) {
        int32_t up = (int32_t) (esp_timer_get_time() / 1000000);
        hour = (up / 3600) % 24;
        minute = (up / 60) % 60;
    } else {
        struct tm t;
        localtime_r(&now, &t);
        hour = t.tm_hour;
        minute = t.tm_min;
    }
    int h12 = hour % 12;
    if (h12 == 0) h12 = 12;
    snprintf(out, len, "%d:%02d", h12, minute);
    snprintf(ampm, ampm_len, "%s", hour < 12 ? "AM" : "PM");
}

static void set_if_changed(lv_obj_t *label, const char *text)
{
    if (!label || !text) return;
    if (strcmp(lv_label_get_text(label), text) != 0) lv_label_set_text(label, text);
}

/* Sit `small` on the same baseline as `big`, to its right. A label's box ends
 * base_line pixels below its baseline, so the boxes are offset by the
 * difference of the two fonts' descents. */
static void align_baseline(lv_obj_t *small, lv_obj_t *big, int gap)
{
    const lv_font_t *fb = lv_obj_get_style_text_font(big, 0);
    const lv_font_t *fs = lv_obj_get_style_text_font(small, 0);
    lv_obj_update_layout(big);
    lv_obj_align_to(small, big, LV_ALIGN_OUT_RIGHT_BOTTOM, gap, (int) fs->base_line - (int) fb->base_line);
}

/* Push the latest figures into a card. Only labels whose text actually
 * changed are touched, so a steady value costs no redraw of its pane. */
static void card_refresh(card_t *c)
{
    const home_stats_t *st = home_stats();
    char buf[64], buf2[64];

    switch (c->id) {
    case GLASS_WIDGET_HASHRATE: {
        const char *hr = nz(st->hashrate, "");
        if (hr[0]) {
            const char *dot = strchr(hr, '.');
            size_t n = dot ? (size_t) (dot - hr) : strlen(hr);
            if (n >= sizeof(buf)) n = sizeof(buf) - 1;
            memcpy(buf, hr, n);
            buf[n] = 0;
            snprintf(buf2, sizeof(buf2), "%s", dot ? dot : "");
        } else {
            snprintf(buf, sizeof(buf), "--");
            buf2[0] = 0;
        }
        set_if_changed(c->value, buf);
        set_if_changed(c->value2, buf2);
        align_baseline(c->value2, c->value, 4);
        align_baseline(c->unit, buf2[0] ? c->value2 : c->value, 10);
        set_if_changed(c->aux, nz(st->efficiency, "-- J/TH"));
        snprintf(buf, sizeof(buf), "%s  %s", nz(st->hardware->model, "Bitaxe"), nz(st->hardware->chip, ""));
        set_if_changed(c->sub, buf);
        break;
    }
    case GLASS_WIDGET_TEMPERATURE:
        set_if_changed(c->value, nz(st->temperature, "--"));
        break;
    case GLASS_WIDGET_POWER:
        set_if_changed(c->value, nz(st->power, "--"));
        set_if_changed(c->sub, nz(st->efficiency, "-- J/TH"));
        break;
    case GLASS_WIDGET_SHARES: {
        const char *s = nz(st->shares, "");
        if (s[0]) {
            const char *slash = strchr(s, '/');
            if (slash) snprintf(buf, sizeof(buf), "%.*s / %s", (int) (slash - s), s, slash + 1);
            else       snprintf(buf, sizeof(buf), "%s", s);
        } else {
            snprintf(buf, sizeof(buf), "--");
        }
        set_if_changed(c->value, buf);
        break;
    }
    case GLASS_WIDGET_BEST_DIFF:
        set_if_changed(c->value, nz(st->best_diff, "--"));
        break;
    case GLASS_WIDGET_FAN:
        set_if_changed(c->value, nz(st->fan, "--"));
        break;
    case GLASS_WIDGET_POOL:
        /* The pool user is not shown. On a solo pool it is the payout
         * address, which is a long string that tells the owner nothing they
         * do not know and puts their address on a screen that faces a room.
         * The port is the useful half. */
        set_if_changed(c->value, nz(st->pool->url, "--"));
        snprintf(buf, sizeof(buf), "port %s", nz(st->pool->port, "--"));
        set_if_changed(c->sub, buf);
        break;
    case GLASS_WIDGET_BLOCK:
        set_if_changed(c->value, block_get_height_text());
        break;
    case GLASS_WIDGET_PRICE:
        /* Follow the currency chosen in settings rather than assuming USD. */
        snprintf(buf, sizeof(buf), "%s%s", chain_ccy_prefix(chain_get_ccy()),
                 price_get_text());
        set_if_changed(c->value, buf);
        set_if_changed(c->sub, price_get_status());
        break;
    case GLASS_WIDGET_MEMPOOL:
        if (mempool_get_latest(buf, sizeof(buf), buf2, sizeof(buf2))) {
            set_if_changed(c->value, buf);
            set_if_changed(c->sub, buf2);
        } else {
            set_if_changed(c->value, "--");
            set_if_changed(c->sub, "waiting for mempool.space");
        }
        break;
    case GLASS_WIDGET_CLOCK:
        clock_text(buf, sizeof(buf), buf2, sizeof(buf2));
        set_if_changed(c->value, buf);
        set_if_changed(c->sub, buf2);
        break;

    case GLASS_WIDGET_ODDS: {
        /* The same figure the odds screen leads with, from the miner hashrate
         * and the network difficulty, both of which are already to hand. */
        double per_day = 0.0;
        const double ghs = st->hashrate && st->hashrate[0] ? strtod(st->hashrate, NULL) : 0.0;
        if (chain_solo_odds(ghs, NULL, &per_day, NULL) && per_day > 0.0) {
            char compact[24];
            chain_fmt_compact(1.0 / per_day, compact, sizeof(compact));
            snprintf(buf, sizeof(buf), "1 in %s", compact);
            set_if_changed(c->value, buf);
            set_if_changed(c->sub, "chance of a block");
        } else {
            set_if_changed(c->value, "--");
            set_if_changed(c->sub, ghs > 0.0 ? "waiting for network" : "waiting for miner");
        }
        break;
    }

    case GLASS_WIDGET_HALVING: {
        /* Derived from the tip height, so this is exact once chain.c has
         * seen one, and does not drift between fetches. */
        const chain_data_t *d = chain_data();
        if (d->blocks_to_halving > 0) {
            chain_fmt_grouped(d->blocks_to_halving, buf, sizeof(buf));
            set_if_changed(c->value, buf);
            snprintf(buf2, sizeof(buf2), "%.0f days", d->days_to_halving);
            set_if_changed(c->sub, buf2);
        } else {
            set_if_changed(c->value, "--");
            set_if_changed(c->sub, "waiting for network");
        }
        break;
    }
    default:
        break;
    }
}

static const char *widget_caption(glass_widget_t id)
{
    switch (id) {
    case GLASS_WIDGET_HASHRATE:    return "HASHRATE";
    case GLASS_WIDGET_TEMPERATURE: return "ASIC TEMP";
    case GLASS_WIDGET_POWER:       return "POWER";
    case GLASS_WIDGET_SHARES:      return "SHARES  ACCEPTED / REJECTED";
    case GLASS_WIDGET_BEST_DIFF:   return "BEST DIFFICULTY";
    case GLASS_WIDGET_FAN:         return "FAN";
    case GLASS_WIDGET_POOL:        return "POOL";
    case GLASS_WIDGET_BLOCK:       return "BLOCK HEIGHT";
    case GLASS_WIDGET_PRICE: {
        static char cap[24];
        snprintf(cap, sizeof(cap), "BTC / %s", chain_ccy_code(chain_get_ccy()));
        return cap;
    }
    case GLASS_WIDGET_MEMPOOL:     return "MEMPOOL  LATEST BLOCK";
    case GLASS_WIDGET_CLOCK:       return "TIME";
    case GLASS_WIDGET_HALVING:     return "HALVING  BLOCKS LEFT";
    case GLASS_WIDGET_ODDS:        return "SOLO ODDS  PER DAY";
    default:                       return "";
    }
}

static bool widget_has_sub(glass_widget_t id)
{
    /* Power carries efficiency only when the hero is not already showing it. */
    if (id == GLASS_WIDGET_POWER) return !(s_mask & (1u << GLASS_WIDGET_HASHRATE));
    return id == GLASS_WIDGET_POOL || id == GLASS_WIDGET_PRICE ||
           id == GLASS_WIDGET_MEMPOOL || id == GLASS_WIDGET_CLOCK ||
           id == GLASS_WIDGET_HALVING || id == GLASS_WIDGET_ODDS;
}

static void build_hero(card_t *c)
{
    lv_obj_t *card = glass_panel_create(s_grid, CONTENT_W, HERO_H, CARD_RADIUS);
    c->card = card;

    /* The display-off control floats over the top corners. When it is
     * visible the hero yields that corner, so nothing sits under it. */
    display_control_config_t dc;
    display_control_get_config(&dc);
    /* Hidden mode keeps the corner tappable, so the hero yields it either way. */
    bool corner_left  = dc.power_button_corner == DISPLAY_POWER_BUTTON_TOP_LEFT;
    bool corner_right = !corner_left;

    lv_obj_t *cap = glass_caption(card, widget_caption(c->id));
    lv_obj_set_pos(cap, corner_left ? 26 + 60 : 26, 18);

    c->value = glass_label(card, "--", &montserrat_120, LV_OPA_COVER);
    lv_obj_align(c->value, LV_ALIGN_LEFT_MID, 22, 14);

    c->value2 = glass_label(card, "", &lv_font_montserrat_40, LV_OPA_COVER);
    c->unit = glass_label(card, "GH/s", &lv_font_montserrat_26, LV_OPA_COVER);
    lv_obj_set_user_data(c->unit, MARK_ACCENT);

    int aux_x = corner_right ? -26 - 60 : -26;
    c->aux = glass_label(card, "-- J/TH", &lv_font_montserrat_28, LV_OPA_COVER);
    lv_obj_align(c->aux, LV_ALIGN_TOP_RIGHT, aux_x, 18);
    lv_obj_t *aux_cap = glass_caption(card, "EFFICIENCY");
    lv_obj_set_style_text_font(aux_cap, &lv_font_montserrat_12, 0);
    lv_obj_align(aux_cap, LV_ALIGN_TOP_RIGHT, aux_x, 54);

    c->sub = glass_subvalue(card, "");
    lv_obj_align(c->sub, LV_ALIGN_BOTTOM_RIGHT, -26, -18);
}

static void build_twin_card(card_t *c)
{
    lv_obj_t *card = glass_panel_create(s_grid, TWIN_W, TWIN_H, CARD_RADIUS);
    c->card = card;

    widget_icon(card, c->id, 22, 18);
    lv_obj_t *cap = glass_caption(card, widget_caption(c->id));
    lv_obj_set_pos(cap, 56, 22);

    const lv_font_t *vf = (c->id == GLASS_WIDGET_POOL) ? &lv_font_montserrat_22 : &lv_font_montserrat_36;
    c->value = glass_label(card, "--", vf, LV_OPA_COVER);
    lv_obj_set_width(c->value, TWIN_W - 48);
    lv_label_set_long_mode(c->value, LV_LABEL_LONG_DOT);
    lv_obj_align(c->value, LV_ALIGN_BOTTOM_LEFT, 22, widget_has_sub(c->id) ? -38 : -22);

    if (widget_has_sub(c->id)) {
        c->sub = glass_subvalue(card, "");
        lv_obj_set_width(c->sub, TWIN_W - 48);
        lv_label_set_long_mode(c->sub, LV_LABEL_LONG_DOT);
        lv_obj_align(c->sub, LV_ALIGN_BOTTOM_LEFT, 24, -14);
    }
}

static void build_single_card(card_t *c)
{
    lv_obj_t *card = glass_panel_create(s_grid, CONTENT_W, SINGLE_H, CARD_RADIUS);
    c->card = card;

    widget_icon(card, c->id, 26, (SINGLE_H - 24) / 2);
    lv_obj_t *cap = glass_caption(card, widget_caption(c->id));
    lv_obj_align(cap, LV_ALIGN_LEFT_MID, 64, widget_has_sub(c->id) ? -13 : 0);

    if (widget_has_sub(c->id)) {
        c->sub = glass_subvalue(card, "");
        lv_obj_set_width(c->sub, 330);
        lv_label_set_long_mode(c->sub, LV_LABEL_LONG_DOT);
        lv_obj_align(c->sub, LV_ALIGN_LEFT_MID, 64, 13);
    }

    const lv_font_t *vf = (c->id == GLASS_WIDGET_POOL) ? &lv_font_montserrat_22 : &lv_font_montserrat_36;
    c->value = glass_label(card, "--", vf, LV_OPA_COVER);
    lv_obj_set_width(c->value, 340);
    lv_label_set_long_mode(c->value, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_align(c->value, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_align(c->value, LV_ALIGN_RIGHT_MID, -26, 0);
}

static void build_grid(void)
{
    if (s_grid) {
        /* Cards are the only panes inside the grid; drop their frost refs
         * before the delete so the registry never holds a dead pointer. */
        for (int i = 0; i < s_card_count; i++) unregister_frost(s_cards[i].card);
        lv_obj_del(s_grid);
        s_grid = NULL;
    }
    s_card_count = 0;
    memset(s_cards, 0, sizeof(s_cards));

    s_grid = lv_obj_create(s_screen);
    lv_obj_set_size(s_grid, SCREEN_WIDTH, SCREEN_HEIGHT);
    lv_obj_set_pos(s_grid, 0, 0);
    lv_obj_set_style_bg_opa(s_grid, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_grid, 0, 0);
    lv_obj_set_style_radius(s_grid, 0, 0);
    lv_obj_set_style_pad_left(s_grid, EDGE_PAD, 0);
    lv_obj_set_style_pad_right(s_grid, EDGE_PAD, 0);
    lv_obj_set_style_pad_top(s_grid, 22, 0);
    lv_obj_set_style_pad_bottom(s_grid, 22, 0);
    lv_obj_set_style_pad_row(s_grid, GAP, 0);
    lv_obj_set_style_pad_column(s_grid, GAP, 0);
    lv_obj_set_flex_flow(s_grid, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(s_grid, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_scroll_dir(s_grid, LV_DIR_VER);
    /* A thin indicator while scrolling: the only cue of position in a
     * surface that can be three screens tall. */
    lv_obj_set_scrollbar_mode(s_grid, LV_SCROLLBAR_MODE_ACTIVE);
    lv_obj_set_style_bg_color(s_grid, lv_color_white(), LV_PART_SCROLLBAR);
    lv_obj_set_style_bg_opa(s_grid, LV_OPA_50, LV_PART_SCROLLBAR);
    lv_obj_set_style_width(s_grid, 4, LV_PART_SCROLLBAR);
    lv_obj_set_style_radius(s_grid, 2, LV_PART_SCROLLBAR);
    lv_obj_set_style_pad_right(s_grid, 6, LV_PART_SCROLLBAR);
    glass_track_scroll(s_grid);
    glass_attach_drawer_toggle(s_grid);
    /* Keep the drawer above the grid whatever order things were built in. */
    lv_obj_move_background(s_grid);
    if (s_host_wall) lv_obj_move_background(s_host_wall);

    for (int n = 0; n < GLASS_WIDGET_COUNT; n++) {
        int id = s_order[n];
        if (!(s_mask & (1u << id))) continue;
        card_t *c = &s_cards[s_card_count++];
        c->id = (glass_widget_t) id;
        if (id == GLASS_WIDGET_HASHRATE)            build_hero(c);
        else if (s_layout == GLASS_LAYOUT_TWIN)     build_twin_card(c);
        else                                        build_single_card(c);
        card_refresh(c);
    }

    if (s_card_count == 0) {
        lv_obj_t *hint = glass_label(s_grid, "Tap to open the menu and choose widgets", &lv_font_montserrat_18, CAPTION_OPA);
        lv_obj_set_width(hint, CONTENT_W);
        lv_obj_set_style_text_align(hint, LV_TEXT_ALIGN_CENTER, 0);
    }

    if (s_mask & (1u << GLASS_WIDGET_PRICE))   price_ensure_task();
    if (s_mask & (1u << GLASS_WIDGET_MEMPOOL)) mempool_ensure_task();

    glass_screen_ready(s_screen);
}

static void rebuild_grid_async(void *unused)
{
    (void) unused;
    if (!s_screen) return;
    build_grid();
    lv_obj_invalidate(s_screen);
}

static void refresh_cb(lv_timer_t *t)
{
    (void) t;
    for (int i = 0; i < s_card_count; i++) card_refresh(&s_cards[i]);
}

/* ---------------- navigation ---------------- */

typedef struct {
    void      (*create)(void);
    lv_obj_t *(*get)(void);
    void      (*destroy)(void);
} nav_fns_t;

static const nav_fns_t k_nav[GLASS_SCREEN_COUNT] = {
    { home_screen_create,     home_get_screen,     home_screen_destroy     },
    { block_screen_create,    block_get_screen,    block_screen_destroy    },
    { mempool_screen_create,  mempool_get_screen,  mempool_screen_destroy  },
    { clock_screen_create,    clock_get_screen,    clock_screen_destroy    },
    { price_screen_create,    price_get_screen,    price_screen_destroy    },
    { wifi_screen_create,     wifi_get_screen,     wifi_screen_destroy     },
    { settings_screen_create, settings_get_screen, settings_screen_destroy },
    { night_screen_create,    night_get_screen,    night_screen_destroy    },
    { odds_screen_create,     odds_get_screen,     odds_screen_destroy     },
    { payout_screen_create,   payout_get_screen,   payout_screen_destroy   },
};

/* Same order as every classic handler: build the next screen, load it, then
 * tear down the one we came from. Deleting the loaded screen first would leave
 * LVGL's active-screen pointer dangling. */
/* Re-point the host state at a screen that already exists, which is what a
 * create that no-opped skipped. */
static void glass_adopt_host(glass_screen_t kind)
{
    const host_rec_t *rec = &s_hosts[kind];
    if (!rec->scr) return;

    s_host      = rec->scr;
    s_host_kind = kind;
    s_host_dim  = rec->dim;
    s_host_wall = rec->wall;
    display_control_set_power_button_dim(rec->dim);
    display_control_refresh_skin();
}

static void cache_touch(glass_screen_t k)
{
    int at = -1;
    for (int i = 0; i < s_mru_n; i++) if (s_mru[i] == k) { at = i; break; }
    if (at >= 0) {
        for (int i = at; i > 0; i--) s_mru[i] = s_mru[i - 1];
    } else {
        if (s_mru_n < GLASS_SCREEN_COUNT) s_mru_n++;
        for (int i = s_mru_n - 1; i > 0; i--) s_mru[i] = s_mru[i - 1];
    }
    s_mru[0] = k;
}

/* Tear down anything past the cache bound, oldest first, never the one on
 * display. */
static void cache_evict(void)
{
    while (s_mru_n > SCREEN_CACHE_MAX) {
        glass_screen_t victim = s_mru[s_mru_n - 1];
        s_mru_n--;
        if (victim == s_host_kind) continue;
        k_nav[victim].destroy();
        s_hosts[victim] = (host_rec_t){ 0 };
    }
}

void glass_screens_forget(void)
{
    /* Anything cached was built with the old palette, skin or wallpaper, so
     * it cannot simply be shown again. The one on display is left alone; its
     * owner rebuilds it. */
    for (int i = 0; i < GLASS_SCREEN_COUNT; i++) {
        if (i == (int) s_host_kind) continue;
        if (s_hosts[i].scr) {
            k_nav[i].destroy();
            s_hosts[i] = (host_rec_t){ 0 };
        }
    }
    s_mru_n = 0;
    cache_touch(s_host_kind);
}

static void glass_navigate(glass_screen_t target);

void glass_goto(glass_screen_t target) { glass_navigate(target); }

static void glass_navigate(glass_screen_t target)
{
    if (target < 0 || target >= GLASS_SCREEN_COUNT) return;
    if (target == s_host_kind) { glass_drawer_close(); return; }

    /* create() is a no-op when the screen survives from a previous visit, in
     * which case the host state it would have set has to be adopted back. */
    k_nav[target].create();
    glass_adopt_host(target);
    lv_scr_load(k_nav[target].get());

    cache_touch(target);
    cache_evict();
}

/* ---------------- drawer ---------------- */

static void drawer_nav_cb(lv_event_t *e)
{
    glass_navigate((glass_screen_t) (intptr_t) lv_event_get_user_data(e));
}

static void drawer_sheet_cb(lv_event_t *e)
{
    glass_sheet_t sheet = (glass_sheet_t) (intptr_t) lv_event_get_user_data(e);
    glass_drawer_close();
    glass_sheet_open(sheet);
}

static void drawer_scrim_cb(lv_event_t *e)
{
    (void) e;
    glass_drawer_close();
}

static void drawer_toggle_cb(lv_event_t *e)
{
    (void) e;
    if (s_sheet != GLASS_SHEET_NONE) return;
    for (int i = 0; i < s_tap_interceptor_count; i++) {
        if (s_tap_interceptor[i].host == s_host && s_tap_interceptor[i].cb && s_tap_interceptor[i].cb()) return;
    }
    if (s_drawer_open) glass_drawer_close();
    else               glass_drawer_open();
}

void glass_attach_drawer_toggle(lv_obj_t *obj)
{
    lv_obj_add_event_cb(obj, drawer_toggle_cb, LV_EVENT_CLICKED, NULL);
}

void glass_set_tap_interceptor(bool (*cb)(void))
{
    if (!cb || !s_host || s_tap_interceptor_count >= TAP_INTERCEPTOR_MAX) return;
    for (int i = 0; i < s_tap_interceptor_count; i++) {
        if (s_tap_interceptor[i].cb == cb && s_tap_interceptor[i].host == s_host) return;
    }
    s_tap_interceptor[s_tap_interceptor_count].cb = cb;
    s_tap_interceptor[s_tap_interceptor_count].host = s_host;
    s_tap_interceptor_count++;
}

static void drawer_anim_cb(void *obj, int32_t v)
{
    lv_obj_set_y((lv_obj_t *) obj, (lv_coord_t) v);
    frost_sync();
}

static void drawer_closed_cb(lv_anim_t *a)
{
    (void) a;
    s_drawer_anim = false;
    display_control_pop_overlay();
    if (s_drawer_sheet) { unregister_frost(s_drawer_sheet); lv_obj_del(s_drawer_sheet); s_drawer_sheet = NULL; }
    if (s_drawer_scrim) { lv_obj_del(s_drawer_scrim); s_drawer_scrim = NULL; }
    s_drawer_host = NULL;
}

static void drawer_opened_cb(lv_anim_t *a)
{
    (void) a;
    s_drawer_anim = false;
}

static void drawer_slide(lv_obj_t *sheet, int from, int to, lv_anim_ready_cb_t done)
{
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, sheet);
    lv_anim_set_values(&a, from, to);
    lv_anim_set_time(&a, 260);
    lv_anim_set_exec_cb(&a, drawer_anim_cb);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    lv_anim_set_ready_cb(&a, done);
    s_drawer_anim = true;
    lv_anim_start(&a);
}

/* Forget the drawer without touching its objects: the screen they sit on is
 * being deleted and takes them with it. */
static void drawer_reset(void)
{
    if (s_drawer_sheet) {
        lv_anim_del(s_drawer_sheet, drawer_anim_cb);
        display_control_pop_overlay();
    }
    s_drawer_sheet = NULL;
    s_drawer_scrim = NULL;
    s_drawer_host  = NULL;
    s_drawer_open  = false;
    s_drawer_anim  = false;
}

bool glass_drawer_is_open(void) { return s_drawer_open; }

/* Bottom drawer. On a landscape panel that sits on a desk the bottom edge is
 * where a hand naturally rests, it is where the classic nav bar lived so the
 * muscle memory carries over, and a sheet rising from the bottom never covers
 * the hero figure at the top of the surface. There is no separate back
 * control: Home is always the first button, so "back" is the same gesture as
 * everything else, and a permanent back chevron would be exactly the kind of
 * chrome the full-screen surface exists to remove. */
void glass_drawer_open(void)
{
    if (!s_host || s_drawer_open || s_drawer_anim) return;
    s_drawer_open = true;
    s_drawer_host = s_host;
    /* The corner control floats on lv_layer_top, above the sheet; it must not
     * stay live over a modal surface. */
    display_control_push_overlay();

    s_drawer_scrim = lv_obj_create(s_host);
    lv_obj_set_size(s_drawer_scrim, SCREEN_WIDTH, SCREEN_HEIGHT);
    lv_obj_set_pos(s_drawer_scrim, 0, 0);
    lv_obj_set_style_bg_color(s_drawer_scrim, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_drawer_scrim, LV_OPA_30, 0);
    lv_obj_set_style_border_width(s_drawer_scrim, 0, 0);
    lv_obj_set_style_radius(s_drawer_scrim, 0, 0);
    lv_obj_set_style_pad_all(s_drawer_scrim, 0, 0);
    lv_obj_clear_flag(s_drawer_scrim, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(s_drawer_scrim, drawer_scrim_cb, LV_EVENT_CLICKED, NULL);

    const int sheet_w = SCREEN_WIDTH - 2 * DRAWER_MARGIN;
    s_drawer_sheet = glass_panel_create(s_host, sheet_w, DRAWER_H, 30);
    lv_obj_add_flag(s_drawer_sheet, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_pos(s_drawer_sheet, DRAWER_MARGIN, SCREEN_HEIGHT);

    /* iOS grabber, purely a cue that this is a sheet. */
    lv_obj_t *grab = lv_obj_create(s_drawer_sheet);
    lv_obj_set_size(grab, 40, 5);
    lv_obj_align(grab, LV_ALIGN_TOP_MID, 0, 8);
    lv_obj_set_style_radius(grab, 3, 0);
    lv_obj_set_style_bg_color(grab, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(grab, LV_OPA_50, 0);
    lv_obj_set_style_border_width(grab, 0, 0);
    lv_obj_clear_flag(grab, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);

    const glass_screen_t k = s_host_kind;

    /* Tier one: where you can go. Home is always here and always first; it
     * used to give up its slot to Widgets on the home screen, so the first
     * cell meant different things depending on where you already were. */
    lv_obj_t *grid = lv_obj_create(s_drawer_sheet);
    lv_obj_set_size(grid, GRID_W, 2 * CELL_H);
    lv_obj_align(grid, LV_ALIGN_TOP_MID, 0, 24);
    lv_obj_set_style_bg_opa(grid, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(grid, 0, 0);
    lv_obj_set_style_pad_all(grid, 0, 0);
    lv_obj_set_style_pad_row(grid, 0, 0);
    lv_obj_set_style_pad_column(grid, 0, 0);
    lv_obj_clear_flag(grid, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_flex_flow(grid, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(grid, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START);

    glass_grid_cell(grid, LV_SYMBOL_HOME,     NULL, "Home",     drawer_nav_cb, (void *) GLASS_SCREEN_HOME,     k == GLASS_SCREEN_HOME);
    glass_grid_cell(grid, NULL, &cube_solid_full,   "Blocks",   drawer_nav_cb, (void *) GLASS_SCREEN_BLOCK,    k == GLASS_SCREEN_BLOCK);
    glass_grid_cell(grid, NULL, &cubes_solid_full,  "Mempool",  drawer_nav_cb, (void *) GLASS_SCREEN_MEMPOOL,  k == GLASS_SCREEN_MEMPOOL);
    glass_grid_cell(grid, NULL, &clock_solid_full,  "Clock",    drawer_nav_cb, (void *) GLASS_SCREEN_CLOCK,    k == GLASS_SCREEN_CLOCK);
    glass_grid_cell(grid, "$",                NULL, "Price",    drawer_nav_cb, (void *) GLASS_SCREEN_PRICE,    k == GLASS_SCREEN_PRICE);
    glass_grid_cell(grid, "%",                NULL, "Odds",     drawer_nav_cb, (void *) GLASS_SCREEN_ODDS,     k == GLASS_SCREEN_ODDS);
    glass_grid_cell(grid, LV_SYMBOL_DOWNLOAD, NULL, "Payout",   drawer_nav_cb, (void *) GLASS_SCREEN_PAYOUT,   k == GLASS_SCREEN_PAYOUT);
    glass_grid_cell(grid, LV_SYMBOL_WIFI,     NULL, "Wi-Fi",    drawer_nav_cb, (void *) GLASS_SCREEN_WIFI,     k == GLASS_SCREEN_WIFI);
    glass_grid_cell(grid, LV_SYMBOL_SETTINGS, NULL, "Settings", drawer_nav_cb, (void *) GLASS_SCREEN_SETTINGS, k == GLASS_SCREEN_SETTINGS);
    glass_grid_cell(grid, LV_SYMBOL_EYE_OPEN, NULL, "Night",    drawer_nav_cb, (void *) GLASS_SCREEN_NIGHT,    k == GLASS_SCREEN_NIGHT);

    /* Tier two: what you can change here. A rule and a heading, because these
     * open a sheet over the screen you are on rather than taking you away. */
    lv_obj_t *rule = lv_obj_create(s_drawer_sheet);
    lv_obj_set_size(rule, GRID_W, 1);
    lv_obj_align(rule, LV_ALIGN_TOP_MID, 0, 236);
    lv_obj_set_style_bg_color(rule, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(rule, LV_OPA_20, 0);
    lv_obj_set_style_border_width(rule, 0, 0);
    lv_obj_clear_flag(rule, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *heading = glass_label(s_drawer_sheet, "CUSTOMISE", &lv_font_montserrat_12, CAPTION_OPA);
    lv_obj_align(heading, LV_ALIGN_TOP_LEFT, (SCREEN_WIDTH - 2 * DRAWER_MARGIN - GRID_W) / 2, 246);

    lv_obj_t *actions = lv_obj_create(s_drawer_sheet);
    lv_obj_set_size(actions, GRID_W, 44);
    lv_obj_align(actions, LV_ALIGN_TOP_MID, 0, 262);
    lv_obj_set_style_bg_opa(actions, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(actions, 0, 0);
    lv_obj_set_style_pad_all(actions, 0, 0);
    lv_obj_clear_flag(actions, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_flex_flow(actions, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(actions, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    /* Widgets only arranges the home surface, so it is offered only there. */
    if (k == GLASS_SCREEN_HOME) {
        glass_pill_button(actions, LV_SYMBOL_LIST, "Widgets", drawer_sheet_cb, (void *) GLASS_SHEET_WIDGETS);
    }
    glass_pill_button(actions, LV_SYMBOL_IMAGE,  "Style", drawer_sheet_cb, (void *) GLASS_SHEET_WALLPAPER);
    glass_pill_button(actions, LV_SYMBOL_UPLOAD, "Pool",  drawer_sheet_cb, (void *) GLASS_SHEET_POOL);

    lv_obj_update_layout(s_host);
    drawer_slide(s_drawer_sheet, SCREEN_HEIGHT, SCREEN_HEIGHT - DRAWER_H - DRAWER_MARGIN, drawer_opened_cb);
}

void glass_drawer_close(void)
{
    if (!s_drawer_open || !s_drawer_sheet) return;
    s_drawer_open = false;
    if (s_drawer_anim) {
        lv_anim_del(s_drawer_sheet, drawer_anim_cb);
        s_drawer_anim = false;
    }
    if (s_drawer_scrim) { lv_obj_del(s_drawer_scrim); s_drawer_scrim = NULL; }
    drawer_slide(s_drawer_sheet, lv_obj_get_y(s_drawer_sheet), SCREEN_HEIGHT, drawer_closed_cb);
}

/* ---------------- sheets ---------------- */

static void sheet_close_cb(lv_event_t *e)
{
    (void) e;
    glass_sheet_close();
}

static void widget_switch_cb(lv_event_t *e)
{
    lv_obj_t *sw = lv_event_get_target(e);
    int id = (int) (intptr_t) lv_event_get_user_data(e);
    uint32_t mask = s_mask;
    if (lv_obj_has_state(sw, LV_STATE_CHECKED)) mask |= (1u << id);
    else                                        mask &= ~(1u << id);
    glass_set_widget_mask(mask);
}

static void layout_pick_cb(lv_event_t *e)
{
    glass_layout_t l = (glass_layout_t) (intptr_t) lv_event_get_user_data(e);
    glass_set_layout(l);
    glass_sheet_close();
}

static void wallpaper_pick_cb(lv_event_t *e)
{
    int idx = (int) (intptr_t) lv_event_get_user_data(e);
    glass_set_wallpaper(idx);
    glass_sheet_close();
}

static void icon_pick_cb(lv_event_t *e)
{
    int idx = (int) (intptr_t) lv_event_get_user_data(e);
    if (idx < 0 || idx >= ICON_CHOICE_COUNT) return;
    theme_set_icon_override(k_icon_choices[idx].rgb);
    glass_sheet_close();
    material_reapply_all();
    glass_sheet_open(GLASS_SHEET_WALLPAPER);
}

static lv_obj_t *sheet_frame(int w, int h, const char *title)
{
    s_sheet_host = s_host;
    display_control_push_overlay();
    s_sheet_scrim = lv_obj_create(s_host);
    lv_obj_set_size(s_sheet_scrim, SCREEN_WIDTH, SCREEN_HEIGHT);
    lv_obj_set_pos(s_sheet_scrim, 0, 0);
    lv_obj_set_style_bg_color(s_sheet_scrim, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_sheet_scrim, LV_OPA_40, 0);
    lv_obj_set_style_border_width(s_sheet_scrim, 0, 0);
    lv_obj_set_style_radius(s_sheet_scrim, 0, 0);
    lv_obj_set_style_pad_all(s_sheet_scrim, 0, 0);
    lv_obj_clear_flag(s_sheet_scrim, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(s_sheet_scrim, sheet_close_cb, LV_EVENT_CLICKED, NULL);

    s_sheet_panel = glass_panel_create(s_host, w, h, 28);
    lv_obj_add_flag(s_sheet_panel, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_align(s_sheet_panel, LV_ALIGN_CENTER, 0, 0);

    lv_obj_t *t = glass_label(s_sheet_panel, title, &lv_font_montserrat_22, LV_OPA_COVER);
    lv_obj_set_pos(t, 28, 22);

    lv_obj_t *close = lv_obj_create(s_sheet_panel);
    lv_obj_set_size(close, 36, 36);
    lv_obj_align(close, LV_ALIGN_TOP_RIGHT, -20, 16);
    lv_obj_set_style_radius(close, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(close, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(close, LV_OPA_20, 0);
    lv_obj_set_style_border_width(close, 0, 0);
    lv_obj_set_style_pad_all(close, 0, 0);
    lv_obj_clear_flag(close, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(close, sheet_close_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *x = glass_label(close, LV_SYMBOL_CLOSE, &lv_font_montserrat_16, LV_OPA_90);
    lv_obj_center(x);

    return s_sheet_panel;
}

static void reopen_widgets_sheet_async(void *unused)
{
    (void) unused;
    if (s_sheet == GLASS_SHEET_WIDGETS) {
        glass_sheet_close();
        glass_sheet_open(GLASS_SHEET_WIDGETS);
    }
}

/* Move a widget one place up or down in the display order. */
static void widget_move_cb(lv_event_t *e)
{
    int packed = (int) (intptr_t) lv_event_get_user_data(e);
    int pos = packed >> 1, dir = (packed & 1) ? 1 : -1;
    int other = pos + dir;
    if (pos < 0 || pos >= GLASS_WIDGET_COUNT || other < 0 || other >= GLASS_WIDGET_COUNT) return;
    uint8_t t = s_order[pos];
    s_order[pos] = s_order[other];
    s_order[other] = t;
    prefs_save();
    if (s_screen) lv_async_call(rebuild_grid_async, NULL);
    lv_async_call(reopen_widgets_sheet_async, NULL);
}

static void layout_seg_cb(lv_event_t *e)
{
    glass_layout_t l = (glass_layout_t) (intptr_t) lv_event_get_user_data(e);
    if (l == s_layout) return;
    glass_set_layout(l);
    lv_async_call(reopen_widgets_sheet_async, NULL);
}

static lv_obj_t *small_round_button(lv_obj_t *parent, const char *symbol, int x, int y, lv_event_cb_t cb, void *ud, bool enabled)
{
    lv_obj_t *b = lv_obj_create(parent);
    lv_obj_set_size(b, 30, 30);
    lv_obj_set_pos(b, x, y);
    lv_obj_set_style_radius(b, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(b, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(b, enabled ? LV_OPA_20 : LV_OPA_10, 0);
    lv_obj_set_style_border_width(b, 0, 0);
    lv_obj_set_style_pad_all(b, 0, 0);
    lv_obj_clear_flag(b, LV_OBJ_FLAG_SCROLLABLE);
    if (enabled) lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, ud);
    else         lv_obj_clear_flag(b, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_t *l = glass_label(b, symbol, &lv_font_montserrat_12, enabled ? LV_OPA_90 : LV_OPA_40);
    lv_obj_center(l);
    return b;
}

static void build_widgets_sheet(void)
{
    lv_obj_t *p = sheet_frame(640, 420, "Widgets");

    /* Layout lives with the widgets it arranges: a two-way segmented control. */
    for (int i = 0; i < 2; i++) {
        bool on = (s_layout == (glass_layout_t) i);
        lv_obj_t *seg = lv_obj_create(p);
        lv_obj_set_size(seg, 96, 34);
        lv_obj_set_pos(seg, 370 + i * 100, 18);
        lv_obj_set_style_radius(seg, 17, 0);
        lv_obj_set_style_bg_color(seg, on ? COLOR_ACCENT : lv_color_white(), 0);
        lv_obj_set_style_bg_opa(seg, on ? LV_OPA_COVER : LV_OPA_20, 0);
        lv_obj_set_style_border_width(seg, 0, 0);
        lv_obj_set_style_pad_all(seg, 0, 0);
        lv_obj_clear_flag(seg, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_event_cb(seg, layout_seg_cb, LV_EVENT_CLICKED, (void *) (intptr_t) i);
        lv_obj_t *l = glass_label(seg, i == 0 ? "Single" : "Twin", &lv_font_montserrat_14, LV_OPA_COVER);
        if (on) { lv_obj_set_style_text_color(l, COLOR_TEXT_ON_ACCENT, 0); lv_obj_set_user_data(l, (void *) 3); }
        lv_obj_center(l);
    }

    /* Two columns in display order: name, move up/down, toggle.
     *
     * The rows live in their own scroller rather than being placed straight on
     * the sheet, which was a fixed two-by-six and so silently capped the widget
     * list at twelve: a thirteenth landed in a third column past the edge. */
    lv_obj_t *list = lv_obj_create(p);
    lv_obj_set_size(list, 600, 336);
    lv_obj_set_pos(list, 20, 64);
    lv_obj_set_style_bg_opa(list, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(list, 0, 0);
    lv_obj_set_style_pad_all(list, 0, 0);
    lv_obj_set_scroll_dir(list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_set_style_bg_color(list, lv_color_white(), LV_PART_SCROLLBAR);
    lv_obj_set_style_bg_opa(list, LV_OPA_40, LV_PART_SCROLLBAR);
    lv_obj_set_style_width(list, 4, LV_PART_SCROLLBAR);

    const int per_col = (GLASS_WIDGET_COUNT + 1) / 2;

    for (int pos = 0; pos < GLASS_WIDGET_COUNT; pos++) {
        int id = s_order[pos];
        int col = pos / per_col, row = pos % per_col;
        int x = 8 + col * 306, y = row * 54;

        lv_obj_t *name = glass_label(list, k_widget_names[id], &lv_font_montserrat_16, LV_OPA_COVER);
        lv_obj_set_width(name, 140);
        lv_label_set_long_mode(name, LV_LABEL_LONG_DOT);
        lv_obj_set_pos(name, x, y + 7);

        small_round_button(list, LV_SYMBOL_UP,   x + 146, y, widget_move_cb, (void *) (intptr_t) ((pos << 1) | 0), pos > 0);
        small_round_button(list, LV_SYMBOL_DOWN, x + 180, y, widget_move_cb, (void *) (intptr_t) ((pos << 1) | 1), pos < GLASS_WIDGET_COUNT - 1);

        lv_obj_t *sw = lv_switch_create(list);
        lv_obj_set_size(sw, 54, 30);
        lv_obj_set_pos(sw, x + 222, y);
        lv_obj_set_style_bg_color(sw, lv_color_white(), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(sw, LV_OPA_30, LV_PART_MAIN);
        lv_obj_set_style_bg_color(sw, COLOR_ACCENT, LV_PART_INDICATOR | LV_STATE_CHECKED);
        lv_obj_set_style_bg_opa(sw, LV_OPA_COVER, LV_PART_INDICATOR | LV_STATE_CHECKED);
        lv_obj_set_style_bg_color(sw, lv_color_white(), LV_PART_KNOB);
        lv_obj_set_style_bg_opa(sw, LV_OPA_COVER, LV_PART_KNOB);
        lv_obj_set_style_pad_all(sw, -3, LV_PART_KNOB);
        lv_obj_set_style_border_width(sw, 0, LV_PART_MAIN);
        lv_obj_set_style_outline_width(sw, 0, LV_PART_MAIN | LV_STATE_FOCUS_KEY);
        if (s_mask & (1u << id)) lv_obj_add_state(sw, LV_STATE_CHECKED);
        lv_obj_add_event_cb(sw, widget_switch_cb, LV_EVENT_VALUE_CHANGED, (void *) (intptr_t) id);
    }
}

/* A little pictogram of the layout: rounded bars in the arrangement the
 * option produces, so the choice reads without words. */
static lv_obj_t *layout_tile(lv_obj_t *parent, glass_layout_t which, int x)
{
    bool selected = (s_layout == which);
    lv_obj_t *tile = lv_obj_create(parent);
    lv_obj_set_size(tile, 250, 220);
    lv_obj_set_pos(tile, x, 74);
    lv_obj_set_style_radius(tile, 20, 0);
    lv_obj_set_style_bg_color(tile, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(tile, selected ? LV_OPA_30 : LV_OPA_10, 0);
    lv_obj_set_style_border_width(tile, 2, 0);
    lv_obj_set_style_border_color(tile, selected ? COLOR_ACCENT : lv_color_white(), 0);
    lv_obj_set_style_border_opa(tile, selected ? LV_OPA_COVER : LV_OPA_20, 0);
    lv_obj_set_style_pad_all(tile, 0, 0);
    lv_obj_clear_flag(tile, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(tile, layout_pick_cb, LV_EVENT_CLICKED, (void *) (intptr_t) which);

    const int px = 30, py = 26, pw = 190;
    int rows = (which == GLASS_LAYOUT_SINGLE) ? 3 : 2;
    int bar_h = (which == GLASS_LAYOUT_SINGLE) ? 22 : 40;
    int y = py;
    lv_obj_t *hero = lv_obj_create(tile);
    lv_obj_set_size(hero, pw, 44);
    lv_obj_set_pos(hero, px, y);
    lv_obj_set_style_radius(hero, 8, 0);
    lv_obj_set_style_bg_color(hero, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(hero, LV_OPA_50, 0);
    lv_obj_set_style_border_width(hero, 0, 0);
    lv_obj_clear_flag(hero, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    y += 44 + 8;
    for (int r = 0; r < rows; r++) {
        int cols = (which == GLASS_LAYOUT_SINGLE) ? 1 : 2;
        for (int c = 0; c < cols; c++) {
            int w = (cols == 1) ? pw : (pw - 8) / 2;
            lv_obj_t *bar = lv_obj_create(tile);
            lv_obj_set_size(bar, w, bar_h);
            lv_obj_set_pos(bar, px + c * (w + 8), y);
            lv_obj_set_style_radius(bar, 8, 0);
            lv_obj_set_style_bg_color(bar, lv_color_white(), 0);
            lv_obj_set_style_bg_opa(bar, LV_OPA_40, 0);
            lv_obj_set_style_border_width(bar, 0, 0);
            lv_obj_clear_flag(bar, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
        }
        y += bar_h + 8;
    }

    lv_obj_t *cap = glass_label(tile, which == GLASS_LAYOUT_SINGLE ? "Single column" : "Twin column",
                                &lv_font_montserrat_16, LV_OPA_COVER);
    lv_obj_align(cap, LV_ALIGN_BOTTOM_MID, 0, -14);
    if (selected) {
        lv_obj_t *tick = glass_label(tile, LV_SYMBOL_OK, &lv_font_montserrat_16, LV_OPA_COVER);
        lv_obj_set_user_data(tick, MARK_ACCENT);
        lv_obj_align(tick, LV_ALIGN_TOP_RIGHT, -12, 10);
    }
    return tile;
}

static void build_layout_sheet(void)
{
    lv_obj_t *p = sheet_frame(620, 330, "Layout");
    layout_tile(p, GLASS_LAYOUT_SINGLE, 40);
    layout_tile(p, GLASS_LAYOUT_TWIN, 330);
}

static void build_wallpaper_sheet(void)
{
    int n = wallpaper_count();
    if (n > 3) n = 3;
    lv_obj_t *p = sheet_frame(640, 420, "Style");

    for (int i = 0; i < n; i++) {
        s_thumb_buf[i] = malloc((size_t) THUMB_W * THUMB_H * 2);
        bool selected = (s_wall == i);
        int x = 32 + i * (THUMB_W + 20);

        lv_obj_t *tile = lv_obj_create(p);
        lv_obj_set_size(tile, THUMB_W + 12, THUMB_H + 12);
        lv_obj_set_pos(tile, x - 6, 70);
        lv_obj_set_style_radius(tile, 18, 0);
        lv_obj_set_style_bg_opa(tile, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(tile, 3, 0);
        lv_obj_set_style_border_color(tile, selected ? COLOR_ACCENT : lv_color_white(), 0);
        lv_obj_set_style_border_opa(tile, selected ? LV_OPA_COVER : LV_OPA_30, 0);
        lv_obj_set_style_pad_all(tile, 0, 0);
        lv_obj_set_style_clip_corner(tile, true, 0);
        lv_obj_clear_flag(tile, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_event_cb(tile, wallpaper_pick_cb, LV_EVENT_CLICKED, (void *) (intptr_t) i);

        if (s_thumb_buf[i]) {
            wallpaper_render_thumb(i, &s_thumb_dsc[i], s_thumb_buf[i], THUMB_W, THUMB_H);
            lv_obj_t *img = lv_img_create(tile);
            lv_img_set_src(img, &s_thumb_dsc[i]);
            lv_obj_center(img);
            lv_obj_clear_flag(img, LV_OBJ_FLAG_CLICKABLE);
        }

        lv_obj_t *cap = glass_label(p, wallpaper_name(i), &lv_font_montserrat_16, LV_OPA_COVER);
        lv_obj_set_width(cap, THUMB_W);
        lv_obj_set_style_text_align(cap, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_pos(cap, x, 70 + THUMB_H + 24);
        if (selected) lv_obj_set_user_data(cap, MARK_ACCENT);
    }

    /* Icon tint below the wallpapers: the two choices that make a Glass
     * surface, in one place. Each swatch shows the colour as drawn. */
    lv_obj_t *icon_cap = glass_caption(p, "ICON COLOUR");
    lv_obj_set_pos(icon_cap, 30, 236);
    uint32_t current = theme_get_icon_override();
    for (int i = 0; i < ICON_CHOICE_COUNT; i++) {
        bool selected = (k_icon_choices[i].rgb == current);
        int x = 36 + i * 82;

        lv_obj_t *ring = lv_obj_create(p);
        lv_obj_set_size(ring, 56, 56);
        lv_obj_set_pos(ring, x, 262);
        lv_obj_set_style_radius(ring, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(ring, lv_color_white(), 0);
        lv_obj_set_style_bg_opa(ring, LV_OPA_10, 0);
        lv_obj_set_style_border_width(ring, 3, 0);
        lv_obj_set_style_border_color(ring, lv_color_white(), 0);
        lv_obj_set_style_border_opa(ring, selected ? LV_OPA_COVER : LV_OPA_20, 0);
        lv_obj_set_style_pad_all(ring, 0, 0);
        lv_obj_clear_flag(ring, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_event_cb(ring, icon_pick_cb, LV_EVENT_CLICKED, (void *) (intptr_t) i);

        lv_color_t base = k_icon_choices[i].rgb ? lv_color_hex(k_icon_choices[i].rgb) : theme_color(THEME_ACCENT);
        material_t dark_m = { .substrate = 0.10f, .tint_opa = 0 };
        lv_obj_t *dot = lv_obj_create(ring);
        lv_obj_set_size(dot, 34, 34);
        lv_obj_center(dot);
        lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(dot, vibrant(base, &dark_m), 0);
        lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(dot, 0, 0);
        lv_obj_clear_flag(dot, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t *cap = glass_label(p, k_icon_choices[i].name, &lv_font_montserrat_12, CAPTION_OPA);
        lv_obj_set_width(cap, 56);
        lv_obj_set_style_text_align(cap, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_pos(cap, x, 326);
    }
    lv_obj_t *hint = glass_caption(p, "Accent follows the colour theme; icons are lifted for contrast on every pane.");
    lv_obj_set_width(hint, 580);
    lv_obj_set_pos(hint, 30, 366);
}

static void build_pool_sheet(void)
{
    const home_stats_t *st = home_stats();
    lv_obj_t *p = sheet_frame(660, 340, "Pool");

    char buf[160];
    const char *labels[3] = { "URL", "Port", "Worker" };
    const char *values[3] = { st->pool->url, st->pool->port, st->pool->worker_name };
    for (int i = 0; i < 3; i++) {
        lv_obj_t *k = glass_caption(p, labels[i]);
        lv_obj_set_pos(k, 30, 78 + i * 62);
        snprintf(buf, sizeof(buf), "%s", nz(values[i], "--"));
        lv_obj_t *v = glass_label(p, buf, &lv_font_montserrat_20, LV_OPA_COVER);
        lv_obj_set_width(v, 360);
        lv_label_set_long_mode(v, LV_LABEL_LONG_DOT);
        lv_obj_set_pos(v, 30, 98 + i * 62);
    }

    const char *ip = wifi_get_current_ip();
    bool ip_ok = ip && ip[0] && strcmp(ip, "0.0.0.0") != 0;
    lv_obj_t *qr_bg = lv_obj_create(p);
    lv_obj_set_size(qr_bg, 176, 176);
    lv_obj_align(qr_bg, LV_ALIGN_TOP_RIGHT, -30, 70);
    lv_obj_set_style_radius(qr_bg, 16, 0);
    lv_obj_set_style_bg_color(qr_bg, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(qr_bg, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(qr_bg, 0, 0);
    lv_obj_set_style_pad_all(qr_bg, 0, 0);
    lv_obj_clear_flag(qr_bg, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    if (ip_ok) {
        char url[96];
        snprintf(url, sizeof(url), "http://%s/#/pool", ip);
        lv_obj_t *qr = lv_qrcode_create(qr_bg, 150, lv_color_black(), lv_color_white());
        lv_qrcode_update(qr, url, strlen(url));
        lv_obj_center(qr);
    } else {
        /* Black on the white QR box, deliberately outside the material walk. */
        lv_obj_t *l = lv_label_create(qr_bg);
        lv_label_set_text(l, "No IP yet");
        lv_obj_set_style_text_font(l, &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_color(l, lv_color_hex(0x000000), 0);
        lv_obj_center(l);
    }
    lv_obj_t *hint = glass_caption(p, ip_ok ? "Scan to open AxeOS" : "Connect Wi-Fi for a setup QR");
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_12, 0);
    lv_obj_set_width(hint, 200);
    lv_obj_set_style_text_align(hint, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(hint, LV_ALIGN_TOP_RIGHT, -18, 254);
    if (ip_ok) {
        lv_obj_t *ipl = glass_label(p, ip, &lv_font_montserrat_16, LV_OPA_COVER);
        lv_obj_set_user_data(ipl, MARK_ACCENT);
        lv_obj_set_width(ipl, 200);
        lv_obj_set_style_text_align(ipl, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_align(ipl, LV_ALIGN_TOP_RIGHT, -18, 276);
    }
}

void glass_sheet_open(glass_sheet_t sheet)
{
    if (!s_host) return;
    if (s_sheet != GLASS_SHEET_NONE) glass_sheet_close();
    if (s_drawer_open) glass_drawer_close();
    if (sheet == GLASS_SHEET_NONE) return;
    s_sheet = sheet;

    switch (sheet) {
    case GLASS_SHEET_WIDGETS:   build_widgets_sheet();   break;
    case GLASS_SHEET_LAYOUT:    build_layout_sheet();    break;
    case GLASS_SHEET_WALLPAPER: build_wallpaper_sheet(); break;
    case GLASS_SHEET_POOL:      build_pool_sheet();      break;
    case GLASS_SHEET_ICONS:     build_wallpaper_sheet(); break;   /* same sheet: Style */
    default: break;
    }
    glass_screen_ready(s_host);
}

static void thumbs_free(void)
{
    for (int i = 0; i < 3; i++) {
        if (s_thumb_buf[i]) { free(s_thumb_buf[i]); s_thumb_buf[i] = NULL; }
    }
}

static void sheet_reset(void)
{
    if (s_sheet != GLASS_SHEET_NONE) display_control_pop_overlay();
    s_sheet = GLASS_SHEET_NONE;
    s_sheet_panel = NULL;
    s_sheet_scrim = NULL;
    s_sheet_host  = NULL;
    thumbs_free();
}

void glass_sheet_close(void)
{
    if (s_sheet == GLASS_SHEET_NONE) return;
    if (s_sheet_panel) { unregister_frost(s_sheet_panel); lv_obj_del(s_sheet_panel); }
    if (s_sheet_scrim) lv_obj_del(s_sheet_scrim);
    sheet_reset();
}

/* ---------------- screens ---------------- */

lv_obj_t *glass_screen_create(glass_screen_t kind, bool dim)
{
    prefs_load();
    /* Rendering runs on a worker without the LVGL lock; until it lands the
     * panes are flat, then the screen is rebuilt with crops. */
    wallpaper_prepare_async(s_wall, wallpaper_ready_cb);
    s_host_wall_index = wallpaper_current();

    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr, lv_color_hex(dim ? 0x000000 : 0x070B1F), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(scr, LV_SCROLLBAR_MODE_OFF);
    lv_obj_add_event_cb(scr, drawer_toggle_cb, LV_EVENT_CLICKED, NULL);

    s_host      = scr;
    s_host_kind = kind;
    s_host_dim  = dim;
    s_host_wall = NULL;

    s_hosts[kind] = (host_rec_t){ .scr = scr, .wall = NULL, .dim = dim };
    display_control_set_power_button_dim(dim);
    display_control_refresh_skin();

    const lv_img_dsc_t *sharp = wallpaper_image(WALLPAPER_SHARP);
    if (sharp) {
        s_host_wall = lv_img_create(scr);
        lv_img_set_src(s_host_wall, sharp);
        s_hosts[kind].wall = s_host_wall;
        lv_obj_set_pos(s_host_wall, 0, 0);
        /* Night: the wallpaper is structure, not light. */
        if (dim) lv_obj_set_style_img_opa(s_host_wall, LV_OPA_20, 0);
        lv_obj_clear_flag(s_host_wall, LV_OBJ_FLAG_CLICKABLE);
    }

    /* Grabber: the one persistent cue that the surface answers a tap. It is
     * not a hit target itself, so the tap reaches the screen like any other. */
    lv_obj_t *grab = lv_obj_create(scr);
    lv_obj_set_size(grab, 56, 6);
    lv_obj_align(grab, LV_ALIGN_BOTTOM_MID, 0, -6);
    lv_obj_set_style_radius(grab, 3, 0);
    lv_obj_set_style_bg_color(grab, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(grab, dim ? LV_OPA_20 : LV_OPA_50, 0);
    lv_obj_set_style_border_width(grab, 0, 0);
    lv_obj_set_style_pad_all(grab, 0, 0);
    lv_obj_clear_flag(grab, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    return scr;
}

void glass_rebuild_host(void)
{
    if (!s_host || s_host_kind < 0 || s_host_kind >= GLASS_SCREEN_COUNT) return;
    if (lv_scr_act() != s_host) return;
    glass_screen_t kind = s_host_kind;
    lv_obj_t *scratch = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scratch, lv_color_black(), 0);
    lv_scr_load(scratch);
    k_nav[kind].destroy();
    k_nav[kind].create();
    lv_scr_load(k_nav[kind].get());
    lv_obj_del(scratch);
}

void glass_screen_detach(lv_obj_t *scr)
{
    /* Forget the cached host state for whichever screen this is, so a later
     * visit rebuilds instead of adopting a freed object. */
    for (int i = 0; i < GLASS_SCREEN_COUNT; i++) {
        if (s_hosts[i].scr == scr) s_hosts[i] = (host_rec_t){ 0 };
    }

    if (!scr) return;
    if (s_drawer_host == scr) drawer_reset();
    if (s_sheet_host == scr)  sheet_reset();
    frost_drop_host(scr);
    interceptors_drop_host(scr);
    if (s_host == scr) {
        s_host = NULL;
        s_host_wall = NULL;
        s_host_dim = false;
        display_control_set_power_button_dim(false);
    }
    /* Leaving Glass for Classic: the 1.5MB of wallpaper is no longer wanted. */
    if (!glass_active()) wallpaper_release();
}

void glass_home_create(void)
{
    if (s_screen) return;
    s_screen = glass_screen_create(GLASS_SCREEN_HOME, false);
    build_grid();
    s_refresh = lv_timer_create(refresh_cb, 1000, NULL);
    ESP_LOGI(TAG, "free internal heap after glass home: %u",
             (unsigned) heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
    ESP_LOGI(TAG, "glass home: layout=%d widgets=0x%03x wallpaper=%s",
             (int) s_layout, (unsigned) s_mask, wallpaper_name(s_wall));
}

void glass_home_destroy(void)
{
    if (!s_screen) return;
    if (s_refresh) { lv_timer_del(s_refresh); s_refresh = NULL; }
    glass_screen_detach(s_screen);
    lv_obj_del(s_screen);
    s_screen = NULL;
    s_grid = NULL;
    s_card_count = 0;
    /* The wallpaper buffers stay allocated: the next glass screen would
     * otherwise re-render both variants, and PSRAM has the room. */
}

lv_obj_t *glass_home_get_screen(void) { return s_screen; }
bool      glass_home_is_active(void)  { return s_screen != NULL; }

void glass_scroll_to(int y)
{
    if (!s_grid) return;
    lv_obj_scroll_to_y(s_grid, (lv_coord_t) y, LV_ANIM_OFF);
    frost_sync();
}
