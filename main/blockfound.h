#ifndef BLOCKFOUND_H
#define BLOCKFOUND_H

#include "lvgl.h"

/* The block found screen.
 *
 * Not reachable from the menu: it appears on its own when this miner solves a
 * block, and it is dismissed by touching it.
 *
 * How it knows. Nothing in BAP announces a solved block, so it is derived.
 * The miner reports best_difficulty, the highest share it has ever produced,
 * and chain.c knows the network difficulty. A share at or above the network
 * difficulty is a block, by definition: that is what solving one means. So
 * when best_difficulty crosses the network target, this fires.
 *
 * The honest limits of that. best_difficulty is an all-time high water mark,
 * so a second block would only register if it were luckier than the first,
 * and a device whose best share predates this firmware will not re-announce
 * it. What has been celebrated is kept in NVS so the screen appears once per
 * block rather than on every boot. */

void      blockfound_screen_create(void);
void      blockfound_screen_destroy(void);
lv_obj_t *blockfound_get_screen(void);

/* Re-evaluate the telemetry and show the screen if a block has been solved
 * that has not been announced. Cheap, and safe to call whenever
 * best_difficulty or the network difficulty changes. */
void blockfound_check(void);

/* Show it regardless, for the simulator and for trying it on a real panel.
 * Either argument may be NULL, in which case the live values are used. */
void blockfound_trigger(const char *height, const char *difficulty);

/* True while it is on screen, so navigation does not fight it. */
bool blockfound_is_showing(void);

#endif /* BLOCKFOUND_H */
