#include "chain.h"

#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "lvgl_port.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static const char *TAG = "chain";

#define CHAIN_NVS_NAMESPACE "gtdisplay"
#define CHAIN_NVS_SOURCE    "chain_src"
#define CHAIN_NVS_CCY       "chain_ccy"

/* One response at a time, on the chain task only. The largest thing we ask
 * for is mempool.space's hashrate summary; a bitview vecs reply is under 128
 * bytes. Sized for the former with room to spare, and still a sixty-fourth of
 * what the blocks feed reserves. */
#define CHAIN_HTTP_BUF_SIZE 1024

#define CHAIN_REFRESH_MS       (5 * 60 * 1000)
#define CHAIN_RETRY_MS         (20 * 1000)
#define CHAIN_HALVING_INTERVAL 210000

static const char *SOURCE_BASE[CHAIN_SRC_COUNT] = {
    [CHAIN_SRC_MEMPOOL] = "https://mempool.space",
    [CHAIN_SRC_BITVIEW] = "https://bitview.space",
};

static const char *SOURCE_NAME[CHAIN_SRC_COUNT] = {
    [CHAIN_SRC_MEMPOOL] = "mempool.space",
    [CHAIN_SRC_BITVIEW] = "bitview.space",
};

static const char *CCY_CODE[CHAIN_CCY_COUNT] = {
    "USD", "AUD", "NZD", "GBP", "EUR", "CAD", "JPY",
};

/* The 140px face used for the price has glyphs for 32..45 and 48..58 only:
 * digits, comma and the dollar sign, with no alphabet and no pound, euro or
 * yen. So the prefix is a dollar sign or nothing, and the currency is named in
 * the title and the suffix instead. */
static const char *CCY_PREFIX[CHAIN_CCY_COUNT] = {
    "$",   /* USD */
    "$",   /* AUD */
    "$",   /* NZD */
    "",    /* GBP */
    "",    /* EUR */
    "$",   /* CAD */
    "",    /* JPY */
};

static chain_source_t s_source = CHAIN_SRC_BITVIEW;
static chain_ccy_t    s_ccy    = CHAIN_CCY_USD;
static double         s_fx     = 1.0;

static chain_data_t s_data;
static char         s_http_buf[CHAIN_HTTP_BUF_SIZE];
static int          s_http_len = 0;
static TaskHandle_t s_task = NULL;
static chain_address_t s_addr;

/* ---- preferences ---- */

static void chain_load_prefs(void)
{
    nvs_handle_t h;
    if (nvs_open(CHAIN_NVS_NAMESPACE, NVS_READONLY, &h) != ESP_OK)
    {
        return;
    }

    uint8_t v = 0;
    if (nvs_get_u8(h, CHAIN_NVS_SOURCE, &v) == ESP_OK && v < CHAIN_SRC_COUNT)
    {
        s_source = (chain_source_t)v;
    }
    if (nvs_get_u8(h, CHAIN_NVS_CCY, &v) == ESP_OK && v < CHAIN_CCY_COUNT)
    {
        s_ccy = (chain_ccy_t)v;
    }
    nvs_close(h);
}

static void chain_save_u8(const char *key, uint8_t value)
{
    nvs_handle_t h;
    if (nvs_open(CHAIN_NVS_NAMESPACE, NVS_READWRITE, &h) != ESP_OK)
    {
        return;
    }
    nvs_set_u8(h, key, value);
    nvs_commit(h);
    nvs_close(h);
}

chain_source_t chain_get_source(void) { return s_source; }
chain_ccy_t    chain_get_ccy(void)    { return s_ccy; }

const char *chain_source_name(chain_source_t src)
{
    return (src < CHAIN_SRC_COUNT) ? SOURCE_NAME[src] : "";
}

const char *chain_ccy_code(chain_ccy_t ccy)
{
    return (ccy < CHAIN_CCY_COUNT) ? CCY_CODE[ccy] : "USD";
}

