/**
 * @file weather.c
 *
 * See weather.h. Both sources are key-free; the only per-source difference
 * that matters operationally is that met.no requires a User-Agent naming the
 * application, and will refuse requests without one.
 */

#include "weather.h"

#include "esp_crt_bundle.h"
#include "esp_event.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "wifi.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *TAG = "weather";

#define WEATHER_HTTP_BUF_SIZE 8192
#define WEATHER_REFRESH_MS    (15 * 60 * 1000)
#define WEATHER_RETRY_MS      (60 * 1000)

#define WEATHER_NVS_NS     "weather"
#define WEATHER_NVS_SRC    "src"
#define WEATHER_NVS_NAME   "name"
#define WEATHER_NVS_LAT    "lat"
#define WEATHER_NVS_LON    "lon"

/* met.no's terms require a User-Agent that identifies the application and a
 * way to reach whoever runs it. Requests without one are rejected. */
#define WEATHER_USER_AGENT "GT-TT-Display/1 (github.com/bayanimills/GT-TT-Display)"

static char   s_http_buf[WEATHER_HTTP_BUF_SIZE];
static int    s_http_len = 0;
static bool   s_netif_ready = false;

static weather_data_t   s_data;
static weather_place_t  s_place;
static weather_source_t s_source = WEATHER_SRC_OPEN_METEO;
static bool             s_prefs_loaded = false;
static TaskHandle_t     s_task = NULL;

static weather_place_t s_results[WEATHER_SEARCH_MAX];
static int             s_result_count = 0;
static volatile bool   s_search_busy = false;
static char            s_search_query[WEATHER_PLACE_MAX];
static volatile bool   s_search_pending = false;

/* ---- preferences ---- */

static void weather_prefs_load(void)
{
    if (s_prefs_loaded) return;
    s_prefs_loaded = true;

    nvs_handle_t h;
    if (nvs_open(WEATHER_NVS_NS, NVS_READONLY, &h) != ESP_OK) return;

    uint8_t src = 0;
    if (nvs_get_u8(h, WEATHER_NVS_SRC, &src) == ESP_OK && src < WEATHER_SRC_COUNT) {
        s_source = (weather_source_t) src;
    }
    size_t len = sizeof(s_place.name);
    if (nvs_get_str(h, WEATHER_NVS_NAME, s_place.name, &len) != ESP_OK) {
        s_place.name[0] = 0;
    }
    /* Coordinates are stored as microdegrees so NVS only has to carry ints. */
    int32_t v = 0;
    if (nvs_get_i32(h, WEATHER_NVS_LAT, &v) == ESP_OK) s_place.lat = (double) v / 1e6;
    if (nvs_get_i32(h, WEATHER_NVS_LON, &v) == ESP_OK) s_place.lon = (double) v / 1e6;
    nvs_close(h);
}

static void weather_prefs_save(void)
{
    nvs_handle_t h;
    if (nvs_open(WEATHER_NVS_NS, NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_u8(h, WEATHER_NVS_SRC, (uint8_t) s_source);
    nvs_set_str(h, WEATHER_NVS_NAME, s_place.name);
    nvs_set_i32(h, WEATHER_NVS_LAT, (int32_t) lround(s_place.lat * 1e6));
    nvs_set_i32(h, WEATHER_NVS_LON, (int32_t) lround(s_place.lon * 1e6));
    nvs_commit(h);
    nvs_close(h);
}

/* ---- HTTP ---- */

static esp_err_t weather_http_event(esp_http_client_event_t *evt)
{
    if (evt->event_id == HTTP_EVENT_ON_DATA && evt->data && evt->data_len > 0) {
        int copy_len = evt->data_len;
        if (s_http_len + copy_len >= WEATHER_HTTP_BUF_SIZE) {
            copy_len = WEATHER_HTTP_BUF_SIZE - s_http_len - 1;
        }
        if (copy_len > 0) {
            memcpy(s_http_buf + s_http_len, evt->data, copy_len);
            s_http_len += copy_len;
            s_http_buf[s_http_len] = 0;
        }
    }
    return ESP_OK;
}

/* Same guard as chain.c: without it the first fetch can run before lwIP has a
 * mailbox, and the stack asserts rather than returning an error. */
static bool weather_net_ready(void)
{
    if (!s_netif_ready) {
        esp_err_t ret = esp_netif_init();
        if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) return false;
        ret = esp_event_loop_create_default();
        if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) return false;
        s_netif_ready = true;
    }
    return wifi_is_connected();
}

