#ifndef ODDS_H
#define ODDS_H

#include "lvgl.h"

/* The solo odds screen.
 *
 * Combines the two halves nothing else on the display puts together: the
 * hashrate the miner reports over BAP, and the network difficulty chain.c
 * fetches. Everything shown is derived on the panel, so the figures keep
 * updating between fetches as the miner's own hashrate moves. */

void      odds_screen_create(void);
void      odds_screen_destroy(void);
lv_obj_t *odds_get_screen(void);

/* Repaint from the current BAP stats and chain snapshot. Cheap, and safe to
 * call when the screen is not built. */
void odds_refresh(void);

void odds_home_clicked(lv_event_t *e);
void odds_block_clicked(lv_event_t *e);
void odds_mempool_clicked(lv_event_t *e);
void odds_clock_clicked(lv_event_t *e);
void odds_price_clicked(lv_event_t *e);
void odds_wifi_clicked(lv_event_t *e);
void odds_settings_clicked(lv_event_t *e);
void odds_night_clicked(lv_event_t *e);

#endif /* ODDS_H */