const char *chain_ccy_prefix(chain_ccy_t ccy)
{
    return (ccy < CHAIN_CCY_COUNT) ? CCY_PREFIX[ccy] : "$";
}

const char *chain_base_url(void)
{
    return SOURCE_BASE[s_source];
}

bool chain_have_hashprice(void)
{
    return s_source == CHAIN_SRC_BITVIEW;
}

void chain_set_source(chain_source_t src)
{
    if (src >= CHAIN_SRC_COUNT || src == s_source)
    {
        return;
    }
    s_source = src;
    chain_save_u8(CHAIN_NVS_SOURCE, (uint8_t)src);

    /* The old snapshot came from the other provider. Drop what is
     * provider-specific and keep difficulty and the halving, which are facts
     * about the chain rather than about the API, then wake the task. */
    if (lvgl_port_lock(-1))
    {
        s_data.hashprice_usd_ths  = 0.0;
        s_data.hashvalue_sats_ths = 0.0;
        lvgl_port_unlock();
    }
    if (s_task)
    {
        xTaskNotifyGive(s_task);
    }
    ESP_LOGI(TAG, "source now %s", chain_source_name(src));
}

void chain_set_ccy(chain_ccy_t ccy)
{
    if (ccy >= CHAIN_CCY_COUNT || ccy == s_ccy)
    {
        return;
    }
    s_ccy = ccy;
    /* The old ratio belongs to the old currency. Neutral until price.c
     * publishes one for the new one, so nothing is ever shown converted by
     * the wrong factor in between. */
    s_fx = 1.0;
    chain_save_u8(CHAIN_NVS_CCY, (uint8_t)ccy);
    ESP_LOGI(TAG, "currency now %s", chain_ccy_code(ccy));
}

double chain_fx_to_usd(void) { return s_fx; }

void chain_set_fx_to_usd(double ratio)
{
    if (ratio > 0.0 && isfinite(ratio))
    {
        s_fx = ratio;
    }
}

const chain_data_t *chain_data(void) { return &s_data; }

const chain_address_t *chain_address(void) { return &s_addr; }

/* Cheap plausibility check. The point is to reject a pool user that is a
 * plain username rather than to validate bech32 or base58: a wrong-looking
 * string here costs a doomed request every refresh, and the API is the thing
 * that actually decides whether an address exists. */
static bool looks_like_address(const char *a)
{
    if (!a) {
        return false;
    }
    const size_t n = strlen(a);
    if (n < 26 || n >= sizeof(s_addr.address)) {
        return false;
    }
    if (a[0] == 'b' && a[1] == 'c' && a[2] == '1') {
        return true;              /* bc1... bech32 and bech32m */
    }
    return a[0] == '1' || a[0] == '3';   /* P2PKH, P2SH */
}

void chain_set_watch_address(const char *addr)
{
    char buf[sizeof(s_addr.address)];

    if (!addr || !addr[0]) {
        return;
    }
    /* A solo pool user is the address, a dot, then the worker name. */
    size_t i = 0;
    while (addr[i] && addr[i] != '.' && i < sizeof(buf) - 1) {
        buf[i] = addr[i];
        i++;
    }
    buf[i] = 0;

    if (!looks_like_address(buf) || strcmp(buf, s_addr.address) == 0) {
        return;
    }

    memset(&s_addr, 0, sizeof(s_addr));
    strncpy(s_addr.address, buf, sizeof(s_addr.address) - 1);
    s_addr.watching = true;
    ESP_LOGI(TAG, "watching payout address ending %s",
             s_addr.address + (strlen(s_addr.address) > 6 ? strlen(s_addr.address) - 6 : 0));
    if (s_task) {
        xTaskNotifyGive(s_task);
    }
}

