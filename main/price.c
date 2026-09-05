#include "price.h"
#include "payout.h"
#include "odds.h"
#include "chain.h"
#include "home.h"
#include "block.h"
#include "clock.h"
#include "mempool.h"
#include "wifi.h"
#include "settings.h"
#include "night.h"
#include "custom_fonts.h"
#include "glass.h"
#include "lvgl_port.h"
#include "esp_event.h"
#include "esp_http_client.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "esp_crt_bundle.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdlib.h>
#include <string.h>
#include "ota_update.h"

#define PRICE_HTTP_BUF_SIZE 512
#define PRICE_FETCH_INTERVAL_MS 60000

/* CoinGecko quotes several currencies in one response, so the selected one and
 * USD are asked for together: the pair gives both the figure to display and
 * the ratio every USD-denominated number elsewhere is converted through. That
 * is why there is no separate FX provider to go down. */
static void price_build_coingecko_url(char *out, size_t n)
{
    char lower[8];
    const char *code = chain_ccy_code(chain_get_ccy());
    size_t i = 0;
    for (; code[i] && i < sizeof(lower) - 1; i++)
    {
        lower[i] = (char)((code[i] >= 'A' && code[i] <= 'Z') ? code[i] + 32 : code[i]);
    }
    lower[i] = 0;

    snprintf(out, n,
             "https://api.coingecko.com/api/v3/simple/price?ids=bitcoin&vs_currencies=%s,usd",
             lower);
}

/* Coinbase quotes one currency per request, so the fallback can show the right
 * number but cannot establish a ratio. */
static void price_build_coinbase_url(char *out, size_t n)
{
    snprintf(out, n, "https://api.coinbase.com/v2/prices/BTC-%s/spot",
             chain_ccy_code(chain_get_ccy()));
}

static lv_obj_t *price_screen = NULL;
static lv_obj_t *price_value_cont = NULL;
static lv_obj_t *price_prefix_label = NULL;
static lv_obj_t *price_value_label = NULL;
static lv_obj_t *price_suffix_label = NULL;
static lv_obj_t *price_title_label = NULL;
static lv_obj_t *price_status_label = NULL;
static lv_obj_t *price_glass_card = NULL;
static lv_obj_t *price_glass_settings = NULL;
static lv_obj_t *price_ccy_buttons[CHAIN_CCY_COUNT] = { NULL };
static TaskHandle_t price_task_handle = NULL;
static bool price_netif_ready = false;

static lv_obj_t   *price_cagr_value[2] = { NULL, NULL };
static lv_obj_t   *price_cagr_caption[2] = { NULL, NULL };
static lv_timer_t *price_cagr_timer = NULL;

static char current_price_text[32] = "--";
static char current_price_status[24] = "LOADING...";

static lv_obj_t *create_bottom_nav_btn(lv_obj_t *parent, const char *symbol, lv_event_cb_t event_cb, bool active);
static lv_obj_t *create_bottom_nav_btn_img(lv_obj_t *parent, const lv_img_dsc_t *img_dsc, lv_event_cb_t event_cb, bool active);
static void apply_cached_price(void);
static void price_apply_value_label(void);
static void price_task(void *arg);
static bool price_fetch_once(void);
static bool price_fetch_from_url(const char *url);
static bool price_parse_coingecko(const char *json, double *out_price);
static bool price_parse_coinbase(const char *json, double *out_price);
static bool price_ensure_netif(void);
static bool price_wifi_connected(void);
static void format_price_with_commas(long long value, char *out, size_t out_size);
static void price_set_status(const char *status);
static void price_build_cagr_cards(lv_obj_t *parent, bool glass);
static void price_refresh_cagr(void);
static void price_build_glass_settings(void);
static void price_glass_open_settings(lv_event_t *e);
static void price_glass_close_settings(lv_event_t *e);

static char price_http_buf[PRICE_HTTP_BUF_SIZE];
static int price_http_len = 0;

