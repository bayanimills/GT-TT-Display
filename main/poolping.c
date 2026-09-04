#include "poolping.h"

#include "wifi.h"

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "lwip/sockets.h"
#include "lwip/netdb.h"

#include <errno.h>
#include <string.h>

static const char *TAG = "poolping";

/* Every one of these was checked to resolve and accept a connection on the
 * port given. A pool that has moved will simply read as unreachable, which is
 * the honest outcome rather than a wrong number. */
static const pool_entry_t k_pools[] = {
    { "ausolo.ckpool.org", 3333,  "ckpool solo AU",  true  },
    { "eusolo.ckpool.org", 3333,  "ckpool solo EU",  true  },
    { "solo.ckpool.org",   3333,  "ckpool solo",     true  },
    { "public-pool.io",    21496, "public-pool.io",  true  },
    { "stratum.braiins.com", 3333, "Braiins",        false },
    { "btc.viabtc.io",     3333,  "ViaBTC",          false },
};

#define POOL_COUNT ((int)(sizeof(k_pools) / sizeof(k_pools[0])))

/* Long enough for a slow path across the world, short enough that six of them
 * in series does not make the screen feel stuck. */
#define CONNECT_TIMEOUT_MS 3000
#define SWEEP_INTERVAL_MS  (15 * 1000)

static int          s_latency[POOL_COUNT];
static int64_t      s_last_sweep_us = 0;
static TaskHandle_t s_task = NULL;

int poolping_count(void) { return POOL_COUNT; }

const pool_entry_t *poolping_entry(int i)
{
    return (i >= 0 && i < POOL_COUNT) ? &k_pools[i] : NULL;
}

int poolping_latency_ms(int i)
{
    return (i >= 0 && i < POOL_COUNT) ? s_latency[i] : POOLPING_FAILED;
}

int poolping_age_seconds(void)
{
    if (s_last_sweep_us == 0) {
        return -1;
    }
    return (int)((esp_timer_get_time() - s_last_sweep_us) / 1000000);
}

int poolping_ranked(int rank)
{
    /* Six entries, so a selection sort costs nothing and avoids keeping a
     * second array in step with the measurements. */
    int order[POOL_COUNT];
    for (int i = 0; i < POOL_COUNT; i++) {
        order[i] = i;
    }
    for (int i = 0; i < POOL_COUNT - 1; i++) {
        for (int j = i + 1; j < POOL_COUNT; j++) {
            const int a = s_latency[order[i]];
            const int b = s_latency[order[j]];
            /* Anything not yet measured, or unreachable, sorts last so the
             * list does not reshuffle while the first sweep is running. */
            const bool a_bad = (a < 0);
            const bool b_bad = (b < 0);
            const bool swap = (a_bad && !b_bad) || (!a_bad && !b_bad && b < a);
            if (swap) {
                const int t = order[i];
                order[i] = order[j];
                order[j] = t;
            }
        }
    }
    return (rank >= 0 && rank < POOL_COUNT) ? order[rank] : 0;
}

/* Time a TCP connect. Non-blocking plus select, because a blocking connect
 * cannot be given a timeout and an unreachable pool would otherwise stall the
 * sweep for however long the stack decides. */
static int measure_one(const pool_entry_t *p)
{
    char port_str[8];
    snprintf(port_str, sizeof(port_str), "%u", (unsigned)p->port);

    struct addrinfo hints = { .ai_family = AF_INET, .ai_socktype = SOCK_STREAM };
    struct addrinfo *res = NULL;

    /* Resolve before starting the clock. Including the lookup made the first
     * sweep read several hundred milliseconds worse than every sweep after
     * it, purely because the resolver had cached by then, which made the
     * numbers incomparable with each other and with themselves. What is being
     * compared is the path to the pool, not the state of a DNS cache. */
    if (getaddrinfo(p->host, port_str, &hints, &res) != 0 || !res) {
        return POOLPING_FAILED;
    }

    const int64_t t0 = esp_timer_get_time();

    const int fd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (fd < 0) {
        freeaddrinfo(res);
        return POOLPING_FAILED;
    }

    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);

    int result = POOLPING_FAILED;
    const int rc = connect(fd, res->ai_addr, res->ai_addrlen);

    if (rc == 0) {
        result = (int)((esp_timer_get_time() - t0) / 1000);
    } else if (errno == EINPROGRESS) {
        fd_set wr;
        FD_ZERO(&wr);
        FD_SET(fd, &wr);
        struct timeval tv = {
            .tv_sec  = CONNECT_TIMEOUT_MS / 1000,
            .tv_usec = (CONNECT_TIMEOUT_MS % 1000) * 1000,
        };
        if (select(fd + 1, NULL, &wr, NULL, &tv) > 0) {
            int err = 0;
            socklen_t len = sizeof(err);
            if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &len) == 0 && err == 0) {
                result = (int)((esp_timer_get_time() - t0) / 1000);
            }
        }
    }

    close(fd);
    freeaddrinfo(res);
    return result;
}

static void poolping_task(void *arg)
{
    (void)arg;

    for (int i = 0; i < POOL_COUNT; i++) {
        s_latency[i] = POOLPING_PENDING;
    }

    for (;;) {
        if (!wifi_is_connected()) {
            vTaskDelay(pdMS_TO_TICKS(2000));
            continue;
        }

        for (int i = 0; i < POOL_COUNT; i++) {
            s_latency[i] = measure_one(&k_pools[i]);
            /* Yield between pools: six connects back to back would hold the
             * task for seconds if several are unreachable. */
            vTaskDelay(pdMS_TO_TICKS(20));
        }
        s_last_sweep_us = esp_timer_get_time();

        ESP_LOGI(TAG, "sweep: %s %d ms is fastest",
                 k_pools[poolping_ranked(0)].label,
                 s_latency[poolping_ranked(0)]);

        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(SWEEP_INTERVAL_MS));
    }
}

void poolping_refresh_now(void)
{
    if (s_task) {
        xTaskNotifyGive(s_task);
    }
}

void poolping_start(void)
{
    if (s_task) {
        return;
    }
    xTaskCreate(poolping_task, "poolping", 4096, NULL, 3, &s_task);
}
