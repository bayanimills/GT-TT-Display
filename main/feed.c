#include "feed.h"
#include "feed_web.h"
#include "home.h"
#include "glass.h"
#include "weather.h"
#include "lvgl__lvgl/src/extra/libs/qrcode/lv_qrcode.h"

#include "custom_fonts.h"

#include <math.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#define FEED_CAPACITY 8
#define FEED_MAX_VISIBLE 4

typedef struct {
    feed_kind_t kind;
    char headline[FEED_RSS_TITLE_MAX];
    char detail[96];
    time_t occurred_at;
} feed_item_t;

static lv_obj_t *s_screen = NULL;
static lv_obj_t *s_content = NULL;
static lv_obj_t *s_age_labels[FEED_MAX_VISIBLE] = { NULL };
static int       s_row_item[FEED_MAX_VISIBLE] = { -1, -1, -1, -1 };
static lv_timer_t *s_timer = NULL;
static bool s_show_settings = false;

/* One screen, two things to look at. The activity feed answers "what has the
 * miner been doing"; the weather answers a question the panel is well placed
 * to answer and nothing else here does. They share a settings sheet because
 * they share a screen, and two sheets for one screen is one too many. */
typedef enum { AUX_VIEW_FEED = 0, AUX_VIEW_WEATHER } aux_view_t;
static aux_view_t s_view = AUX_VIEW_FEED;
static lv_obj_t   *s_search_ta = NULL;
static lv_obj_t   *s_keyboard = NULL;
static lv_timer_t *s_search_timer = NULL;
/* What was last searched for, kept so a search that finds nothing can say so
 * and can hand the text back rather than making it all be typed again. */
static char s_last_query[WEATHER_PLACE_MAX] = { 0 };
static uint32_t s_weather_sig = 0;

static feed_item_t s_items[FEED_CAPACITY];
static int s_count = 0;
static feed_config_t s_config = { .visible_rows = 4, .show_age = true };

static void feed_render(void);
static void feed_view_tabs(lv_obj_t *parent);
static void feed_view_tabs_at(lv_obj_t *parent, int x, int y);
static void feed_render_weather(void);
static void feed_render_weather_settings(void);

static void feed_view_cb(lv_event_t *event)
{
    const aux_view_t want = (aux_view_t)(intptr_t) lv_event_get_user_data(event);
    if (want == s_view) return;
    s_view = want;
    if (s_view == AUX_VIEW_WEATHER) weather_init();
    feed_render();
}

static void feed_settings_open_cb(lv_event_t *event)
{
    LV_UNUSED(event);
    s_show_settings = true;
    feed_render();
}

static void feed_settings_back_cb(lv_event_t *event)
{
    LV_UNUSED(event);
    s_show_settings = false;
    feed_render();
}

static lv_obj_t *feed_button_create(lv_obj_t *parent, const char *text,
                                    lv_event_cb_t callback)
{
    lv_obj_t *button = lv_btn_create(parent);
    lv_obj_set_size(button, 104, 48);
    lv_obj_set_style_radius(button, 12, 0);
    lv_obj_set_style_bg_color(button, COLOR_ACCENT, 0);
    lv_obj_set_style_bg_opa(button, LV_OPA_COVER, 0);
    lv_obj_set_style_shadow_width(button, 0, 0);
    lv_obj_add_event_cb(button, callback, LV_EVENT_CLICKED, NULL);

    lv_obj_t *label = lv_label_create(button);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_color(label, lv_color_black(), 0);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_16, 0);
    lv_obj_center(label);
    return button;
}

/* Tabs the render and the settings sheet both use; the settings sheet nests
 * them in a container, so the origin is a parameter. */
static void feed_view_tabs_at(lv_obj_t *parent, int x, int y)
{
    static const char *k_names[] = { "FEED", "WEATHER" };
    for (int i = 0; i < 2; i++) {
        const bool on = (aux_view_t) i == s_view;
        lv_obj_t *tab = lv_btn_create(parent);
        lv_obj_set_size(tab, 132, 44);
        lv_obj_set_pos(tab, x + i * 142, y);
        lv_obj_set_style_radius(tab, 12, 0);
        lv_obj_set_style_shadow_width(tab, 0, 0);
        lv_obj_set_style_bg_color(tab, on ? COLOR_ACCENT : lv_color_white(), 0);
        lv_obj_set_style_bg_opa(tab, on ? LV_OPA_COVER : LV_OPA_10, 0);
        lv_obj_set_style_border_width(tab, on ? 0 : 1, 0);
        lv_obj_set_style_border_color(tab, lv_color_white(), 0);
        lv_obj_set_style_border_opa(tab, LV_OPA_30, 0);
        lv_obj_add_event_cb(tab, feed_view_cb, LV_EVENT_CLICKED, (void *)(intptr_t) i);

        lv_obj_t *label = lv_label_create(tab);
        lv_label_set_text(label, k_names[i]);
        lv_obj_set_style_text_color(label, on ? COLOR_TEXT_ON_ACCENT : COLOR_TEXT_PRIMARY, 0);
        lv_obj_set_style_text_font(label, &lv_font_montserrat_16, 0);
        lv_obj_center(label);
    }
}