void chain_fmt_compact(double v, char *buf, size_t n)
{
    static const char *suffix[] = { "", "K", "M", "B", "T", "P", "E" };
    int tier = 0;

    if (!isfinite(v) || v <= 0.0)
    {
        snprintf(buf, n, "--");
        return;
    }
    while (v >= 1000.0 && tier < 6)
    {
        v /= 1000.0;
        tier++;
    }
    if (v >= 100.0)     { snprintf(buf, n, "%.0f%s", v, suffix[tier]); }
    else if (v >= 10.0) { snprintf(buf, n, "%.1f%s", v, suffix[tier]); }
    else                { snprintf(buf, n, "%.2f%s", v, suffix[tier]); }
}

void chain_fmt_grouped(long v, char *buf, size_t n)
{
    if (!buf || n == 0)
    {
        return;
    }

    char digits[24];
    snprintf(digits, sizeof(digits), "%ld", v < 0 ? -v : v);

    const int len = (int)strlen(digits);
    size_t w = 0;

    if (v < 0 && w + 1 < n)
    {
        buf[w++] = '-';
    }
    for (int i = 0; i < len; i++)
    {
        if (i > 0 && (len - i) % 3 == 0 && w + 1 < n)
        {
            buf[w++] = ',';
        }
        if (w + 1 < n)
        {
            buf[w++] = digits[i];
        }
    }
    buf[w < n ? w : n - 1] = 0;
}

/* ---- odds ---- */

bool chain_solo_odds(double hashrate_ghs,
                     double *expected_seconds,
                     double *chance_per_day,
                     double *chance_per_year)
{
    if (!s_data.valid || s_data.difficulty <= 0.0 || hashrate_ghs <= 0.0)
    {
        return false;
    }

    const double hashes_per_share = 4294967296.0;   /* 2^32 */
    const double h = hashrate_ghs * 1e9;
    const double expected = s_data.difficulty * hashes_per_share / h;

    if (!isfinite(expected) || expected <= 0.0)
    {
        return false;
    }

    if (expected_seconds) *expected_seconds = expected;
    /* Poisson tail: the chance of at least one block in the window. expm1
     * keeps its precision where the plain exp would round to 1. */
    if (chance_per_day)  *chance_per_day  = -expm1(-86400.0 / expected);
    if (chance_per_year) *chance_per_year = -expm1(-31556952.0 / expected);
    return true;
}

bool chain_expected_sats_per_day(double hashrate_ghs, double *sats)
{
    if (!s_data.valid || s_data.hashvalue_sats_ths <= 0.0 || hashrate_ghs <= 0.0)
    {
        return false;
    }
    if (sats)
    {
        *sats = s_data.hashvalue_sats_ths * (hashrate_ghs / 1000.0);
    }
    return true;
}

/* ---- HTTP ---- */

static esp_err_t chain_http_event(esp_http_client_event_t *evt)
{
    if (evt->event_id == HTTP_EVENT_ON_DATA && evt->data && evt->data_len > 0)
    {
        int copy_len = evt->data_len;
        if (s_http_len + copy_len >= CHAIN_HTTP_BUF_SIZE)
        {
            copy_len = CHAIN_HTTP_BUF_SIZE - s_http_len - 1;
        }
        if (copy_len > 0)
        {
            memcpy(s_http_buf + s_http_len, evt->data, copy_len);
            s_http_len += copy_len;
            s_http_buf[s_http_len] = 0;
        }
    }
    return ESP_OK;
}

/* GET into s_http_buf. False on transport error, non-2xx, or an empty body.
 * The buffer is NUL-terminated whenever this returns true. */
static bool chain_get(const char *url)
{
    esp_http_client_config_t config = {
        .url = url,
        .event_handler = chain_http_event,
        .timeout_ms = 12000,
    };

#if defined(CONFIG_ESP_TLS_SKIP_SERVER_CERT_VERIFY) && CONFIG_ESP_TLS_SKIP_SERVER_CERT_VERIFY
    config.skip_cert_common_name_check = true;
#else
    config.crt_bundle_attach = esp_crt_bundle_attach;
#endif

    s_http_len = 0;
    s_http_buf[0] = 0;

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client)
    {
        return false;
    }

    esp_err_t err = esp_http_client_perform(client);
    int status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);

    return err == ESP_OK && status >= 200 && status < 300 && s_http_len > 0;
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

