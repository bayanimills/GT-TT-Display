#pragma once
#include <time.h>
typedef enum { SNTP_OPMODE_POLL = 0 } sntp_operatingmode_t;
static inline void sntp_setoperatingmode(sntp_operatingmode_t m) { (void)m; }
static inline void sntp_setservername(int i, const char *s) { (void)i; (void)s; }
static inline void sntp_init(void) { }
static inline void sntp_stop(void) { }
static inline int  sntp_enabled(void) { return 1; }
