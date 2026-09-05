#include "odds.h"
#include "payout.h"

#include "chain.h"
#include "home.h"
#include "block.h"
#include "clock.h"
#include "price.h"
#include "mempool.h"
#include "wifi.h"
#include "settings.h"
#include "night.h"
#include "glass.h"
#include "custom_fonts.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static lv_obj_t *odds_screen = NULL;
static lv_obj_t *odds_hero_label = NULL;
static lv_obj_t *odds_hero_caption = NULL;
static lv_obj_t *odds_subtitle_label = NULL;
static lv_obj_t *odds_stat_value[3] = { NULL, NULL, NULL };
static lv_obj_t *odds_stat_caption[3] = { NULL, NULL, NULL };
static lv_timer_t *odds_timer = NULL;
static bool odds_show_hashrate = false;

static lv_obj_t *create_bottom_nav_btn(lv_obj_t *parent, const char *symbol, lv_event_cb_t event_cb, bool active);
static lv_obj_t *create_bottom_nav_btn_img(lv_obj_t *parent, const lv_img_dsc_t *img_dsc, lv_event_cb_t event_cb, bool active);

/* An expected wait, in whatever unit keeps it readable. Solo on a Bitaxe this
 * is nearly always years, but the same screen has to stay sane for someone
 * pointing a shelf of miners at it. */
static void fmt_wait(double seconds, char *buf, size_t n)
{
    if (!isfinite(seconds) || seconds <= 0.0)
    {
        snprintf(buf, n, "--");
        return;
    }

    const double minutes = seconds / 60.0;
    const double hours   = minutes / 60.0;
    const double days    = hours / 24.0;
    const double years   = seconds / 31556952.0;

    if (minutes < 90.0)
    {
        snprintf(buf, n, "%.0f min", minutes);
    }
    else if (hours < 48.0)
    {
        snprintf(buf, n, "%.1f hrs", hours);
    }
    else if (days < 90.0)
    {
        snprintf(buf, n, "%.0f days", days);
    }
    else if (years < 1.0)
    {
        snprintf(buf, n, "%.1f mo", days / 30.44);
    }
    else if (years < 1e6)
    {
        char grouped[24];
        chain_fmt_grouped((long)(years + 0.5), grouped, sizeof(grouped));
        snprintf(buf, n, "%s yrs", grouped);
    }
    else
    {
        /* Past a million years the digits stop meaning anything; a magnitude
         * is the honest presentation. */
        char compact[16];
        chain_fmt_compact(years, compact, sizeof(compact));
        snprintf(buf, n, "%s yrs", compact);
    }
}

/* The miner's hashrate as BAP last reported it, in GH/s. 0 when nothing has
 * arrived yet, which every caller treats as "unknown" rather than "idle". */
static double odds_hashrate_ghs(void)
{
    const home_stats_t *s = home_stats();
    if (!s || !s->hashrate || s->hashrate[0] == 0)
    {
        return 0.0;
    }
    return strtod(s->hashrate, NULL);
}

static void odds_set(lv_obj_t *label, const char *text)
{
    if (label)
    {
        lv_label_set_text(label, text);
    }
}

