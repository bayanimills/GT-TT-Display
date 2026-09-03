#include "payout.h"

#include "chain.h"
#include "home.h"
#include "block.h"
#include "clock.h"
#include "price.h"
#include "mempool.h"
#include "odds.h"
#include "wifi.h"
#include "settings.h"
#include "night.h"
#include "glass.h"
#include "custom_fonts.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static lv_obj_t *payout_screen = NULL;
static lv_obj_t *payout_hero_label = NULL;
static lv_obj_t *payout_hero_caption = NULL;
static lv_obj_t *payout_address_label = NULL;
static lv_obj_t *payout_stat_value[2] = { NULL, NULL };
static lv_timer_t *payout_timer = NULL;

static lv_obj_t *create_bottom_nav_btn(lv_obj_t *parent, const char *symbol, lv_event_cb_t event_cb, bool active);
static lv_obj_t *create_bottom_nav_btn_img(lv_obj_t *parent, const lv_img_dsc_t *img_dsc, lv_event_cb_t event_cb, bool active);
static void payout_refresh(void);

/* Sats are the honest unit here: a solo payout is either zero or three and a
 * bit BTC, and a balance of a few thousand sats reads as 0.00003 in BTC. */
static void fmt_sats(int64_t sats, char *buf, size_t n)
{
    if (sats == 0)
    {
        snprintf(buf, n, "0");
        return;
    }
    if (sats >= 100000000 || sats <= -100000000)
    {
        snprintf(buf, n, "%.4f BTC", (double)sats / 100000000.0);
        return;
    }
    char grouped[32];
    chain_fmt_grouped((long)sats, grouped, sizeof(grouped));
    snprintf(buf, n, "%s sats", grouped);
}

/* An address is too long for the panel and the middle carries no meaning to a
 * reader, so show the ends: enough to check it against a wallet at a glance. */
static void fmt_address(const char *addr, char *buf, size_t n)
{
    const size_t len = strlen(addr);
    if (len <= 20)
    {
        snprintf(buf, n, "%s", addr);
        return;
    }
    snprintf(buf, n, "%.10s...%s", addr, addr + len - 8);
}

static void payout_set(lv_obj_t *label, const char *text)
{
    if (label)
    {
        lv_label_set_text(label, text);
    }
}

static void payout_refresh(void)
{
    if (!payout_screen)
    {
        return;
    }

    const chain_address_t *a = chain_address();
    char buf[80];

    if (!a->watching)
    {
        /* Nothing to watch means the miner has not reported a pool user that
         * looks like an address: a pooled setup, or a username login. */
        payout_set(payout_hero_label, "--");
        payout_set(payout_hero_caption, "NO PAYOUT ADDRESS IN THE POOL USER");
        payout_set(payout_address_label, "Solo pools use the address as the user name");
        payout_set(payout_stat_value[0], "--");
        payout_set(payout_stat_value[1], "--");
        return;
    }

    fmt_address(a->address, buf, sizeof(buf));
    payout_set(payout_address_label, buf);

    if (!a->valid)
    {
        payout_set(payout_hero_label, "--");
        payout_set(payout_hero_caption, "LOOKING UP");
        payout_set(payout_stat_value[0], "--");
        payout_set(payout_stat_value[1], "--");
        return;
    }

    fmt_sats(a->confirmed_sats, buf, sizeof(buf));
    payout_set(payout_hero_label, buf);
    payout_set(payout_hero_caption, "CONFIRMED BALANCE");

    if (a->pending_sats != 0)
    {
        char amount[48];
        fmt_sats(a->pending_sats, amount, sizeof(amount));
        snprintf(buf, sizeof(buf), "%s%s", a->pending_sats > 0 ? "+" : "", amount);
    }
    else
    {
        snprintf(buf, sizeof(buf), "none");
    }
    payout_set(payout_stat_value[0], buf);

    char grouped[24];
    chain_fmt_grouped(a->tx_count, grouped, sizeof(grouped));
    payout_set(payout_stat_value[1], grouped);
}

static void payout_timer_cb(lv_timer_t *t)
{
    (void)t;
    payout_refresh();
}

