#ifndef POOLPING_H
#define POOLPING_H

#include <stdbool.h>
#include <stdint.h>

/* Latency to known mining pools.
 *
 * Not ICMP: there is no raw socket here, and a ping would answer a different
 * question anyway. This times a TCP connect to the pool's stratum port, which
 * is what a miner actually does, so it includes the handshake a share has to
 * survive rather than just whether the host is up.
 *
 * The display cannot change which pool the miner uses. BAP has no pool
 * parameter at all: pool, poolPort and poolUser are only ever reported, never
 * set. So this measures and ranks; switching is still done in AxeOS. */

typedef struct {
    const char *host;
    uint16_t    port;
    const char *label;   /* what the screen shows */
    bool        solo;    /* solo pool rather than a pooled one */
} pool_entry_t;

/* Latency values below zero are states rather than measurements. */
#define POOLPING_PENDING (-1)
#define POOLPING_FAILED  (-2)

int                 poolping_count(void);
const pool_entry_t *poolping_entry(int i);

/* Last measurement in ms, or one of the states above. */
int poolping_latency_ms(int i);

/* Entry index for a given place in the ranking, fastest first. Unmeasured and
 * failed entries sort last, so the list is stable while the first sweep is
 * still running. */
int poolping_ranked(int rank);

/* Seconds since the last completed sweep, or -1 if none has finished. */
int poolping_age_seconds(void);

/* Start the sweep task. Safe to call once, from app_main; it waits for the
 * radio and re-measures on an interval. */
void poolping_start(void);

/* Measure now rather than at the next interval, for a screen that has just
 * opened. */
void poolping_refresh_now(void);

#endif /* POOLPING_H */
