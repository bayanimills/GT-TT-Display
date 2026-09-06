/**
 * @file weather.h
 *
 * Current conditions and a short forecast for one saved location.
 *
 * Two sources, both usable without an account: Open-Meteo and met.no (Yr).
 * Neither needs an API key, which matters here because the only way to enter
 * one would be to type it on the panel's on-screen keyboard.
 *
 * Open-Meteo also publishes a free geocoding endpoint, so the location is
 * chosen by searching for a place name rather than by entering coordinates.
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    WEATHER_SRC_OPEN_METEO = 0,
    WEATHER_SRC_MET_NO,
    WEATHER_SRC_COUNT,
} weather_source_t;

/* WMO code buckets, which both sources map onto. Kept coarse deliberately:
 * the panel has one line to say what it is doing outside. */
typedef enum {
    WEATHER_COND_UNKNOWN = 0,
    WEATHER_COND_CLEAR,
    WEATHER_COND_PARTLY,
    WEATHER_COND_CLOUD,
    WEATHER_COND_FOG,
    WEATHER_COND_DRIZZLE,
    WEATHER_COND_RAIN,
    WEATHER_COND_SNOW,
    WEATHER_COND_STORM,
} weather_cond_t;

#define WEATHER_PLACE_MAX   48
#define WEATHER_FORECAST_DAYS 3
#define WEATHER_SEARCH_MAX  5

typedef struct {
    weather_cond_t cond;
    float          high_c;
    float          low_c;
} weather_day_t;

typedef struct {
    bool           valid;       /* a fetch has landed at least once */
    weather_cond_t cond;
    float          temp_c;
    float          feels_c;
    float          wind_kph;
    int            humidity_pct;
    weather_day_t  days[WEATHER_FORECAST_DAYS];
    int            day_count;
    char           place[WEATHER_PLACE_MAX];
    char           error[64];   /* empty when the last fetch succeeded */
} weather_data_t;

typedef struct {
    char   name[WEATHER_PLACE_MAX];
    double lat;
    double lon;
} weather_place_t;

/* Start the fetch task. Safe to call more than once. */
void weather_init(void);

/* A snapshot of the last successful fetch. Never NULL. */
const weather_data_t *weather_data(void);

weather_source_t weather_get_source(void);
void weather_set_source(weather_source_t source);
const char *weather_source_name(weather_source_t source);

/* The saved location. `name` is empty until one has been chosen. */
const weather_place_t *weather_get_place(void);
void weather_set_place(const weather_place_t *place);

/* Ask for a refresh now rather than at the next interval. */
void weather_refresh(void);

/* Look a place name up through Open-Meteo's geocoding endpoint. Blocking, so
 * call it from a task and not from the LVGL callback. Returns how many results
 * were written, up to WEATHER_SEARCH_MAX. */
int weather_search(const char *query, weather_place_t *out, int max);

/* Start a background search whose results are collected with
 * weather_search_results(). The UI cannot block, so the keyboard's Go key
 * kicks this and the panel polls. */
void weather_search_begin(const char *query);
bool weather_search_busy(void);
int weather_search_results(const weather_place_t **out);

const char *weather_cond_text(weather_cond_t cond);

#ifdef __cplusplus
}
#endif
