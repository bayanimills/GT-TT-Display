#ifndef CHAIN_H
#define CHAIN_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

/* Network-side chain data, and where it comes from.
 *
 * Everything the screens need about the network rather than the miner:
 * difficulty, network hashrate, hashprice, the halving and the next retarget.
 * The miner's own numbers still arrive over BAP and live in home.c.
 *
 * Two providers are supported and both speak the mempool.space REST shape, so
 * mempool.c and price.c only need the base URL. bitview.space additionally
 * exposes BRK's time-series endpoint, which answers difficulty, hashrate,
 * hashprice and the halving countdown in a single ~94 byte response. On
 * mempool.space those come from two larger calls and hashprice is unavailable;
 * chain_have_hashprice() says which case you are in.
 *
 * Nothing here blocks: a background task refreshes on an interval and screens
 * read the last good snapshot through chain_data(). */

typedef enum {
    CHAIN_SRC_MEMPOOL = 0,   /* mempool.space */
    CHAIN_SRC_BITVIEW,       /* bitview.space, the public BRK instance */
    CHAIN_SRC_COUNT
} chain_source_t;

/* Fiat the price screen and every converted figure are shown in. USD is the
 * only currency the chain APIs quote, so the rest are reached by applying
 * fx_to_usd from the same CoinGecko response that carries the price. */
typedef enum {
    CHAIN_CCY_USD = 0,
    CHAIN_CCY_AUD,
    CHAIN_CCY_NZD,
    CHAIN_CCY_GBP,
    CHAIN_CCY_EUR,
    CHAIN_CCY_CAD,
    CHAIN_CCY_JPY,
    CHAIN_CCY_COUNT
} chain_ccy_t;

/* Daily closes kept for the price sparkline: a month of context in a 269 byte
 * response and 120 bytes of state. */
#define CHAIN_PRICE_HISTORY 30

typedef struct {
    /* Chain state. Zero means "not fetched yet"; check valid first. */
    double   difficulty;          /* current network difficulty */
    double   network_hashrate;    /* H/s */
    double   hashprice_usd_ths;   /* USD per TH/s per day, 0 if unavailable */
    double   hashvalue_sats_ths;  /* sats per TH/s per day, 0 if unavailable */

    /* Halving. Derived from the tip height, so exact and always present. */
    int32_t  blocks_to_halving;
    double   days_to_halving;
    int32_t  halving_epoch;

    /* Next difficulty retarget. */
    int32_t  retarget_blocks_left;
    double   retarget_change_pct;   /* estimated, signed */
    double   retarget_progress_pct;
    int64_t  retarget_eta_seconds;

    /* Recommended fees in sat/vB, from the route both providers serve. */
    double   fee_fastest;
    double   fee_half_hour;
    double   fee_hour;
    double   fee_economy;
    double   fee_minimum;
    bool     fees_valid;

    /* Mempool backlog. The response carries a thousand-entry fee histogram
     * after these three; the fetch buffer truncates it and nothing reads it. */
    int32_t  mempool_tx_count;
    int64_t  mempool_vsize;
    int64_t  mempool_total_fee;

    /* Daily USD closes, oldest last. Needs the time-series endpoint, so this
     * stays empty on mempool.space and the sparkline hides itself. */
    float    price_history[CHAIN_PRICE_HISTORY];
    int      price_history_len;

    bool     valid;               /* a fetch has succeeded at least once */
    int64_t  fetched_at;          /* time() of that fetch, 0 if never */
} chain_data_t;

/* A watched address, for the payout screen.
 *
 * Set from the pool user the miner reports over BAP, which on a solo pool is
 * the payout address with the worker name after a dot. Fetched on the same
 * pass as everything else, so it costs one more request and no extra task. */
typedef struct {
    char     address[80];
    int64_t  confirmed_sats;
    int64_t  pending_sats;     /* mempool delta; negative when spending */
    int32_t  tx_count;
    bool     valid;            /* a lookup has succeeded */
    bool     watching;         /* an address has been set */
} chain_address_t;

/* Ignores anything that is not a plausible mainnet address, so a pool user
 * that is a plain username does not become a doomed lookup. Passing the same
 * address twice is free. */
void chain_set_watch_address(const char *addr);
const chain_address_t *chain_address(void);

/* Re-read the watched address on the next task wake, skipping the rest of
 * the cycle. The address is otherwise fetched last of seven requests on a
 * five minute timer, so a screen that opens just after a cycle would show
 * a figure up to five minutes old, or nothing at all on first boot. */
void chain_refresh_address_now(void);

/* Read NVS and start the refresh task. Safe to call once, from app_main. */
void chain_init(void);

/* The last good snapshot. Never NULL; check ->valid. */
const chain_data_t *chain_data(void);

/* True when the active source can quote hashprice (bitview only). */
bool chain_have_hashprice(void);

/* Base URL for the mempool.space-compatible REST API, no trailing slash.
 * mempool.c and price.c build their paths onto this. */
const char *chain_base_url(void);

/* ---- source and currency preferences (persisted) ---- */
chain_source_t chain_get_source(void);
void           chain_set_source(chain_source_t src);
const char    *chain_source_name(chain_source_t src);

chain_ccy_t chain_get_ccy(void);
void        chain_set_ccy(chain_ccy_t ccy);
const char *chain_ccy_code(chain_ccy_t ccy);    /* "AUD" */

/* What may be drawn ahead of the figure in the 140px face. That font carries
 * only the digits, the comma and the dollar sign, so this is "$" for the
 * dollar currencies and empty for the rest: a letter here renders as a blank
 * box. Which currency it is belongs in the title and the suffix, both of
 * which are set in fonts that have an alphabet. */
const char *chain_ccy_prefix(chain_ccy_t ccy);

/* Ratio of the selected currency to USD, published by price.c from the same
 * response that carries the price. 1.0 until a multi-currency fetch lands, so
 * a USD figure converted through this is never wrong by more than being stale.
 */
double chain_fx_to_usd(void);
void   chain_set_fx_to_usd(double ratio);

/* ---- solo mining odds ----
 *
 * Expected seconds to find a block at `hashrate_ghs`:
 *     T = difficulty * 2^32 / H
 * and the chance of at least one block in `seconds` is 1 - exp(-seconds/T),
 * the Poisson tail. Returns false when difficulty or hashrate is unknown, in
 * which case the outputs are untouched.
 *
 * Any of the out pointers may be NULL. */
bool chain_solo_odds(double hashrate_ghs,
                     double *expected_seconds,
                     double *chance_per_day,
                     double *chance_per_year);

/* Sats per day this hashrate would earn on a pool at the current hashprice.
 * Returns false when the source cannot quote hashprice. */
bool chain_expected_sats_per_day(double hashrate_ghs, double *sats);

/* 84740 -> "84,740". Shared by the screens that show these counts, because
 * block heights and year counts are read as quantities: an unseparated
 * six-digit run makes the reader count digits. */
void chain_fmt_grouped(long v, char *buf, size_t n);

/* 2910000 -> "2.91M". Three significant figures, because the inputs move
 * by more than that between fetches. Shared for the same reason as the
 * grouped formatter: more than one screen shows these magnitudes. */
void chain_fmt_compact(double v, char *buf, size_t n);

#endif /* CHAIN_H */
