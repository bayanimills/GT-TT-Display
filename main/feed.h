#ifndef FEED_H
#define FEED_H

#include "lvgl.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define FEED_RSS_MAX_ITEMS 4
#define FEED_RSS_TITLE_MAX 96

/* Compact event stream for the 800x480 display. Producers own the event
 * meaning; the feed only keeps a bounded display copy. Do not post secrets. */
typedef enum {
    FEED_KIND_MINER = 0,
    FEED_KIND_POOL,
    FEED_KIND_NETWORK,
    FEED_KIND_SYSTEM,
    FEED_KIND_NEWS,
    FEED_KIND_COUNT
} feed_kind_t;

typedef struct {
    uint8_t visible_rows;  /* 1..4; values outside the range are clamped. */
    bool    show_age;
} feed_config_t;

void      feed_screen_create(void);
void      feed_screen_destroy(void);
lv_obj_t *feed_get_screen(void);

/* Cache or update a feed item. Calling before the screen exists is supported.
 * When the screen is visible, call from the LVGL task (or hold its lock). */
void feed_post(feed_kind_t kind, const char *headline, const char *detail);
void feed_clear(void);
void feed_refresh(void);

/* Replace the RSS portion of the feed while preserving locally generated
 * activity below it. `source` must be a display-safe host label, never a full
 * URL (which may contain credentials or tokens). */
void feed_publish_rss(const char *source,
                      const char titles[][FEED_RSS_TITLE_MAX], size_t count);
void feed_clear_rss(void);

void          feed_configure(const feed_config_t *config);
feed_config_t feed_get_config(void);

#endif /* FEED_H */
