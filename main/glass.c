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
#include "wifi.h"
#include "custom_fonts.h"
#include "assets/temperature_icon.h"
#include "assets/fan.h"
#include "assets/star.h"
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

/* Surface geometry. The whole 800x480 is the canvas; there is no chrome. */
#define EDGE_PAD      24
#define GAP           16
#define CONTENT_W     (SCREEN_WIDTH - 2 * EDGE_PAD)          /* 752 */
#define TWIN_W        ((CONTENT_W - GAP) / 2)                /* 368 */
#define HERO_H        168
#define TWIN_H        116
#define SINGLE_H      88
#define CARD_RADIUS   24

#define DRAWER_H      124
#define DRAWER_MARGIN 14

/* Translucent white is the whole material. Opacities are tuned against the
 * frost crop underneath: enough to read as a pane, not so much that it goes
 * flat grey and loses the wallpaper. */
#define TINT_OPA       LV_OPA_20
#define BORDER_OPA     LV_OPA_30
#define SPECULAR_OPA   LV_OPA_60
#define TEXT_DIM_OPA   LV_OPA_60

#define DEFAULT_MASK ((1u << GLASS_WIDGET_HASHRATE) | (1u << GLASS_WIDGET_TEMPERATURE) | \
                      (1u << GLASS_WIDGET_POWER)    | (1u << GLASS_WIDGET_SHARES) | \
                      (1u << GLASS_WIDGET_BEST_DIFF)| (1u << GLASS_WIDGET_FAN) | \
                      (1u << GLASS_WIDGET_BLOCK))

typedef struct {
    glass_widget_t id;
    lv_obj_t *card;
    lv_obj_t *value;      /* main figure */
    lv_obj_t *value2;     /* hero only: fractional part */
    lv_obj_t *unit;       /* hero only */
    lv_obj_t *sub;        /* secondary line */
    lv_obj_t *aux;        /* hero only: right-hand figure */
} card_t;

/* Every glass pane keeps a frost crop of the wallpaper as its backdrop, and the
 * crop has to track the pane's on-screen position (scrolling, the drawer
 * sliding in), so panes register here and glass_frost_sync() re-aims them. */
typedef struct {
    lv_obj_t *panel;
    lv_obj_t *frost;
} frost_ref_t;

static lv_obj_t *s_screen       = NULL;
static lv_obj_t *s_wall_img     = NULL;
static lv_obj_t *s_grid         = NULL;
static lv_obj_t *s_drawer_scrim = NULL;
static lv_obj_t *s_drawer_sheet = NULL;
static lv_obj_t *s_sheet_scrim  = NULL;
static lv_obj_t *s_sheet_panel  = NULL;
static lv_timer_t *s_refresh    = NULL;

static card_t      s_cards[GLASS_WIDGET_COUNT];
static int         s_card_count = 0;
static frost_ref_t s_frost[GLASS_WIDGET_COUNT + 8];
static int         s_frost_count = 0;

static glass_sheet_t s_sheet       = GLASS_SHEET_NONE;
static bool          s_drawer_open = false;
static bool          s_drawer_anim = false;

static uint32_t       s_mask   = DEFAULT_MASK;
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
    "Pool", "Block height", "BTC price", "Mempool", "Clock",
};

static void build_grid(void);
static void rebuild_grid_async(void *unused);
static void frost_sync(void);
static void drawer_toggle_cb(lv_event_t *e);

/* ---------------- preferences ---------------- */

static void prefs_load(void)
{
    if (s_prefs_loaded) return;
    s_prefs_loaded = true;

    nvs_handle_t h;
    if (nvs_open(GLASS_NVS_NS, NVS_READONLY, &h) != ESP_OK) return;
    int32_t v;
    if (nvs_get_i32(h, GLASS_NVS_WIDGETS, &v) == ESP_OK) s_mask = (uint32_t) v;
    if (nvs_get_i32(h, GLASS_NVS_LAYOUT, &v) == ESP_OK && v >= 0 && v < GLASS_LAYOUT_COUNT) s_layout = (glass_layout_t) v;
    if (nvs_get_i32(h, GLASS_NVS_WALL, &v) == ESP_OK && v >= 0 && v < wallpaper_count()) s_wall = (int) v;
    nvs_close(h);
}

