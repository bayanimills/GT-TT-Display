#ifndef BLOCKFOUND_H
#define BLOCKFOUND_H

#include "lvgl.h"

/* The block found screen.
 *
 * Not reachable from the menu: it appears on its own when this miner solves a
 * block, and it is dismissed by touching it.
 *
 * How it knows, in order of preference.
 *
 * The miner counts the blocks it has solved and pushes that count over BAP as
 * "found_block" whenever it changes. That is authoritative and needs no
 * network, so it is the primary trigger: a count above the last one seen is a
 * block. The first count seen only sets a baseline, because a display plugged
 * into a miner that has already solved something must not celebrate history.
 *
 * As a second path, a best_difficulty at or above the network difficulty is a
 * block by definition, and catches a miner whose counter was reset. It is
 * weaker: best_difficulty is an all time high water mark, so it cannot see a
 * second block luckier than the first, and it needs the network difficulty to
 * have been fetched.
 *
 * Both feed one announcement, and what has been announced is kept in NVS, so
 * a block is shown once rather than on every boot. */

void      blockfound_screen_create(void);
void      blockfound_screen_destroy(void);
lv_obj_t *blockfound_get_screen(void);

/* The miner's own count of solved blocks, as pushed over BAP. This is the
 * authoritative signal: the miner knows, and says so, without the display
 * having to compare a best share against a network target it may not have
 * fetched yet. A count that rises above the last one seen announces a
 * block. The count is kept in NVS so a restart is not a celebration. */
void blockfound_report_count(const char *count);

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