static void payout_build_stat(lv_obj_t *parent, int index, const char *caption,
                              int x, int y, int w, int h, bool glass)
{
    lv_obj_t *card;
    if (glass)
    {
        card = glass_pane(parent, w, h, 20);
    }
    else
    {
        card = lv_obj_create(parent);
        lv_obj_set_size(card, w, h);
        lv_obj_set_style_bg_color(card, COLOR_CARD_BG, 0);
        lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(card, 2, 0);
        lv_obj_set_style_border_color(card, COLOR_BORDER, 0);
        lv_obj_set_style_radius(card, 14, 0);
        lv_obj_set_style_shadow_width(card, 0, 0);
        lv_obj_set_style_pad_all(card, 0, 0);
        lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    }
    lv_obj_align(card, LV_ALIGN_TOP_LEFT, x, y);

    lv_obj_t *cap = lv_label_create(card);
    lv_label_set_text(cap, caption);
    lv_obj_set_style_text_color(cap, COLOR_TEXT_SECONDARY, 0);
    lv_obj_set_style_text_font(cap, &lv_font_montserrat_14, 0);
    lv_obj_align(cap, LV_ALIGN_TOP_MID, 0, 10);

    payout_stat_value[index] = lv_label_create(card);
    lv_label_set_text(payout_stat_value[index], "--");
    lv_obj_set_style_text_color(payout_stat_value[index], COLOR_TEXT_PRIMARY, 0);
    lv_obj_set_style_text_font(payout_stat_value[index], &lv_font_montserrat_28, 0);
    lv_obj_align(payout_stat_value[index], LV_ALIGN_TOP_MID, 0, 34);
}

