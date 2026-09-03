#ifndef PRICE_H
#define PRICE_H

#include "lvgl.h"

void price_screen_create(void);
void price_screen_destroy(void);
lv_obj_t *price_get_screen(void);

/* Cached values for widgets that show the price without opening this screen.
 * price_ensure_task() starts the fetch loop if the screen never did. */
const char *price_get_text(void);
const char *price_get_status(void);
void        price_ensure_task(void);

void price_home_clicked(lv_event_t *e);
void price_block_clicked(lv_event_t *e);
void price_clock_clicked(lv_event_t *e);
void price_mempool_clicked(lv_event_t *e);
void price_wifi_clicked(lv_event_t *e);
void price_settings_clicked(lv_event_t *e);
void price_night_clicked(lv_event_t *e);

void price_odds_clicked(lv_event_t * e);

/* Relabel and refetch after the currency setting changes. Call on the
 * LVGL task. */
void price_currency_changed(void);

#endif // PRICE_H
