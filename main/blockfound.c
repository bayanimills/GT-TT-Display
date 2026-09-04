#include "blockfound.h"

#include "chain.h"
#include "home.h"
#include "block.h"
#include "glass.h"
#include "theme.h"
#include "custom_fonts.h"

#include "esp_log.h"
#include "nvs.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *TAG = "blockfound";

#define BLOCKFOUND_NVS_NAMESPACE "gtdisplay"
#define BLOCKFOUND_NVS_BEST      "bf_best"
#define BLOCKFOUND_NVS_COUNT     "bf_count"

static lv_obj_t *bf_screen = NULL;
static lv_obj_t *bf_return_to = NULL;
static double    bf_announced = 0.0;
static bool      bf_prefs_loaded = false;
static int32_t   bf_seen_count = -1;

static char bf_height[24] = "";
static char bf_difficulty[24] = "";

/* "3.65M" -> 3650000. The miner sends difficulty with a magnitude suffix, and
 * comparing it against the network target needs a real number. An unknown
 * suffix returns 0, which every caller treats as "do not act on this". */
static double parse_suffixed(const char *s)
{
    if (!s || !s[0])
    {
        return 0.0;
    }

    char *end = NULL;
    double v = strtod(s, &end);
    if (end == s || !isfinite(v) || v <= 0.0)
    {
        return 0.0;
    }

    while (*end == ' ')
    {
        end++;
    }

    switch (*end)
    {
    case 0:   return v;
    case 'k':
    case 'K': return v * 1e3;
    case 'M': return v * 1e6;
    case 'G': return v * 1e9;
    case 'T': return v * 1e12;
    case 'P': return v * 1e15;
    case 'E': return v * 1e18;
    default:  return 0.0;
    }
}

static void bf_load_prefs(void)
{
    if (bf_prefs_loaded)
    {
        return;
    }
    bf_prefs_loaded = true;

    nvs_handle_t h;
    if (nvs_open(BLOCKFOUND_NVS_NAMESPACE, NVS_READONLY, &h) != ESP_OK)
    {
        return;
    }
    /* Stored as a string: the value spans far more than an int32 can hold. */
    char buf[32];
    size_t len = sizeof(buf);
    if (nvs_get_str(h, BLOCKFOUND_NVS_BEST, buf, &len) == ESP_OK)
    {
        bf_announced = strtod(buf, NULL);
    }
    int32_t c = 0;
    if (nvs_get_i32(h, BLOCKFOUND_NVS_COUNT, &c) == ESP_OK) {
        bf_seen_count = c;
    }
    nvs_close(h);
}

static void bf_save_count(int32_t c)
{
    nvs_handle_t h;
    if (nvs_open(BLOCKFOUND_NVS_NAMESPACE, NVS_READWRITE, &h) != ESP_OK) {
        return;
    }
    nvs_set_i32(h, BLOCKFOUND_NVS_COUNT, c);
    nvs_commit(h);
    nvs_close(h);
}

void blockfound_report_count(const char *count)
{
    if (!count || !count[0]) {
        return;
    }
    bf_load_prefs();

    const int32_t now = (int32_t) strtol(count, NULL, 10);
    if (now < 0) {
        return;
    }

    /* First sighting establishes the baseline. A display added to a miner
     * that has already solved something must not announce that on plug-in. */
    if (bf_seen_count < 0) {
        bf_seen_count = now;
        bf_save_count(now);
        ESP_LOGI(TAG, "miner reports %d solved; baseline set", (int) now);
        return;
    }

    if (now <= bf_seen_count) {
        return;
    }

    ESP_LOGW(TAG, "miner reports a block: %d -> %d", (int) bf_seen_count, (int) now);
    bf_seen_count = now;
    bf_save_count(now);
    blockfound_trigger(block_get_height_text(), NULL);
}

static void bf_save_announced(double v)
{
    nvs_handle_t h;
    if (nvs_open(BLOCKFOUND_NVS_NAMESPACE, NVS_READWRITE, &h) != ESP_OK)
    {
        return;
    }
    char buf[32];
    snprintf(buf, sizeof(buf), "%.0f", v);
    nvs_set_str(h, BLOCKFOUND_NVS_BEST, buf);
    nvs_commit(h);
    nvs_close(h);
}

bool blockfound_is_showing(void)
{
    return bf_screen != NULL;
}