/* Set when the currency changes, so the poll interval is cut short rather
 * than leaving the old currency's figure up for another minute. */
static volatile bool price_refetch_requested = false;

static void price_wait(uint32_t ms)
{
    const uint32_t slice = 500;
    for (uint32_t waited = 0; waited < ms; waited += slice)
    {
        if (price_refetch_requested)
        {
            return;
        }
        vTaskDelay(pdMS_TO_TICKS(slice));
    }
}

/* Called from the settings dropdown, on the LVGL task with the lock already
 * held. Relabels what is on screen and blanks the figure, because the old
 * number in the new currency's symbol would be a lie until the fetch lands. */
void price_currency_changed(void)
{
    const chain_ccy_t ccy = chain_get_ccy();

    strncpy(current_price_text, "--", sizeof(current_price_text) - 1);
    current_price_text[sizeof(current_price_text) - 1] = 0;

    if (price_title_label)
    {
        lv_label_set_text_fmt(price_title_label, "Bitcoin Exchange Rate (%s)", chain_ccy_code(ccy));
    }
    if (price_prefix_label)
    {
        lv_label_set_text(price_prefix_label, chain_ccy_prefix(ccy));
    }
    if (price_suffix_label)
    {
        lv_label_set_text(price_suffix_label,
                          ccy == CHAIN_CCY_USD ? "" : chain_ccy_code(ccy));
    }
    if (price_value_label)
    {
        price_apply_value_label();
    }
    price_set_status("LOADING...");

    price_refetch_requested = true;
}

static esp_err_t price_http_event_handler(esp_http_client_event_t *evt)
{
    if (evt->event_id == HTTP_EVENT_ON_DATA && evt->data && evt->data_len > 0)
    {
        int copy_len = evt->data_len;
        if (price_http_len + copy_len >= PRICE_HTTP_BUF_SIZE)
        {
            copy_len = PRICE_HTTP_BUF_SIZE - price_http_len - 1;
        }
        if (copy_len > 0)
        {
            memcpy(price_http_buf + price_http_len, evt->data, copy_len);
            price_http_len += copy_len;
            price_http_buf[price_http_len] = '\0';
        }
    }
    return ESP_OK;
}

#define SPARK_W 600
#define SPARK_H 64

static void price_cagr_timer_cb(lv_timer_t *t)
{
    (void) t;
    price_refresh_cagr();
}