static bool weather_get(const char *url)
{
    esp_http_client_config_t config = {
        .url = url,
        .event_handler = weather_http_event,
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
    if (!client) return false;
    esp_http_client_set_header(client, "User-Agent", WEATHER_USER_AGENT);

    esp_err_t err = esp_http_client_perform(client);
    int status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);

    return err == ESP_OK && status >= 200 && status < 300 && s_http_len > 0;
}

/* ---- tiny JSON readers ----
 *
 * Both responses are small, flat and machine-generated, so a scan for the key
 * is enough and a parser would be a lot of flash for no extra certainty. */

static bool json_number_after(const char *from, const char *key, double *out)
{
    if (!from || !key || !out) return false;
    const char *p = strstr(from, key);
    if (!p) return false;
    p += strlen(key);
    while (*p == ' ' || *p == ':' || *p == '"') p++;
    char *end = NULL;
    double v = strtod(p, &end);
    if (end == p) return false;
    *out = v;
    return true;
}

static bool json_string_after(const char *from, const char *key, char *out, size_t n)
{
    if (!from || !key || !out || n == 0) return false;
    const char *p = strstr(from, key);
    if (!p) return false;
    p += strlen(key);
    while (*p == ' ' || *p == ':') p++;
    if (*p != '"') return false;
    p++;
    size_t i = 0;
    while (*p && *p != '"' && i + 1 < n) {
        /* Place names carry escapes rarely; pass the common ones through. */
        if (*p == '\\' && p[1]) p++;
        out[i++] = *p++;
    }
    out[i] = 0;
    return i > 0;
}

/* ---- condition mapping ---- */

static weather_cond_t cond_from_wmo(int code)
{
    if (code == 0) return WEATHER_COND_CLEAR;
    if (code == 1 || code == 2) return WEATHER_COND_PARTLY;
    if (code == 3) return WEATHER_COND_CLOUD;
    if (code == 45 || code == 48) return WEATHER_COND_FOG;
    if (code >= 51 && code <= 57) return WEATHER_COND_DRIZZLE;
    if ((code >= 61 && code <= 67) || (code >= 80 && code <= 82)) return WEATHER_COND_RAIN;
    if ((code >= 71 && code <= 77) || code == 85 || code == 86) return WEATHER_COND_SNOW;
    if (code >= 95) return WEATHER_COND_STORM;
    return WEATHER_COND_UNKNOWN;
}

/* met.no reports a symbol name rather than a numeric code. */
static weather_cond_t cond_from_symbol(const char *symbol)
{
    if (!symbol || !symbol[0]) return WEATHER_COND_UNKNOWN;
    if (strstr(symbol, "thunder"))     return WEATHER_COND_STORM;
    if (strstr(symbol, "snow"))        return WEATHER_COND_SNOW;
    if (strstr(symbol, "sleet"))       return WEATHER_COND_SNOW;
    if (strstr(symbol, "rain"))        return WEATHER_COND_RAIN;
    if (strstr(symbol, "drizzle"))     return WEATHER_COND_DRIZZLE;
    if (strstr(symbol, "fog"))         return WEATHER_COND_FOG;
    if (strstr(symbol, "cloudy"))      return WEATHER_COND_CLOUD;
    if (strstr(symbol, "partlycloudy")) return WEATHER_COND_PARTLY;
    if (strstr(symbol, "fair"))        return WEATHER_COND_PARTLY;
    if (strstr(symbol, "clearsky"))    return WEATHER_COND_CLEAR;
    return WEATHER_COND_UNKNOWN;
}