/* A vecs query answers as an array of one-element arrays, in the order the
 * ids were asked for: [[1.25e14],[8.6e20],[0.0388]]. Pull the nth number out
 * positionally rather than walking a JSON tree. An empty element array means
 * that series had no value for the bucket. */
static bool vecs_nth(const char *body, int n, double *out)
{
    const char *p = strchr(body, '[');
    if (!p)
    {
        return false;
    }
    p++;                                  /* step inside the outer array */

    for (int i = 0; i <= n; i++)
    {
        p = strchr(p, '[');
        if (!p)
        {
            return false;
        }
        p++;
    }

    if (*p == ']')
    {
        return false;
    }

    char *end = NULL;
    const double v = strtod(p, &end);
    if (end == p || !isfinite(v))
    {
        return false;
    }
    *out = v;
    return true;
}

/* The halving follows from the height, so it never needs an API. */
static void chain_fill_halving(chain_data_t *d, long long height)
{
    if (height <= 0)
    {
        return;
    }
    const long long epoch = height / CHAIN_HALVING_INTERVAL;
    const long long next  = (epoch + 1) * CHAIN_HALVING_INTERVAL;
    d->halving_epoch     = (int32_t)epoch;
    d->blocks_to_halving = (int32_t)(next - height);
    d->days_to_halving   = (double)d->blocks_to_halving * 10.0 / 1440.0;
}

/* /api/v1/difficulty-adjustment, served by both providers. */
static bool chain_fetch_retarget(chain_data_t *d)
{
    char url[160];
    snprintf(url, sizeof(url), "%s/api/v1/difficulty-adjustment", chain_base_url());
    if (!chain_get(url))
    {
        return false;
    }

    double v = 0.0;
    if (json_get_double(s_http_buf, "\"remainingBlocks\":", &v))
    {
        d->retarget_blocks_left = (int32_t)v;
    }
    if (json_get_double(s_http_buf, "\"difficultyChange\":", &v))
    {
        d->retarget_change_pct = v;
    }
    if (json_get_double(s_http_buf, "\"progressPercent\":", &v))
    {
        d->retarget_progress_pct = v;
    }
    if (json_get_double(s_http_buf, "\"remainingTime\":", &v))
    {
        d->retarget_eta_seconds = (int64_t)(v / 1000.0);   /* ms in the API */
    }
    return true;
}

/* One vecs query answers difficulty, network hashrate and both hashprice
 * figures. minute10 rather than day1 so the numbers move during the day. */
static bool chain_fetch_bitview(chain_data_t *d)
{
    char url[320];
    snprintf(url, sizeof(url),
             "%s/api/vecs/query?i=minute10&f=-1"
             "&ids=difficulty,hash_rate,hash_price_ths,hash_value_ths",
             chain_base_url());
    if (!chain_get(url))
    {
        return false;
    }

    double v = 0.0;
    bool got_difficulty = false;

    if (vecs_nth(s_http_buf, 0, &v) && v > 0.0)
    {
        d->difficulty = v;
        got_difficulty = true;
    }
    if (vecs_nth(s_http_buf, 1, &v) && v > 0.0)
    {
        d->network_hashrate = v;
    }
    if (vecs_nth(s_http_buf, 2, &v) && v > 0.0)
    {
        d->hashprice_usd_ths = v;
    }
    if (vecs_nth(s_http_buf, 3, &v) && v > 0.0)
    {
        d->hashvalue_sats_ths = v;
    }
    return got_difficulty;
}