static lv_obj_t *price_cagr_card(lv_obj_t *parent, int index, const char *caption,
                                 int x, bool glass)
{
    lv_obj_t *card = lv_obj_create(parent);
    lv_obj_set_size(card, 318, 88);
    lv_obj_set_pos(card, x, 198);
    lv_obj_set_style_radius(card, 18, 0);
    lv_obj_set_style_bg_color(card, glass ? lv_color_white() : COLOR_BACKGROUND, 0);
    lv_obj_set_style_bg_opa(card, glass ? LV_OPA_10 : LV_OPA_50, 0);
    lv_obj_set_style_border_color(card, glass ? lv_color_white() : COLOR_BORDER, 0);
    lv_obj_set_style_border_opa(card, glass ? LV_OPA_30 : LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_shadow_width(card, 0, 0);
    lv_obj_set_style_pad_all(card, 0, 0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

    price_cagr_caption[index] = lv_label_create(card);
    lv_label_set_text(price_cagr_caption[index], caption);
    lv_obj_set_style_text_color(price_cagr_caption[index], COLOR_TEXT_SECONDARY, 0);
    lv_obj_set_style_text_font(price_cagr_caption[index], &lv_font_montserrat_14, 0);
    lv_obj_align(price_cagr_caption[index], LV_ALIGN_TOP_MID, 0, 10);

    price_cagr_value[index] = lv_label_create(card);
    lv_label_set_text(price_cagr_value[index], "--");
    lv_obj_set_style_text_color(price_cagr_value[index], COLOR_ACCENT, 0);
    lv_obj_set_style_text_font(price_cagr_value[index], &lv_font_montserrat_32, 0);
    lv_obj_align(price_cagr_value[index], LV_ALIGN_BOTTOM_MID, 0, -10);
    return card;
}

static void price_build_cagr_cards(lv_obj_t *parent, bool glass)
{
    price_cagr_card(parent, 0, "BEST 4-6 YEAR CAGR", 44, glass);
    price_cagr_card(parent, 1, "BEST 7-10 YEAR CAGR", 382, glass);
    price_cagr_timer = lv_timer_create(price_cagr_timer_cb, 5000, NULL);
    price_refresh_cagr();
}

static void price_refresh_cagr(void)
{
    const chain_data_t *d = chain_data();
    if (!price_cagr_value[0] || !price_cagr_value[1]) return;
    if (!d->price_cagr_valid)
    {
        lv_label_set_text(price_cagr_value[0], "--");
        lv_label_set_text(price_cagr_value[1], "--");
        return;
    }
    /* LVGL's formatter intentionally omits floating-point support on-device;
     * format with libc first so live values never degrade to the literal
     * `f%` seen in the offline placeholder screenshot. */
    char value[32];
    snprintf(value, sizeof(value), "%.1f%%  -  %uY",
             d->price_cagr_short, (unsigned)d->price_cagr_short_years);
    lv_label_set_text(price_cagr_value[0], value);
    snprintf(value, sizeof(value), "%.1f%%  -  %uY",
             d->price_cagr_long, (unsigned)d->price_cagr_long_years);
    lv_label_set_text(price_cagr_value[1], value);
}

void price_screen_create(void)
{
    if (price_screen != NULL)
    {
        return;
    }

    const bool glass = glass_active();
    if (glass)
    {
        price_screen = glass_screen_create(GLASS_SCREEN_PRICE, false);
    }
    else
    {
        price_screen = lv_obj_create(NULL);
        lv_obj_set_style_bg_color(price_screen, COLOR_BACKGROUND, 0);
        lv_obj_set_style_bg_opa(price_screen, LV_OPA_COVER, 0);
        lv_obj_clear_flag(price_screen, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_scrollbar_mode(price_screen, LV_SCROLLBAR_MODE_OFF);
    }

    price_title_label = lv_label_create(price_screen);
    lv_label_set_text_fmt(price_title_label, "Bitcoin Exchange Rate (%s)",
                          chain_ccy_code(chain_get_ccy()));
    lv_obj_set_style_text_color(price_title_label, COLOR_TEXT_PRIMARY, 0);
    lv_obj_set_style_text_font(price_title_label, &lv_font_montserrat_24, 0);
    lv_obj_align(price_title_label, LV_ALIGN_TOP_MID, 0, 14);
    if (glass) glass_pill_label(price_title_label, false);

    price_status_label = lv_label_create(price_screen);
    lv_label_set_text(price_status_label, current_price_status);
    lv_obj_set_style_text_color(price_status_label, COLOR_TEXT_SECONDARY, 0);
    lv_obj_set_style_text_opa(price_status_label, (lv_opa_t)192, 0);
    lv_obj_set_style_text_font(price_status_label, &lv_font_montserrat_14, 0);
    lv_obj_align(price_status_label, LV_ALIGN_TOP_MID, 0, 45);
    if (glass) glass_pill_label(price_status_label, false);

    lv_obj_t *parent;
    if (glass)
    {
        parent = glass_pane(price_screen, 744, 306, 28);
    }
    else
    {
        parent = lv_obj_create(price_screen);
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
    if (glass)
    {
        price_glass_card = parent;
        lv_obj_add_flag(price_glass_card, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(price_glass_card, price_glass_open_settings,
                            LV_EVENT_CLICKED, NULL);
        lv_obj_t *hint = lv_label_create(price_glass_card);
        lv_label_set_text(hint, "Tap price to change currency");
        lv_obj_set_style_text_color(hint, COLOR_TEXT_SECONDARY, 0);
        lv_obj_set_style_text_font(hint, &lv_font_montserrat_14, 0);
        lv_obj_align(hint, LV_ALIGN_TOP_MID, 0, 14);
        lv_obj_clear_flag(hint, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    }

    price_value_cont = lv_obj_create(parent);
    lv_obj_set_size(price_value_cont, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(price_value_cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(price_value_cont, 0, 0);
    lv_obj_set_style_pad_all(price_value_cont, 0, 0);
    lv_obj_set_style_pad_column(price_value_cont, 10, 0);
    lv_obj_set_flex_flow(price_value_cont, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(price_value_cont, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_align(price_value_cont, LV_ALIGN_CENTER, 0, -38);
    if (glass)
    {
        lv_obj_clear_flag(price_value_cont, LV_OBJ_FLAG_CLICKABLE);
    }

    price_prefix_label = lv_label_create(price_value_cont);
    lv_label_set_text(price_prefix_label, chain_ccy_prefix(chain_get_ccy()));
    lv_obj_set_style_text_color(price_prefix_label, COLOR_TEXT_PRIMARY, 0);
    lv_obj_set_style_text_opa(price_prefix_label, (lv_opa_t)192, 0);
    lv_obj_set_style_text_font(price_prefix_label, &montserrat_140, 0);

    price_value_label = lv_label_create(price_value_cont);
    lv_obj_set_style_text_color(price_value_label, COLOR_TEXT_PRIMARY, 0);
    lv_obj_set_style_text_letter_space(price_value_label, 2, 0);
    price_apply_value_label();

    price_suffix_label = lv_label_create(price_value_cont);
    /* The suffix is in a font with an alphabet, so it carries the currency
     * for anything other than USD. That also covers GBP, EUR and JPY, whose
     * symbols the big face cannot draw at all. */
    lv_label_set_text(price_suffix_label,
                      chain_get_ccy() == CHAIN_CCY_USD ? "" : chain_ccy_code(chain_get_ccy()));
    lv_obj_set_style_text_color(price_suffix_label, COLOR_TEXT_PRIMARY, 0);
    lv_obj_set_style_text_opa(price_suffix_label, (lv_opa_t)192, 0);
    lv_obj_set_style_text_font(price_suffix_label, &lv_font_montserrat_48, 0);

    price_build_cagr_cards(parent, glass);

    if (glass)
    {
        price_build_glass_settings();
        apply_cached_price();
        /* One fetch loop for both skins and the home widget: started here or
         * by price_ensure_task(), never twice. */
        price_ensure_task();
        glass_screen_ready(price_screen);
        return;
    }

    lv_obj_t *bottom_nav = lv_obj_create(price_screen);
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

    create_bottom_nav_btn(bottom_nav, LV_SYMBOL_HOME, price_home_clicked, false);
    create_bottom_nav_btn_img(bottom_nav, &cube_solid_full, price_block_clicked, false);
    create_bottom_nav_btn_img(bottom_nav, &cubes_solid_full, price_mempool_clicked, false);
    create_bottom_nav_btn_img(bottom_nav, &clock_solid_full, price_clock_clicked, false);
    create_bottom_nav_btn(bottom_nav, "$", NULL, true);
    create_bottom_nav_btn(bottom_nav, "%", price_odds_clicked, false);
    create_bottom_nav_btn(bottom_nav, LV_SYMBOL_DOWNLOAD, price_payout_clicked, false);
    create_bottom_nav_btn(bottom_nav, LV_SYMBOL_WIFI, price_wifi_clicked, false);
    create_bottom_nav_btn(bottom_nav, LV_SYMBOL_SETTINGS, price_settings_clicked, false);
    create_bottom_nav_btn(bottom_nav, LV_SYMBOL_EYE_OPEN, price_night_clicked, false);

    apply_cached_price();

    if (price_task_handle == NULL)
    {
        xTaskCreate(price_task, "price_fetch_task", 4096, NULL, 5, &price_task_handle);
    }
}

static lv_obj_t *price_settings_label(lv_obj_t *parent, const char *text,
                                      const lv_font_t *font, lv_color_t color)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, color, 0);
    lv_obj_clear_flag(label, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    return label;
}

static void price_style_ccy_button(lv_obj_t *button, bool selected)
{
    if (!button) return;
    lv_obj_set_style_bg_color(button, selected ? COLOR_ACCENT : lv_color_white(), 0);
    lv_obj_set_style_bg_opa(button, selected ? LV_OPA_COVER : LV_OPA_10, 0);
    lv_obj_set_style_border_color(button, selected ? COLOR_ACCENT : lv_color_white(), 0);
    lv_obj_set_style_border_opa(button, selected ? LV_OPA_COVER : LV_OPA_30, 0);
    lv_obj_t *label = lv_obj_get_child(button, 0);
    if (label)
        lv_obj_set_style_text_color(label,
                                    selected ? COLOR_TEXT_ON_ACCENT : COLOR_TEXT_PRIMARY, 0);
}

static void price_refresh_ccy_buttons(void)
{
    const chain_ccy_t selected = chain_get_ccy();
    for (int i = 0; i < CHAIN_CCY_COUNT; i++)
        price_style_ccy_button(price_ccy_buttons[i], i == (int)selected);
}

static void price_ccy_clicked(lv_event_t *e)
{
    const chain_ccy_t ccy = (chain_ccy_t)(intptr_t)lv_event_get_user_data(e);
    if (ccy >= CHAIN_CCY_COUNT || ccy == chain_get_ccy()) return;

    /* One callback owns the whole hit target. The child label is deliberately
     * not clickable, so a tap cannot select/refetch twice through bubbling. */
    chain_set_ccy(ccy);
    price_currency_changed();
    price_refresh_ccy_buttons();
}

static lv_obj_t *price_ccy_button(lv_obj_t *parent, chain_ccy_t ccy, int x, int y)
{
    lv_obj_t *button = lv_btn_create(parent);
    lv_obj_set_size(button, 204, 62);
    lv_obj_set_pos(button, x, y);
    lv_obj_set_style_radius(button, 18, 0);
    lv_obj_set_style_border_width(button, 1, 0);
    lv_obj_set_style_shadow_width(button, 0, 0);
    lv_obj_set_style_bg_color(button, COLOR_ACCENT, LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(button, LV_OPA_70, LV_STATE_PRESSED);
    lv_obj_clear_flag(button, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_add_event_cb(button, price_ccy_clicked, LV_EVENT_CLICKED,
                        (void *)(intptr_t)ccy);

    lv_obj_t *label = price_settings_label(button, chain_ccy_code(ccy),
                                            &lv_font_montserrat_20, COLOR_TEXT_PRIMARY);
    lv_obj_center(label);
    return button;
}

static void price_glass_open_settings(lv_event_t *e)
{
    LV_UNUSED(e);
    if (!price_glass_card || !price_glass_settings) return;
    lv_obj_add_flag(price_glass_card, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(price_title_label, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(price_status_label, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(price_glass_settings, LV_OBJ_FLAG_HIDDEN);
    price_refresh_ccy_buttons();
    lv_obj_invalidate(price_screen);
}

static void price_glass_close_settings(lv_event_t *e)
{
    LV_UNUSED(e);
    if (!price_glass_card || !price_glass_settings) return;
    lv_obj_add_flag(price_glass_settings, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(price_title_label, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(price_status_label, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(price_glass_card, LV_OBJ_FLAG_HIDDEN);
    lv_obj_invalidate(price_screen);
}

static void price_build_glass_settings(void)
{
    price_glass_settings = glass_pane(price_screen, 744, 356, 28);
    lv_obj_align(price_glass_settings, LV_ALIGN_TOP_MID, 0, 48);
    lv_obj_clear_flag(price_glass_settings, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(price_glass_settings, LV_SCROLLBAR_MODE_OFF);

    lv_obj_t *back = lv_btn_create(price_glass_settings);
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
    lv_obj_clear_flag(back, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_add_event_cb(back, price_glass_close_settings, LV_EVENT_CLICKED, NULL);
    lv_obj_t *back_label = price_settings_label(back, LV_SYMBOL_LEFT "  Back",
                                                 &lv_font_montserrat_16,
                                                 COLOR_TEXT_PRIMARY);
    lv_obj_center(back_label);

    lv_obj_t *title = price_settings_label(price_glass_settings,
                                            "BTC EXCHANGE CURRENCY",
                                            &lv_font_montserrat_24,
                                            COLOR_TEXT_PRIMARY);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 28);

    const int x[3] = { 24, 270, 516 };
    const int y[3] = { 88, 164, 240 };
    for (int i = 0; i < CHAIN_CCY_COUNT; i++)
    {
        int col = i % 3;
        int row = i / 3;
        if (i == CHAIN_CCY_JPY) col = 1;
        price_ccy_buttons[i] = price_ccy_button(price_glass_settings,
                                                 (chain_ccy_t)i, x[col], y[row]);
    }

    price_refresh_ccy_buttons();
    lv_obj_add_flag(price_glass_settings, LV_OBJ_FLAG_HIDDEN);
}

void price_screen_destroy(void)
{
    if (price_cagr_timer)
    {
        lv_timer_del(price_cagr_timer);
        price_cagr_timer = NULL;
    }
    memset(price_cagr_value, 0, sizeof(price_cagr_value));
    memset(price_cagr_caption, 0, sizeof(price_cagr_caption));
    if (price_screen)
    {
        glass_screen_detach(price_screen);
        lv_obj_del(price_screen);
        price_screen = NULL;
        price_value_cont = NULL;
        price_prefix_label = NULL;
        price_value_label = NULL;
        price_suffix_label = NULL;
        price_title_label = NULL;
        price_status_label = NULL;
        price_glass_card = NULL;
        price_glass_settings = NULL;
        memset(price_ccy_buttons, 0, sizeof(price_ccy_buttons));
    }
}

lv_obj_t *price_get_screen(void)
{
    return price_screen;
}

const char *price_get_text(void)   { return current_price_text; }
const char *price_get_status(void) { return current_price_status; }

void price_ensure_task(void)
{
    if (price_task_handle == NULL)
    {
        xTaskCreate(price_task, "price_fetch_task", 4096, NULL, 5, &price_task_handle);
    }
}

static void apply_cached_price(void)
{
    if (price_value_label)
    {
        price_apply_value_label();
    }
    if (price_status_label)
    {
        lv_label_set_text(price_status_label, current_price_status);
    }
}

/* Large-fiat currencies (notably JPY) routinely reach eight digits. The 140px
 * face that looks good for USD would push the value and suffix beyond the
 * 720px glass pane, so step down before layout rather than clipping it. */
static void price_apply_value_label(void)
{
    if (!price_value_label) return;
    lv_label_set_text(price_value_label, current_price_text);
    lv_obj_set_style_text_font(price_value_label,
                               strlen(current_price_text) >= 8 ? &Nevan_RUS_96
                                                                : &montserrat_140,
                               0);
}

static bool price_fetch_once(void)
{
    if (!price_ensure_netif() || !price_wifi_connected())
    {
        return false;
    }

    char url[192];

    price_build_coingecko_url(url, sizeof(url));
    if (price_fetch_from_url(url))
    {
        return true;
    }

    price_build_coinbase_url(url, sizeof(url));
    return price_fetch_from_url(url);
}

static bool price_ensure_netif(void)
{
    if (price_netif_ready)
    {
        return true;
    }

    esp_err_t ret = esp_netif_init();
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE)
    {
        return false;
    }

    ret = esp_event_loop_create_default();
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE)
    {
        return false;
    }

    price_netif_ready = true;
    return true;
}

static bool price_wifi_connected(void)
{
    return wifi_is_connected();
}

static bool price_fetch_from_url(const char *url)
{
    if (!url)
    {
        return false;
    }

    esp_http_client_config_t config = {
        .url = url,
        .event_handler = price_http_event_handler,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms = 5000,
    };

    price_http_len = 0;
    price_http_buf[0] = '\0';

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client)
    {
        return false;
    }

    esp_err_t err = esp_http_client_perform(client);
    int status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);

    if (err != ESP_OK || status < 200 || status >= 300 || price_http_len == 0)
    {
        return false;
    }

    double price = 0.0;
    bool parsed = false;
    if (strstr(url, "coingecko"))
    {
        parsed = price_parse_coingecko(price_http_buf, &price);
    }
    else
    {
        parsed = price_parse_coinbase(price_http_buf, &price);
    }

    if (!parsed || price <= 0.0)
    {
        return false;
    }

    long long rounded_price = (long long)(price + 0.5);
    char formatted[sizeof(current_price_text)];
    format_price_with_commas(rounded_price, formatted, sizeof(formatted));
    /* The home widget reads current_price_text on the LVGL task every second. */
    if (lvgl_port_lock(-1))
    {
        strncpy(current_price_text, formatted, sizeof(current_price_text) - 1);
        current_price_text[sizeof(current_price_text) - 1] = '\0';
        lvgl_port_unlock();
    }
    return true;
}

/* Pull one lowercase currency key out of the flat CoinGecko response. */
static bool price_parse_coingecko_key(const char *json, const char *code, double *out)
{
    char key[16];
    size_t i = 0;

    key[i++] = '"';
    for (size_t j = 0; code[j] && i < sizeof(key) - 3; j++)
    {
        key[i++] = (char)((code[j] >= 'A' && code[j] <= 'Z') ? code[j] + 32 : code[j]);
    }
    key[i++] = '"';
    key[i++] = ':';
    key[i] = 0;

    const char *p = strstr(json, key);
    if (!p)
    {
        return false;
    }
    const double v = strtod(p + strlen(key), NULL);
    if (v <= 0.0)
    {
        return false;
    }
    *out = v;
    return true;
}

static bool price_parse_coingecko(const char *json, double *out_price)
{
    if (!json || !out_price)
    {
        return false;
    }

    const chain_ccy_t ccy = chain_get_ccy();
    double local = 0.0;
    if (!price_parse_coingecko_key(json, chain_ccy_code(ccy), &local))
    {
        return false;
    }

    /* Both keys are in the same response, so the ratio is taken from a single
     * instant. When USD is selected the two are the same key and the ratio is
     * exactly 1, which is the right answer rather than a special case. */
    double usd = 0.0;
    if (price_parse_coingecko_key(json, "usd", &usd) && usd > 0.0)
    {
        chain_set_fx_to_usd(local / usd);
    }

    *out_price = local;
    return true;
}

static bool price_parse_coinbase(const char *json, double *out_price)
{
    if (!json || !out_price)
    {
        return false;
    }

    const char *amount_ptr = strstr(json, "\"amount\":\"");
    if (!amount_ptr)
    {
        return false;
    }

    amount_ptr += 10;
    double price = strtod(amount_ptr, NULL);
    if (price <= 0.0)
    {
        return false;
    }

    *out_price = price;
    return true;
}

static void format_price_with_commas(long long value, char *out, size_t out_size)
{
    char temp[32];
    snprintf(temp, sizeof(temp), "%lld", value);

    int len = (int)strlen(temp);
    int commas = (len > 0) ? (len - 1) / 3 : 0;
    int out_len = len + commas;
    if (out_len + 1 > (int)out_size)
    {
        strncpy(out, temp, out_size - 1);
        out[out_size - 1] = '\0';
        return;
    }

    out[out_len] = '\0';
    int src = len - 1;
    int dst = out_len - 1;
    int group = 0;
    while (src >= 0)
    {
        out[dst--] = temp[src--];
        group++;
        if (group == 3 && src >= 0)
        {
            out[dst--] = ',';
            group = 0;
        }
    }
}

static void price_set_status(const char *status)
{
    if (!status)
    {
        return;
    }

    strncpy(current_price_status, status, sizeof(current_price_status) - 1);
    current_price_status[sizeof(current_price_status) - 1] = '\0';

    if (price_status_label)
    {
        lv_label_set_text(price_status_label, current_price_status);
    }
}

static void price_task(void *arg)
{
    (void)arg;
    while (1)
    {
        price_refetch_requested = false;

        if (ota_update_is_running())
        {
            vTaskDelay(pdMS_TO_TICKS(5000));
            continue;
        }

        if (!price_wifi_connected())
        {
            if (lvgl_port_lock(50))
            {
                price_set_status("WAITING FOR WIFI");
                lvgl_port_unlock();
            }
            vTaskDelay(pdMS_TO_TICKS(5000));
            continue;
        }

        if (!price_ensure_netif())
        {
            if (lvgl_port_lock(50))
            {
                price_set_status("NETIF ERROR");
                lvgl_port_unlock();
            }
            vTaskDelay(pdMS_TO_TICKS(5000));
            continue;
        }

        if (lvgl_port_lock(50))
        {
            price_set_status("LOADING...");
            lvgl_port_unlock();
        }

        bool updated = price_fetch_once();
        if (lvgl_port_lock(50))
        {
            if (updated)
            {
                if (price_value_label)
                {
                    price_apply_value_label();
                }
                price_set_status("LIVE");
                lvgl_port_unlock();
                price_wait(PRICE_FETCH_INTERVAL_MS);
                continue;
            }

            price_set_status("RETRYING...");
            lvgl_port_unlock();
        }

        price_wait(10000);
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

void price_home_clicked(lv_event_t *e)
{
    LV_UNUSED(e);
    home_screen_create();
    lv_scr_load(home_get_screen());
    price_screen_destroy();
}

void price_block_clicked(lv_event_t *e)
{
    LV_UNUSED(e);
    block_screen_create();
    lv_scr_load(block_get_screen());
    price_screen_destroy();
}

void price_mempool_clicked(lv_event_t *e)
{
    LV_UNUSED(e);
    mempool_screen_create();
    lv_scr_load(mempool_get_screen());
    price_screen_destroy();
}

void price_clock_clicked(lv_event_t *e)
{
    LV_UNUSED(e);
    clock_screen_create();
    lv_scr_load(clock_get_screen());
    price_screen_destroy();
}

void price_wifi_clicked(lv_event_t *e)
{
    LV_UNUSED(e);
    wifi_screen_create();
    lv_scr_load(wifi_get_screen());
    price_screen_destroy();
}

void price_settings_clicked(lv_event_t *e)
{
    LV_UNUSED(e);
    settings_screen_create();
    lv_scr_load(settings_get_screen());
    price_screen_destroy();
}

void price_night_clicked(lv_event_t *e)
{
    LV_UNUSED(e);
    night_screen_create();
    lv_scr_load(night_get_screen());
    price_screen_destroy();
}

void price_odds_clicked(lv_event_t *e)
{
    odds_screen_create();
    lv_scr_load(odds_get_screen());
    price_screen_destroy();
}

void price_payout_clicked(lv_event_t *e)
{
    payout_screen_create();
    lv_scr_load(payout_get_screen());
    price_screen_destroy();
}
