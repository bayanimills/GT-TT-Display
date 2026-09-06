#include "clock.h"
#include "payout.h"
#include "odds.h"
#include "home.h"
#include "block.h"
#include "wifi.h"
#include "settings.h"
#include "night.h"
#include "price.h"
#include "mempool.h"
#include "chain.h"
#include "custom_fonts.h"
#include "glass.h"
#include "esp_timer.h"
#include "nvs.h"
#include "lwip/apps/sntp.h"
#include "settings.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define CLOCK_NVS_NS      "clock"
#define CLOCK_NVS_24H     "use24h"
#define CLOCK_NVS_TWIN    "twin"
#define CLOCK_NVS_DIGITAL "digital"
#define CLOCK_NVS_STAT0   "stat0"
#define CLOCK_NVS_STAT1   "stat1"
#define CLOCK_FACE_TICKS  12

typedef enum {
    CLOCK_STAT_PRICE = 0,
    CLOCK_STAT_HASHRATE,
    CLOCK_STAT_DIFFICULTY,
    CLOCK_STAT_BLOCK,
    CLOCK_STAT_HALVING_DAYS,
    CLOCK_STAT_TEMPERATURE,
    CLOCK_STAT_POWER,
    CLOCK_STAT_BEST_DIFF,
    CLOCK_STAT_COUNT
} clock_stat_t;

static lv_obj_t *clock_screen = NULL;
static lv_obj_t *clock_time_cont = NULL;
static lv_obj_t *clock_time_label = NULL;
static lv_obj_t *clock_ampm_label = NULL;
static lv_obj_t *clock_title_label = NULL;
static lv_obj_t *clock_tz_dropdown = NULL;
static lv_obj_t *clock_date_label = NULL;
static lv_timer_t *clock_timer = NULL;

/* Glass-only clock surface. Classic deliberately keeps its existing digital
 * clock and navigation untouched. */
static bool clock_glass_mode = false;
static bool clock_use_24h = false;
static bool clock_twin_layout = true;
static bool clock_digital_face = false;
static uint8_t clock_stat_kind[2] = { CLOCK_STAT_PRICE, CLOCK_STAT_HASHRATE };
static bool clock_prefs_loaded = false;
static lv_obj_t *clock_display_card = NULL;
static lv_obj_t *clock_display_content = NULL;
static lv_obj_t *clock_settings_card = NULL;
static lv_obj_t *clock_face = NULL;
static lv_obj_t *clock_hour_hand = NULL;
static lv_obj_t *clock_minute_hand = NULL;
static lv_obj_t *clock_second_hand = NULL;
static lv_obj_t *clock_stat_value[2] = { NULL, NULL };
static lv_obj_t *clock_stat_caption[2] = { NULL, NULL };
static lv_obj_t *clock_face_buttons[2] = { NULL, NULL };
static lv_obj_t *clock_format_buttons[2] = { NULL, NULL };
static lv_obj_t *clock_layout_buttons[2] = { NULL, NULL };
static lv_point_t clock_tick_points[CLOCK_FACE_TICKS][2];
static lv_point_t clock_hour_points[2];
static lv_point_t clock_minute_points[2];
static lv_point_t clock_second_points[2];
static int clock_face_size = 0;

static char current_time_text[16] = "--:--";
static char current_ampm_text[4] = "--";
static char current_date_text[32] = "SYNCING TIME";
/* "06 September 2026" for the title and "Sunday" for under the face. One
 * strftime each rather than slicing the combined string, because the combined
 * form is what neither of them wants. */
static char current_datetitle_text[32] = "SYNCING TIME";
static char current_weekday_text[16] = "";

static lv_obj_t *create_bottom_nav_btn(lv_obj_t *parent, const char *symbol, lv_event_cb_t event_cb, bool active);
static lv_obj_t *create_bottom_nav_btn_img(lv_obj_t *parent, const lv_img_dsc_t *img_dsc, lv_event_cb_t event_cb, bool active);
static void clock_start_sntp(void);
static void clock_update_time_text(void);
static void clock_timer_cb(lv_timer_t *timer);
static void clock_prefs_load(void);
static void clock_prefs_save(void);
static void clock_build_glass_display(void);
static void clock_build_glass_settings(void);
static void clock_update_analogue(const struct tm *time_info);
static void clock_sync_fixed_time(void);
static void clock_update_glass_stats(void);
static void clock_build_digital(lv_obj_t *parent, int x, int y, int w, int h,
                                const lv_font_t *font);