void odds_refresh(void)
{
    if (!odds_screen)
    {
        return;
    }

    const chain_data_t *d = chain_data();
    const double ghs = odds_hashrate_ghs();

    char buf[64];

    /* Subtitle: what the two inputs currently are, so a wrong-looking number
     * can be traced to a stale hashrate or a chain fetch that never landed. */
    if (ghs > 0.0)
    {
        snprintf(buf, sizeof(buf), "%s",
                 d->valid ? chain_source_name(chain_get_source()) : "waiting for network");
    }
    else
    {
        snprintf(buf, sizeof(buf), "waiting for miner");
    }
    odds_set(odds_subtitle_label, buf);

    double expected = 0.0, per_day = 0.0, per_year = 0.0;
    if (chain_solo_odds(ghs, &expected, &per_day, &per_year))
    {
        char compact[16];
        chain_fmt_compact(1.0 / per_day, compact, sizeof(compact));
        snprintf(buf, sizeof(buf), "1 in %s", compact);
        odds_set(odds_hero_label, buf);
        odds_set(odds_hero_caption, "CHANCE OF A BLOCK, PER DAY");

        fmt_wait(expected, buf, sizeof(buf));
        odds_set(odds_stat_value[0], buf);

        if (odds_show_hashrate)
        {
            snprintf(buf, sizeof(buf), "%.2f TH/s", ghs / 1000.0);
            odds_set(odds_stat_caption[1], "MINER HASHRATE  -  TAP");
        }
        else
        {
            chain_fmt_compact(d->difficulty, buf, sizeof(buf));
            odds_set(odds_stat_caption[1], "NETWORK DIFFICULTY  -  TAP");
        }
        odds_set(odds_stat_value[1], buf);

        /* Third cell: what the same hashrate would be worth on a pool. It is
         * the honest counterweight to the odds, and it is the one figure only
         * bitview can answer, so say so plainly when it cannot. */
        double sats = 0.0;
        if (chain_expected_sats_per_day(ghs, &sats))
        {
            snprintf(buf, sizeof(buf), "%.0f sats", sats);
            odds_set(odds_stat_value[2], buf);
            odds_set(odds_stat_caption[2], "IF POOLED, PER DAY");
        }
        else
        {
            odds_set(odds_stat_value[2], "--");
            odds_set(odds_stat_caption[2], "POOLED: BITVIEW ONLY");
        }
    }
    else
    {
        odds_set(odds_hero_label, "1 in --");
        odds_set(odds_hero_caption, ghs > 0.0 ? "WAITING FOR DIFFICULTY"
                                              : "WAITING FOR HASHRATE");
        odds_set(odds_stat_value[0], "--");
        odds_set(odds_stat_value[1], "--");
        odds_set(odds_stat_caption[1], odds_show_hashrate
                                         ? "MINER HASHRATE  -  TAP"
                                         : "NETWORK DIFFICULTY  -  TAP");
        odds_set(odds_stat_value[2], "--");
    }
}

static void odds_timer_cb(lv_timer_t *t)
{
    (void)t;
    odds_refresh();
}

static void odds_network_card_clicked(lv_event_t *e)
{
    (void)e;
    odds_show_hashrate = !odds_show_hashrate;
    odds_refresh();
}

/* One statistic: a value over a caption, on a card in the classic skin and on
 * a frosted pane under Glass. */