void payout_screen_create(void)
{
    if (payout_screen != NULL)
    {
        return;
    }

    const bool glass = glass_active();
    lv_obj_t *parent;

    if (glass)
    {
        payout_screen = glass_screen_create(GLASS_SCREEN_PAYOUT, false);
        parent = payout_screen;
    }
    else
    {
        payout_screen = lv_obj_create(NULL);
        lv_obj_set_style_bg_color(payout_screen, COLOR_BACKGROUND, 0);
        lv_obj_set_style_bg_opa(payout_screen, LV_OPA_COVER, 0);
        lv_obj_clear_flag(payout_screen, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_scrollbar_mode(payout_screen, LV_SCROLLBAR_MODE_OFF);
        parent = payout_screen;
    }

    lv_obj_t *title = lv_label_create(parent);
    lv_label_set_text(title, "PAYOUT");
    lv_obj_set_style_text_color(title, COLOR_TEXT_PRIMARY, 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 16);
    if (glass) glass_pill_label(title, false);

    payout_address_label = lv_label_create(parent);
    lv_label_set_text(payout_address_label, "waiting for the pool user");
    lv_obj_set_style_text_color(payout_address_label, COLOR_TEXT_SECONDARY, 0);
    lv_obj_set_style_text_font(payout_address_label, &lv_font_montserrat_16, 0);
    lv_obj_align(payout_address_label, LV_ALIGN_TOP_MID, 0, 46);
    if (glass) glass_pill_label(payout_address_label, false);

    const int hero_w = 740;
    const int hero_h = 150;
    lv_obj_t *hero;
    if (glass)
    {
        hero = glass_pane(parent, hero_w, hero_h, 28);
    }
    else
    {
        hero = lv_obj_create(parent);
        lv_obj_set_size(hero, hero_w, hero_h);
        lv_obj_set_style_bg_color(hero, COLOR_CARD_BG, 0);
        lv_obj_set_style_bg_opa(hero, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(hero, 2, 0);
        lv_obj_set_style_border_color(hero, COLOR_ACCENT, 0);
        lv_obj_set_style_radius(hero, 18, 0);
        lv_obj_set_style_shadow_width(hero, 0, 0);
        lv_obj_set_style_pad_all(hero, 0, 0);
        lv_obj_clear_flag(hero, LV_OBJ_FLAG_SCROLLABLE);
    }
    lv_obj_align(hero, LV_ALIGN_TOP_MID, 0, 78);

    payout_hero_label = lv_label_create(hero);
    lv_label_set_text(payout_hero_label, "--");
    lv_obj_set_style_text_color(payout_hero_label, COLOR_ACCENT, 0);
    lv_obj_set_style_text_font(payout_hero_label, &lv_font_montserrat_48, 0);
    lv_obj_align(payout_hero_label, LV_ALIGN_CENTER, 0, -16);

    payout_hero_caption = lv_label_create(hero);
    lv_label_set_text(payout_hero_caption, "CONFIRMED BALANCE");
    lv_obj_set_style_text_color(payout_hero_caption, COLOR_TEXT_SECONDARY, 0);
    lv_obj_set_style_text_font(payout_hero_caption, &lv_font_montserrat_16, 0);
    lv_obj_align(payout_hero_caption, LV_ALIGN_CENTER, 0, 34);

    const int card_w = 364;
    const int card_h = 108;
    const int gap    = 12;
    const int row_x  = (SCREEN_WIDTH - (card_w * 2 + gap)) / 2;
    const int row_y  = 250;

    payout_build_stat(parent, 0, "UNCONFIRMED", row_x, row_y, card_w, card_h, glass);
    payout_build_stat(parent, 1, "TRANSACTIONS", row_x + card_w + gap, row_y, card_w, card_h, glass);

    payout_timer = lv_timer_create(payout_timer_cb, 3000, NULL);
    payout_refresh();

    if (glass)
    {
        glass_screen_ready(payout_screen);
        return;
    }

    lv_obj_t *bottom_nav = lv_obj_create(payout_screen);
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

    create_bottom_nav_btn(bottom_nav, LV_SYMBOL_HOME, payout_home_clicked, false);
    create_bottom_nav_btn_img(bottom_nav, &cube_solid_full, payout_block_clicked, false);
    create_bottom_nav_btn_img(bottom_nav, &cubes_solid_full, payout_mempool_clicked, false);
    create_bottom_nav_btn_img(bottom_nav, &clock_solid_full, payout_clock_clicked, false);
    create_bottom_nav_btn(bottom_nav, "$", payout_price_clicked, false);
    create_bottom_nav_btn(bottom_nav, "%", payout_odds_clicked, false);
    create_bottom_nav_btn(bottom_nav, LV_SYMBOL_DOWNLOAD, NULL, true);
    create_bottom_nav_btn(bottom_nav, LV_SYMBOL_WIFI, payout_wifi_clicked, false);
    create_bottom_nav_btn(bottom_nav, LV_SYMBOL_SETTINGS, payout_settings_clicked, false);
    create_bottom_nav_btn(bottom_nav, LV_SYMBOL_EYE_OPEN, payout_night_clicked, false);
}

void payout_screen_destroy(void)
{
    if (!payout_screen)
    {
        return;
    }

    if (payout_timer)
    {
        lv_timer_del(payout_timer);
        payout_timer = NULL;
    }

    glass_screen_detach(payout_screen);
    lv_obj_del(payout_screen);
    payout_screen = NULL;
    payout_hero_label = NULL;
    payout_hero_caption = NULL;
    payout_address_label = NULL;
    payout_stat_value[0] = NULL;
    payout_stat_value[1] = NULL;
}

lv_obj_t *payout_get_screen(void)
{
    return payout_screen;
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
    lv_obj_set_style_text_color(label, active ? theme_ink_on(COLOR_ACCENT) : COLOR_ACCENT, 0);
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
    lv_obj_set_style_img_recolor(img, active ? theme_ink_on(COLOR_ACCENT) : COLOR_ACCENT, 0);
    lv_obj_set_style_img_recolor_opa(img, LV_OPA_COVER, 0);
    lv_obj_center(img);

    if (event_cb)
    {
        lv_obj_add_event_cb(btn, event_cb, LV_EVENT_CLICKED, NULL);
    }

    return btn;
}

void payout_home_clicked(lv_event_t *e)
{
    home_screen_create();
    lv_scr_load(home_get_screen());
    payout_screen_destroy();
}

void payout_block_clicked(lv_event_t *e)
{
    block_screen_create();
    lv_scr_load(block_get_screen());
    payout_screen_destroy();
}

void payout_mempool_clicked(lv_event_t *e)
{
    mempool_screen_create();
    lv_scr_load(mempool_get_screen());
    payout_screen_destroy();
}

void payout_clock_clicked(lv_event_t *e)
{
    clock_screen_create();
    lv_scr_load(clock_get_screen());
    payout_screen_destroy();
}

void payout_price_clicked(lv_event_t *e)
{
    price_screen_create();
    lv_scr_load(price_get_screen());
    payout_screen_destroy();
}

void payout_odds_clicked(lv_event_t *e)
{
    odds_screen_create();
    lv_scr_load(odds_get_screen());
    payout_screen_destroy();
}

void payout_wifi_clicked(lv_event_t *e)
{
    wifi_screen_create();
    lv_scr_load(wifi_get_screen());
    payout_screen_destroy();
}

void payout_settings_clicked(lv_event_t *e)
{
    settings_screen_create();
    lv_scr_load(settings_get_screen());
    payout_screen_destroy();
}

void payout_night_clicked(lv_event_t *e)
{
    night_screen_create();
    lv_scr_load(night_get_screen());
    payout_screen_destroy();
}
