#include "mempool.h"
#include "payout.h"
#include "odds.h"
#include "chain.h"
#include "custom_fonts.h"
#include "glass.h"
#include "home.h"
#include "block.h"
#include "clock.h"
#include "price.h"
#include "wifi.h"
#include "settings.h"
#include "night.h"
#include "lvgl_port.h"
#include "sdkconfig.h"
#include "esp_event.h"
#include "esp_http_client.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "esp_crt_bundle.h"
#include "esp_log.h"
#include "lwip/apps/sntp.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "ota_update.h"

/* The feed returns fifteen blocks; we parse the first MEMPOOL_MAX_BLOCKS.
 * Those measure about 17.6 KB against mempool.space and bitview alike, so
 * this holds them with half again spare. Anything past the end is truncated,
 * which costs at worst the last card rather than the fetch, and it keeps
 * 40 KB of internal RAM out of a static array on a board that has to fit
 * LVGL in the same heap. */
#define MEMPOOL_HTTP_BUF_SIZE 24576
#define MEMPOOL_MAX_BLOCKS 8
#define MEMPOOL_FETCH_INTERVAL_MS 60000

#define MEMPOOL_VISIBLE_BLOCKS 3
#define CARD_W 240
#define CARD_H 218

/* Path only: the host comes from the data source setting, and both providers
 * serve this route with the same response shape. */
#define MEMPOOL_API_PATH "/api/v1/blocks"

typedef struct
{
    long long height;
    long long timestamp;
    int tx_count;
    double median_fee;
    double fee_min;
    double fee_max;
    long long total_fees_sat;
    char pool_name[32];
    int minutes_ago;
} mempool_block_t;

static lv_obj_t *mempool_screen = NULL;
static lv_obj_t *mempool_status_label = NULL;
static lv_obj_t *mempool_source_label = NULL;
static lv_obj_t *mempool_row = NULL;
static lv_obj_t *mempool_fee_value[4] = { NULL, NULL, NULL, NULL };
static lv_timer_t *mempool_fee_timer = NULL;
static TaskHandle_t mempool_task_handle = NULL;
static bool mempool_netif_ready = false;
static bool mempool_log_tuned = false;
static bool mempool_sntp_started = false;

static mempool_block_t mempool_blocks[MEMPOOL_MAX_BLOCKS];
static int mempool_block_count = 0;

static char mempool_http_buf[MEMPOOL_HTTP_BUF_SIZE];
static int mempool_http_len = 0;

static lv_obj_t *create_bottom_nav_btn(lv_obj_t *parent, const char *symbol, lv_event_cb_t event_cb, bool active);
static lv_obj_t *create_bottom_nav_btn_img(lv_obj_t *parent, const lv_img_dsc_t *img_dsc, lv_event_cb_t event_cb, bool active);
static void mempool_task(void *arg);
static bool mempool_fetch_once(void);
static bool mempool_ensure_netif(void);
static bool mempool_wifi_connected(void);
static bool mempool_ip_ready(void);
static void mempool_start_sntp(void);
static bool mempool_time_ready(void);
static void mempool_set_status(const char *status);
static void mempool_rebuild_cards(void);
static void mempool_build_fee_strip(lv_obj_t *host, bool glass);
static void mempool_fee_timer_cb(lv_timer_t *t);
static void mempool_refresh_fees(void);

static bool json_get_ll(const char *obj, const char *key, long long *out);
static bool json_get_double(const char *obj, const char *key, double *out);
static void format_btc_from_sats(long long sats, char *buf, size_t buf_size);
static int split_top_level_objects(const char *json, int len, int starts[], int ends[], int max_items);
static bool parse_fee_range(const char *obj, double *out_min, double *out_max);
static void parse_pool_name(const char *obj, char *out, size_t out_size);
static int compute_minutes_ago(long long ts);

static esp_err_t mempool_http_event_handler(esp_http_client_event_t *evt)
{
    if (evt->event_id == HTTP_EVENT_ON_DATA && evt->data && evt->data_len > 0)
    {
        int copy_len = evt->data_len;
        if (mempool_http_len + copy_len >= MEMPOOL_HTTP_BUF_SIZE)
        {
            copy_len = MEMPOOL_HTTP_BUF_SIZE - mempool_http_len - 1;
        }
        if (copy_len > 0)
        {
            memcpy(mempool_http_buf + mempool_http_len, evt->data, copy_len);
            mempool_http_len += copy_len;
            mempool_http_buf[mempool_http_len] = '\0';
        }
    }
    return ESP_OK;
}