/* mempool.space carries no hashprice series, so difficulty and hashrate come
 * from the mining summary and the two hashprice fields stay zero. */
static bool chain_fetch_mempool(chain_data_t *d)
{
    char url[160];
    snprintf(url, sizeof(url), "%s/api/v1/mining/hashrate/3d", chain_base_url());
    if (!chain_get(url))
    {
        return false;
    }

    double v = 0.0;
    bool got_difficulty = false;

    if (json_get_double(s_http_buf, "\"currentDifficulty\":", &v) && v > 0.0)
    {
        d->difficulty = v;
        got_difficulty = true;
    }
    if (json_get_double(s_http_buf, "\"currentHashrate\":", &v) && v > 0.0)
    {
        d->network_hashrate = v;
    }
    return got_difficulty;
}

/* Recommended fees. 90 bytes, and both providers serve it. */
static bool chain_fetch_fees(chain_data_t *d)
{
    char url[160];
    snprintf(url, sizeof(url), "%s/api/v1/fees/recommended", chain_base_url());
    if (!chain_get(url))
    {
        return false;
    }

    double v = 0.0;
    bool got = false;
    if (json_get_double(s_http_buf, "\"fastestFee\":", &v))   { d->fee_fastest   = v; got = true; }
    if (json_get_double(s_http_buf, "\"halfHourFee\":", &v))  { d->fee_half_hour = v; }
    if (json_get_double(s_http_buf, "\"hourFee\":", &v))      { d->fee_hour      = v; }
    if (json_get_double(s_http_buf, "\"economyFee\":", &v))   { d->fee_economy   = v; }
    if (json_get_double(s_http_buf, "\"minimumFee\":", &v))   { d->fee_minimum   = v; }
    d->fees_valid = got;
    return got;
}

/* Mempool backlog. The body is about 13 KB because of a thousand-entry fee
 * histogram, but count, vsize and total_fee are the first sixty bytes of it,
 * so the truncating buffer costs nothing we read. */
static bool chain_fetch_mempool_backlog(chain_data_t *d)
{
    char url[160];
    snprintf(url, sizeof(url), "%s/api/mempool", chain_base_url());
    if (!chain_get(url))
    {
        return false;
    }

    double v = 0.0;
    if (!json_get_double(s_http_buf, "\"count\":", &v) || v <= 0.0)
    {
        return false;
    }
    d->mempool_tx_count = (int32_t) v;
    if (json_get_double(s_http_buf, "\"vsize\":", &v))     { d->mempool_vsize     = (int64_t) v; }
    if (json_get_double(s_http_buf, "\"total_fee\":", &v)) { d->mempool_total_fee = (int64_t) v; }
    return true;
}

/* A month of daily closes for the sparkline, in one 269 byte response. The
 * vecs endpoint is bitview only, so on mempool.space this leaves the history
 * empty and the price screen draws no chart rather than a misleading one. */
static bool chain_fetch_price_history(chain_data_t *d)
{
    if (s_source != CHAIN_SRC_BITVIEW)
    {
        d->price_history_len = 0;
        return false;
    }

    char url[220];
    snprintf(url, sizeof(url), "%s/api/vecs/query?i=day1&ids=price_close&f=-%d",
             chain_base_url(), CHAIN_PRICE_HISTORY);
    if (!chain_get(url))
    {
        return false;
    }

    /* One series, so the body is a flat list of numbers: [64573.41,...]. */
    const char *p = strchr(s_http_buf, '[');
    if (!p)
    {
        return false;
    }
    p++;

    int n = 0;
    while (n < CHAIN_PRICE_HISTORY)
    {
        char *end = NULL;
        const double v = strtod(p, &end);
        if (end == p || !isfinite(v))
        {
            break;
        }
        d->price_history[n++] = (float) v;
        p = end;
        while (*p == ' ') { p++; }
        if (*p != ',') { break; }
        p++;
    }
    d->price_history_len = n;
    return n > 0;
}