void clock_screen_create(void)
{
    if (clock_screen != NULL)
    {
        return;
    }

    const bool glass = glass_active();
    if (glass)
    {
        clock_screen = glass_screen_create(GLASS_SCREEN_CLOCK, false);
    }
    else
    {
        clock_screen = lv_obj_create(NULL);
        lv_obj_set_style_bg_color(clock_screen, COLOR_BACKGROUND, 0);
        lv_obj_set_style_bg_opa(clock_screen, LV_OPA_COVER, 0);
        lv_obj_clear_flag(clock_screen, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_scrollbar_mode(clock_screen, LV_SCROLLBAR_MODE_OFF);
    }

    clock_glass_mode = glass;
    if (glass)
    {
        clock_prefs_load();

        /* The title is the date. A screen showing a clock does not need to be
         * captioned "CLOCK", but it does need to say which day it is, and the
         * top of the screen is where a title goes. */
        clock_title_label = lv_label_create(clock_screen);
        lv_label_set_text(clock_title_label, current_datetitle_text);
        lv_obj_set_style_text_color(clock_title_label, COLOR_TEXT_PRIMARY, 0);
        lv_obj_set_style_text_font(clock_title_label, &lv_font_montserrat_20, 0);
        /* A caption, not a headline. The pill stands 30px at this size, so 23
         * centres it in the 76px between the top of the screen and the card. */
        lv_obj_align(clock_title_label, LV_ALIGN_TOP_MID, 0, 23);
        lv_obj_clear_flag(clock_title_label, LV_OBJ_FLAG_CLICKABLE);
        glass_pill_label(clock_title_label, false);

        clock_build_glass_display();
        clock_build_glass_settings();
        clock_start_sntp();
        price_ensure_task();
        clock_update_time_text();
        clock_timer = lv_timer_create(clock_timer_cb, 1000, NULL);
        glass_screen_ready(clock_screen);
        return;
    }

    clock_title_label = lv_label_create(clock_screen);
    lv_label_set_text(clock_title_label, current_datetitle_text);
    lv_obj_set_style_text_color(clock_title_label, COLOR_TEXT_PRIMARY, 0);
    lv_obj_set_style_text_font(clock_title_label, &lv_font_montserrat_20, 0);
    lv_obj_align(clock_title_label, LV_ALIGN_TOP_MID, 0, 23);
    if (glass) glass_pill_label(clock_title_label, false);

    lv_obj_t *parent;
    if (glass)
    {
        parent = glass_pane(clock_screen, 744, 306, 28);
    }
    else
    {
        parent = lv_obj_create(clock_screen);
        lv_obj_set_size(parent, 744, 306);
        lv_obj_set_style_bg_color(parent, COLOR_CARD_BG, 0);
        lv_obj_set_style_bg_opa(parent, LV_OPA_COVER, 0);
        lv_obj_set_style_border_color(parent, COLOR_BORDER, 0);
        lv_obj_set_style_border_width(parent, 1, 0);
        lv_obj_set_style_radius(parent, 18, 0);
        lv_obj_set_style_shadow_width(parent, 0, 0);
        lv_obj_set_style_pad_all(parent, 0, 0);
        lv_obj_clear_flag(parent, LV_OBJ_FLAG_SCROLLABLE);
    }
    lv_obj_align(parent, LV_ALIGN_TOP_MID, 0, 76);

    clock_time_cont = lv_obj_create(parent);
    lv_obj_set_size(clock_time_cont, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(clock_time_cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(clock_time_cont, 0, 0);
    lv_obj_set_style_pad_all(clock_time_cont, 0, 0);
    lv_obj_set_style_pad_column(clock_time_cont, 10, 0);
    lv_obj_set_flex_flow(clock_time_cont, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(clock_time_cont, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_align(clock_time_cont, LV_ALIGN_CENTER, 0, 22);
    if (glass)
    {
        /* Let taps fall through to the screen so the drawer opens. */
        lv_obj_clear_flag(clock_time_cont, LV_OBJ_FLAG_CLICKABLE);
    }

    clock_time_label = lv_label_create(clock_time_cont);
    lv_label_set_text(clock_time_label, current_time_text);
    lv_obj_set_style_text_color(clock_time_label, COLOR_TEXT_PRIMARY, 0);
    lv_obj_set_style_text_font(clock_time_label, &montserrat_140, 0);
    lv_obj_set_style_text_letter_space(clock_time_label, 2, 0);

    clock_ampm_label = lv_label_create(clock_time_cont);
    lv_label_set_text(clock_ampm_label, current_ampm_text);
    lv_obj_set_style_text_color(clock_ampm_label, COLOR_TEXT_PRIMARY, 0);
    lv_obj_set_style_text_opa(clock_ampm_label, (lv_opa_t)192, 0);
    lv_obj_set_style_text_font(clock_ampm_label, &lv_font_montserrat_48, 0);

    if (glass)
    {
        clock_start_sntp();
        clock_update_time_text();
        clock_timer = lv_timer_create(clock_timer_cb, 1000, NULL);
        glass_screen_ready(clock_screen);
        return;
    }

    lv_obj_t *bottom_nav = lv_obj_create(clock_screen);
    lv_obj_set_size(bottom_nav, SCREEN_WIDTH, 64);
    lv_obj_align(bottom_nav, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_color(bottom_nav, COLOR_NAV_BG, 0);
    lv_obj_set_style_bg_opa(bottom_nav, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(bottom_nav, 0, 0);
    lv_obj_set_style_radius(bottom_nav, 0, 0);
    lv_obj_set_style_pad_all(bottom_nav, 8, 0);
    lv_obj_clear_flag(bottom_nav, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(bottom_nav, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_flex_flow(bottom_nav, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(bottom_nav, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    create_bottom_nav_btn(bottom_nav, LV_SYMBOL_HOME, clock_home_clicked, false);
    create_bottom_nav_btn_img(bottom_nav, &cube_solid_full, clock_block_clicked, false);
    create_bottom_nav_btn_img(bottom_nav, &cubes_solid_full, clock_mempool_clicked, false);
    create_bottom_nav_btn_img(bottom_nav, &clock_solid_full, NULL, true);
    create_bottom_nav_btn(bottom_nav, "$", clock_price_clicked, false);
    create_bottom_nav_btn(bottom_nav, "%", clock_odds_clicked, false);
    create_bottom_nav_btn(bottom_nav, LV_SYMBOL_DOWNLOAD, clock_payout_clicked, false);
    create_bottom_nav_btn(bottom_nav, LV_SYMBOL_WIFI, clock_wifi_clicked, false);
    create_bottom_nav_btn(bottom_nav, LV_SYMBOL_SETTINGS, clock_settings_clicked, false);
    create_bottom_nav_btn(bottom_nav, LV_SYMBOL_EYE_OPEN, clock_night_clicked, false);

    clock_start_sntp();
    clock_update_time_text();
    clock_timer = lv_timer_create(clock_timer_cb, 1000, NULL);
}

void clock_screen_destroy(void)
{
    if (clock_screen)
    {
        if (clock_timer)
        {
            lv_timer_del(clock_timer);
            clock_timer = NULL;
        }
        glass_screen_detach(clock_screen);
        lv_obj_del(clock_screen);
        clock_screen = NULL;
        clock_time_cont = NULL;
        clock_time_label = NULL;
        clock_ampm_label = NULL;
        clock_title_label = NULL;
        clock_date_label = NULL;
        clock_display_card = NULL;
        clock_display_content = NULL;
        clock_settings_card = NULL;
        clock_face = NULL;
        clock_hour_hand = NULL;
        clock_minute_hand = NULL;
        clock_second_hand = NULL;
        memset(clock_stat_value, 0, sizeof(clock_stat_value));
        memset(clock_stat_caption, 0, sizeof(clock_stat_caption));
        memset(clock_face_buttons, 0, sizeof(clock_face_buttons));
        memset(clock_format_buttons, 0, sizeof(clock_format_buttons));
        memset(clock_layout_buttons, 0, sizeof(clock_layout_buttons));
        clock_face_size = 0;
        clock_glass_mode = false;
    }
}

lv_obj_t *clock_get_screen(void)
{
    return clock_screen;
}

static void clock_update_time_text(void)
{
    time_t now = time(NULL);
    struct tm time_info = { 0 };
    bool synced = now >= 946684800;

    if (!synced)
    {
        int64_t uptime_us = esp_timer_get_time();
        int32_t uptime_sec = (int32_t)(uptime_us / 1000000);
        time_info.tm_hour = (uptime_sec / 3600) % 24;
        time_info.tm_min = (uptime_sec / 60) % 60;
        time_info.tm_sec = uptime_sec % 60;
        lv_snprintf(current_date_text, sizeof(current_date_text), "SYNCING TIME");
        lv_snprintf(current_datetitle_text, sizeof(current_datetitle_text), "SYNCING TIME");
        current_weekday_text[0] = 0;
    }
    else
    {
        localtime_r(&now, &time_info);
        strftime(current_date_text, sizeof(current_date_text), "%A, %d %B %Y", &time_info);
        strftime(current_datetitle_text, sizeof(current_datetitle_text), "%B %d, %Y", &time_info);
        strftime(current_weekday_text, sizeof(current_weekday_text), "%A", &time_info);
    }

    if (clock_glass_mode && clock_use_24h)
    {
        lv_snprintf(current_time_text, sizeof(current_time_text), "%02d:%02d",
                    time_info.tm_hour, time_info.tm_min);
        current_ampm_text[0] = '\0';
    }
    else
    {
        int hour12 = time_info.tm_hour % 12;
        if (hour12 == 0) hour12 = 12;
        lv_snprintf(current_time_text, sizeof(current_time_text), "%02d:%02d",
                    hour12, time_info.tm_min);
        lv_snprintf(current_ampm_text, sizeof(current_ampm_text), "%s",
                    time_info.tm_hour < 12 ? "AM" : "PM");
    }

    if (clock_time_label)
    {
        lv_label_set_text(clock_time_label, current_time_text);
    }
    /* The digital faces draw the time a character at a time; see
     * clock_build_fixed_time for why. */
    clock_sync_fixed_time();
    if (clock_ampm_label)
    {
        lv_label_set_text(clock_ampm_label, current_ampm_text);
        if (clock_glass_mode && clock_use_24h)
            lv_obj_add_flag(clock_ampm_label, LV_OBJ_FLAG_HIDDEN);
        else
            lv_obj_clear_flag(clock_ampm_label, LV_OBJ_FLAG_HIDDEN);
    }
    if (clock_glass_mode && clock_time_label)
    {
        lv_obj_align(clock_time_label, LV_ALIGN_CENTER, clock_use_24h ? 0 : -9, 0);
    }
    if (clock_date_label)
    {
        lv_label_set_text(clock_date_label, current_weekday_text);
    }
    if (clock_title_label)
    {
        lv_label_set_text(clock_title_label, current_datetitle_text);
    }
    if (clock_glass_mode)
    {
        clock_update_analogue(&time_info);
        clock_update_glass_stats();
    }
}

static void clock_prefs_load(void)
{
    if (clock_prefs_loaded) return;
    clock_prefs_loaded = true;

    nvs_handle_t h;
    if (nvs_open(CLOCK_NVS_NS, NVS_READONLY, &h) != ESP_OK) return;

    uint8_t value = 0;
    if (nvs_get_u8(h, CLOCK_NVS_24H, &value) == ESP_OK)
        clock_use_24h = value != 0;
    if (nvs_get_u8(h, CLOCK_NVS_TWIN, &value) == ESP_OK)
        clock_twin_layout = value != 0;
    if (nvs_get_u8(h, CLOCK_NVS_DIGITAL, &value) == ESP_OK)
        clock_digital_face = value != 0;
    if (nvs_get_u8(h, CLOCK_NVS_STAT0, &value) == ESP_OK && value < CLOCK_STAT_COUNT)
        clock_stat_kind[0] = value;
    if (nvs_get_u8(h, CLOCK_NVS_STAT1, &value) == ESP_OK && value < CLOCK_STAT_COUNT)
        clock_stat_kind[1] = value;
    nvs_close(h);
}

static void clock_prefs_save(void)
{
    nvs_handle_t h;
    if (nvs_open(CLOCK_NVS_NS, NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_u8(h, CLOCK_NVS_24H, clock_use_24h ? 1U : 0U);
    nvs_set_u8(h, CLOCK_NVS_TWIN, clock_twin_layout ? 1U : 0U);
    nvs_set_u8(h, CLOCK_NVS_DIGITAL, clock_digital_face ? 1U : 0U);
    nvs_set_u8(h, CLOCK_NVS_STAT0, clock_stat_kind[0]);
    nvs_set_u8(h, CLOCK_NVS_STAT1, clock_stat_kind[1]);
    nvs_commit(h);
    nvs_close(h);
}

static void clock_set_label_if_changed(lv_obj_t *label, const char *text)
{
    if (!label || !text) return;
    if (strcmp(lv_label_get_text(label), text) != 0) lv_label_set_text(label, text);
}

static lv_obj_t *clock_text_label(lv_obj_t *parent, const char *text,
                                  const lv_font_t *font, lv_color_t color)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, color, 0);
    lv_obj_clear_flag(label, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    return label;
}

static void clock_style_choice(lv_obj_t *button, bool selected)
{
    if (!button) return;
    lv_obj_set_style_bg_color(button, selected ? COLOR_ACCENT : lv_color_white(), 0);
    lv_obj_set_style_bg_opa(button, selected ? LV_OPA_COVER : LV_OPA_10, 0);
    lv_obj_set_style_border_color(button, selected ? COLOR_ACCENT : lv_color_white(), 0);
    lv_obj_set_style_border_opa(button, selected ? LV_OPA_COVER : LV_OPA_30, 0);
    lv_obj_t *label = lv_obj_get_child(button, 0);
    if (label)
        lv_obj_set_style_text_color(label, selected ? COLOR_TEXT_ON_ACCENT : COLOR_TEXT_PRIMARY, 0);
}

static void clock_refresh_choices(void)
{
    clock_style_choice(clock_format_buttons[0], !clock_use_24h);
    clock_style_choice(clock_format_buttons[1], clock_use_24h);
    clock_style_choice(clock_layout_buttons[0], !clock_twin_layout);
    clock_style_choice(clock_layout_buttons[1], clock_twin_layout);
    clock_style_choice(clock_face_buttons[0], !clock_digital_face);
    clock_style_choice(clock_face_buttons[1], clock_digital_face);
}

static void clock_face_choice_cb(lv_event_t *e)
{
    clock_digital_face = (intptr_t)lv_event_get_user_data(e) != 0;
    clock_prefs_save();
    clock_refresh_choices();
}

static void clock_format_choice_cb(lv_event_t *e)
{
    clock_use_24h = (intptr_t)lv_event_get_user_data(e) != 0;
    clock_prefs_save();
    clock_refresh_choices();
    clock_update_time_text();
}

static void clock_layout_choice_cb(lv_event_t *e)
{
    clock_twin_layout = (intptr_t)lv_event_get_user_data(e) != 0;
    clock_prefs_save();
    clock_refresh_choices();
}

static void clock_tz_changed_cb(lv_event_t *e)
{
    settings_timezone_select((int) lv_dropdown_get_selected(lv_event_get_target(e)));
    /* The offset moved, so every string on this screen is now stale. */
    clock_update_time_text();
}

static void clock_open_settings_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    if (!clock_settings_card || !clock_display_card) return;
    /* The date rides inside the display card now, so hiding the card hides
     * it too; there is nothing left up here to hide separately. */
    lv_obj_add_flag(clock_display_card, LV_OBJ_FLAG_HIDDEN);
    if (clock_title_label) lv_obj_add_flag(clock_title_label, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(clock_settings_card, LV_OBJ_FLAG_HIDDEN);
    clock_refresh_choices();
    /* The grabber predates this pane in z-order. A full invalidation keeps its
     * visual cue present after the old display pane and shadow disappear. */
    lv_obj_invalidate(clock_screen);
}

static void clock_close_settings_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    if (!clock_settings_card || !clock_screen) return;
    lv_obj_add_flag(clock_settings_card, LV_OBJ_FLAG_HIDDEN);
    if (clock_title_label) lv_obj_clear_flag(clock_title_label, LV_OBJ_FLAG_HIDDEN);
    clock_build_glass_display();
    lv_obj_clear_flag(clock_display_card, LV_OBJ_FLAG_HIDDEN);
    clock_update_time_text();
    lv_obj_invalidate(clock_screen);
}

static lv_obj_t *clock_choice_button(lv_obj_t *parent, const char *text, int x, int y,
                                     lv_event_cb_t cb, intptr_t value)
{
    lv_obj_t *button = lv_btn_create(parent);
    lv_obj_set_size(button, 184, 54);
    lv_obj_set_pos(button, x, y);
    lv_obj_set_style_radius(button, 18, 0);
    lv_obj_set_style_border_width(button, 1, 0);
    lv_obj_set_style_shadow_width(button, 0, 0);
    lv_obj_set_style_bg_color(button, COLOR_ACCENT, LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(button, LV_OPA_70, LV_STATE_PRESSED);
    lv_obj_clear_flag(button, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(button, cb, LV_EVENT_CLICKED, (void *)value);

    lv_obj_t *label = clock_text_label(button, text, &lv_font_montserrat_18, COLOR_TEXT_PRIMARY);
    lv_obj_center(label);
    return button;
}

static void clock_build_glass_settings(void)
{
    clock_settings_card = glass_pane(clock_screen, 744, 372, 28);
    lv_obj_align(clock_settings_card, LV_ALIGN_TOP_MID, 0, 40);
    lv_obj_clear_flag(clock_settings_card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(clock_settings_card, LV_SCROLLBAR_MODE_OFF);

    lv_obj_t *back = lv_btn_create(clock_settings_card);
    lv_obj_set_size(back, 112, 50);
    lv_obj_set_pos(back, 20, 16);
    lv_obj_set_style_radius(back, 16, 0);
    lv_obj_set_style_bg_color(back, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(back, LV_OPA_10, 0);
    lv_obj_set_style_bg_color(back, COLOR_ACCENT, LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(back, LV_OPA_50, LV_STATE_PRESSED);
    lv_obj_set_style_border_color(back, lv_color_white(), 0);
    lv_obj_set_style_border_opa(back, LV_OPA_30, 0);
    lv_obj_set_style_border_width(back, 1, 0);
    lv_obj_set_style_shadow_width(back, 0, 0);
    lv_obj_clear_flag(back, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(back, clock_close_settings_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *back_label = clock_text_label(back, LV_SYMBOL_LEFT "  Back",
                                             &lv_font_montserrat_16, COLOR_TEXT_PRIMARY);
    lv_obj_center(back_label);

    lv_obj_t *title = clock_text_label(clock_settings_card, "CLOCK SETTINGS",
                                       &lv_font_montserrat_24, COLOR_TEXT_PRIMARY);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 27);

    lv_obj_t *face_caption = clock_text_label(clock_settings_card, "CLOCK FACE",
                                               &lv_font_montserrat_14, COLOR_TEXT_SECONDARY);
    lv_obj_set_pos(face_caption, 38, 83);
    clock_face_buttons[0] = clock_choice_button(clock_settings_card, "ANALOGUE", 292, 60,
                                                clock_face_choice_cb, 0);
    clock_face_buttons[1] = clock_choice_button(clock_settings_card, "DIGITAL", 488, 60,
                                                clock_face_choice_cb, 1);

    lv_obj_t *format_caption = clock_text_label(clock_settings_card, "TIME FORMAT",
                                                 &lv_font_montserrat_14, COLOR_TEXT_SECONDARY);
    lv_obj_set_pos(format_caption, 38, 165);
    clock_format_buttons[0] = clock_choice_button(clock_settings_card, "12 HOUR", 292, 142,
                                                   clock_format_choice_cb, 0);
    clock_format_buttons[1] = clock_choice_button(clock_settings_card, "24 HOUR", 488, 142,
                                                   clock_format_choice_cb, 1);

    lv_obj_t *layout_caption = clock_text_label(clock_settings_card, "CLOCK LAYOUT",
                                                 &lv_font_montserrat_14, COLOR_TEXT_SECONDARY);
    lv_obj_set_pos(layout_caption, 38, 235);
    clock_layout_buttons[0] = clock_choice_button(clock_settings_card, "SINGLE", 292, 212,
                                                   clock_layout_choice_cb, 0);
    clock_layout_buttons[1] = clock_choice_button(clock_settings_card, "TWIN", 488, 212,
                                                   clock_layout_choice_cb, 1);

    /* The panel gets the time from SNTP and the offset from here. This is the
     * only control for it on the Glass skin: the classic settings page has one
     * too, and Glass never builds that page. */
    lv_obj_t *tz_caption = clock_text_label(clock_settings_card, "TIME ZONE",
                                            &lv_font_montserrat_14, COLOR_TEXT_SECONDARY);
    lv_obj_set_pos(tz_caption, 38, 305);
    clock_tz_dropdown = lv_dropdown_create(clock_settings_card);
    lv_obj_set_size(clock_tz_dropdown, 380, 50);
    lv_obj_set_pos(clock_tz_dropdown, 292, 288);
    lv_dropdown_set_options(clock_tz_dropdown, settings_timezone_option_list());
    lv_dropdown_set_selected(clock_tz_dropdown, (uint16_t) settings_timezone_index());
    glass_style_dropdown(clock_tz_dropdown);
    lv_obj_add_event_cb(clock_tz_dropdown, clock_tz_changed_cb, LV_EVENT_VALUE_CHANGED, NULL);

    clock_refresh_choices();
    lv_obj_add_flag(clock_settings_card, LV_OBJ_FLAG_HIDDEN);
}

static const char *clock_stat_name(clock_stat_t kind)
{
    static const char *name[CLOCK_STAT_COUNT] = {
        "BITCOIN RATE", "MINER HASHRATE",
        "NETWORK DIFFICULTY", "BLOCK HEIGHT",
        "DAYS TO HALVING", "ASIC TEMPERATURE",
        "POWER", "BEST DIFFICULTY",
    };
    return kind < CLOCK_STAT_COUNT ? name[kind] : "METRIC";
}

static void clock_rebuild_display_async(void *unused)
{
    (void)unused;
    clock_build_glass_display();
    clock_update_time_text();
}

/* Packed as slot<<1 | forward, because a click callback carries one pointer
 * and there are four zones. */
static void clock_stat_clicked(lv_event_t *e)
{
    const int packed = (int)(intptr_t)lv_event_get_user_data(e);
    const int slot = packed >> 1;
    const bool forward = (packed & 1) != 0;
    if (slot < 0 || slot > 1) return;

    /* Skip whatever the other card is already showing, in whichever direction
     * is being asked for, so back and forward are true inverses. */
    do {
        const int step = forward ? 1 : (CLOCK_STAT_COUNT - 1);
        clock_stat_kind[slot] =
            (uint8_t)((clock_stat_kind[slot] + step) % CLOCK_STAT_COUNT);
    } while (clock_stat_kind[slot] == clock_stat_kind[1 - slot]);

    clock_prefs_save();
    lv_async_call(clock_rebuild_display_async, NULL);
}

/* One half of a stat card. The chevron is always there so the control is
 * discoverable without tapping, and the half lights on press so it is obvious
 * which way the tap went. */
static void clock_stat_zone(lv_obj_t *card, int slot, bool forward)
{
    lv_obj_t *zone = lv_obj_create(card);
    lv_obj_set_size(zone, 155, 112);
    lv_obj_set_pos(zone, forward ? 163 : 4, 4);
    lv_obj_set_style_radius(zone, 20, 0);
    lv_obj_set_style_bg_opa(zone, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(zone, 0, 0);
    lv_obj_set_style_shadow_width(zone, 0, 0);
    lv_obj_set_style_pad_all(zone, 0, 0);
    lv_obj_set_style_bg_color(zone, COLOR_ACCENT, LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(zone, LV_OPA_30, LV_STATE_PRESSED);
    lv_obj_clear_flag(zone, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(zone, LV_OBJ_FLAG_CLICKABLE);
    /* Reclaim the 4px inset so the whole half is still hittable. */
    lv_obj_set_ext_click_area(zone, 6);
    lv_obj_add_event_cb(zone, clock_stat_clicked, LV_EVENT_CLICKED,
                        (void *)(intptr_t)((slot << 1) | (forward ? 1 : 0)));

    lv_obj_t *chev = lv_label_create(zone);
    lv_label_set_text(chev, forward ? LV_SYMBOL_RIGHT : LV_SYMBOL_LEFT);
    lv_obj_set_style_text_color(chev, COLOR_TEXT_PRIMARY, 0);
    lv_obj_set_style_text_opa(chev, (lv_opa_t)110, 0);
    lv_obj_set_style_text_font(chev, &lv_font_montserrat_16, 0);
    lv_obj_align(chev, forward ? LV_ALIGN_RIGHT_MID : LV_ALIGN_LEFT_MID,
                 forward ? -12 : 12, 0);
    lv_obj_clear_flag(chev, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
}

static lv_obj_t *clock_stat_card(lv_obj_t *parent, int slot, int y)
{
    lv_obj_t *card = lv_obj_create(parent);
    lv_obj_set_size(card, 322, 120);
    lv_obj_set_pos(card, 388, y);
    lv_obj_set_style_radius(card, 22, 0);
    lv_obj_set_style_bg_color(card, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_10, 0);
    lv_obj_set_style_border_color(card, lv_color_white(), 0);
    lv_obj_set_style_border_opa(card, LV_OPA_20, 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_shadow_width(card, 0, 0);
    lv_obj_set_style_pad_all(card, 0, 0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    /* The card itself is no longer a button. Cycling in one direction only
     * meant overshooting cost seven more taps; the two halves below step
     * either way, and light up so which half does what is visible. */
    lv_obj_clear_flag(card, LV_OBJ_FLAG_CLICKABLE);

    clock_stat_zone(card, slot, false);
    clock_stat_zone(card, slot, true);

    /* Text last, so it draws over the zones, and not clickable, so a tap on
     * it still reaches the half underneath. Inset past both chevrons. */
    clock_stat_caption[slot] = clock_text_label(card,
                                                clock_stat_name((clock_stat_t)clock_stat_kind[slot]),
                                                &lv_font_montserrat_12,
                                                COLOR_TEXT_SECONDARY);
    lv_obj_set_width(clock_stat_caption[slot], 228);
    lv_label_set_long_mode(clock_stat_caption[slot], LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_align(clock_stat_caption[slot], LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(clock_stat_caption[slot], 47, 24);

    clock_stat_value[slot] = clock_text_label(card, "--", &lv_font_montserrat_32,
                                              COLOR_TEXT_PRIMARY);
    lv_obj_set_width(clock_stat_value[slot], 228);
    lv_label_set_long_mode(clock_stat_value[slot], LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_align(clock_stat_value[slot], LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(clock_stat_value[slot], 47, 56);
    return card;
}

static void clock_build_face(lv_obj_t *parent, int x, int y, int size)
{
    clock_face_size = size;
    clock_face = lv_obj_create(parent);
    lv_obj_set_size(clock_face, size, size);
    lv_obj_set_pos(clock_face, x, y);
    lv_obj_set_style_radius(clock_face, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(clock_face, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(clock_face, (lv_opa_t)13, 0);
    lv_obj_set_style_border_color(clock_face, lv_color_white(), 0);
    lv_obj_set_style_border_opa(clock_face, LV_OPA_50, 0);
    lv_obj_set_style_border_width(clock_face, 2, 0);
    lv_obj_set_style_pad_all(clock_face, 0, 0);
    lv_obj_set_style_shadow_width(clock_face, 0, 0);
    lv_obj_clear_flag(clock_face, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);

    const double turn = 6.28318530717958647692;
    int center = size / 2;
    int outer = center - 13;
    for (int i = 0; i < CLOCK_FACE_TICKS; i++)
    {
        double angle = turn * (double)i / (double)CLOCK_FACE_TICKS;
        int inner = outer - ((i % 3) == 0 ? 13 : 7);
        clock_tick_points[i][0].x = (lv_coord_t)(center + sin(angle) * inner);
        clock_tick_points[i][0].y = (lv_coord_t)(center - cos(angle) * inner);
        clock_tick_points[i][1].x = (lv_coord_t)(center + sin(angle) * outer);
        clock_tick_points[i][1].y = (lv_coord_t)(center - cos(angle) * outer);
        lv_obj_t *tick = lv_line_create(clock_face);
        lv_obj_set_size(tick, size, size);
        lv_obj_set_pos(tick, 0, 0);
        lv_line_set_points(tick, clock_tick_points[i], 2);
        lv_obj_set_style_line_color(tick, COLOR_TEXT_PRIMARY, 0);
        lv_obj_set_style_line_opa(tick, (i % 3) == 0 ? LV_OPA_80 : LV_OPA_40, 0);
        lv_obj_set_style_line_width(tick, (i % 3) == 0 ? 3 : 2, 0);
        lv_obj_set_style_line_rounded(tick, true, 0);
        lv_obj_clear_flag(tick, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    }

    clock_hour_hand = lv_line_create(clock_face);
    clock_minute_hand = lv_line_create(clock_face);
    clock_second_hand = lv_line_create(clock_face);
    lv_obj_t *hands[] = { clock_hour_hand, clock_minute_hand, clock_second_hand };
    for (int i = 0; i < 3; i++)
    {
        lv_obj_set_size(hands[i], size, size);
        lv_obj_set_pos(hands[i], 0, 0);
        lv_obj_set_style_line_rounded(hands[i], true, 0);
        lv_obj_clear_flag(hands[i], LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    }
    lv_obj_set_style_line_color(clock_hour_hand, COLOR_TEXT_PRIMARY, 0);
    lv_obj_set_style_line_width(clock_hour_hand, 7, 0);
    lv_obj_set_style_line_color(clock_minute_hand, COLOR_TEXT_PRIMARY, 0);
    lv_obj_set_style_line_width(clock_minute_hand, 4, 0);
    lv_obj_set_style_line_color(clock_second_hand, COLOR_ACCENT, 0);
    lv_obj_set_style_line_width(clock_second_hand, 2, 0);

    lv_obj_t *hub = lv_obj_create(clock_face);
    lv_obj_set_size(hub, 14, 14);
    lv_obj_align(hub, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_radius(hub, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(hub, COLOR_ACCENT, 0);
    lv_obj_set_style_bg_opa(hub, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(hub, 0, 0);
    lv_obj_set_style_pad_all(hub, 0, 0);
    lv_obj_clear_flag(hub, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);

    /* Nothing on the dial and no digital readout under it. A clock face that
     * has to be accompanied by the time in figures is not doing its job. What
     * the hands genuinely cannot say is which day it is, so that is what goes
     * underneath; the date itself titles the screen. */
    const int face_bottom = y + size;
    const bool twin = clock_twin_layout;

    clock_date_label = clock_text_label(parent, current_weekday_text,
                                        twin ? &lv_font_montserrat_40 : &lv_font_montserrat_48,
                                        COLOR_TEXT_PRIMARY);
    lv_obj_set_width(clock_date_label, size + 120);
    lv_obj_set_style_text_align(clock_date_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(clock_date_label, x - 60, face_bottom + (twin ? 8 : 16));
}

/* Montserrat's figures are proportional, and at 140px the spread is extreme:
 * a "1" advances 52px against a "4" at 94px, so a centred HH:MM slides by up
 * to 167px between 11:11 and 04:44 - the clock visibly jumps every time a
 * digit changes. Each character gets its own fixed-width cell instead, which
 * costs five labels and holds the face still. The time is always five
 * characters, "%02d:%02d", so the row never has to be rebuilt. */
#define CLOCK_TIME_CHARS 5

static lv_obj_t *clock_digit_cells[CLOCK_TIME_CHARS];

static lv_obj_t *clock_build_fixed_time(lv_obj_t *parent, const lv_font_t *font,
                                        int digit_w, int colon_w, int cell_h)
{
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_set_size(row, digit_w * 4 + colon_w, cell_h);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_pad_all(row, 0, 0);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);

    int x = 0;
    for (int i = 0; i < CLOCK_TIME_CHARS; i++) {
        const bool colon = (i == 2);
        const int cw = colon ? colon_w : digit_w;
        lv_obj_t *cell = lv_label_create(row);
        const char text[2] = { current_time_text[i] ? current_time_text[i] : '0', 0 };
        lv_label_set_text(cell, text);
        lv_obj_set_style_text_color(cell, COLOR_TEXT_PRIMARY, 0);
        lv_obj_set_style_text_font(cell, font, 0);
        lv_obj_set_style_text_align(cell, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_width(cell, cw);
        lv_obj_set_pos(cell, x, 0);
        lv_obj_clear_flag(cell, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
        clock_digit_cells[i] = cell;
        x += cw;
    }
    return row;
}

static void clock_sync_fixed_time(void)
{
    if (!clock_digit_cells[0]) return;
    for (int i = 0; i < CLOCK_TIME_CHARS; i++) {
        if (!clock_digit_cells[i]) continue;
        /* The seven-segment face carries digits and a colon and nothing else,
         * not even a space, so a short string leaves the cell alone rather
         * than asking for a glyph that does not exist. */
        if (!current_time_text[i]) continue;
        const char text[2] = { current_time_text[i], 0 };
        lv_label_set_text(clock_digit_cells[i], text);
    }
}

static void clock_build_digital(lv_obj_t *parent, int x, int y, int w, int h,
                                const lv_font_t *font)
{
    clock_time_cont = lv_obj_create(parent);
    lv_obj_set_size(clock_time_cont, w, h);
    lv_obj_set_pos(clock_time_cont, x, y);
    lv_obj_set_style_radius(clock_time_cont, 24, 0);
    lv_obj_set_style_bg_color(clock_time_cont, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(clock_time_cont, LV_OPA_10, 0);
    lv_obj_set_style_border_color(clock_time_cont, lv_color_white(), 0);
    lv_obj_set_style_border_opa(clock_time_cont, LV_OPA_30, 0);
    lv_obj_set_style_border_width(clock_time_cont, 1, 0);
    lv_obj_set_style_shadow_width(clock_time_cont, 0, 0);
    lv_obj_set_style_pad_all(clock_time_cont, 0, 0);
    lv_obj_clear_flag(clock_time_cont, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);

    /* The large face is monospace, so its cells are simply the font's own
     * advance and the grid the cells impose is the one the font already has.
     * The small face is still Montserrat, where the cells are doing real work. */
    /* DSEG7 is monospace, so each cell is exactly the font's advance and the
     * row is as wide as the digits can ever draw. Both sizes were chosen to
     * fill their box: 4x160 + 40 = 680 of 700, and 4x66 + 16 = 280 of 298. */
    const bool big = (font == &dseg7_196);
    const int digit_w = big ? 160 : 66;
    const int colon_w = big ? 40 : 16;
    const int cell_h  = big ? 205 : 88;

    lv_obj_t *row = clock_build_fixed_time(clock_time_cont, font, digit_w, colon_w, cell_h);
    /* Five monospace cells take 665 of the 700, so there is no room beside the
     * digits for anything. The row stays centred in both formats and the
     * meridiem moves down to share the weekday's line. */
    lv_obj_align(row, LV_ALIGN_CENTER, 0, big ? -24 : -12);
    clock_time_label = NULL;

    clock_ampm_label = clock_text_label(clock_time_cont, current_ampm_text,
                                        big ? &lv_font_montserrat_40 : &lv_font_montserrat_24,
                                        COLOR_TEXT_SECONDARY);
    /* Neither face leaves room beside the digits, so the meridiem shares the
     * weekday's line in both. */
    lv_obj_align(clock_ampm_label, LV_ALIGN_BOTTOM_RIGHT, big ? -24 : -14,
                 big ? -16 : -10);

    /* The weekday, to match the analogue face. The numeric date is the title
     * and does not need saying twice. */
    clock_date_label = clock_text_label(clock_time_cont, current_weekday_text,
                                        big ? &lv_font_montserrat_48 : &lv_font_montserrat_28,
                                        COLOR_TEXT_SECONDARY);
    lv_obj_set_width(clock_date_label, w - 24);
    lv_obj_set_style_text_align(clock_date_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(clock_date_label, LV_ALIGN_BOTTOM_MID, 0, big ? -12 : -8);
}

static void clock_build_glass_display(void)
{
    if (!clock_screen) return;
    if (!clock_display_card)
    {
        /* Keep the registered glass pane for the lifetime of the screen.
         * Layout changes only replace this transparent inner surface's
         * children, so the frost registry never sees transient stale panes. */
        clock_display_card = glass_pane(clock_screen, 744, 322, 28);
        lv_obj_align(clock_display_card, LV_ALIGN_TOP_MID, 0, 76);
        lv_obj_clear_flag(clock_display_card, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_scrollbar_mode(clock_display_card, LV_SCROLLBAR_MODE_OFF);
        lv_obj_add_flag(clock_display_card, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(clock_display_card, clock_open_settings_cb, LV_EVENT_CLICKED, NULL);

        clock_display_content = lv_obj_create(clock_display_card);
        lv_obj_set_size(clock_display_content, 744, 322);
        lv_obj_set_pos(clock_display_content, 0, 0);
        lv_obj_set_style_bg_opa(clock_display_content, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(clock_display_content, 0, 0);
        lv_obj_set_style_pad_all(clock_display_content, 0, 0);
        lv_obj_clear_flag(clock_display_content,
                          LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    }
    else if (clock_display_content)
    {
        lv_obj_clean(clock_display_content);
    }
    clock_face = NULL;
    clock_hour_hand = NULL;
    clock_minute_hand = NULL;
    clock_second_hand = NULL;
    clock_time_cont = NULL;
    clock_time_label = NULL;
    clock_ampm_label = NULL;
    clock_date_label = NULL;
    memset(clock_digit_cells, 0, sizeof(clock_digit_cells));
    memset(clock_stat_value, 0, sizeof(clock_stat_value));
    memset(clock_stat_caption, 0, sizeof(clock_stat_caption));

    if (clock_twin_layout)
    {
        if (clock_digital_face)
            clock_build_digital(clock_display_content, 34, 35, 298, 252,
                                &dseg7_80);
        else
            /* One line under the dial now, not two, so it keeps more of its
             * diameter: 22 + 230 + 8 + 24 against 322. */
            clock_build_face(clock_display_content, 75, 22, 230);
        clock_stat_card(clock_display_content, 0, 34);
        clock_stat_card(clock_display_content, 1, 168);
    }
    else
    {
        if (clock_digital_face)
            clock_build_digital(clock_display_content, 22, 14, 700, 296,
                                &dseg7_196);
        else
            /* 10 + 220 + 16 + 57 leaves 19 at the foot of the 322px card,
             * with the weekday now at 48px. */
            clock_build_face(clock_display_content, 262, 10, 220);
    }
}

static void clock_update_analogue(const struct tm *time_info)
{
    if (!time_info || !clock_hour_hand || !clock_minute_hand ||
        !clock_second_hand || clock_face_size <= 0) return;

    const double turn = 6.28318530717958647692;
    int center = clock_face_size / 2;
    double hour_angle = turn * ((double)(time_info->tm_hour % 12) +
                                      (double)time_info->tm_min / 60.0) / 12.0;
    double minute_angle = turn * ((double)time_info->tm_min +
                                        (double)time_info->tm_sec / 60.0) / 60.0;
    double second_angle = turn * (double)time_info->tm_sec / 60.0;

    clock_hour_points[0] = (lv_point_t){ (lv_coord_t)center, (lv_coord_t)center };
    clock_hour_points[1] = (lv_point_t){
        (lv_coord_t)(center + sin(hour_angle) * clock_face_size * 0.24),
        (lv_coord_t)(center - cos(hour_angle) * clock_face_size * 0.24) };
    clock_minute_points[0] = clock_hour_points[0];
    clock_minute_points[1] = (lv_point_t){
        (lv_coord_t)(center + sin(minute_angle) * clock_face_size * 0.36),
        (lv_coord_t)(center - cos(minute_angle) * clock_face_size * 0.36) };
    clock_second_points[0] = clock_hour_points[0];
    clock_second_points[1] = (lv_point_t){
        (lv_coord_t)(center + sin(second_angle) * clock_face_size * 0.39),
        (lv_coord_t)(center - cos(second_angle) * clock_face_size * 0.39) };

    lv_line_set_points(clock_hour_hand, clock_hour_points, 2);
    lv_line_set_points(clock_minute_hand, clock_minute_points, 2);
    lv_line_set_points(clock_second_hand, clock_second_points, 2);
}

static void clock_update_glass_stats(void)
{
    if (!clock_twin_layout) return;

    const home_stats_t *stats = home_stats();
    const chain_data_t *chain = chain_data();
    for (int slot = 0; slot < 2; slot++)
    {
        char value[48] = "--";
        switch ((clock_stat_t)clock_stat_kind[slot])
        {
            case CLOCK_STAT_PRICE: {
                const char *price = price_get_text();
                lv_snprintf(value, sizeof(value), "%s%s", chain_ccy_prefix(chain_get_ccy()),
                            (price && price[0]) ? price : "--");
                break;
            }
            case CLOCK_STAT_HASHRATE: {
                const double ghs = (stats && stats->hashrate) ? strtod(stats->hashrate, NULL) : 0.0;
                if (ghs >= 1000.0) snprintf(value, sizeof(value), "%.2f TH/s", ghs / 1000.0);
                else if (ghs > 0.0) snprintf(value, sizeof(value), "%.0f GH/s", ghs);
                break;
            }
            case CLOCK_STAT_DIFFICULTY:
                if (chain->difficulty > 0.0) chain_fmt_compact(chain->difficulty, value, sizeof(value));
                break;
            case CLOCK_STAT_BLOCK: {
                const long height = strtol(block_get_height_text(), NULL, 10);
                if (height > 0) chain_fmt_grouped(height, value, sizeof(value));
                break;
            }
            case CLOCK_STAT_HALVING_DAYS:
                if (chain->blocks_to_halving > 0)
                    snprintf(value, sizeof(value), "%.0f days", chain->days_to_halving);
                break;
            case CLOCK_STAT_TEMPERATURE:
                if (stats && stats->temperature && stats->temperature[0])
                    lv_snprintf(value, sizeof(value), "%s", stats->temperature);
                break;
            case CLOCK_STAT_POWER:
                if (stats && stats->power && stats->power[0])
                    lv_snprintf(value, sizeof(value), "%s", stats->power);
                break;
            case CLOCK_STAT_BEST_DIFF:
                if (stats && stats->best_diff && stats->best_diff[0])
                    lv_snprintf(value, sizeof(value), "%s", stats->best_diff);
                break;
            default:
                break;
        }
        clock_set_label_if_changed(clock_stat_value[slot], value);
    }
}

static void clock_start_sntp(void)
{
    static bool sntp_started = false;
    if (sntp_started || sntp_enabled())
    {
        sntp_started = true;
        return;
    }

    sntp_started = true;
    sntp_setoperatingmode(SNTP_OPMODE_POLL);
    sntp_setservername(0, "pool.ntp.org");
    sntp_init();
}

static void clock_timer_cb(lv_timer_t *timer)
{
    LV_UNUSED(timer);
    clock_update_time_text();
}

static lv_obj_t *create_bottom_nav_btn(lv_obj_t *parent, const char *symbol, lv_event_cb_t event_cb, bool active)
{
    lv_obj_t *btn = lv_btn_create(parent);
    lv_obj_set_size(btn, 56, 46);
    lv_obj_set_style_bg_color(btn, active ? COLOR_ACCENT : COLOR_CARD_BG, 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(btn, active ? 0 : 2, 0);
    lv_obj_set_style_border_color(btn, COLOR_ACCENT, 0);
    lv_obj_set_style_border_opa(btn, active ? LV_OPA_TRANSP : LV_OPA_COVER, 0);
    lv_obj_set_style_radius(btn, 10, 0);
    lv_obj_set_style_shadow_width(btn, 0, 0);

    lv_obj_t *label = lv_label_create(btn);
    lv_label_set_text(label, symbol);
    lv_obj_set_style_text_color(label, active ? COLOR_TEXT_ON_ACCENT : COLOR_ACCENT, 0);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_18, 0);
    lv_obj_center(label);

    if (event_cb)
    {
        lv_obj_add_event_cb(btn, event_cb, LV_EVENT_CLICKED, NULL);
    }

    return btn;
}

static lv_obj_t *create_bottom_nav_btn_img(lv_obj_t *parent, const lv_img_dsc_t *img_dsc, lv_event_cb_t event_cb, bool active)
{
    lv_obj_t *btn = lv_btn_create(parent);
    lv_obj_set_size(btn, 56, 46);
    lv_obj_set_style_bg_color(btn, active ? COLOR_ACCENT : COLOR_CARD_BG, 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(btn, active ? 0 : 2, 0);
    lv_obj_set_style_border_color(btn, COLOR_ACCENT, 0);
    lv_obj_set_style_border_opa(btn, active ? LV_OPA_TRANSP : LV_OPA_COVER, 0);
    lv_obj_set_style_radius(btn, 10, 0);
    lv_obj_set_style_shadow_width(btn, 0, 0);

    lv_obj_t *img = lv_img_create(btn);
    lv_img_set_src(img, img_dsc);
    lv_obj_set_style_img_recolor(img, active ? COLOR_TEXT_ON_ACCENT : COLOR_ACCENT, 0);
    lv_obj_set_style_img_recolor_opa(img, LV_OPA_COVER, 0);
    lv_obj_center(img);

    if (event_cb)
    {
        lv_obj_add_event_cb(btn, event_cb, LV_EVENT_CLICKED, NULL);
    }

    return btn;
}

void clock_home_clicked(lv_event_t *e)
{
    LV_UNUSED(e);
    home_screen_create();
    lv_scr_load(home_get_screen());
    clock_screen_destroy();
}

void clock_block_clicked(lv_event_t *e)
{
    LV_UNUSED(e);
    block_screen_create();
    lv_scr_load(block_get_screen());
    clock_screen_destroy();
}

void clock_mempool_clicked(lv_event_t *e)
{
    LV_UNUSED(e);
    mempool_screen_create();
    lv_scr_load(mempool_get_screen());
    clock_screen_destroy();
}

void clock_price_clicked(lv_event_t *e)
{
    LV_UNUSED(e);
    price_screen_create();
    lv_scr_load(price_get_screen());
    clock_screen_destroy();
}

void clock_wifi_clicked(lv_event_t *e)
{
    LV_UNUSED(e);
    wifi_screen_create();
    lv_scr_load(wifi_get_screen());
    clock_screen_destroy();
}

void clock_settings_clicked(lv_event_t *e)
{
    LV_UNUSED(e);
    settings_screen_create();
    lv_scr_load(settings_get_screen());
    clock_screen_destroy();
}

void clock_night_clicked(lv_event_t *e)
{
    LV_UNUSED(e);
    night_screen_create();
    lv_scr_load(night_get_screen());
    clock_screen_destroy();
}

void clock_odds_clicked(lv_event_t *e)
{
    odds_screen_create();
    lv_scr_load(odds_get_screen());
    clock_screen_destroy();
}

void clock_payout_clicked(lv_event_t *e)
{
    payout_screen_create();
    lv_scr_load(payout_get_screen());
    clock_screen_destroy();
}
