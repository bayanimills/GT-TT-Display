#ifndef FEED_WEB_H
#define FEED_WEB_H

#include "feed.h"

#include <stdbool.h>
#include <stddef.h>

#define FEED_WEB_URL_MAX 255
#define FEED_WEB_CONFIG_URL_MAX 128

/* Load/create persisted configuration and start the refresh worker. The LAN
 * listener comes up only after the display itself has a STA address. */
bool feed_web_init(void);

/* URL encoded into the on-device configuration QR. This deliberately uses
 * the display's own address, not wifi_get_current_ip() (the AxeOS/miner IP). */
bool feed_web_get_config_url(char *out, size_t out_size);

/* Pure, bounded helpers kept public so the host build can test the security
 * boundary and RSS/Atom parsing even though its HTTP server is compiled out. */
bool feed_web_validate_rss_url(const char *url);
void feed_web_source_label(const char *url, char *out, size_t out_size);
size_t feed_web_parse_titles(const char *xml, size_t xml_len,
                             char titles[][FEED_RSS_TITLE_MAX],
                             size_t max_titles);

#endif /* FEED_WEB_H */