/* Balance of the watched address. Skipped entirely when none is set. */
static bool chain_fetch_address(void)
{
    if (!s_addr.watching || !s_addr.address[0])
    {
        return false;
    }

    char url[200];
    snprintf(url, sizeof(url), "%s/api/address/%s", chain_base_url(), s_addr.address);
    if (!chain_get(url))
    {
        return false;
    }

    /* chain_stats comes before mempool_stats, and both carry a "balance" key,
     * so read the confirmed one from the front and the pending delta from the
     * mempool object rather than from the first match. */
    double v = 0.0;
    if (!json_get_double(s_http_buf, "\"balance\":", &v))
    {
        return false;
    }

    chain_address_t next = s_addr;
    next.confirmed_sats = (int64_t) v;
    if (json_get_double(s_http_buf, "\"tx_count\":", &v)) { next.tx_count = (int32_t) v; }

    const char *mp = strstr(s_http_buf, "\"mempool_stats\"");
    next.pending_sats = 0;
    if (mp && json_get_double(mp, "\"balance_delta\":", &v))
    {
        next.pending_sats = (int64_t) v;
    }
    next.valid = true;

    if (lvgl_port_lock(-1))
    {
        s_addr = next;
        lvgl_port_unlock();
    }
    else
    {
        s_addr = next;
    }
    return true;
}

/* The tip height, for the halving countdown. Plain text on both providers. */
static bool chain_fetch_tip(long long *height)
{
    char url[160];
    snprintf(url, sizeof(url), "%s/api/blocks/tip/height", chain_base_url());
    if (!chain_get(url))
    {
        return false;
    }
    const long long h = strtoll(s_http_buf, NULL, 10);
    if (h <= 0)
    {
        return false;
    }
    *height = h;
    return true;
}

static bool chain_fetch_once(void)
{
    /* Start from the published snapshot so a provider that answers only some
     * of these keeps the rest rather than blanking the screen. */
    chain_data_t next = s_data;

    const bool got_chain = (s_source == CHAIN_SRC_BITVIEW)
                               ? chain_fetch_bitview(&next)
                               : chain_fetch_mempool(&next);

    long long height = 0;
    if (chain_fetch_tip(&height))
    {
        chain_fill_halving(&next, height);
    }

    chain_fetch_retarget(&next);
    chain_fetch_fees(&next);
    chain_fetch_mempool_backlog(&next);
    chain_fetch_price_history(&next);

    if (!got_chain && !next.valid)
    {
        return false;
    }

    next.valid = true;
    next.fetched_at = (int64_t)time(NULL);

    /* Screens read the snapshot on the LVGL task, so publish it whole. */
    if (lvgl_port_lock(-1))
    {
        s_data = next;
        lvgl_port_unlock();
    }
    else
    {
        s_data = next;
    }

    chain_fetch_address();

    ESP_LOGI(TAG, "%s: diff %.4g, %d blocks to halving, retarget %+.2f%% in %d",
             chain_source_name(s_source), next.difficulty,
             (int)next.blocks_to_halving, next.retarget_change_pct,
             (int)next.retarget_blocks_left);
    return true;
}

static void chain_task(void *arg)
{
    (void)arg;
    for (;;)
    {
        const bool ok = chain_fetch_once();
        const uint32_t wait_ms = ok ? CHAIN_REFRESH_MS : CHAIN_RETRY_MS;
        /* A source change notifies us, so a switch takes effect at once
         * rather than at the end of the refresh interval. */
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(wait_ms));
    }
}

void chain_init(void)
{
    if (s_task)
    {
        return;
    }
    memset(&s_data, 0, sizeof(s_data));
    chain_load_prefs();
    ESP_LOGI(TAG, "source %s, currency %s",
             chain_source_name(s_source), chain_ccy_code(s_ccy));
    xTaskCreate(chain_task, "chain", 5120, NULL, 4, &s_task);
}