static void odds_build_stat(lv_obj_t *parent, int index, const char *caption,
                            int x, int y, int w, int h, bool glass)
{
    lv_obj_t *card;
    if (glass)
    {
        card = glass_pane(parent, w, h, 22);
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
        lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_scrollbar_mode(card, LV_SCROLLBAR_MODE_OFF);
    }
    lv_obj_align(card, LV_ALIGN_TOP_LEFT, x, y);

    if (index == 1)
    {
        lv_obj_add_flag(card, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_bg_color(card, COLOR_ACCENT, LV_STATE_PRESSED);
        lv_obj_set_style_bg_opa(card, LV_OPA_20, LV_STATE_PRESSED);
        lv_obj_add_event_cb(card, odds_network_card_clicked, LV_EVENT_CLICKED, NULL);
    }

    odds_stat_value[index] = lv_label_create(card);
    lv_label_set_text(odds_stat_value[index], "--");
    lv_obj_set_style_text_color(odds_stat_value[index], COLOR_TEXT_PRIMARY, 0);
    lv_obj_set_style_text_font(odds_stat_value[index], &lv_font_montserrat_32, 0);
    lv_obj_align(odds_stat_value[index], LV_ALIGN_CENTER, 0, -12);
    lv_obj_clear_flag(odds_stat_value[index], LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);

    odds_stat_caption[index] = lv_label_create(card);
    lv_label_set_text(odds_stat_caption[index], caption);
    lv_obj_set_style_text_color(odds_stat_caption[index], COLOR_TEXT_SECONDARY, 0);
    lv_obj_set_style_text_font(odds_stat_caption[index], &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_align(odds_stat_caption[index], LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(odds_stat_caption[index], w - 20);
    lv_obj_align(odds_stat_caption[index], LV_ALIGN_CENTER, 0, 24);
    lv_obj_clear_flag(odds_stat_caption[index], LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
}

void odds_screen_create(void)
{
    if (odds_screen != NULL)
    {
        return;
    }

    const bool glass = glass_active();
    lv_obj_t *parent;

    if (glass)
    {
        odds_screen = glass_screen_create(GLASS_SCREEN_ODDS, false);
        parent = odds_screen;
    }
    else
    {
        odds_screen = lv_obj_create(NULL);
        lv_obj_set_style_bg_color(odds_screen, COLOR_BACKGROUND, 0);
        lv_obj_set_style_bg_opa(odds_screen, LV_OPA_COVER, 0);
        lv_obj_clear_flag(odds_screen, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_scrollbar_mode(odds_screen, LV_SCROLLBAR_MODE_OFF);
        parent = odds_screen;
    }

    lv_obj_t *title = lv_label_create(parent);
    lv_label_set_text(title, "What are the odds?");
    lv_obj_set_style_text_color(title, COLOR_TEXT_PRIMARY, 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_28, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 14);
    if (glass) glass_pill_label(title, false);

    odds_subtitle_label = lv_label_create(parent);
    lv_label_set_text(odds_subtitle_label, "waiting for miner");
    lv_obj_set_style_text_color(odds_subtitle_label, COLOR_TEXT_SECONDARY, 0);
    lv_obj_set_style_text_font(odds_subtitle_label, &lv_font_montserrat_14, 0);
    lv_obj_align(odds_subtitle_label, LV_ALIGN_TOP_MID, 0, 45);
    if (glass) glass_pill_label(odds_subtitle_label, false);

    /* Hero: the one number the screen exists for. */
    const int hero_w = 744;
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
        lv_obj_clear_flag(hero, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_scrollbar_mode(hero, LV_SCROLLBAR_MODE_OFF);
    }
    lv_obj_align(hero, LV_ALIGN_TOP_MID, 0, 76);

    odds_hero_label = lv_label_create(hero);
    lv_label_set_text(odds_hero_label, "1 in --");
    lv_obj_set_style_text_color(odds_hero_label, COLOR_ACCENT, 0);
    lv_obj_set_style_text_font(odds_hero_label, &lv_font_montserrat_48, 0);
    lv_obj_align(odds_hero_label, LV_ALIGN_CENTER, 0, -16);

    odds_hero_caption = lv_label_create(hero);
    lv_label_set_text(odds_hero_caption, "CHANCE OF A BLOCK, PER DAY");
    lv_obj_set_style_text_color(odds_hero_caption, COLOR_TEXT_SECONDARY, 0);
    lv_obj_set_style_text_font(odds_hero_caption, &lv_font_montserrat_16, 0);
    lv_obj_align(odds_hero_caption, LV_ALIGN_CENTER, 0, 34);

    const int card_w = 240;
    const int card_h = 142;
    const int gap    = 10;
    const int row_x  = (SCREEN_WIDTH - (card_w * 3 + gap * 2)) / 2;
    const int row_y  = 242;

    odds_build_stat(parent, 0, "EXPECTED WAIT",     row_x,                       row_y, card_w, card_h, glass);
    odds_build_stat(parent, 1, "NETWORK DIFFICULTY", row_x + card_w + gap,        row_y, card_w, card_h, glass);
    odds_build_stat(parent, 2, "IF POOLED, PER DAY", row_x + (card_w + gap) * 2,  row_y, card_w, card_h, glass);

    lv_obj_t *source = lv_label_create(parent);
    lv_label_set_text(source, "bitview.space");
    lv_obj_set_style_text_color(source, COLOR_TEXT_SECONDARY, 0);
    lv_obj_set_style_text_font(source, &lv_font_montserrat_14, 0);
    lv_obj_align(source, LV_ALIGN_BOTTOM_MID, 0, -10);
    if (glass) glass_pill_label(source, false);

    /* The miner's hashrate moves between BAP frames and the chain snapshot
     * lands on its own schedule, so repaint on a timer rather than wiring a
     * callback into both. */
    odds_timer = lv_timer_create(odds_timer_cb, 2000, NULL);
    odds_refresh();

    if (glass)
    {
        glass_screen_ready(odds_screen);
        return;
    }

    lv_obj_t *bottom_nav = lv_obj_create(odds_screen);
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

    create_bottom_nav_btn(bottom_nav, LV_SYMBOL_HOME, odds_home_clicked, false);
    create_bottom_nav_btn_img(bottom_nav, &cube_solid_full, odds_block_clicked, false);
    create_bottom_nav_btn_img(bottom_nav, &cubes_solid_full, odds_mempool_clicked, false);
    create_bottom_nav_btn_img(bottom_nav, &clock_solid_full, odds_clock_clicked, false);
    create_bottom_nav_btn(bottom_nav, "$", odds_price_clicked, false);
    create_bottom_nav_btn(bottom_nav, "%", NULL, true);
    create_bottom_nav_btn(bottom_nav, LV_SYMBOL_DOWNLOAD, odds_payout_clicked, false);
    create_bottom_nav_btn(bottom_nav, LV_SYMBOL_WIFI, odds_wifi_clicked, false);
    create_bottom_nav_btn(bottom_nav, LV_SYMBOL_SETTINGS, odds_settings_clicked, false);
    create_bottom_nav_btn(bottom_nav, LV_SYMBOL_EYE_OPEN, odds_night_clicked, false);
}

void odds_screen_destroy(void)
{
    if (!odds_screen)
    {
        return;
    }

    /* Kill the timer before the labels it writes to go away. */
    if (odds_timer)
    {
        lv_timer_del(odds_timer);
        odds_timer = NULL;
    }

    glass_screen_detach(odds_screen);
    lv_obj_del(odds_screen);
    odds_screen = NULL;
    odds_hero_label = NULL;
    odds_hero_caption = NULL;
    odds_subtitle_label = NULL;
    for (int i = 0; i < 3; i++)
    {
        odds_stat_value[i] = NULL;
        odds_stat_caption[i] = NULL;
    }
}

lv_obj_t *odds_get_screen(void)
{
    return odds_screen;
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

void odds_home_clicked(lv_event_t *e)
{
    home_screen_create();
    lv_scr_load(home_get_screen());
    odds_screen_destroy();
}

void odds_block_clicked(lv_event_t *e)
{
    block_screen_create();
    lv_scr_load(block_get_screen());
    odds_screen_destroy();
}

void odds_mempool_clicked(lv_event_t *e)
{
    mempool_screen_create();
    lv_scr_load(mempool_get_screen());
    odds_screen_destroy();
}

void odds_clock_clicked(lv_event_t *e)
{
    clock_screen_create();
    lv_scr_load(clock_get_screen());
    odds_screen_destroy();
}

void odds_price_clicked(lv_event_t *e)
{
    price_screen_create();
    lv_scr_load(price_get_screen());
    odds_screen_destroy();
}

void odds_wifi_clicked(lv_event_t *e)
{
    wifi_screen_create();
    lv_scr_load(wifi_get_screen());
    odds_screen_destroy();
}

void odds_settings_clicked(lv_event_t *e)
{
    settings_screen_create();
    lv_scr_load(settings_get_screen());
    odds_screen_destroy();
}

void odds_night_clicked(lv_event_t *e)
{
    night_screen_create();
    lv_scr_load(night_get_screen());
    odds_screen_destroy();
}

void odds_payout_clicked(lv_event_t *e)
{
    payout_screen_create();
    lv_scr_load(payout_get_screen());
    odds_screen_destroy();
}