const char *weather_cond_text(weather_cond_t cond)
{
    switch (cond) {
    case WEATHER_COND_CLEAR:   return "Clear";
    case WEATHER_COND_PARTLY:  return "Partly cloudy";
    case WEATHER_COND_CLOUD:   return "Cloudy";
    case WEATHER_COND_FOG:     return "Fog";
    case WEATHER_COND_DRIZZLE: return "Drizzle";
    case WEATHER_COND_RAIN:    return "Rain";
    case WEATHER_COND_SNOW:    return "Snow";
    case WEATHER_COND_STORM:   return "Thunderstorms";
    default:                   return "--";
    }
}

const char *weather_source_name(weather_source_t source)
{
    switch (source) {
    case WEATHER_SRC_MET_NO: return "met.no";
    default:                 return "Open-Meteo";
    }
}

/* ---- parsing ---- */

static bool parse_open_meteo(weather_data_t *out)
{
    const char *cur = strstr(s_http_buf, "\"current\"");
    if (!cur) return false;

    double v = 0;
    if (!json_number_after(cur, "\"temperature_2m\"", &v)) return false;
    out->temp_c = (float) v;
    out->feels_c = json_number_after(cur, "\"apparent_temperature\"", &v) ? (float) v : out->temp_c;
    out->humidity_pct = json_number_after(cur, "\"relative_humidity_2m\"", &v) ? (int) v : -1;
    out->wind_kph = json_number_after(cur, "\"wind_speed_10m\"", &v) ? (float) v : 0.0f;
    out->cond = json_number_after(cur, "\"weather_code\"", &v)
              ? cond_from_wmo((int) v) : WEATHER_COND_UNKNOWN;

    /* The daily block is three parallel arrays, so walk them positionally. */
    const char *daily = strstr(s_http_buf, "\"daily\"");
    out->day_count = 0;
    if (daily) {
        const char *codes = strstr(daily, "\"weather_code\"");
        const char *highs = strstr(daily, "\"temperature_2m_max\"");
        const char *lows  = strstr(daily, "\"temperature_2m_min\"");
        if (codes && highs && lows) {
            codes = strchr(codes, '[');
            highs = strchr(highs, '[');
            lows  = strchr(lows, '[');
        }
        while (codes && highs && lows && out->day_count < WEATHER_FORECAST_DAYS) {
            char *ce = NULL, *he = NULL, *le = NULL;
            double c = strtod(codes + 1, &ce);
            double hi = strtod(highs + 1, &he);
            double lo = strtod(lows + 1, &le);
            if (ce == codes + 1 || he == highs + 1 || le == lows + 1) break;
            weather_day_t *d = &out->days[out->day_count++];
            d->cond = cond_from_wmo((int) c);
            d->high_c = (float) hi;
            d->low_c = (float) lo;
            codes = strchr(ce, ',');
            highs = strchr(he, ',');
            lows = strchr(le, ',');
        }
    }
    return true;
}