void mempool_screen_create(void)
{
    if (mempool_screen != NULL)
    {
        return;
    }

    if (!mempool_log_tuned)
    {
        // Timeouts are expected on unreliable networks; keep logs readable.
        esp_log_level_set("esp-tls", ESP_LOG_WARN);
        esp_log_level_set("HTTP_CLIENT", ESP_LOG_WARN);
        esp_log_level_set("transport_base", ESP_LOG_WARN);
        mempool_log_tuned = true;
    }

    const bool glass = glass_active();
    if (glass)
    {
        mempool_screen = glass_screen_create(GLASS_SCREEN_MEMPOOL, false);
    }
    else
    {
        mempool_screen = lv_obj_create(NULL);
        lv_obj_set_style_bg_color(mempool_screen, COLOR_BACKGROUND, 0);
        lv_obj_set_style_bg_opa(mempool_screen, LV_OPA_COVER, 0);
        lv_obj_clear_flag(mempool_screen, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_scrollbar_mode(mempool_screen, LV_SCROLLBAR_MODE_OFF);
    }

    lv_obj_t *title = lv_label_create(mempool_screen);
    lv_label_set_text(title, "NETWORK FEES");
    lv_obj_set_style_text_color(title, COLOR_TEXT_PRIMARY, 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 14);
    if (glass) glass_pill_label(title, false);

    mempool_status_label = lv_label_create(mempool_screen);
    lv_label_set_text(mempool_status_label, "LOADING...");
    lv_obj_set_style_text_color(mempool_status_label, COLOR_TEXT_SECONDARY, 0);
    lv_obj_set_style_text_opa(mempool_status_label, (lv_opa_t)192, 0);
    lv_obj_set_style_text_font(mempool_status_label, &lv_font_montserrat_16, 0);
    lv_obj_align(mempool_status_label, LV_ALIGN_TOP_MID, 0, 44);
    if (glass) glass_pill_label(mempool_status_label, false);

    mempool_build_fee_strip(mempool_screen, glass);

    lv_obj_t *recent_title = lv_label_create(mempool_screen);
    lv_label_set_text(recent_title, "RECENT BLOCKS");
    lv_obj_set_style_text_color(recent_title, COLOR_TEXT_SECONDARY, 0);
    lv_obj_set_style_text_font(recent_title, &lv_font_montserrat_14, 0);
    lv_obj_align(recent_title, LV_ALIGN_TOP_MID, 0, 146);
    if (glass) glass_pill_label(recent_title, false);

    mempool_row = lv_obj_create(mempool_screen);
    const int row_y = 168;
    const int row_h = CARD_H;
    lv_obj_set_size(mempool_row, 740, row_h);
    lv_obj_align(mempool_row, LV_ALIGN_TOP_MID, 0, row_y);
    lv_obj_set_style_bg_opa(mempool_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(mempool_row, 0, 0);
    lv_obj_set_style_pad_left(mempool_row, 0, 0);
    lv_obj_set_style_pad_right(mempool_row, 0, 0);
    lv_obj_set_style_pad_top(mempool_row, 0, 0);
    lv_obj_set_style_pad_bottom(mempool_row, 0, 0);
    lv_obj_set_style_pad_column(mempool_row, 10, 0);
    lv_obj_set_scroll_dir(mempool_row, LV_DIR_NONE);
    lv_obj_clear_flag(mempool_row, LV_OBJ_FLAG_SCROLLABLE |
                                    LV_OBJ_FLAG_SCROLL_MOMENTUM |
                                    LV_OBJ_FLAG_SCROLL_ELASTIC);
    lv_obj_set_scrollbar_mode(mempool_row, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_flex_flow(mempool_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(mempool_row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    if (glass)
    {
        /* Centre this directly below the middle recent-block card and just
         * above the bottom Settings affordance, so it reads as attribution
         * rather than as part of a block. */
        mempool_source_label = lv_label_create(mempool_screen);
        lv_label_set_text(mempool_source_label, chain_source_name(chain_get_source()));
        lv_obj_set_style_text_color(mempool_source_label, COLOR_TEXT_SECONDARY, 0);
        lv_obj_set_style_text_font(mempool_source_label, &lv_font_montserrat_12, 0);
        lv_obj_align(mempool_source_label, LV_ALIGN_BOTTOM_MID, 0, -20);
        glass_pill_label(mempool_source_label, false);
    }

    if (glass)
    {
        /* The whole latest-block snapshot fits without a gesture. A tap on the
         * quiet space still opens the drawer. */
        glass_attach_drawer_toggle(mempool_row);
        mempool_rebuild_cards();
        mempool_ensure_task();
        glass_screen_ready(mempool_screen);
        return;
    }

    lv_obj_t *bottom_nav = lv_obj_create(mempool_screen);
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

    create_bottom_nav_btn(bottom_nav, LV_SYMBOL_HOME, mempool_home_clicked, false);
    create_bottom_nav_btn_img(bottom_nav, &cube_solid_full, mempool_block_clicked, false);
    create_bottom_nav_btn_img(bottom_nav, &cubes_solid_full, NULL, true);
    create_bottom_nav_btn_img(bottom_nav, &clock_solid_full, mempool_clock_clicked, false);
    create_bottom_nav_btn(bottom_nav, "$", mempool_price_clicked, false);
    create_bottom_nav_btn(bottom_nav, "%", mempool_odds_clicked, false);
    create_bottom_nav_btn(bottom_nav, LV_SYMBOL_DOWNLOAD, mempool_payout_clicked, false);
    create_bottom_nav_btn(bottom_nav, LV_SYMBOL_WIFI, mempool_wifi_clicked, false);
    create_bottom_nav_btn(bottom_nav, LV_SYMBOL_SETTINGS, mempool_settings_clicked, false);
    create_bottom_nav_btn(bottom_nav, LV_SYMBOL_EYE_OPEN, mempool_night_clicked, false);

    mempool_rebuild_cards();

    if (mempool_task_handle == NULL)
    {
        xTaskCreate(mempool_task, "mempool_task", 6144, NULL, 5, &mempool_task_handle);
    }
}

static void mempool_fee_timer_cb(lv_timer_t *t)
{
    (void) t;
    mempool_refresh_fees();
}

/* Recommended fee bands and the size of the backlog behind them.
 *
 * The block cards below say what the last few blocks charged, which is history
 * by the time you read it. This says what it would cost to get into the next
 * one, which is the number anyone actually wants from a mempool screen. */
static void mempool_build_fee_strip(lv_obj_t *host, bool glass)
{
    static const char *k_band[4] = { "FASTEST", "30 MIN", "1 HOUR", "ECONOMY" };

    const int cell_w = 168;
    const int gap    = 8;
    const int x0     = (SCREEN_WIDTH - (cell_w * 4 + gap * 3)) / 2;
    const int y      = 72;
    const int h      = 64;

    for (int i = 0; i < 4; i++)
    {
        lv_obj_t *cell;
        if (glass)
        {
            cell = glass_pane(host, cell_w, h, 14);
        }
        else
        {
            cell = lv_obj_create(host);
            lv_obj_set_size(cell, cell_w, h);
            lv_obj_set_style_bg_color(cell, COLOR_CARD_BG, 0);
            lv_obj_set_style_bg_opa(cell, LV_OPA_COVER, 0);
            lv_obj_set_style_border_width(cell, 1, 0);
            lv_obj_set_style_border_color(cell, COLOR_BORDER, 0);
            lv_obj_set_style_radius(cell, 10, 0);
            lv_obj_set_style_shadow_width(cell, 0, 0);
            lv_obj_set_style_pad_all(cell, 0, 0);
            lv_obj_clear_flag(cell, LV_OBJ_FLAG_SCROLLABLE);
        }
        lv_obj_align(cell, LV_ALIGN_TOP_LEFT, x0 + i * (cell_w + gap), y);

        lv_obj_t *cap = lv_label_create(cell);
        lv_label_set_text(cap, k_band[i]);
        lv_obj_set_style_text_color(cap, COLOR_TEXT_SECONDARY, 0);
        lv_obj_set_style_text_font(cap, &lv_font_montserrat_12, 0);
        lv_obj_align(cap, LV_ALIGN_TOP_MID, 0, 9);

        mempool_fee_value[i] = lv_label_create(cell);
        lv_label_set_text(mempool_fee_value[i], "--");
        lv_obj_set_style_text_color(mempool_fee_value[i], COLOR_TEXT_PRIMARY, 0);
        lv_obj_set_style_text_font(mempool_fee_value[i], &lv_font_montserrat_22, 0);
        lv_obj_align(mempool_fee_value[i], LV_ALIGN_TOP_MID, 0, 31);
    }


    mempool_fee_timer = lv_timer_create(mempool_fee_timer_cb, 3000, NULL);
    mempool_refresh_fees();
}

/* Under a sat/vB the integer is a lie, so keep one decimal down there. */
static void fmt_fee_rate(double v, char *buf, size_t n)
{
    if (v <= 0.0)      { snprintf(buf, n, "--"); }
    else if (v < 10.0) { snprintf(buf, n, "%.1f", v); }
    else               { snprintf(buf, n, "%.0f", v); }
}

static void mempool_refresh_fees(void)
{
    const chain_data_t *d = chain_data();
    char buf[96];

    const double band[4] = { d->fee_fastest, d->fee_half_hour, d->fee_hour, d->fee_economy };
    for (int i = 0; i < 4; i++)
    {
        if (!mempool_fee_value[i]) continue;
        if (d->fees_valid)
        {
            char n[16];
            fmt_fee_rate(band[i], n, sizeof(n));
            snprintf(buf, sizeof(buf), "%s sat/vB", n);
        }
        else
        {
            snprintf(buf, sizeof(buf), "--");
        }
        lv_label_set_text(mempool_fee_value[i], buf);
    }

    /* Keep queue size/vsize out of this deliberately calm header. The four
     * fee bands and recent blocks already carry the useful detail. */
    if (mempool_status_label && mempool_block_count > 0)
    {
        snprintf(buf, sizeof(buf), "%d RECENT BLOCKS",
                 mempool_block_count < MEMPOOL_VISIBLE_BLOCKS
                    ? mempool_block_count : MEMPOOL_VISIBLE_BLOCKS);
        lv_label_set_text(mempool_status_label, buf);
    }
    if (mempool_source_label)
        lv_label_set_text(mempool_source_label, chain_source_name(chain_get_source()));
}

void mempool_screen_destroy(void)
{
    if (mempool_fee_timer)
    {
        lv_timer_del(mempool_fee_timer);
        mempool_fee_timer = NULL;
    }
    for (int i = 0; i < 4; i++) { mempool_fee_value[i] = NULL; }
    if (mempool_screen)
    {
        glass_screen_detach(mempool_screen);
        lv_obj_del(mempool_screen);
        mempool_screen = NULL;
        mempool_status_label = NULL;
        mempool_source_label = NULL;
        mempool_row = NULL;
    }
}

lv_obj_t *mempool_get_screen(void)
{
    return mempool_screen;
}

bool mempool_get_latest(char *fee_out, size_t fee_len, char *detail_out, size_t detail_len)
{
    if (mempool_block_count <= 0)
    {
        return false;
    }
    const mempool_block_t *b = &mempool_blocks[0];
    if (fee_out && fee_len)
    {
        snprintf(fee_out, fee_len, "~%.0f sat/vB", b->median_fee);
    }
    if (detail_out && detail_len)
    {
        snprintf(detail_out, detail_len, "%lld  %d min ago  %s",
                 b->height, b->minutes_ago, b->pool_name);
    }
    return true;
}

void mempool_ensure_task(void)
{
    if (mempool_task_handle == NULL)
    {
        xTaskCreate(mempool_task, "mempool_task", 6144, NULL, 5, &mempool_task_handle);
    }
}

static void mempool_task(void *arg)
{
    (void)arg;
    while (1)
    {
        if (ota_update_is_running())
        {
            vTaskDelay(pdMS_TO_TICKS(5000));
            continue;
        }

        if (!mempool_wifi_connected())
        {
            if (lvgl_port_lock(50))
            {
                mempool_set_status("WAITING FOR WIFI");
                lvgl_port_unlock();
            }
            vTaskDelay(pdMS_TO_TICKS(5000));
            continue;
        }

        if (!mempool_ensure_netif())
        {
            if (lvgl_port_lock(50))
            {
                mempool_set_status("NETIF ERROR");
                lvgl_port_unlock();
            }
            vTaskDelay(pdMS_TO_TICKS(5000));
            continue;
        }

        if (!mempool_ip_ready())
        {
            if (lvgl_port_lock(50))
            {
                mempool_set_status("WAITING FOR IP");
                lvgl_port_unlock();
            }
            vTaskDelay(pdMS_TO_TICKS(3000));
            continue;
        }

        mempool_start_sntp();
        if (!mempool_time_ready())
        {
            if (lvgl_port_lock(50))
            {
                mempool_set_status("SYNCING TIME");
                lvgl_port_unlock();
            }
            vTaskDelay(pdMS_TO_TICKS(2000));
            continue;
        }

        if (lvgl_port_lock(50))
        {
            mempool_set_status("LOADING...");
            lvgl_port_unlock();
        }

        bool updated = mempool_fetch_once();
        if (lvgl_port_lock(50))
        {
            if (updated)
            {
                /* Name the provider actually used, not the one that used to
                 * be hardcoded here. */
                mempool_refresh_fees();
                mempool_rebuild_cards();
                lvgl_port_unlock();
                vTaskDelay(pdMS_TO_TICKS(MEMPOOL_FETCH_INTERVAL_MS));
                continue;
            }

            mempool_set_status("RETRYING...");
            lvgl_port_unlock();
        }

        vTaskDelay(pdMS_TO_TICKS(10000));
    }
}

static bool mempool_fetch_once(void)
{
    char url[160];
    snprintf(url, sizeof(url), "%s%s", chain_base_url(), MEMPOOL_API_PATH);

    esp_http_client_config_t config = {
        .url = url,
        .event_handler = mempool_http_event_handler,
        .timeout_ms = 12000,
    };

#if defined(CONFIG_ESP_TLS_SKIP_SERVER_CERT_VERIFY) && CONFIG_ESP_TLS_SKIP_SERVER_CERT_VERIFY
    // Optional insecure mode to reduce TLS overhead on constrained links.
    config.skip_cert_common_name_check = true;
#else
    config.crt_bundle_attach = esp_crt_bundle_attach;
#endif

    mempool_http_len = 0;
    mempool_http_buf[0] = '\0';

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client)
    {
        return false;
    }

    esp_err_t err = esp_http_client_perform(client);
    int status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);

    if (err != ESP_OK || status < 200 || status >= 300 || mempool_http_len == 0)
    {
        return false;
    }

    int starts[MEMPOOL_MAX_BLOCKS];
    int ends[MEMPOOL_MAX_BLOCKS];
    int obj_count = split_top_level_objects(mempool_http_buf, mempool_http_len, starts, ends, MEMPOOL_MAX_BLOCKS);
    if (obj_count <= 0)
    {
        return false;
    }

    /* Parse into a local set and publish it under the LVGL lock: the home
     * widget and the card builder read mempool_blocks on the LVGL task. */
    mempool_block_t parsed[MEMPOOL_MAX_BLOCKS];
    int parsed_count = 0;

    for (int i = 0; i < obj_count && i < MEMPOOL_MAX_BLOCKS; i++)
    {
        int len = ends[i] - starts[i] + 1;
        if (len <= 0 || len > MEMPOOL_HTTP_BUF_SIZE - 1)
        {
            continue;
        }

        char *obj = malloc((size_t)len + 1);
        if (!obj)
        {
            continue;
        }
        memcpy(obj, mempool_http_buf + starts[i], (size_t)len);
        obj[len] = '\0';

        mempool_block_t block = {0};
        long long ts = 0;
        long long height = 0;
        long long txc = 0;
        double median = 0.0;
        double fee_min = 0.0;
        double fee_max = 0.0;
        long long total_fees = 0;

        if (!json_get_ll(obj, "\"height\":", &height))
        {
            free(obj);
            continue;
        }
        json_get_ll(obj, "\"timestamp\":", &ts);
        json_get_ll(obj, "\"tx_count\":", &txc);
        json_get_double(obj, "\"medianFee\":", &median);
        json_get_ll(obj, "\"totalFees\":", &total_fees);
        parse_fee_range(obj, &fee_min, &fee_max);
        parse_pool_name(obj, block.pool_name, sizeof(block.pool_name));

        block.height = height;
        block.timestamp = ts;
        block.tx_count = (int)txc;
        block.median_fee = median;
        block.fee_min = fee_min;
        block.fee_max = fee_max;
        block.total_fees_sat = total_fees;
        block.minutes_ago = compute_minutes_ago(ts);

        parsed[parsed_count++] = block;
        free(obj);
    }

    if (parsed_count > 0 && lvgl_port_lock(-1))
    {
        memcpy(mempool_blocks, parsed, sizeof(mempool_block_t) * (size_t)parsed_count);
        mempool_block_count = parsed_count;
        lvgl_port_unlock();
    }

    return parsed_count > 0;
}

static bool mempool_ensure_netif(void)
{
    if (mempool_netif_ready)
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

    mempool_netif_ready = true;
    return true;
}

static bool mempool_wifi_connected(void)
{
    return wifi_is_connected();
}

static bool mempool_ip_ready(void)
{
    esp_netif_t *sta = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (!sta)
    {
        return false;
    }

    esp_netif_ip_info_t ip_info;
    if (esp_netif_get_ip_info(sta, &ip_info) != ESP_OK)
    {
        return false;
    }

    // 0.0.0.0 means DHCP hasn't completed yet.
    return ip_info.ip.addr != 0;
}

static void mempool_start_sntp(void)
{
    if (mempool_sntp_started || sntp_enabled())
    {
        mempool_sntp_started = true;
        return;
    }

    sntp_setoperatingmode(SNTP_OPMODE_POLL);
    sntp_setservername(0, "pool.ntp.org");
    sntp_init();
    mempool_sntp_started = true;
}

static bool mempool_time_ready(void)
{
    time_t now = time(NULL);
    struct tm time_info;
    localtime_r(&now, &time_info);
    // Pre-2023 typically means SNTP hasn't synced yet.
    return time_info.tm_year >= (2023 - 1900);
}

static void mempool_set_status(const char *status)
{
    if (!status || !mempool_status_label)
    {
        return;
    }

    lv_label_set_text(mempool_status_label, status);
}

static void mempool_rebuild_cards(void)
{
    if (!mempool_row)
    {
        return;
    }

    lv_obj_clean(mempool_row);

    if (mempool_block_count <= 0)
    {
        return;
    }

    const bool glass = glass_active();
    const int shown = mempool_block_count < MEMPOOL_VISIBLE_BLOCKS
                    ? mempool_block_count : MEMPOOL_VISIBLE_BLOCKS;

    for (int i = 0; i < shown; i++)
    {
        mempool_block_t *b = &mempool_blocks[i];
        lv_obj_t *card;
        if (glass)
        {
            /* Recent-block cards are rebuilt after every fetch.  Keep them
             * out of glass.c's persistent frost registry so lv_obj_clean()
             * can never leave that registry holding freed LVGL objects. */
            card = lv_obj_create(mempool_row);
            lv_obj_set_size(card, CARD_W, CARD_H);
            lv_obj_set_style_radius(card, 20, 0);
            lv_obj_set_style_bg_color(card, lv_color_black(), 0);
            lv_obj_set_style_bg_opa(card, LV_OPA_60, 0);
            lv_obj_set_style_border_color(card, lv_color_white(), 0);
            lv_obj_set_style_border_opa(card, LV_OPA_40, 0);
            lv_obj_set_style_border_width(card, 1, 0);
            lv_obj_set_style_shadow_width(card, 0, 0);
            lv_obj_set_style_pad_all(card, 0, 0);
            lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
        }
        else
        {
            card = lv_obj_create(mempool_row);
            lv_obj_set_size(card, CARD_W, CARD_H);
            lv_obj_set_style_radius(card, 14, 0);
            lv_obj_set_style_border_width(card, 1, 0);
            lv_obj_set_style_border_color(card, COLOR_BORDER, 0);
            lv_obj_set_style_pad_all(card, 0, 0);
            lv_obj_set_style_bg_color(card, COLOR_CARD_BG, 0);
            lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
            lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
        }

        char height_txt[24];
        chain_fmt_grouped((long)b->height, height_txt, sizeof(height_txt));
        lv_obj_t *height_label = lv_label_create(card);
        lv_label_set_text_fmt(height_label, "BLOCK %s", height_txt);
        lv_obj_set_style_text_color(height_label, COLOR_TEXT_SECONDARY, 0);
        lv_obj_set_style_text_font(height_label, &lv_font_montserrat_14, 0);
        lv_obj_align(height_label, LV_ALIGN_TOP_MID, 0, 14);

        char median_txt[32];
        int median_fee_i = (int)(b->median_fee + 0.5);
        lv_snprintf(median_txt, sizeof(median_txt), "~%d", median_fee_i);
        lv_obj_t *median_label = lv_label_create(card);
        lv_label_set_text(median_label, median_txt);
        lv_obj_set_style_text_color(median_label, COLOR_ACCENT, 0);
        lv_obj_set_style_text_font(median_label, &lv_font_montserrat_36, 0);
        lv_obj_align(median_label, LV_ALIGN_TOP_MID, 0, 42);

        lv_obj_t *median_cap = lv_label_create(card);
        lv_label_set_text(median_cap, "MEDIAN SAT/VB");
        lv_obj_set_style_text_color(median_cap, COLOR_TEXT_SECONDARY, 0);
        lv_obj_set_style_text_font(median_cap, &lv_font_montserrat_12, 0);
        lv_obj_align(median_cap, LV_ALIGN_TOP_MID, 0, 82);

        char btc_txt[32];
        format_btc_from_sats(b->total_fees_sat, btc_txt, sizeof(btc_txt));
        char tx_txt[40];
        lv_snprintf(tx_txt, sizeof(tx_txt), "%d tx  -  %s", b->tx_count, btc_txt);
        lv_obj_t *detail_label = lv_label_create(card);
        lv_label_set_text(detail_label, tx_txt);
        lv_obj_set_style_text_color(detail_label, COLOR_TEXT_PRIMARY, 0);
        lv_obj_set_style_text_font(detail_label, &lv_font_montserrat_14, 0);
        lv_obj_set_width(detail_label, CARD_W - 20);
        lv_label_set_long_mode(detail_label, LV_LABEL_LONG_DOT);
        lv_obj_set_style_text_align(detail_label, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_align(detail_label, LV_ALIGN_TOP_MID, 0, 114);

        char footer[80];
        if (b->minutes_ago >= 0)
        {
            lv_snprintf(footer, sizeof(footer), "%s  -  %d min ago",
                        b->pool_name[0] ? b->pool_name : "Unknown pool",
                        b->minutes_ago);
        }
        else
        {
            lv_snprintf(footer, sizeof(footer), "%s",
                        b->pool_name[0] ? b->pool_name : "Unknown pool");
        }
        lv_obj_t *footer_label = lv_label_create(card);
        lv_label_set_text(footer_label, footer);
        lv_label_set_long_mode(footer_label, LV_LABEL_LONG_DOT);
        lv_obj_set_width(footer_label, CARD_W - 24);
        lv_obj_set_style_text_color(footer_label, COLOR_TEXT_SECONDARY, 0);
        lv_obj_set_style_text_font(footer_label, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_align(footer_label, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_align(footer_label, LV_ALIGN_BOTTOM_MID, 0, -18);
    }

    if (glass)
    {
        glass_screen_ready(mempool_screen);
    }
}

static bool json_get_ll(const char *obj, const char *key, long long *out)
{
    if (!obj || !key || !out)
    {
        return false;
    }

    const char *p = strstr(obj, key);
    if (!p)
    {
        return false;
    }
    p += strlen(key);
    *out = strtoll(p, NULL, 10);
    return true;
}

static bool json_get_double(const char *obj, const char *key, double *out)
{
    if (!obj || !key || !out)
    {
        return false;
    }

    const char *p = strstr(obj, key);
    if (!p)
    {
        return false;
    }
    p += strlen(key);
    *out = strtod(p, NULL);
    return true;
}

static void format_btc_from_sats(long long sats, char *buf, size_t buf_size)
{
    if (sats < 0)
    {
        sats = 0;
    }

    long long whole = sats / 100000000LL;
    long long frac = sats % 100000000LL;
    long long frac3 = (frac + 50000LL) / 100000LL; // round to 3 decimals
    if (frac3 >= 1000LL)
    {
        whole += 1;
        frac3 = 0;
    }

    lv_snprintf(buf, buf_size, "%lld.%03lld BTC", whole, frac3);
}

static int split_top_level_objects(const char *json, int len, int starts[], int ends[], int max_items)
{
    int count = 0;
    int depth = 0;
    bool in_str = false;
    bool esc = false;
    int start_idx = -1;

    for (int i = 0; i < len; i++)
    {
        char c = json[i];
        if (in_str)
        {
            if (esc)
            {
                esc = false;
            }
            else if (c == '\\')
            {
                esc = true;
            }
            else if (c == '"')
            {
                in_str = false;
            }
            continue;
        }

        if (c == '"')
        {
            in_str = true;
            continue;
        }

        if (c == '{')
        {
            if (depth == 0)
            {
                start_idx = i;
            }
            depth++;
        }
        else if (c == '}')
        {
            depth--;
            if (depth == 0 && start_idx >= 0)
            {
                if (count < max_items)
                {
                    starts[count] = start_idx;
                    ends[count] = i;
                    count++;
                }
                else
                {
                    break;
                }
                start_idx = -1;
            }
        }
    }

    return count;
}

static bool parse_fee_range(const char *obj, double *out_min, double *out_max)
{
    if (!obj || !out_min || !out_max)
    {
        return false;
    }

    const char *p = strstr(obj, "\"feeRange\":[");
    if (!p)
    {
        return false;
    }
    p += strlen("\"feeRange\":[");

    double min_v = 0.0;
    double max_v = 0.0;
    bool have = false;

    while (*p && *p != ']')
    {
        char *endptr = NULL;
        double v = strtod(p, &endptr);
        if (endptr == p)
        {
            p++;
            continue;
        }
        if (!have)
        {
            min_v = max_v = v;
            have = true;
        }
        else
        {
            if (v < min_v)
                min_v = v;
            if (v > max_v)
                max_v = v;
        }
        p = endptr;
        if (*p == ',')
        {
            p++;
        }
    }

    if (!have)
    {
        return false;
    }

    *out_min = min_v;
    *out_max = max_v;
    return true;
}

static void parse_pool_name(const char *obj, char *out, size_t out_size)
{
    if (!obj || !out || out_size == 0)
    {
        return;
    }

    out[0] = '\0';
    const char *pool = strstr(obj, "\"pool\"");
    if (!pool)
    {
        return;
    }
    const char *name = strstr(pool, "\"name\":\"");
    if (!name)
    {
        return;
    }
    name += strlen("\"name\":\"");
    const char *end = strchr(name, '"');
    if (!end)
    {
        return;
    }

    size_t len = (size_t)(end - name);
    if (len >= out_size)
    {
        len = out_size - 1;
    }
    memcpy(out, name, len);
    out[len] = '\0';
}

static int compute_minutes_ago(long long ts)
{
    time_t now = time(NULL);
    if (now < 946684800 || ts <= 0)
    {
        return -1;
    }

    long long diff = (long long)now - ts;
    if (diff < 0)
    {
        diff = 0;
    }
    return (int)(diff / 60);
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

void mempool_home_clicked(lv_event_t *e)
{
    LV_UNUSED(e);
    home_screen_create();
    lv_scr_load(home_get_screen());
    mempool_screen_destroy();
}

void mempool_block_clicked(lv_event_t *e)
{
    LV_UNUSED(e);
    block_screen_create();
    lv_scr_load(block_get_screen());
    mempool_screen_destroy();
}

void mempool_clock_clicked(lv_event_t *e)
{
    LV_UNUSED(e);
    clock_screen_create();
    lv_scr_load(clock_get_screen());
    mempool_screen_destroy();
}

void mempool_price_clicked(lv_event_t *e)
{
    LV_UNUSED(e);
    price_screen_create();
    lv_scr_load(price_get_screen());
    mempool_screen_destroy();
}

void mempool_wifi_clicked(lv_event_t *e)
{
    LV_UNUSED(e);
    wifi_screen_create();
    lv_scr_load(wifi_get_screen());
    mempool_screen_destroy();
}

void mempool_settings_clicked(lv_event_t *e)
{
    LV_UNUSED(e);
    settings_screen_create();
    lv_scr_load(settings_get_screen());
    mempool_screen_destroy();
}

void mempool_night_clicked(lv_event_t *e)
{
    LV_UNUSED(e);
    night_screen_create();
    lv_scr_load(night_get_screen());
    mempool_screen_destroy();
}

void mempool_odds_clicked(lv_event_t *e)
{
    odds_screen_create();
    lv_scr_load(odds_get_screen());
    mempool_screen_destroy();
}

void mempool_payout_clicked(lv_event_t *e)
{
    payout_screen_create();
    lv_scr_load(payout_get_screen());
    mempool_screen_destroy();
}
