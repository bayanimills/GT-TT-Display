#ifndef PAYOUT_H
#define PAYOUT_H

#include "lvgl.h"

/* The payout screen.
 *
 * Watches the address the miner is actually mining to, which chain.c takes
 * from the pool user BAP reports. Answers the two questions a solo miner has
 * no other way to ask from the panel: has anything ever landed, and is
 * anything landing now.
 *
 * It is also the check nothing else performs. A Bitaxe shipped with a factory
 * default pool user will mine perfectly and pay someone else; a balance
 * pinned to the address on screen makes that visible instead of silent. */

void      payout_screen_create(void);
void      payout_screen_destroy(void);
lv_obj_t *payout_get_screen(void);

void payout_home_clicked(lv_event_t *e);
void payout_block_clicked(lv_event_t *e);
void payout_mempool_clicked(lv_event_t *e);
void payout_clock_clicked(lv_event_t *e);
void payout_price_clicked(lv_event_t *e);
void payout_odds_clicked(lv_event_t *e);
void payout_wifi_clicked(lv_event_t *e);
void payout_settings_clicked(lv_event_t *e);
void payout_night_clicked(lv_event_t *e);

#endif /* PAYOUT_H */