static void feed_source_cb(lv_event_t *event)
{
    weather_set_source((weather_source_t)(intptr_t) lv_event_get_user_data(event));
    feed_render();
}

/* A search runs on the weather task, so the sheet polls for its result rather
 * than blocking the UI on a DNS lookup and an HTTPS round trip. */
static void feed_search_poll_cb(lv_timer_t *timer)
{
    LV_UNUSED(timer);
    if (weather_search_busy()) return;
    if (s_search_timer) {
        lv_timer_del(s_search_timer);
        s_search_timer = NULL;
    }
    feed_render();
}

static void feed_search_ready_cb(lv_event_t *event)
{
    lv_obj_t *ta = lv_event_get_target(event);
    const char *text = lv_textarea_get_text(ta);
    if (!text || !text[0]) return;
    snprintf(s_last_query, sizeof(s_last_query), "%s", text);
    weather_init();
    weather_search_begin(text);
    if (!s_search_timer) {
        s_search_timer = lv_timer_create(feed_search_poll_cb, 300, NULL);
    }
    feed_render();
}

static void feed_result_cb(lv_event_t *event)
{
    const int index = (int)(intptr_t) lv_event_get_user_data(event);
    const weather_place_t *results = NULL;
    const int n = weather_search_results(&results);
    if (index < 0 || index >= n || !results) return;
    weather_set_place(&results[index]);
    s_last_query[0] = 0;
    feed_render();
}

static void feed_render_weather_settings(void)
{
    lv_obj_t *title = lv_label_create(s_content);
    lv_label_set_text(title, "WEATHER SETTINGS");
    lv_obj_set_style_text_color(title, COLOR_TEXT_PRIMARY, 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 66);

    lv_obj_t *src_cap = lv_label_create(s_content);
    lv_label_set_text(src_cap, "SOURCE");
    lv_obj_set_style_text_color(src_cap, COLOR_TEXT_SECONDARY, 0);
    lv_obj_set_style_text_font(src_cap, &lv_font_montserrat_14, 0);
    lv_obj_set_pos(src_cap, 30, 106);

    const weather_source_t active = weather_get_source();
    for (int i = 0; i < WEATHER_SRC_COUNT; i++) {
        const bool on = (weather_source_t) i == active;
        lv_obj_t *btn = lv_btn_create(s_content);
        lv_obj_set_size(btn, 170, 48);
        lv_obj_set_pos(btn, 30, 128 + i * 56);
        lv_obj_set_style_radius(btn, 12, 0);
        lv_obj_set_style_shadow_width(btn, 0, 0);
        lv_obj_set_style_bg_color(btn, on ? COLOR_ACCENT : lv_color_white(), 0);
        lv_obj_set_style_bg_opa(btn, on ? LV_OPA_COVER : LV_OPA_10, 0);
        lv_obj_set_style_border_width(btn, on ? 0 : 1, 0);
        lv_obj_set_style_border_color(btn, lv_color_white(), 0);
        lv_obj_set_style_border_opa(btn, LV_OPA_30, 0);
        lv_obj_add_event_cb(btn, feed_source_cb, LV_EVENT_CLICKED, (void *)(intptr_t) i);

        lv_obj_t *label = lv_label_create(btn);
        lv_label_set_text(label, weather_source_name((weather_source_t) i));
        lv_obj_set_style_text_color(label, on ? COLOR_TEXT_ON_ACCENT : COLOR_TEXT_PRIMARY, 0);
        lv_obj_set_style_text_font(label, &lv_font_montserrat_16, 0);
        lv_obj_center(label);
    }

    lv_obj_t *loc_cap = lv_label_create(s_content);
    const weather_place_t *place = weather_get_place();
    if (place->name[0]) {
        lv_label_set_text_fmt(loc_cap, "LOCATION  -  %s", place->name);
    } else {
        lv_label_set_text(loc_cap, "LOCATION  -  none set");
    }
    lv_label_set_long_mode(loc_cap, LV_LABEL_LONG_DOT);
    lv_obj_set_width(loc_cap, 500);
    lv_obj_set_style_text_color(loc_cap, COLOR_TEXT_SECONDARY, 0);
    lv_obj_set_style_text_font(loc_cap, &lv_font_montserrat_14, 0);
    lv_obj_set_pos(loc_cap, 236, 106);

    s_search_ta = lv_textarea_create(s_content);
    lv_obj_set_size(s_search_ta, 500, 46);
    lv_obj_set_pos(s_search_ta, 236, 128);
    lv_textarea_set_one_line(s_search_ta, true);
    lv_textarea_set_placeholder_text(s_search_ta, "Search for a town or city");
    lv_obj_set_style_text_font(s_search_ta, &lv_font_montserrat_16, 0);
    /* Re-seed with the last query. Rendering is what clears the field, and a
     * search that found nothing is exactly when the text is worth keeping. */
    if (s_last_query[0]) lv_textarea_set_text(s_search_ta, s_last_query);
    lv_obj_add_event_cb(s_search_ta, feed_search_ready_cb, LV_EVENT_READY, NULL);

    if (weather_search_busy()) {
        lv_obj_t *busy = lv_label_create(s_content);
        lv_label_set_text(busy, "Searching...");
        lv_obj_set_style_text_color(busy, COLOR_TEXT_SECONDARY, 0);
        lv_obj_set_style_text_font(busy, &lv_font_montserrat_16, 0);
        lv_obj_set_pos(busy, 236, 190);
    } else {
        const weather_place_t *results = NULL;
        const int n = weather_search_results(&results);
        for (int i = 0; i < n; i++) {
            lv_obj_t *row = lv_btn_create(s_content);
            lv_obj_set_size(row, 500, 40);
            lv_obj_set_pos(row, 236, 186 + i * 46);
            lv_obj_set_style_radius(row, 10, 0);
            lv_obj_set_style_shadow_width(row, 0, 0);
            lv_obj_set_style_bg_color(row, lv_color_white(), 0);
            lv_obj_set_style_bg_opa(row, LV_OPA_10, 0);
            lv_obj_set_style_border_width(row, 0, 0);
            lv_obj_add_event_cb(row, feed_result_cb, LV_EVENT_CLICKED, (void *)(intptr_t) i);

            lv_obj_t *label = lv_label_create(row);
            lv_label_set_text(label, results[i].name);
            lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
            lv_obj_set_width(label, 470);
            lv_obj_set_style_text_color(label, COLOR_TEXT_PRIMARY, 0);
            lv_obj_set_style_text_font(label, &lv_font_montserrat_16, 0);
            lv_obj_align(label, LV_ALIGN_LEFT_MID, 8, 0);
        }
        if (n == 0) {
            lv_obj_t *hint = lv_label_create(s_content);
            if (s_last_query[0]) {
                lv_label_set_text_fmt(hint, "No match for \"%s\". Try a larger town.",
                                      s_last_query);
            } else {
                lv_label_set_text(hint, "Type a place name and press the tick.");
            }
            lv_label_set_long_mode(hint, LV_LABEL_LONG_DOT);
            lv_obj_set_width(hint, 500);
            lv_obj_set_style_text_color(hint, COLOR_TEXT_SECONDARY, 0);
            lv_obj_set_style_text_font(hint, &lv_font_montserrat_16, 0);
            lv_obj_set_pos(hint, 236, 190);
        }
    }

    /* Only while there is still typing to do. With results on screen the
     * keyboard is covering the one thing the user now needs to reach. */
    const weather_place_t *shown = NULL;
    if (!weather_search_busy() && weather_search_results(&shown) == 0) {
        s_keyboard = lv_keyboard_create(s_content);
        lv_obj_set_size(s_keyboard, 760, 186);
        lv_obj_align(s_keyboard, LV_ALIGN_BOTTOM_MID, 0, -6);
        lv_keyboard_set_textarea(s_keyboard, s_search_ta);
    }
}

