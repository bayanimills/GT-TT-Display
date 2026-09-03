#include "block.h"
#include "odds.h"
#include "chain.h"
#include "home.h"
#include "wifi.h"
#include "settings.h"
#include "night.h"
#include "clock.h"
#include "price.h"
#include "mempool.h"
#include "glass.h"
#include "custom_fonts.h"
#include <stdio.h>
#include <stdlib.h>

static lv_obj_t *block_screen = NULL;
static lv_obj_t *block_height_label = NULL;
static lv_obj_t *block_title_label = NULL;
static lv_obj_t *block_halving_value = NULL;
static lv_obj_t *block_retarget_value = NULL;
static lv_timer_t *block_network_timer = NULL;

static char current_block_height_text[24] = "0000000";

static lv_obj_t *create_bottom_nav_btn(lv_obj_t *parent, const char *symbol, lv_event_cb_t event_cb, bool active);
static lv_obj_t *create_bottom_nav_btn_img(lv_obj_t *parent, const lv_img_dsc_t *img_dsc, lv_event_cb_t event_cb, bool active);
static void apply_cached_block_height(void);
static void block_build_network_row(lv_obj_t *host, bool glass);
static void block_refresh_network(void);

void block_screen_create(void)
{
    if (block_screen != NULL)
    {
        return;
    }

    /* Under Glass the figure sits on one pane over the wallpaper and the
     * drawer replaces the nav bar; the labels are the same either way. */
    const bool glass = glass_active();
    lv_obj_t *parent;
    if (glass)
    {
        block_screen = glass_screen_create(GLASS_SCREEN_BLOCK, false);
        /* Shorter than a centred pane so the halving and retarget row has
         * somewhere to sit underneath it. */
        parent = glass_pane(block_screen, 720, 252, 28);
        lv_obj_align(parent, LV_ALIGN_TOP_MID, 0, 60);
    }
    else
    {
        block_screen = lv_obj_create(NULL);
        lv_obj_set_style_bg_color(block_screen, COLOR_BACKGROUND, 0);
        lv_obj_set_style_bg_opa(block_screen, LV_OPA_COVER, 0);
        lv_obj_clear_flag(block_screen, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_scrollbar_mode(block_screen, LV_SCROLLBAR_MODE_OFF);
        parent = block_screen;
    }

    block_title_label = lv_label_create(parent);
    lv_label_set_text(block_title_label, "CURRENT BLOCK HEIGHT");
    lv_obj_set_style_text_color(block_title_label, COLOR_TEXT_SECONDARY, 0);
    lv_obj_set_style_text_font(block_title_label, &lv_font_montserrat_20, 0);
    lv_obj_align(block_title_label, LV_ALIGN_TOP_MID, 0, 30);

    block_height_label = lv_label_create(parent);
    lv_label_set_text(block_height_label, current_block_height_text);
    lv_obj_set_style_text_color(block_height_label, COLOR_TEXT_PRIMARY, 0);
    lv_obj_set_style_text_font(block_height_label, &montserrat_140, 0);
    lv_obj_set_style_text_letter_space(block_height_label, 15, 0);
    lv_obj_set_style_text_align(block_height_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(block_height_label, glass ? 680 : SCREEN_WIDTH - 40);
    lv_obj_align(block_height_label, LV_ALIGN_CENTER, 0, glass ? 18 : -46);

    /* The two countdowns hang off the screen, not the pane, so they clear the
     * height figure in both skins. */
    block_build_network_row(block_screen, glass);

    if (glass)
    {
        apply_cached_block_height();
        glass_screen_ready(block_screen);
        return;
    }

    lv_obj_t *bottom_nav = lv_obj_create(block_screen);
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

    create_bottom_nav_btn(bottom_nav, LV_SYMBOL_HOME, block_home_clicked, false);
    create_bottom_nav_btn_img(bottom_nav, &cube_solid_full, NULL, true);
    create_bottom_nav_btn_img(bottom_nav, &cubes_solid_full, block_mempool_clicked, false);
    create_bottom_nav_btn_img(bottom_nav, &clock_solid_full, block_clock_clicked, false);
    create_bottom_nav_btn(bottom_nav, "$", block_price_clicked, false);
    create_bottom_nav_btn(bottom_nav, "%", block_odds_clicked, false);
    create_bottom_nav_btn(bottom_nav, LV_SYMBOL_WIFI, block_wifi_clicked, false);
    create_bottom_nav_btn(bottom_nav, LV_SYMBOL_SETTINGS, block_settings_clicked, false);
    create_bottom_nav_btn(bottom_nav, LV_SYMBOL_EYE_OPEN, block_night_clicked, false);

    apply_cached_block_height();
}

void block_screen_destroy(void)
{
    if (block_screen)
    {
        /* Kill the timer before the labels it writes to go away. */
        if (block_network_timer)
        {
            lv_timer_del(block_network_timer);
            block_network_timer = NULL;
        }
        glass_screen_detach(block_screen);
        lv_obj_del(block_screen);
        block_screen = NULL;
        block_height_label = NULL;
        block_title_label = NULL;
        block_halving_value = NULL;
        block_retarget_value = NULL;
    }
}

lv_obj_t *block_get_screen(void)
{
    return block_screen;
}

const char *block_get_height_text(void)
{
    return current_block_height_text;
}

void block_update_height(const char *height)
{
    if (!height)
    {
        return;
    }

    long parsed_height = strtol(height, NULL, 10);
    if (parsed_height > 0)
    {
        parsed_height -= 1;
    }
    snprintf(current_block_height_text, sizeof(current_block_height_text), "%ld", parsed_height);

    if (block_height_label)
    {
        lv_label_set_text(block_height_label, current_block_height_text);
    }
}

static void apply_cached_block_height(void)
{
    if (block_height_label)
    {
        lv_label_set_text(block_height_label, current_block_height_text);
    }
}

/* One of the two countdown cells: a caption over a value. */
static lv_obj_t *block_network_cell(lv_obj_t *host, const char *caption,
                                    int x, int y, int w, int h, bool glass,
                                    lv_obj_t **out_value)
{
    lv_obj_t *cell;
    if (glass)
    {
        cell = glass_pane(host, w, h, 20);
    }
    else
    {
        cell = lv_obj_create(host);
        lv_obj_set_size(cell, w, h);
        lv_obj_set_style_bg_color(cell, COLOR_CARD_BG, 0);
        lv_obj_set_style_bg_opa(cell, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(cell, 2, 0);
        lv_obj_set_style_border_color(cell, COLOR_BORDER, 0);
        lv_obj_set_style_radius(cell, 12, 0);
        lv_obj_set_style_shadow_width(cell, 0, 0);
        /* A stock lv_obj carries default padding, which in a cell this short
         * pushes the two labels onto each other. Place them by hand instead. */
        lv_obj_set_style_pad_all(cell, 0, 0);
        lv_obj_clear_flag(cell, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_scrollbar_mode(cell, LV_SCROLLBAR_MODE_OFF);
    }
    lv_obj_align(cell, LV_ALIGN_TOP_LEFT, x, y);

    lv_obj_t *cap = lv_label_create(cell);
    lv_label_set_text(cap, caption);
    lv_obj_set_style_text_color(cap, COLOR_TEXT_SECONDARY, 0);
    lv_obj_set_style_text_font(cap, &lv_font_montserrat_14, 0);
    lv_obj_align(cap, LV_ALIGN_TOP_MID, 0, 9);

    lv_obj_t *val = lv_label_create(cell);
    lv_label_set_text(val, "--");
    lv_obj_set_style_text_color(val, COLOR_TEXT_PRIMARY, 0);
    lv_obj_set_style_text_font(val, &lv_font_montserrat_22, 0);
    lv_obj_align(val, LV_ALIGN_TOP_MID, 0, 30);

    *out_value = val;
    return cell;
}

static void block_network_timer_cb(lv_timer_t *t)
{
    (void)t;
    block_refresh_network();
}

static void block_build_network_row(lv_obj_t *host, bool glass)
{
    const int cell_w = 360;
    const int cell_h = 68;
    const int gap    = 20;
    const int x      = (SCREEN_WIDTH - (cell_w * 2 + gap)) / 2;
    const int y      = 332;

    block_network_cell(host, "HALVING IN",    x,                  y, cell_w, cell_h, glass, &block_halving_value);
    block_network_cell(host, "NEXT RETARGET", x + cell_w + gap,   y, cell_w, cell_h, glass, &block_retarget_value);

    /* The snapshot lands on the chain task's schedule, so poll it rather than
     * reaching into chain.c for a callback. A minute is far finer than the
     * five-minute refresh and costs two label writes. */
    block_network_timer = lv_timer_create(block_network_timer_cb, 5000, NULL);
    block_refresh_network();
}

static void block_refresh_network(void)
{
    const chain_data_t *d = chain_data();
    char buf[64];
    char grouped[24];

    if (block_halving_value)
    {
        if (d->blocks_to_halving > 0)
        {
            chain_fmt_grouped(d->blocks_to_halving, grouped, sizeof(grouped));
            snprintf(buf, sizeof(buf), "%s blocks - %.0f days",
                     grouped, d->days_to_halving);
        }
        else
        {
            snprintf(buf, sizeof(buf), "--");
        }
        lv_label_set_text(block_halving_value, buf);
    }

    if (block_retarget_value)
    {
        if (d->valid && d->retarget_blocks_left > 0)
        {
            chain_fmt_grouped(d->retarget_blocks_left, grouped, sizeof(grouped));
            snprintf(buf, sizeof(buf), "%+.2f%% in %s", d->retarget_change_pct, grouped);
        }
        else
        {
            snprintf(buf, sizeof(buf), "--");
        }
        lv_label_set_text(block_retarget_value, buf);
    }
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

void block_home_clicked(lv_event_t *e)
{
    home_screen_create();
    lv_scr_load(home_get_screen());
    block_screen_destroy();
}

void block_clock_clicked(lv_event_t *e)
{
    clock_screen_create();
    lv_scr_load(clock_get_screen());
    block_screen_destroy();
}

void block_mempool_clicked(lv_event_t *e)
{
    mempool_screen_create();
    lv_scr_load(mempool_get_screen());
    block_screen_destroy();
}

void block_price_clicked(lv_event_t *e)
{
    price_screen_create();
    lv_scr_load(price_get_screen());
    block_screen_destroy();
}

void block_wifi_clicked(lv_event_t *e)
{
    wifi_screen_create();
    lv_scr_load(wifi_get_screen());
    block_screen_destroy();
}

void block_settings_clicked(lv_event_t *e)
{
    settings_screen_create();
    lv_scr_load(settings_get_screen());
    block_screen_destroy();
}

void block_night_clicked(lv_event_t *e)
{
    night_screen_create();
    lv_scr_load(night_get_screen());
    block_screen_destroy();
}

void block_odds_clicked(lv_event_t *e)
{
    odds_screen_create();
    lv_scr_load(odds_get_screen());
    block_screen_destroy();
}