static bool parse_met_no(weather_data_t *out)
{
    /* The first timeseries entry is the current hour. */
    const char *entry = strstr(s_http_buf, "\"timeseries\"");
    if (!entry) return false;
    const char *inst = strstr(entry, "\"instant\"");
    if (!inst) return false;

    double v = 0;
    if (!json_number_after(inst, "\"air_temperature\"", &v)) return false;
    out->temp_c = (float) v;
    out->feels_c = out->temp_c;   /* met.no compact does not publish one */
    out->humidity_pct = json_number_after(inst, "\"relative_humidity\"", &v) ? (int) v : -1;
    /* m/s in the feed, and the rest of this screen is in km/h. */
    out->wind_kph = json_number_after(inst, "\"wind_speed\"", &v) ? (float) (v * 3.6) : 0.0f;

    char symbol[32] = {0};
    out->cond = json_string_after(inst, "\"symbol_code\"", symbol, sizeof(symbol))
              ? cond_from_symbol(symbol) : WEATHER_COND_UNKNOWN;
    if (out->cond == WEATHER_COND_UNKNOWN &&
        json_string_after(entry, "\"symbol_code\"", symbol, sizeof(symbol))) {
        out->cond = cond_from_symbol(symbol);
    }

    /* next_6_hours carries min/max, which is the closest thing the compact
     * feed has to a daily high and low without summing the whole series. */
    out->day_count = 0;
    const char *six = strstr(entry, "\"next_6_hours\"");
    double hi = 0, lo = 0;
    if (six && json_number_after(six, "\"air_temperature_max\"", &hi) &&
        json_number_after(six, "\"air_temperature_min\"", &lo)) {
        weather_day_t *d = &out->days[out->day_count++];
        d->cond = json_string_after(six, "\"symbol_code\"", symbol, sizeof(symbol))
                ? cond_from_symbol(symbol) : out->cond;
        d->high_c = (float) hi;
        d->low_c  = (float) lo;
    }
    /* No slot at all beats a slot showing the current temperature twice. */
    return true;
}

/* ---- fetch ---- */

static bool weather_fetch_once(void)
{
    if (s_place.name[0] == 0) {
        strncpy(s_data.error, "No location set", sizeof(s_data.error) - 1);
        return false;
    }

    char url[256];
    if (s_source == WEATHER_SRC_MET_NO) {
        snprintf(url, sizeof(url),
                 /* "complete", not "compact": only the complete feed carries
                  * air_temperature_max/min, and without them the forecast slot
                  * fell back to the current temperature and printed it twice as
                  * though it were a range. */
                 "https://api.met.no/weatherapi/locationforecast/2.0/complete"
                 "?lat=%.4f&lon=%.4f", s_place.lat, s_place.lon);
    } else {
        snprintf(url, sizeof(url),
                 "https://api.open-meteo.com/v1/forecast?latitude=%.4f&longitude=%.4f"
                 "&current=temperature_2m,relative_humidity_2m,apparent_temperature,"
                 "weather_code,wind_speed_10m"
                 "&daily=weather_code,temperature_2m_max,temperature_2m_min"
                 "&timezone=auto&forecast_days=%d",
                 s_place.lat, s_place.lon, WEATHER_FORECAST_DAYS);
    }

    if (!weather_get(url)) {
        snprintf(s_data.error, sizeof(s_data.error), "%s unreachable",
                 weather_source_name(s_source));
        return false;
    }

    weather_data_t fresh = {0};
    const bool ok = (s_source == WEATHER_SRC_MET_NO) ? parse_met_no(&fresh)
                                                     : parse_open_meteo(&fresh);
    if (!ok) {
        snprintf(s_data.error, sizeof(s_data.error), "%s sent no reading",
                 weather_source_name(s_source));
        return false;
    }

    fresh.valid = true;
    strncpy(fresh.place, s_place.name, sizeof(fresh.place) - 1);
    s_data = fresh;
    ESP_LOGI(TAG, "%s: %.1fC %s at %s", weather_source_name(s_source),
             (double) s_data.temp_c, weather_cond_text(s_data.cond), s_data.place);
    return true;
}