static void feed_render_settings(void)
{
    lv_obj_t *back = feed_button_create(s_content, LV_SYMBOL_LEFT "  Back",
                                        feed_settings_back_cb);
    lv_obj_align(back, LV_ALIGN_TOP_LEFT, 20, 14);

    /* One sheet for both views, tabbed the same way the views are, so Edit
     * always lands on the settings for what you were looking at. */
    lv_obj_t *tabs = lv_obj_create(s_content);
    lv_obj_set_size(tabs, 286, 56);
    lv_obj_align(tabs, LV_ALIGN_TOP_RIGHT, -96, 6);
    lv_obj_set_style_bg_opa(tabs, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(tabs, 0, 0);
    lv_obj_set_style_pad_all(tabs, 0, 0);
    lv_obj_clear_flag(tabs, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    feed_view_tabs_at(tabs, 0, 6);

    if (s_view == AUX_VIEW_WEATHER)
    {
        feed_render_weather_settings();
        if (glass_active()) glass_screen_ready(s_screen);
        return;
    }

    lv_obj_t *title = lv_label_create(s_content);
    lv_label_set_text(title, "FEED SETTINGS");
    lv_obj_set_style_text_color(title, COLOR_TEXT_PRIMARY, 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 24);

    char rss_url[FEED_WEB_URL_MAX + 1];
    feed_web_get_rss_url(rss_url, sizeof(rss_url));
    lv_obj_t *current = lv_label_create(s_content);
    if (rss_url[0]) {
        char label[64];
        feed_web_source_label(rss_url, label, sizeof(label));
        lv_label_set_text_fmt(current, "Feed in use: %s", label);
    } else {
        lv_label_set_text(current, "Feed in use: none saved");
    }
    lv_label_set_long_mode(current, LV_LABEL_LONG_DOT);
    lv_obj_set_width(current, 420);
    lv_obj_set_style_text_color(current, rss_url[0] ? COLOR_ACCENT : COLOR_TEXT_SECONDARY, 0);
    lv_obj_set_style_text_font(current, &lv_font_montserrat_16, 0);
    lv_obj_align(current, LV_ALIGN_TOP_MID, 0, 58);

    char config_url[FEED_WEB_CONFIG_URL_MAX];
    const bool available = feed_web_get_config_url(config_url,
                                                    sizeof(config_url));
    if (available)
    {
        lv_obj_t *qr_bg = lv_obj_create(s_content);
        lv_obj_set_size(qr_bg, 222, 222);
        lv_obj_set_pos(qr_bg, 56, 100);
        lv_obj_set_style_radius(qr_bg, 16, 0);
        lv_obj_set_style_bg_color(qr_bg, lv_color_white(), 0);
        lv_obj_set_style_bg_opa(qr_bg, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(qr_bg, 0, 0);
        lv_obj_set_style_pad_all(qr_bg, 0, 0);
        lv_obj_clear_flag(qr_bg, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t *qr = lv_qrcode_create(qr_bg, 198, lv_color_black(),
                                        lv_color_white());
        lv_qrcode_update(qr, config_url, strlen(config_url));
        lv_obj_center(qr);

        lv_obj_t *heading = lv_label_create(s_content);
        lv_label_set_text(heading, "EDIT RSS FROM YOUR PHONE");
        lv_obj_set_style_text_color(heading, COLOR_TEXT_PRIMARY, 0);
        lv_obj_set_style_text_font(heading, &lv_font_montserrat_20, 0);
        lv_obj_set_pos(heading, 330, 110);

        lv_obj_t *help = lv_label_create(s_content);
        lv_label_set_text(help,
            "1. Join the same Wi-Fi network\n"
            "2. Scan the QR code\n"
            "3. Paste an RSS or Atom feed URL");
        lv_obj_set_style_text_color(help, COLOR_TEXT_SECONDARY, 0);
        lv_obj_set_style_text_font(help, &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_line_space(help, 12, 0);
        lv_obj_set_pos(help, 330, 154);

        /* The token remains in the QR only.  Showing it as selectable text
         * would turn review screenshots into reusable admin credentials. */
        char public_url[FEED_WEB_CONFIG_URL_MAX];
        snprintf(public_url, sizeof(public_url), "%s", config_url);
        char *query = strchr(public_url, '?');
        if (query) *query = '\0';

        lv_obj_t *url = lv_label_create(s_content);
        lv_label_set_text(url, public_url);
        lv_label_set_long_mode(url, LV_LABEL_LONG_DOT);
        lv_obj_set_width(url, 420);
        lv_obj_set_style_text_color(url, COLOR_ACCENT, 0);
        lv_obj_set_style_text_font(url, &lv_font_montserrat_16, 0);
        lv_obj_set_pos(url, 330, 282);
    }
    else
    {
        lv_obj_t *panel = lv_obj_create(s_content);
        lv_obj_set_size(panel, 620, 230);
        lv_obj_align(panel, LV_ALIGN_CENTER, 0, 22);
        lv_obj_set_style_bg_color(panel, COLOR_CARD_BG, 0);
        lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, 0);
        lv_obj_set_style_border_color(panel, COLOR_BORDER, 0);
        lv_obj_set_style_border_width(panel, 1, 0);
        lv_obj_set_style_radius(panel, 18, 0);
        lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t *heading = lv_label_create(panel);
        lv_label_set_text(heading, "CONNECT THIS DISPLAY TO WI-FI");
        lv_obj_set_style_text_color(heading, COLOR_TEXT_PRIMARY, 0);
        lv_obj_set_style_text_font(heading, &lv_font_montserrat_20, 0);
        lv_obj_align(heading, LV_ALIGN_TOP_MID, 0, 38);

        lv_obj_t *help = lv_label_create(panel);
        lv_label_set_text(help,
            "The phone editor appears here after this display\n"
            "receives its own LAN address. Feed updates remain off\n"
            "until an RSS or Atom URL is saved.");
        lv_obj_set_width(help, 550);
        lv_obj_set_style_text_align(help, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_color(help, COLOR_TEXT_SECONDARY, 0);
        lv_obj_set_style_text_font(help, &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_line_space(help, 9, 0);
        lv_obj_align(help, LV_ALIGN_CENTER, 0, 28);
    }

    if (glass_active()) glass_screen_ready(s_screen);
}

static const char *kind_name(feed_kind_t kind)
{
    static const char *const names[FEED_KIND_COUNT] = {
        "MINER", "POOL", "NETWORK", "SYSTEM", "NEWS"
    };
    return kind >= 0 && kind < FEED_KIND_COUNT ? names[kind] : "SYSTEM";
}

static lv_color_t kind_color(feed_kind_t kind)
{
    switch (kind)
    {
        case FEED_KIND_MINER:   return COLOR_ACCENT;
        case FEED_KIND_POOL:    return lv_color_hex(0x33C48D);
        case FEED_KIND_NETWORK: return lv_color_hex(0x36A3FF);
        case FEED_KIND_NEWS:    return lv_color_hex(0xF59E0B);
        default:                return COLOR_TEXT_SECONDARY;
    }
}

static void format_age(time_t when, char *out, size_t n)
{
    time_t now = time(NULL);
    if (when <= 0 || now <= when)
    {
        snprintf(out, n, "NOW");
        return;
    }

    long seconds = (long)(now - when);
    if (seconds < 60)       snprintf(out, n, "NOW");
    else if (seconds < 3600) snprintf(out, n, "%ldM", seconds / 60);
    else if (seconds < 86400) snprintf(out, n, "%ldH", seconds / 3600);
    else                     snprintf(out, n, "%ldD", seconds / 86400);
}

static lv_obj_t *feed_card_create(lv_obj_t *parent, int y, const feed_item_t *item)
{
    const bool glass = glass_active();
    /* These rows are rebuilt when RSS/event data changes.  Do not register
     * them as frosted panes: deleting a registered pane behind glass.c would
     * leave its crop registry pointing at freed LVGL objects. */
    lv_obj_t *card = lv_obj_create(parent);
    lv_obj_set_size(card, 744, 74);
    lv_obj_align(card, LV_ALIGN_TOP_MID, 0, y);
    lv_obj_set_style_bg_color(card, glass ? lv_color_black() : COLOR_CARD_BG, 0);
    lv_obj_set_style_bg_opa(card, glass ? LV_OPA_60 : LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(card, glass ? lv_color_white() : COLOR_BORDER, 0);
    lv_obj_set_style_border_opa(card, glass ? LV_OPA_40 : LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_radius(card, glass ? 18 : 14, 0);
    lv_obj_set_style_shadow_width(card, 0, 0);
    lv_obj_set_style_pad_all(card, 0, 0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *rail = lv_obj_create(card);
    lv_obj_set_size(rail, 4, 50);
    lv_obj_align(rail, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_set_style_bg_color(rail, kind_color(item->kind), 0);
    lv_obj_set_style_bg_opa(rail, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(rail, 0, 0);
    lv_obj_set_style_radius(rail, 2, 0);
    lv_obj_set_style_pad_all(rail, 0, 0);
    lv_obj_clear_flag(rail, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *kind = lv_label_create(card);
    lv_label_set_text(kind, kind_name(item->kind));
    lv_obj_set_style_text_color(kind, kind_color(item->kind), 0);
    lv_obj_set_style_text_font(kind, &lv_font_montserrat_12, 0);
    lv_obj_align(kind, LV_ALIGN_TOP_LEFT, 18, 10);

    lv_obj_t *headline = lv_label_create(card);
    lv_label_set_text(headline, item->headline);
    lv_label_set_long_mode(headline, LV_LABEL_LONG_DOT);
    lv_obj_set_width(headline, 450);
    lv_obj_set_style_text_color(headline, COLOR_TEXT_PRIMARY, 0);
    lv_obj_set_style_text_font(headline, &lv_font_montserrat_18, 0);
    lv_obj_align(headline, LV_ALIGN_TOP_LEFT, 112, 7);

    lv_obj_t *detail = lv_label_create(card);
    lv_label_set_text(detail, item->detail);
    lv_label_set_long_mode(detail, LV_LABEL_LONG_DOT);
    lv_obj_set_width(detail, 690);
    lv_obj_set_style_text_color(detail, COLOR_TEXT_SECONDARY, 0);
    lv_obj_set_style_text_font(detail, &lv_font_montserrat_14, 0);
    lv_obj_align(detail, LV_ALIGN_TOP_LEFT, 18, 40);

    return card;
}

/* Two tabs across the top left, leaving the right for Edit and the corner for
 * the display-off shortcut. */
static void feed_view_tabs(lv_obj_t *parent)
{
    feed_view_tabs_at(parent, 26, 12);
}

static void feed_render_weather(void)
{
    const weather_data_t *w = weather_data();
    const weather_place_t *place = weather_get_place();
    const bool glass = glass_active();

    lv_obj_t *card = lv_obj_create(s_content);
    lv_obj_set_size(card, 748, 316);
    lv_obj_set_pos(card, 26, 68);
    lv_obj_set_style_radius(card, 20, 0);
    lv_obj_set_style_bg_color(card, glass ? lv_color_white() : COLOR_CARD_BG, 0);
    lv_obj_set_style_bg_opa(card, glass ? LV_OPA_10 : LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(card, glass ? lv_color_white() : COLOR_BORDER, 0);
    lv_obj_set_style_border_opa(card, glass ? LV_OPA_30 : LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_shadow_width(card, 0, 0);
    lv_obj_set_style_pad_all(card, 0, 0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

    if (!place->name[0]) {
        lv_obj_t *msg = lv_label_create(card);
        lv_label_set_text(msg, "No location set.\n\nOpen Edit to search for one.");
        lv_obj_set_style_text_color(msg, COLOR_TEXT_SECONDARY, 0);
        lv_obj_set_style_text_font(msg, &lv_font_montserrat_20, 0);
        lv_obj_set_style_text_align(msg, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_center(msg);
        return;
    }

    lv_obj_t *where = lv_label_create(card);
    lv_label_set_text(where, place->name);
    lv_label_set_long_mode(where, LV_LABEL_LONG_DOT);
    lv_obj_set_width(where, 420);
    lv_obj_set_style_text_color(where, COLOR_TEXT_PRIMARY, 0);
    lv_obj_set_style_text_font(where, &lv_font_montserrat_24, 0);
    lv_obj_set_pos(where, 24, 18);

    lv_obj_t *src = lv_label_create(card);
    lv_label_set_text(src, weather_source_name(weather_get_source()));
    lv_obj_set_style_text_color(src, COLOR_TEXT_SECONDARY, 0);
    lv_obj_set_style_text_font(src, &lv_font_montserrat_14, 0);
    lv_obj_align(src, LV_ALIGN_TOP_RIGHT, -24, 24);

    if (!w->valid) {
        lv_obj_t *msg = lv_label_create(card);
        lv_label_set_text(msg, w->error[0] ? w->error : "Fetching...");
        lv_obj_set_style_text_color(msg, COLOR_TEXT_SECONDARY, 0);
        lv_obj_set_style_text_font(msg, &lv_font_montserrat_20, 0);
        lv_obj_center(msg);
        return;
    }

    lv_obj_t *temp = lv_label_create(card);
    lv_label_set_text_fmt(temp, "%d", (int) lroundf(w->temp_c));
    lv_obj_set_style_text_color(temp, COLOR_TEXT_PRIMARY, 0);
    lv_obj_set_style_text_font(temp, &montserrat_120, 0);
    lv_obj_set_pos(temp, 30, 52);

    lv_obj_t *deg = lv_label_create(card);
    lv_label_set_text(deg, "C");
    lv_obj_set_style_text_color(deg, COLOR_ACCENT, 0);
    lv_obj_set_style_text_font(deg, &lv_font_montserrat_40, 0);
    lv_obj_align_to(deg, temp, LV_ALIGN_OUT_RIGHT_TOP, 10, 26);

    lv_obj_t *cond = lv_label_create(card);
    lv_label_set_text(cond, weather_cond_text(w->cond));
    lv_obj_set_style_text_color(cond, COLOR_TEXT_PRIMARY, 0);
    lv_obj_set_style_text_font(cond, &lv_font_montserrat_24, 0);
    lv_obj_set_pos(cond, 34, 190);

    lv_obj_t *detail = lv_label_create(card);
    if (w->humidity_pct >= 0) {
        lv_label_set_text_fmt(detail, "Feels %d C   -   %d%% humidity   -   %d km/h",
                              (int) lroundf(w->feels_c), w->humidity_pct,
                              (int) lroundf(w->wind_kph));
    } else {
        lv_label_set_text_fmt(detail, "Feels %d C   -   %d km/h",
                              (int) lroundf(w->feels_c), (int) lroundf(w->wind_kph));
    }
    lv_obj_set_style_text_color(detail, COLOR_TEXT_SECONDARY, 0);
    lv_obj_set_style_text_font(detail, &lv_font_montserrat_16, 0);
    lv_obj_set_pos(detail, 34, 228);

    /* The compact met.no feed carries one forward window rather than three
     * days, so the strip is however many the source actually gave. */
    for (int i = 0; i < w->day_count && i < WEATHER_FORECAST_DAYS; i++) {
        lv_obj_t *slot = lv_obj_create(card);
        /* Three of these have to fit the 748px card beside the figure, so the
         * pitch is set by the card, not by what looks roomy for one. */
        lv_obj_set_size(slot, 112, 100);
        lv_obj_set_pos(slot, 390 + i * 118, 92);
        lv_obj_set_style_radius(slot, 14, 0);
        lv_obj_set_style_bg_color(slot, glass ? lv_color_white() : COLOR_BACKGROUND, 0);
        lv_obj_set_style_bg_opa(slot, glass ? LV_OPA_10 : LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(slot, 0, 0);
        lv_obj_set_style_pad_all(slot, 0, 0);
        lv_obj_clear_flag(slot, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

        lv_obj_t *when = lv_label_create(slot);
        lv_label_set_text(when, i == 0 ? "TODAY" : (i == 1 ? "TOMORROW" : "IN 2 DAYS"));
        lv_obj_set_style_text_color(when, COLOR_TEXT_SECONDARY, 0);
        lv_obj_set_style_text_font(when, &lv_font_montserrat_12, 0);
        lv_obj_align(when, LV_ALIGN_TOP_MID, 0, 10);

        lv_obj_t *hilo = lv_label_create(slot);
        lv_label_set_text_fmt(hilo, "%d / %d", (int) lroundf(w->days[i].high_c),
                              (int) lroundf(w->days[i].low_c));
        lv_obj_set_style_text_color(hilo, COLOR_TEXT_PRIMARY, 0);
        lv_obj_set_style_text_font(hilo, &lv_font_montserrat_24, 0);
        lv_obj_align(hilo, LV_ALIGN_CENTER, 0, 2);

        lv_obj_t *what = lv_label_create(slot);
        lv_label_set_text(what, weather_cond_text(w->days[i].cond));
        lv_label_set_long_mode(what, LV_LABEL_LONG_DOT);
        lv_obj_set_width(what, 104);
        lv_obj_set_style_text_align(what, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_color(what, COLOR_TEXT_SECONDARY, 0);
        lv_obj_set_style_text_font(what, &lv_font_montserrat_12, 0);
        lv_obj_align(what, LV_ALIGN_BOTTOM_MID, 0, -8);
    }
}

static void feed_render(void)
{
    if (!s_screen || !s_content) return;

    /* lv_obj_clean deletes the sheet's widgets, so drop the references before
     * anything can call back into freed objects. */
    s_search_ta = NULL;
    s_keyboard = NULL;
    lv_obj_clean(s_content);
    memset(s_age_labels, 0, sizeof(s_age_labels));
    for (int i = 0; i < FEED_MAX_VISIBLE; i++) s_row_item[i] = -1;

    if (s_show_settings)
    {
        feed_render_settings();
        return;
    }

    /* The two views are peers, so they get a pair of tabs rather than one of
     * them being a mode you can end up in without noticing. */
    feed_view_tabs(s_content);

    lv_obj_t *edit = feed_button_create(s_content, LV_SYMBOL_SETTINGS "  Edit",
                                        feed_settings_open_cb);
    /* Leave the top-right display-off shortcut unobstructed.  Both controls
     * are deliberately large touch targets, so visual separation alone is
     * not enough: their hitboxes must not overlap. */
    lv_obj_align(edit, LV_ALIGN_TOP_RIGHT, -96, 10);

    if (s_view == AUX_VIEW_WEATHER)
    {
        feed_render_weather();
        if (glass_active()) glass_screen_ready(s_screen);
        return;
    }

    if (s_count == 0)
    {
        feed_item_t empty = {
            .kind = FEED_KIND_SYSTEM,
            .headline = "Waiting for activity",
            .detail = "Miner, pool and network events will appear here."
        };
        feed_card_create(s_content, 76, &empty);
        if (glass_active()) glass_screen_ready(s_screen);
        return;
    }

    int visible = s_count < s_config.visible_rows ? s_count : s_config.visible_rows;
    for (int i = 0; i < visible; i++)
    {
        lv_obj_t *card = feed_card_create(s_content, 76 + i * 82, &s_items[i]);
        if (s_config.show_age)
        {
            char age[12];
            format_age(s_items[i].occurred_at, age, sizeof(age));
            s_age_labels[i] = lv_label_create(card);
            lv_label_set_text(s_age_labels[i], age);
            lv_obj_set_style_text_color(s_age_labels[i], COLOR_TEXT_SECONDARY, 0);
            lv_obj_set_style_text_font(s_age_labels[i], &lv_font_montserrat_12, 0);
            lv_obj_align(s_age_labels[i], LV_ALIGN_TOP_RIGHT, -16, 10);
            s_row_item[i] = i;
        }
    }
    if (glass_active()) glass_screen_ready(s_screen);
}

/* The weather view is built once from a snapshot, so it has to be told when a
 * fetch lands or it sits on "Fetching..." forever. Rebuilding only when the
 * reading actually changes keeps this from being a repaint every tick. */
static uint32_t feed_weather_signature(void)
{
    const weather_data_t *w = weather_data();
    uint32_t sig = w->valid ? 1u : 0u;
    sig = sig * 31u + (uint32_t) lroundf(w->temp_c * 10.0f);
    sig = sig * 31u + (uint32_t) w->cond;
    sig = sig * 31u + (uint32_t) w->day_count;
    for (const char *p = w->error; *p; p++) sig = sig * 31u + (unsigned char) *p;
    return sig;
}

static void feed_timer_cb(lv_timer_t *timer)
{
    LV_UNUSED(timer);
    if (s_view == AUX_VIEW_WEATHER && !s_show_settings) {
        const uint32_t sig = feed_weather_signature();
        if (sig != s_weather_sig) {
            s_weather_sig = sig;
            feed_render();
            return;
        }
    }
    feed_refresh();
}

void feed_screen_create(void)
{
    if (s_screen) return;

    if (glass_active()) {
        s_screen = glass_screen_create(GLASS_SCREEN_FEED, false);
    } else {
        s_screen = lv_obj_create(NULL);
        lv_obj_set_style_bg_color(s_screen, COLOR_BACKGROUND, 0);
        lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);
    }
    lv_obj_clear_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(s_screen, LV_SCROLLBAR_MODE_OFF);
    s_content = lv_obj_create(s_screen);
    lv_obj_set_size(s_content, SCREEN_WIDTH, SCREEN_HEIGHT - 34);
    lv_obj_set_pos(s_content, 0, 0);
    lv_obj_set_style_bg_opa(s_content, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_content, 0, 0);
    lv_obj_set_style_pad_all(s_content, 0, 0);
    lv_obj_clear_flag(s_content, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    feed_render();
    /* 2s rather than 15s: the age labels do not need it, but a weather fetch
     * landing does, and this is the timer that notices. */
    s_timer = lv_timer_create(feed_timer_cb, 2000, NULL);
}

void feed_screen_destroy(void)
{
    if (s_timer)
    {
        lv_timer_del(s_timer);
        s_timer = NULL;
    }
    /* The search poll outlives its sheet otherwise, and would render into a
     * screen that no longer exists. */
    if (s_search_timer)
    {
        lv_timer_del(s_search_timer);
        s_search_timer = NULL;
    }
    if (s_screen)
    {
        glass_screen_detach(s_screen);
        lv_obj_del(s_screen);
        s_screen = NULL;
    }
    s_content = NULL;
    s_show_settings = false;
    s_search_ta = NULL;
    s_keyboard = NULL;
    memset(s_age_labels, 0, sizeof(s_age_labels));
}

lv_obj_t *feed_get_screen(void) { return s_screen; }

void feed_post(feed_kind_t kind, const char *headline, const char *detail)
{
    if (!headline || !headline[0]) return;
    if (kind < 0 || kind >= FEED_KIND_COUNT) kind = FEED_KIND_SYSTEM;

    /* Repeated producers update their newest item rather than flooding the
     * display with identical telemetry labels. */
    if (s_count > 0 && s_items[0].kind == kind &&
        strcmp(s_items[0].headline, headline) == 0)
    {
        snprintf(s_items[0].detail, sizeof(s_items[0].detail), "%s",
                 detail ? detail : "");
        s_items[0].occurred_at = time(NULL);
    }
    else
    {
        int move = s_count < FEED_CAPACITY ? s_count : FEED_CAPACITY - 1;
        if (move > 0)
        {
            memmove(&s_items[1], &s_items[0], (size_t)move * sizeof(s_items[0]));
        }
        if (s_count < FEED_CAPACITY) s_count++;
        s_items[0].kind = kind;
        snprintf(s_items[0].headline, sizeof(s_items[0].headline), "%s", headline);
        snprintf(s_items[0].detail, sizeof(s_items[0].detail), "%s",
                 detail ? detail : "");
        s_items[0].occurred_at = time(NULL);
    }

    feed_render();
}

void feed_clear(void)
{
    memset(s_items, 0, sizeof(s_items));
    s_count = 0;
    feed_render();
}

void feed_clear_rss(void)
{
    int write = 0;
    for (int read = 0; read < s_count; read++)
    {
        if (s_items[read].kind == FEED_KIND_NEWS) continue;
        if (write != read) s_items[write] = s_items[read];
        write++;
    }
    if (write < s_count)
    {
        memset(&s_items[write], 0,
               (size_t)(s_count - write) * sizeof(s_items[0]));
        s_count = write;
        feed_render();
    }
}

void feed_publish_rss(const char *source,
                      const char titles[][FEED_RSS_TITLE_MAX], size_t count)
{
    feed_item_t local[FEED_CAPACITY];
    int local_count = 0;

    /* Keep device activity, but make the requested first four RSS entries the
     * visible rows. Old NEWS rows are replaced atomically as one snapshot. */
    for (int i = 0; i < s_count && local_count < FEED_CAPACITY; i++)
    {
        if (s_items[i].kind != FEED_KIND_NEWS)
        {
            local[local_count++] = s_items[i];
        }
    }

    memset(s_items, 0, sizeof(s_items));
    s_count = 0;
    if (count > FEED_RSS_MAX_ITEMS) count = FEED_RSS_MAX_ITEMS;
    for (size_t i = 0; i < count && s_count < FEED_CAPACITY; i++)
    {
        if (!titles[i][0]) continue;
        feed_item_t *item = &s_items[s_count++];
        item->kind = FEED_KIND_NEWS;
        snprintf(item->headline, sizeof(item->headline), "%s", titles[i]);
        snprintf(item->detail, sizeof(item->detail), "RSS | %s",
                 source && source[0] ? source : "configured source");
        item->occurred_at = time(NULL);
    }
    for (int i = 0; i < local_count && s_count < FEED_CAPACITY; i++)
    {
        s_items[s_count++] = local[i];
    }
    feed_render();
}

void feed_refresh(void)
{
    if (!s_screen || !s_config.show_age) return;
    for (int i = 0; i < FEED_MAX_VISIBLE; i++)
    {
        int item = s_row_item[i];
        if (!s_age_labels[i] || item < 0 || item >= s_count) continue;
        char age[12];
        format_age(s_items[item].occurred_at, age, sizeof(age));
        lv_label_set_text(s_age_labels[i], age);
    }
}

void feed_configure(const feed_config_t *config)
{
    if (config)
    {
        s_config = *config;
        if (s_config.visible_rows < 1) s_config.visible_rows = 1;
        if (s_config.visible_rows > FEED_MAX_VISIBLE)
            s_config.visible_rows = FEED_MAX_VISIBLE;
    }
    else
    {
        s_config = (feed_config_t){ .visible_rows = 4, .show_age = true };
    }
    feed_render();
}

feed_config_t feed_get_config(void) { return s_config; }
