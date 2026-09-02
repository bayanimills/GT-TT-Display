#ifndef MEMPOOL_H
#define MEMPOOL_H

#include "lvgl.h"

void mempool_screen_create(void);
void mempool_screen_destroy(void);
lv_obj_t *mempool_get_screen(void);

/* Latest block summary for widgets: returns false until the first fetch.
 * mempool_ensure_task() starts the fetch loop if the screen never did. */
bool mempool_get_latest(char *fee_out, size_t fee_len, char *detail_out, size_t detail_len);
void mempool_ensure_task(void);

void mempool_home_clicked(lv_event_t *e);
void mempool_block_clicked(lv_event_t *e);
void mempool_clock_clicked(lv_event_t *e);
void mempool_price_clicked(lv_event_t *e);
void mempool_wifi_clicked(lv_event_t *e);
void mempool_settings_clicked(lv_event_t *e);
void mempool_night_clicked(lv_event_t *e);

#endif // MEMPOOL_H