int weather_search(const char *query, weather_place_t *out, int max)
{
    if (!query || !query[0] || !out || max <= 0) return 0;
    if (!weather_net_ready()) return 0;

    /* Percent-encode the few characters a place name realistically carries. */
    char q[96];
    size_t qi = 0;
    for (const char *p = query; *p && qi + 4 < sizeof(q); p++) {
        if (*p == ' ') { q[qi++] = '+'; }
        else if ((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') ||
                 (*p >= '0' && *p <= '9') || *p == '-' || *p == '.') { q[qi++] = *p; }
        else { qi += snprintf(q + qi, sizeof(q) - qi, "%%%02X", (unsigned char) *p); }
    }
    q[qi] = 0;
    if (qi == 0) return 0;

    char url[192];
    snprintf(url, sizeof(url),
             "https://geocoding-api.open-meteo.com/v1/search?name=%s&count=%d&format=json",
             q, max > WEATHER_SEARCH_MAX ? WEATHER_SEARCH_MAX : max);
    if (!weather_get(url)) return 0;

    int n = 0;
    const char *p = strstr(s_http_buf, "\"results\"");
    if (!p) return 0;
    while (n < max && n < WEATHER_SEARCH_MAX) {
        const char *name = strstr(p, "\"name\"");
        if (!name) break;
        char place[WEATHER_PLACE_MAX] = {0};
        if (!json_string_after(name, "\"name\"", place, sizeof(place))) break;

        double lat = 0, lon = 0;
        if (!json_number_after(name, "\"latitude\"", &lat) ||
            !json_number_after(name, "\"longitude\"", &lon)) break;

        /* Country disambiguates the many places called the same thing. The
         * two fields are bounded explicitly rather than left to fill the
         * buffer between them: 28 + 2 + 16 fits 48 with room, and the compiler
         * can see that it does. */
        char country[32] = {0};
        if (json_string_after(name, "\"country\"", country, sizeof(country)) && country[0]) {
            snprintf(out[n].name, sizeof(out[n].name), "%.28s, %.16s", place, country);
        } else {
            snprintf(out[n].name, sizeof(out[n].name), "%s", place);
        }
        out[n].lat = lat;
        out[n].lon = lon;
        n++;
        p = name + 6;
    }
    return n;
}

void weather_search_begin(const char *query)
{
    if (!query || !query[0] || s_search_busy) return;
    strncpy(s_search_query, query, sizeof(s_search_query) - 1);
    s_search_query[sizeof(s_search_query) - 1] = 0;
    s_result_count = 0;
    s_search_busy = true;
    s_search_pending = true;
    if (s_task) xTaskNotifyGive(s_task);
}

bool weather_search_busy(void) { return s_search_busy; }

int weather_search_results(const weather_place_t **out)
{
    if (out) *out = s_results;
    return s_result_count;
}

static void weather_task(void *arg)
{
    (void) arg;
    for (;;) {
        if (!weather_net_ready()) {
            vTaskDelay(pdMS_TO_TICKS(2000));
            continue;
        }

        /* A search is what the user is waiting on, so it jumps the queue. */
        if (s_search_pending) {
            s_search_pending = false;
            s_result_count = weather_search(s_search_query, s_results, WEATHER_SEARCH_MAX);
            s_search_busy = false;
            continue;
        }

        const bool ok = weather_fetch_once();
        if (ok) s_data.error[0] = 0;
        const uint32_t wait = ok ? WEATHER_REFRESH_MS : WEATHER_RETRY_MS;
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(wait));
    }
}

void weather_init(void)
{
    if (s_task) return;
    weather_prefs_load();
    xTaskCreate(weather_task, "weather", 5120, NULL, 3, &s_task);
}

const weather_data_t *weather_data(void)
{
    weather_prefs_load();
    return &s_data;
}

weather_source_t weather_get_source(void)
{
    weather_prefs_load();
    return s_source;
}

void weather_set_source(weather_source_t source)
{
    weather_prefs_load();
    if (source >= WEATHER_SRC_COUNT || source == s_source) return;
    s_source = source;
    weather_prefs_save();
    /* The reading on screen came from the other source; drop it rather than
     * leaving it under a label that now names a source that did not produce it. */
    s_data.valid = false;
    s_data.error[0] = 0;
    weather_refresh();
}

const weather_place_t *weather_get_place(void)
{
    weather_prefs_load();
    return &s_place;
}

void weather_set_place(const weather_place_t *place)
{
    if (!place || !place->name[0]) return;
    weather_prefs_load();
    s_place = *place;
    weather_prefs_save();
    s_data.valid = false;
    s_data.error[0] = 0;
    weather_refresh();
}

void weather_refresh(void)
{
    if (s_task) xTaskNotifyGive(s_task);
}