static void prefs_save(void)
{
    nvs_handle_t h;
    if (nvs_open(GLASS_NVS_NS, NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_i32(h, GLASS_NVS_WIDGETS, (int32_t) s_mask);
    nvs_set_i32(h, GLASS_NVS_LAYOUT, (int32_t) s_layout);
    nvs_set_i32(h, GLASS_NVS_WALL, (int32_t) s_wall);
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

void glass_set_widget_mask(uint32_t mask)
{
    prefs_load();
    s_mask = mask & ((1u << GLASS_WIDGET_COUNT) - 1);
    prefs_save();
    if (s_screen) lv_async_call(rebuild_grid_async, NULL);
}

void glass_set_layout(glass_layout_t layout)
{
    prefs_load();
    if (layout < 0 || layout >= GLASS_LAYOUT_COUNT) return;
    s_layout = layout;
    prefs_save();
    if (s_screen) lv_async_call(rebuild_grid_async, NULL);
}

static void wallpaper_swap_async(void *unused)
{
    (void) unused;
    if (!s_screen) return;
    if (!wallpaper_prepare(s_wall)) return;
    /* Same descriptor pointers, new pixels: the cache is disabled in lv_conf
     * so an invalidate is all it takes for every crop to pick them up. */
    if (s_wall_img) {
        lv_img_set_src(s_wall_img, wallpaper_image(WALLPAPER_SHARP));
        lv_obj_invalidate(s_wall_img);
    }
    for (int i = 0; i < s_frost_count; i++) {
        if (s_frost[i].frost) lv_obj_invalidate(s_frost[i].frost);
    }
    lv_obj_invalidate(s_screen);
}

void glass_set_wallpaper(int index)
{
    prefs_load();
    if (index < 0 || index >= wallpaper_count()) return;
    s_wall = index;
    prefs_save();
    if (s_screen) lv_async_call(wallpaper_swap_async, NULL);
}

/* ---------------- the glass material ---------------- */

static void register_frost(lv_obj_t *panel, lv_obj_t *frost)
{
    if (s_frost_count >= (int) (sizeof(s_frost) / sizeof(s_frost[0]))) return;
    s_frost[s_frost_count].panel = panel;
    s_frost[s_frost_count].frost = frost;
    s_frost_count++;
}

static void unregister_frost(lv_obj_t *panel)
{
    for (int i = 0; i < s_frost_count; i++) {
        if (s_frost[i].panel != panel) continue;
        s_frost[i] = s_frost[s_frost_count - 1];
        s_frost_count--;
        return;
    }
}

/* Aim each pane's frost crop at the pane's own screen rectangle, so what shows
 * through the pane is the (softened) wallpaper directly behind it. */
static void frost_sync(void)
{
    for (int i = 0; i < s_frost_count; i++) {
        if (!s_frost[i].frost) continue;
        lv_area_t a;
        lv_obj_get_coords(s_frost[i].panel, &a);
        lv_img_set_offset_x(s_frost[i].frost, (lv_coord_t) -a.x1);
        lv_img_set_offset_y(s_frost[i].frost, (lv_coord_t) -a.y1);
    }
}

static lv_obj_t *glass_panel_create(lv_obj_t *parent, int w, int h, int radius)
{
    lv_obj_t *panel = lv_obj_create(parent);
    lv_obj_set_size(panel, w, h);
    lv_obj_set_style_radius(panel, radius, 0);
    lv_obj_set_style_pad_all(panel, 0, 0);
    lv_obj_set_style_bg_color(panel, lv_color_hex(0x1A1F2E), 0);
    lv_obj_set_style_bg_opa(panel, LV_OPA_70, 0);
    lv_obj_set_style_border_width(panel, 1, 0);
    lv_obj_set_style_border_color(panel, lv_color_white(), 0);
    lv_obj_set_style_border_opa(panel, BORDER_OPA, 0);
    lv_obj_set_style_shadow_width(panel, 36, 0);
    lv_obj_set_style_shadow_ofs_y(panel, 12, 0);
    lv_obj_set_style_shadow_color(panel, lv_color_black(), 0);
    lv_obj_set_style_shadow_opa(panel, LV_OPA_40, 0);
    lv_obj_set_style_clip_corner(panel, true, 0);
    lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
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
    }
    register_frost(panel, frost);

    /* Tint: the light film that makes it a pane rather than a window. */
    lv_obj_t *tint = lv_obj_create(panel);
    lv_obj_set_size(tint, w, h);
    lv_obj_set_pos(tint, 0, 0);
    lv_obj_set_style_radius(tint, radius, 0);
    lv_obj_set_style_bg_color(tint, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(tint, TINT_OPA, 0);
    lv_obj_set_style_border_width(tint, 0, 0);
    lv_obj_set_style_pad_all(tint, 0, 0);
    lv_obj_clear_flag(tint, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);

    /* Specular edge: a 1px brighter line along the top, the cue that light
     * is catching the upper rim of a slab of glass. */
    lv_obj_t *spec = lv_obj_create(panel);
    lv_obj_set_size(spec, w - 2 * (radius / 2), 1);
    lv_obj_set_pos(spec, radius / 2, 1);
    lv_obj_set_style_radius(spec, 0, 0);
    lv_obj_set_style_bg_color(spec, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(spec, SPECULAR_OPA, 0);
    lv_obj_set_style_border_width(spec, 0, 0);
    lv_obj_set_style_pad_all(spec, 0, 0);
    lv_obj_clear_flag(spec, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);

    return panel;
}

static lv_obj_t *glass_label(lv_obj_t *parent, const char *text, const lv_font_t *font, lv_opa_t opa)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_label_set_text(l, text);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, lv_color_white(), 0);
    lv_obj_set_style_text_opa(l, opa, 0);
    lv_obj_clear_flag(l, LV_OBJ_FLAG_CLICKABLE);
    return l;
}

/* Round glass button with an icon (symbol text or image) and a caption. */
static lv_obj_t *glass_round_button(lv_obj_t *parent, const char *symbol, const lv_img_dsc_t *img,
                                    const char *caption, lv_event_cb_t cb, void *user_data, bool active)
{
    lv_obj_t *cont = lv_obj_create(parent);
    lv_obj_set_size(cont, 68, 84);
    lv_obj_set_style_bg_opa(cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(cont, 0, 0);
    lv_obj_set_style_pad_all(cont, 0, 0);
    lv_obj_clear_flag(cont, LV_OBJ_FLAG_SCROLLABLE);
    if (cb) lv_obj_add_event_cb(cont, cb, LV_EVENT_CLICKED, user_data);

    lv_obj_t *btn = lv_obj_create(cont);
    lv_obj_set_size(btn, 54, 54);
    lv_obj_align(btn, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_radius(btn, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(btn, active ? COLOR_ACCENT : lv_color_white(), 0);
    lv_obj_set_style_bg_opa(btn, active ? LV_OPA_90 : LV_OPA_20, 0);
    lv_obj_set_style_border_width(btn, 1, 0);
    lv_obj_set_style_border_color(btn, lv_color_white(), 0);
    lv_obj_set_style_border_opa(btn, LV_OPA_30, 0);
    lv_obj_set_style_pad_all(btn, 0, 0);
    lv_obj_clear_flag(btn, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);

    lv_color_t icon_color = active ? COLOR_TEXT_ON_ACCENT : lv_color_white();
    if (img) {
        lv_obj_t *i = lv_img_create(btn);
        lv_img_set_src(i, img);
        lv_obj_set_style_img_recolor(i, icon_color, 0);
        lv_obj_set_style_img_recolor_opa(i, LV_OPA_COVER, 0);
        lv_obj_center(i);
    } else {
        lv_obj_t *l = glass_label(btn, symbol, &lv_font_montserrat_22, LV_OPA_COVER);
        lv_obj_set_style_text_color(l, icon_color, 0);
        lv_obj_center(l);
    }

    lv_obj_t *cap = glass_label(cont, caption, &lv_font_montserrat_12, LV_OPA_80);
    lv_obj_align(cap, LV_ALIGN_BOTTOM_MID, 0, 0);
    return cont;
}

/* ---------------- widgets ---------------- */

static const lv_img_dsc_t *widget_icon_img(glass_widget_t id)
{
    switch (id) {
    case GLASS_WIDGET_TEMPERATURE: return &temperature_icon;
    case GLASS_WIDGET_BEST_DIFF:   return &star;
    case GLASS_WIDGET_FAN:         return &fan;
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
    default:                  return LV_SYMBOL_DUMMY;
    }
}

/* Caption icon at (x, y) in a 24px box. The star and fan assets are 1-bit
 * alpha bitmaps, which LVGL 8's software renderer cannot scale, so those two
 * are placed at native size as a faint watermark on the card's right instead
 * (see widget_watermark) and get no caption icon. */
static void widget_icon(lv_obj_t *parent, glass_widget_t id, int x, int y)
{
    const lv_img_dsc_t *img = widget_icon_img(id);
    if (img && img->header.cf == LV_IMG_CF_ALPHA_1BIT) return;
    if (img) {
        lv_obj_t *i = lv_img_create(parent);
        lv_img_set_src(i, img);
        lv_obj_set_style_img_recolor(i, COLOR_ACCENT, 0);
        lv_obj_set_style_img_recolor_opa(i, LV_OPA_COVER, 0);
        int native = (int) img->header.w;
        if (native > 26) {
            /* Zoom is 256 = 1:1 and pivots on the centre, so the object keeps
             * its native box and the drawn icon shrinks inside it. */
            lv_img_set_zoom(i, (uint16_t) (256 * 24 / native));
            lv_obj_set_pos(i, x - (native - 24) / 2, y - (native - 24) / 2);
        } else {
            lv_obj_set_pos(i, x + (24 - native) / 2, y + (24 - native) / 2);
        }
        lv_obj_clear_flag(i, LV_OBJ_FLAG_CLICKABLE);
    } else {
        lv_obj_t *l = glass_label(parent, widget_icon_symbol(id), &lv_font_montserrat_20, LV_OPA_COVER);
        lv_obj_set_style_text_color(l, COLOR_ACCENT, 0);
        lv_obj_set_pos(l, x + 2, y + 1);
    }
}

static bool widget_icon_is_caption(glass_widget_t id)
{
    const lv_img_dsc_t *img = widget_icon_img(id);
    return !(img && img->header.cf == LV_IMG_CF_ALPHA_1BIT);
}

static void widget_watermark(lv_obj_t *card, glass_widget_t id, lv_align_t align, int x, int y)
{
    const lv_img_dsc_t *img = widget_icon_img(id);
    if (!img || img->header.cf != LV_IMG_CF_ALPHA_1BIT) return;
    lv_obj_t *i = lv_img_create(card);
    lv_img_set_src(i, img);
    lv_obj_set_style_img_recolor(i, COLOR_ACCENT, 0);
    lv_obj_set_style_img_recolor_opa(i, LV_OPA_COVER, 0);
    lv_obj_set_style_img_opa(i, LV_OPA_50, 0);
    lv_obj_align(i, align, x, y);
    lv_obj_clear_flag(i, LV_OBJ_FLAG_CLICKABLE);
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
        lv_obj_update_layout(c->value);
        lv_obj_align_to(c->value2, c->value, LV_ALIGN_OUT_RIGHT_BOTTOM, 6, -18);
        lv_obj_update_layout(c->value2);
        lv_obj_align_to(c->unit, buf2[0] ? c->value2 : c->value, LV_ALIGN_OUT_RIGHT_BOTTOM, 10, buf2[0] ? 0 : -18);
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
        set_if_changed(c->value, nz(st->pool->url, "--"));
        snprintf(buf, sizeof(buf), "%s  %s", nz(st->pool->port, ""), nz(st->pool->worker_name, ""));
        set_if_changed(c->sub, buf);
        break;
    case GLASS_WIDGET_BLOCK:
        set_if_changed(c->value, block_get_height_text());
        break;
    case GLASS_WIDGET_PRICE:
        snprintf(buf, sizeof(buf), "$%s", price_get_text());
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
    case GLASS_WIDGET_PRICE:       return "BTC / USD";
    case GLASS_WIDGET_MEMPOOL:     return "MEMPOOL  LATEST BLOCK";
    case GLASS_WIDGET_CLOCK:       return "TIME";
    default:                       return "";
    }
}

static bool widget_has_sub(glass_widget_t id)
{
    return id == GLASS_WIDGET_POWER || id == GLASS_WIDGET_POOL || id == GLASS_WIDGET_PRICE ||
           id == GLASS_WIDGET_MEMPOOL || id == GLASS_WIDGET_CLOCK;
}

static void build_hero(card_t *c)
{
    lv_obj_t *card = glass_panel_create(s_grid, CONTENT_W, HERO_H, CARD_RADIUS);
    c->card = card;

    lv_obj_t *cap = glass_label(card, widget_caption(c->id), &lv_font_montserrat_14, TEXT_DIM_OPA);
    lv_obj_set_pos(cap, 26, 18);

    c->value = glass_label(card, "--", &montserrat_120, LV_OPA_COVER);
    lv_obj_align(c->value, LV_ALIGN_LEFT_MID, 22, 14);

    c->value2 = glass_label(card, "", &lv_font_montserrat_40, LV_OPA_90);
    c->unit = glass_label(card, "GH/s", &lv_font_montserrat_26, LV_OPA_COVER);
    lv_obj_set_style_text_color(c->unit, COLOR_ACCENT, 0);

    c->aux = glass_label(card, "-- J/TH", &lv_font_montserrat_28, LV_OPA_90);
    lv_obj_align(c->aux, LV_ALIGN_TOP_RIGHT, -26, 18);
    lv_obj_t *aux_cap = glass_label(card, "EFFICIENCY", &lv_font_montserrat_12, TEXT_DIM_OPA);
    lv_obj_align(aux_cap, LV_ALIGN_TOP_RIGHT, -26, 54);

    c->sub = glass_label(card, "", &lv_font_montserrat_14, TEXT_DIM_OPA);
    lv_obj_align(c->sub, LV_ALIGN_BOTTOM_RIGHT, -26, -18);
}

static void build_twin_card(card_t *c)
{
    lv_obj_t *card = glass_panel_create(s_grid, TWIN_W, TWIN_H, CARD_RADIUS);
    c->card = card;

    widget_icon(card, c->id, 22, 18);
    widget_watermark(card, c->id, LV_ALIGN_RIGHT_MID, -22, 0);
    lv_obj_t *cap = glass_label(card, widget_caption(c->id), &lv_font_montserrat_14, TEXT_DIM_OPA);
    lv_obj_set_pos(cap, widget_icon_is_caption(c->id) ? 56 : 24, 22);

    const lv_font_t *vf = (c->id == GLASS_WIDGET_POOL) ? &lv_font_montserrat_22 : &lv_font_montserrat_36;
    c->value = glass_label(card, "--", vf, LV_OPA_COVER);
    lv_obj_set_width(c->value, TWIN_W - 48);
    lv_label_set_long_mode(c->value, LV_LABEL_LONG_DOT);
    lv_obj_align(c->value, LV_ALIGN_BOTTOM_LEFT, 22, widget_has_sub(c->id) ? -36 : -22);

    if (widget_has_sub(c->id)) {
        c->sub = glass_label(card, "", &lv_font_montserrat_14, TEXT_DIM_OPA);
        lv_obj_set_width(c->sub, TWIN_W - 48);
        lv_label_set_long_mode(c->sub, LV_LABEL_LONG_DOT);
        lv_obj_align(c->sub, LV_ALIGN_BOTTOM_LEFT, 24, -16);
    }
}

static void build_single_card(card_t *c)
{
    lv_obj_t *card = glass_panel_create(s_grid, CONTENT_W, SINGLE_H, CARD_RADIUS);
    c->card = card;

    widget_icon(card, c->id, 26, (SINGLE_H - 24) / 2);
    widget_watermark(card, c->id, LV_ALIGN_RIGHT_MID, -380, 0);
    lv_obj_t *cap = glass_label(card, widget_caption(c->id), &lv_font_montserrat_14, TEXT_DIM_OPA);
    lv_obj_align(cap, LV_ALIGN_LEFT_MID, widget_icon_is_caption(c->id) ? 64 : 28, widget_has_sub(c->id) ? -12 : 0);

    if (widget_has_sub(c->id)) {
        c->sub = glass_label(card, "", &lv_font_montserrat_14, LV_OPA_80);
        lv_obj_set_width(c->sub, 330);
        lv_label_set_long_mode(c->sub, LV_LABEL_LONG_DOT);
        lv_obj_align(c->sub, LV_ALIGN_LEFT_MID, 64, 12);
    }

    const lv_font_t *vf = (c->id == GLASS_WIDGET_POOL) ? &lv_font_montserrat_22 : &lv_font_montserrat_36;
    c->value = glass_label(card, "--", vf, LV_OPA_COVER);
    lv_obj_set_width(c->value, 340);
    lv_label_set_long_mode(c->value, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_align(c->value, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_align(c->value, LV_ALIGN_RIGHT_MID, -26, 0);
}

static void grid_scroll_cb(lv_event_t *e)
{
    (void) e;
    frost_sync();
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
    lv_obj_set_scrollbar_mode(s_grid, LV_SCROLLBAR_MODE_OFF);
    lv_obj_add_event_cb(s_grid, grid_scroll_cb, LV_EVENT_SCROLL, NULL);
    lv_obj_add_event_cb(s_grid, drawer_toggle_cb, LV_EVENT_CLICKED, NULL);
    /* Keep the drawer above the grid whatever order things were built in. */
    lv_obj_move_background(s_grid);
    if (s_wall_img) lv_obj_move_background(s_wall_img);

    for (int id = 0; id < GLASS_WIDGET_COUNT; id++) {
        if (!(s_mask & (1u << id))) continue;
        card_t *c = &s_cards[s_card_count++];
        c->id = (glass_widget_t) id;
        if (id == GLASS_WIDGET_HASHRATE)            build_hero(c);
        else if (s_layout == GLASS_LAYOUT_TWIN)     build_twin_card(c);
        else                                        build_single_card(c);
        card_refresh(c);
    }

    if (s_card_count == 0) {
        lv_obj_t *hint = glass_label(s_grid, "Tap to open the menu and choose widgets", &lv_font_montserrat_18, TEXT_DIM_OPA);
        lv_obj_set_width(hint, CONTENT_W);
        lv_obj_set_style_text_align(hint, LV_TEXT_ALIGN_CENTER, 0);
    }

    if (s_mask & (1u << GLASS_WIDGET_PRICE))   price_ensure_task();
    if (s_mask & (1u << GLASS_WIDGET_MEMPOOL)) mempool_ensure_task();

    lv_obj_update_layout(s_screen);
    frost_sync();
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

/* ---------------- drawer ---------------- */

static void drawer_nav_cb(lv_event_t *e)
{
    lv_event_cb_t target = (lv_event_cb_t) lv_event_get_user_data(e);
    if (target) target(e);
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
    if (s_drawer_open) glass_drawer_close();
    else               glass_drawer_open();
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
    if (s_drawer_sheet) { unregister_frost(s_drawer_sheet); lv_obj_del(s_drawer_sheet); s_drawer_sheet = NULL; }
    if (s_drawer_scrim) { lv_obj_del(s_drawer_scrim); s_drawer_scrim = NULL; }
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

bool glass_drawer_is_open(void) { return s_drawer_open; }

/* Bottom drawer. On a landscape panel that sits on a desk the bottom edge is
 * where a hand naturally rests, it is where the classic nav bar lived so the
 * muscle memory carries over, and a sheet rising from the bottom never covers
 * the hero figure at the top of the surface. */
void glass_drawer_open(void)
{
    if (!s_screen || s_drawer_open || s_drawer_anim) return;
    s_drawer_open = true;

    s_drawer_scrim = lv_obj_create(s_screen);
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
    s_drawer_sheet = glass_panel_create(s_screen, sheet_w, DRAWER_H, 30);
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

    lv_obj_t *row = lv_obj_create(s_drawer_sheet);
    lv_obj_set_size(row, sheet_w - 16, 90);
    lv_obj_align(row, LV_ALIGN_BOTTOM_MID, 0, -10);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_pad_all(row, 0, 0);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    glass_round_button(row, LV_SYMBOL_LIST,     NULL, "Widgets",   drawer_sheet_cb, (void *) GLASS_SHEET_WIDGETS,   false);
    glass_round_button(row, LV_SYMBOL_BARS,     NULL, "Layout",    drawer_sheet_cb, (void *) GLASS_SHEET_LAYOUT,    false);
    glass_round_button(row, LV_SYMBOL_IMAGE,    NULL, "Wallpaper", drawer_sheet_cb, (void *) GLASS_SHEET_WALLPAPER, false);
    glass_round_button(row, NULL, &cube_solid_full,   "Blocks",    drawer_nav_cb, (void *) home_block_clicked,   false);
    glass_round_button(row, NULL, &cubes_solid_full,  "Mempool",   drawer_nav_cb, (void *) home_mempool_clicked, false);
    glass_round_button(row, NULL, &clock_solid_full,  "Clock",     drawer_nav_cb, (void *) home_clock_clicked,   false);
    glass_round_button(row, "$",                NULL, "Price",     drawer_nav_cb, (void *) home_price_clicked,   false);
    glass_round_button(row, LV_SYMBOL_UPLOAD,   NULL, "Pool",      drawer_sheet_cb, (void *) GLASS_SHEET_POOL,      false);
    glass_round_button(row, LV_SYMBOL_WIFI,     NULL, "Wi-Fi",     drawer_nav_cb, (void *) home_wifi_clicked,    false);
    glass_round_button(row, LV_SYMBOL_SETTINGS, NULL, "Settings",  drawer_nav_cb, (void *) home_settings_clicked, false);
    glass_round_button(row, LV_SYMBOL_EYE_OPEN, NULL, "Night",     drawer_nav_cb, (void *) home_night_clicked,   false);

    lv_obj_update_layout(s_screen);
    drawer_slide(s_drawer_sheet, SCREEN_HEIGHT, SCREEN_HEIGHT - DRAWER_H - DRAWER_MARGIN, drawer_opened_cb);
}

void glass_drawer_close(void)
{
    if (!s_screen || !s_drawer_open) return;
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

static lv_obj_t *sheet_frame(int w, int h, const char *title)
{
    s_sheet_scrim = lv_obj_create(s_screen);
    lv_obj_set_size(s_sheet_scrim, SCREEN_WIDTH, SCREEN_HEIGHT);
    lv_obj_set_pos(s_sheet_scrim, 0, 0);
    lv_obj_set_style_bg_color(s_sheet_scrim, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_sheet_scrim, LV_OPA_40, 0);
    lv_obj_set_style_border_width(s_sheet_scrim, 0, 0);
    lv_obj_set_style_radius(s_sheet_scrim, 0, 0);
    lv_obj_set_style_pad_all(s_sheet_scrim, 0, 0);
    lv_obj_clear_flag(s_sheet_scrim, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(s_sheet_scrim, sheet_close_cb, LV_EVENT_CLICKED, NULL);

    s_sheet_panel = glass_panel_create(s_screen, w, h, 28);
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

static void build_widgets_sheet(void)
{
    lv_obj_t *p = sheet_frame(640, 400, "Widgets");

    /* Two columns of toggles, six per column. */
    for (int id = 0; id < GLASS_WIDGET_COUNT; id++) {
        int col = id / 6, row = id % 6;
        int x = 28 + col * 306, y = 70 + row * 52;

        lv_obj_t *name = glass_label(p, k_widget_names[id], &lv_font_montserrat_18, LV_OPA_90);
        lv_obj_set_pos(name, x, y + 6);

        lv_obj_t *sw = lv_switch_create(p);
        lv_obj_set_size(sw, 54, 30);
        lv_obj_set_pos(sw, x + 220, y);
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
    /* hero bar */
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
                                &lv_font_montserrat_16, LV_OPA_90);
    lv_obj_align(cap, LV_ALIGN_BOTTOM_MID, 0, -14);
    if (selected) {
        lv_obj_t *tick = glass_label(tile, LV_SYMBOL_OK, &lv_font_montserrat_16, LV_OPA_COVER);
        lv_obj_set_style_text_color(tick, COLOR_ACCENT, 0);
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
    lv_obj_t *p = sheet_frame(640, 260, "Wallpaper");

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

        lv_obj_t *cap = glass_label(p, wallpaper_name(i), &lv_font_montserrat_16, LV_OPA_90);
        lv_obj_set_width(cap, THUMB_W);
        lv_obj_set_style_text_align(cap, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_pos(cap, x, 70 + THUMB_H + 24);
        if (selected) lv_obj_set_style_text_color(cap, COLOR_ACCENT, 0);
    }
}

static void build_pool_sheet(void)
{
    const home_stats_t *st = home_stats();
    lv_obj_t *p = sheet_frame(660, 340, "Pool");

    char buf[160];
    const char *labels[3] = { "URL", "Port", "Worker" };
    const char *values[3] = { st->pool->url, st->pool->port, st->pool->worker_name };
    for (int i = 0; i < 3; i++) {
        lv_obj_t *k = glass_label(p, labels[i], &lv_font_montserrat_14, TEXT_DIM_OPA);
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
        lv_obj_t *l = glass_label(qr_bg, "No IP yet", &lv_font_montserrat_16, LV_OPA_COVER);
        lv_obj_set_style_text_color(l, lv_color_black(), 0);
        lv_obj_center(l);
    }
    lv_obj_t *hint = glass_label(p, ip_ok ? "Scan to open AxeOS" : "Connect Wi-Fi for a setup QR",
                                 &lv_font_montserrat_12, TEXT_DIM_OPA);
    lv_obj_set_width(hint, 200);
    lv_obj_set_style_text_align(hint, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(hint, LV_ALIGN_TOP_RIGHT, -18, 254);
    if (ip_ok) {
        lv_obj_t *ipl = glass_label(p, ip, &lv_font_montserrat_16, LV_OPA_COVER);
        lv_obj_set_style_text_color(ipl, COLOR_ACCENT, 0);
        lv_obj_set_width(ipl, 200);
        lv_obj_set_style_text_align(ipl, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_align(ipl, LV_ALIGN_TOP_RIGHT, -18, 276);
    }
}

void glass_sheet_open(glass_sheet_t sheet)
{
    if (!s_screen) return;
    if (s_sheet != GLASS_SHEET_NONE) glass_sheet_close();
    if (s_drawer_open) glass_drawer_close();
    if (sheet == GLASS_SHEET_NONE) return;
    s_sheet = sheet;

    switch (sheet) {
    case GLASS_SHEET_WIDGETS:   build_widgets_sheet();   break;
    case GLASS_SHEET_LAYOUT:    build_layout_sheet();    break;
    case GLASS_SHEET_WALLPAPER: build_wallpaper_sheet(); break;
    case GLASS_SHEET_POOL:      build_pool_sheet();      break;
    default: break;
    }
    lv_obj_update_layout(s_screen);
    frost_sync();
}

void glass_sheet_close(void)
{
    if (s_sheet == GLASS_SHEET_NONE) return;
    s_sheet = GLASS_SHEET_NONE;
    if (s_sheet_panel) { unregister_frost(s_sheet_panel); lv_obj_del(s_sheet_panel); s_sheet_panel = NULL; }
    if (s_sheet_scrim) { lv_obj_del(s_sheet_scrim); s_sheet_scrim = NULL; }
    for (int i = 0; i < 3; i++) {
        if (s_thumb_buf[i]) { free(s_thumb_buf[i]); s_thumb_buf[i] = NULL; }
    }
}

/* ---------------- screen ---------------- */

void glass_home_create(void)
{
    if (s_screen) return;
    prefs_load();

    if (!wallpaper_prepare(s_wall)) {
        ESP_LOGW(TAG, "wallpaper alloc failed; glass falls back to flat panes");
    }

    s_screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_screen, lv_color_hex(0x070B1F), 0);
    lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);
    lv_obj_clear_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(s_screen, LV_SCROLLBAR_MODE_OFF);
    lv_obj_add_event_cb(s_screen, drawer_toggle_cb, LV_EVENT_CLICKED, NULL);

    const lv_img_dsc_t *sharp = wallpaper_image(WALLPAPER_SHARP);
    if (sharp) {
        s_wall_img = lv_img_create(s_screen);
        lv_img_set_src(s_wall_img, sharp);
        lv_obj_set_pos(s_wall_img, 0, 0);
        lv_obj_clear_flag(s_wall_img, LV_OBJ_FLAG_CLICKABLE);
    }

    s_frost_count = 0;
    build_grid();

    s_refresh = lv_timer_create(refresh_cb, 1000, NULL);
    ESP_LOGI(TAG, "glass home: layout=%d widgets=0x%03x wallpaper=%s",
             (int) s_layout, (unsigned) s_mask, wallpaper_name(s_wall));
}

void glass_home_destroy(void)
{
    if (!s_screen) return;
    if (s_refresh) { lv_timer_del(s_refresh); s_refresh = NULL; }
    if (s_drawer_sheet) lv_anim_del(s_drawer_sheet, drawer_anim_cb);
    for (int i = 0; i < 3; i++) {
        if (s_thumb_buf[i]) { free(s_thumb_buf[i]); s_thumb_buf[i] = NULL; }
    }
    lv_obj_del(s_screen);
    s_screen = NULL;
    s_wall_img = NULL;
    s_grid = NULL;
    s_drawer_scrim = NULL;
    s_drawer_sheet = NULL;
    s_sheet_scrim = NULL;
    s_sheet_panel = NULL;
    s_sheet = GLASS_SHEET_NONE;
    s_drawer_open = false;
    s_drawer_anim = false;
    s_frost_count = 0;
    s_card_count = 0;
    /* The wallpaper buffers stay allocated: the next visit to home would
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