void blockfound_check(void)
{
    bf_load_prefs();

    const home_stats_t *st = home_stats();
    const chain_data_t *d = chain_data();

    if (!st || !d->valid || d->difficulty <= 0.0)
    {
        return;
    }

    const double best = parse_suffixed(st->best_diff);
    if (best <= 0.0)
    {
        return;
    }

    /* A share at or above the network target is a block. */
    if (best < d->difficulty)
    {
        return;
    }
    /* Already announced this one. */
    if (best <= bf_announced)
    {
        return;
    }

    ESP_LOGW(TAG, "block solved: best share %.4g >= network %.4g", best, d->difficulty);
    bf_announced = best;
    bf_save_announced(best);
    blockfound_trigger(block_get_height_text(), st->best_diff);
}

static void bf_dismiss_cb(lv_event_t *e)
{
    (void)e;
    /* Put back whatever was on screen when this interrupted, so dismissing it
     * does not also navigate somewhere. */
    lv_obj_t *back = bf_return_to;
    if (back && lv_obj_is_valid(back))
    {
        lv_scr_load(back);
    }
    else
    {
        home_screen_create();
        lv_scr_load(home_get_screen());
    }
    blockfound_screen_destroy();
}

void blockfound_screen_create(void)
{
    if (bf_screen != NULL)
    {
        return;
    }

    const bool glass = glass_active();

    /* Deliberately not a glass screen: this one interrupts, so it does not
     * borrow the drawer or the wallpaper the rest of the skin shares. */
    bf_screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(bf_screen, COLOR_ACCENT, 0);
    lv_obj_set_style_bg_opa(bf_screen, LV_OPA_COVER, 0);
    lv_obj_clear_flag(bf_screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(bf_screen, LV_SCROLLBAR_MODE_OFF);
    lv_obj_add_flag(bf_screen, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(bf_screen, bf_dismiss_cb, LV_EVENT_CLICKED, NULL);

    /* Everything sits on the accent, so the ink is whichever of black or
     * white actually reads on it, for all nine palettes and custom ones. */
    const lv_color_t ink = theme_ink_on(COLOR_ACCENT);

    lv_obj_t *kicker = lv_label_create(bf_screen);
    lv_label_set_text(kicker, "BLOCK FOUND");
    lv_obj_set_style_text_color(kicker, ink, 0);
    lv_obj_set_style_text_font(kicker, &lv_font_montserrat_48, 0);
    lv_obj_align(kicker, LV_ALIGN_TOP_MID, 0, 44);

    lv_obj_t *height = lv_label_create(bf_screen);
    lv_label_set_text(height, bf_height[0] ? bf_height : "--");
    lv_obj_set_style_text_color(height, ink, 0);
    lv_obj_set_style_text_font(height, &montserrat_120, 0);
    lv_obj_set_style_text_letter_space(height, 8, 0);
    lv_obj_align(height, LV_ALIGN_CENTER, 0, 6);

    lv_obj_t *detail = lv_label_create(bf_screen);
    if (bf_difficulty[0])
    {
        lv_label_set_text_fmt(detail, "solved at difficulty %s", bf_difficulty);
    }
    else
    {
        lv_label_set_text(detail, "solved by this miner");
    }
    lv_obj_set_style_text_color(detail, ink, 0);
    lv_obj_set_style_text_opa(detail, (lv_opa_t)210, 0);
    lv_obj_set_style_text_font(detail, &lv_font_montserrat_24, 0);
    lv_obj_align(detail, LV_ALIGN_BOTTOM_MID, 0, -76);

    lv_obj_t *hint = lv_label_create(bf_screen);
    lv_label_set_text(hint, "touch to dismiss");
    lv_obj_set_style_text_color(hint, ink, 0);
    lv_obj_set_style_text_opa(hint, (lv_opa_t)150, 0);
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_16, 0);
    lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -32);

    (void)glass;
}

void blockfound_screen_destroy(void)
{
    if (!bf_screen)
    {
        return;
    }
    lv_obj_del(bf_screen);
    bf_screen = NULL;
    bf_return_to = NULL;
}

lv_obj_t *blockfound_get_screen(void)
{
    return bf_screen;
}

void blockfound_trigger(const char *height, const char *difficulty)
{
    if (bf_screen)
    {
        return;
    }

    const char *h = (height && height[0]) ? height : block_get_height_text();
    const home_stats_t *st = home_stats();
    const char *d = (difficulty && difficulty[0]) ? difficulty
                                                  : (st ? st->best_diff : NULL);

    snprintf(bf_height, sizeof(bf_height), "%s", h ? h : "");
    snprintf(bf_difficulty, sizeof(bf_difficulty), "%s", d ? d : "");

    /* Remember where to go back to before taking over the display. */
    bf_return_to = lv_scr_act();

    blockfound_screen_create();
    lv_scr_load(bf_screen);
}
