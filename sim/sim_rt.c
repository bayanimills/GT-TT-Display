/* sim_rt.c -- host-side implementations of the ESP-IDF surface the UI touches.
 * Enough to run the real screen code on a workstation; nothing more. */
#define _GNU_SOURCE
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <errno.h>
#include <unistd.h>
#include <stdint.h>

#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"
#include "freertos/event_groups.h"
#include "nvs.h"
#include "esp_wifi.h"
#include "esp_http_client.h"
#include "driver/uart.h"

static const char *TAG = "sim_rt";

/* ---------------- tasks ---------------- */

typedef struct { TaskFunction_t fn; void *arg; } task_arg_t;

static void *task_trampoline(void *p)
{
    task_arg_t *ta = (task_arg_t *) p;
    TaskFunction_t fn = ta->fn;
    void *arg = ta->arg;
    free(ta);
    fn(arg);
    return NULL;
}

BaseType_t xTaskCreate(TaskFunction_t fn, const char *name, uint32_t stack, void *arg,
                       UBaseType_t prio, TaskHandle_t *out)
{
    (void) stack; (void) prio;
    const char *en = getenv("SIM_TASKS");
    if (en && en[0] == '0') { if (out) *out = NULL; return pdPASS; }

    task_arg_t *ta = malloc(sizeof(*ta));
    ta->fn = fn; ta->arg = arg;
    pthread_t th;
    if (pthread_create(&th, NULL, task_trampoline, ta) != 0) { free(ta); return pdFAIL; }
    pthread_detach(th);
    if (out) *out = (TaskHandle_t) (uintptr_t) th;
    ESP_LOGI(TAG, "task %s started", name ? name : "?");
    return pdPASS;
}

BaseType_t xTaskCreatePinnedToCore(TaskFunction_t fn, const char *name, uint32_t stack, void *arg,
                                   UBaseType_t prio, TaskHandle_t *out, BaseType_t core)
{
    (void) core;
    return xTaskCreate(fn, name, stack, arg, prio, out);
}

void vTaskDelay(TickType_t ticks)
{
    struct timespec ts = { .tv_sec = ticks / 1000, .tv_nsec = (long) (ticks % 1000) * 1000000L };
    nanosleep(&ts, NULL);
}
void vTaskDelete(TaskHandle_t t)  { if (t == NULL) pthread_exit(NULL); }
void vTaskSuspend(TaskHandle_t t) { (void) t; }
void vTaskResume(TaskHandle_t t)  { (void) t; }

TickType_t xTaskGetTickCount(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (TickType_t) (ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
}
TaskHandle_t xTaskGetCurrentTaskHandle(void) { return (TaskHandle_t) (uintptr_t) pthread_self(); }
uint32_t   ulTaskNotifyTake(BaseType_t clear, TickType_t wait) { (void) clear; vTaskDelay(wait > 20 ? 20 : wait); return 1; }
BaseType_t xTaskNotifyGive(TaskHandle_t t) { (void) t; return pdTRUE; }
BaseType_t xTaskNotifyGiveFromISR(TaskHandle_t t, BaseType_t *w) { (void) t; (void) w; return pdTRUE; }

/* ---------------- semaphores ---------------- */

SemaphoreHandle_t xSemaphoreCreateMutex(void)
{
    pthread_mutex_t *m = malloc(sizeof(*m));
    pthread_mutex_init(m, NULL);
    return m;
}
SemaphoreHandle_t xSemaphoreCreateRecursiveMutex(void)
{
    pthread_mutexattr_t a;
    pthread_mutexattr_init(&a);
    pthread_mutexattr_settype(&a, PTHREAD_MUTEX_RECURSIVE);
    pthread_mutex_t *m = malloc(sizeof(*m));
    pthread_mutex_init(m, &a);
    return m;
}
SemaphoreHandle_t xSemaphoreCreateBinary(void) { return xSemaphoreCreateMutex(); }

/* Honour the timeout: on the device lvgl_port_lock(50) can fail while a
 * long render holds the mutex, and that path must be reachable here too. */
BaseType_t xSemaphoreTake(SemaphoreHandle_t s, TickType_t wait)
{
    if (!s) return pdFALSE;
    if (wait == portMAX_DELAY) {
        return pthread_mutex_lock((pthread_mutex_t *) s) == 0 ? pdTRUE : pdFALSE;
    }
    struct timespec deadline;
    clock_gettime(CLOCK_MONOTONIC, &deadline);
    long long end_ns = (long long) deadline.tv_sec * 1000000000LL + deadline.tv_nsec + (long long) wait * 1000000LL;
    for (;;) {
        if (pthread_mutex_trylock((pthread_mutex_t *) s) == 0) return pdTRUE;
        struct timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);
        if ((long long) now.tv_sec * 1000000000LL + now.tv_nsec >= end_ns) return pdFALSE;
        struct timespec nap = { 0, 1000000L };
        nanosleep(&nap, NULL);
    }
}
BaseType_t xSemaphoreGive(SemaphoreHandle_t s)
{
    if (!s) return pdFALSE;
    return pthread_mutex_unlock((pthread_mutex_t *) s) == 0 ? pdTRUE : pdFALSE;
}
BaseType_t xSemaphoreTakeRecursive(SemaphoreHandle_t s, TickType_t w) { return xSemaphoreTake(s, w); }
BaseType_t xSemaphoreGiveRecursive(SemaphoreHandle_t s)              { return xSemaphoreGive(s); }
BaseType_t xSemaphoreGiveFromISR(SemaphoreHandle_t s, BaseType_t *w) { (void) w; return xSemaphoreGive(s); }
void       vSemaphoreDelete(SemaphoreHandle_t s) { if (s) { pthread_mutex_destroy((pthread_mutex_t *) s); free(s); } }

/* ---------------- queues ---------------- */

typedef struct {
    uint8_t *buf;
    UBaseType_t len, item, head, tail, count;
    pthread_mutex_t m;
    pthread_cond_t cv;
} sim_queue_t;

QueueHandle_t xQueueCreate(UBaseType_t len, UBaseType_t item_size)
{
    sim_queue_t *q = calloc(1, sizeof(*q));
    q->buf = calloc(len, item_size);
    q->len = len;
    q->item = item_size;
    pthread_mutex_init(&q->m, NULL);
    pthread_cond_init(&q->cv, NULL);
    return q;
}
BaseType_t xQueueSend(QueueHandle_t h, const void *item, TickType_t wait)
{
    (void) wait;
    sim_queue_t *q = h;
    if (!q) return pdFAIL;
    pthread_mutex_lock(&q->m);
    if (q->count == q->len) { pthread_mutex_unlock(&q->m); return pdFAIL; }
    memcpy(q->buf + q->head * q->item, item, q->item);
    q->head = (q->head + 1) % q->len;
    q->count++;
    pthread_cond_signal(&q->cv);
    pthread_mutex_unlock(&q->m);
    return pdPASS;
}
BaseType_t xQueueReceive(QueueHandle_t h, void *item, TickType_t wait)
{
    sim_queue_t *q = h;
    if (!q) return pdFAIL;
    pthread_mutex_lock(&q->m);
    if (q->count == 0) {
        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        uint32_t ms = (wait == portMAX_DELAY) ? 100 : wait;
        ts.tv_nsec += (long) (ms % 1000) * 1000000L;
        ts.tv_sec  += ms / 1000 + ts.tv_nsec / 1000000000L;
        ts.tv_nsec %= 1000000000L;
        pthread_cond_timedwait(&q->cv, &q->m, &ts);
    }
    if (q->count == 0) { pthread_mutex_unlock(&q->m); return pdFAIL; }
    memcpy(item, q->buf + q->tail * q->item, q->item);
    q->tail = (q->tail + 1) % q->len;
    q->count--;
    pthread_mutex_unlock(&q->m);
    return pdPASS;
}
void vQueueDelete(QueueHandle_t h) { sim_queue_t *q = h; if (q) { free(q->buf); free(q); } }

/* ---------------- event groups ---------------- */

typedef struct { EventBits_t bits; pthread_mutex_t m; } sim_eg_t;

EventGroupHandle_t xEventGroupCreate(void)
{
    sim_eg_t *g = calloc(1, sizeof(*g));
    pthread_mutex_init(&g->m, NULL);
    return g;
}
EventBits_t xEventGroupSetBits(EventGroupHandle_t h, EventBits_t b)
{
    sim_eg_t *g = h;
    pthread_mutex_lock(&g->m);
    g->bits |= b;
    EventBits_t r = g->bits;
    pthread_mutex_unlock(&g->m);
    return r;
}
EventBits_t xEventGroupClearBits(EventGroupHandle_t h, EventBits_t b)
{
    sim_eg_t *g = h;
    pthread_mutex_lock(&g->m);
    g->bits &= ~b;
    EventBits_t r = g->bits;
    pthread_mutex_unlock(&g->m);
    return r;
}
EventBits_t xEventGroupWaitBits(EventGroupHandle_t h, EventBits_t b, BaseType_t clear, BaseType_t all, TickType_t wait)
{
    (void) all;
    sim_eg_t *g = h;
    TickType_t waited = 0;
    while (waited < wait) {
        pthread_mutex_lock(&g->m);
        EventBits_t got = g->bits & b;
        if (got) { if (clear) g->bits &= ~b; pthread_mutex_unlock(&g->m); return got; }
        pthread_mutex_unlock(&g->m);
        vTaskDelay(20);
        waited += 20;
    }
    return 0;
}

/* ---------------- NVS (one flat key=value file) ---------------- */

#define NVS_PATH "sim_nvs.txt"
#define NVS_MAX  128

typedef struct { char key[64]; char val[512]; } nvs_row_t;
static nvs_row_t s_nvs[NVS_MAX];
static int       s_nvs_n = 0;
static bool      s_nvs_loaded = false;
static pthread_mutex_t s_nvs_m = PTHREAD_MUTEX_INITIALIZER;

static void nvs_load(void)
{
    if (s_nvs_loaded) return;
    s_nvs_loaded = true;
    FILE *f = fopen(NVS_PATH, "r");
    if (!f) return;
    char line[640];
    while (fgets(line, sizeof(line), f) && s_nvs_n < NVS_MAX) {
        char *eq = strchr(line, '=');
        if (!eq) continue;
        *eq = 0;
        char *nl = strchr(eq + 1, '\n');
        if (nl) *nl = 0;
        snprintf(s_nvs[s_nvs_n].key, sizeof(s_nvs[0].key), "%s", line);
        snprintf(s_nvs[s_nvs_n].val, sizeof(s_nvs[0].val), "%s", eq + 1);
        s_nvs_n++;
    }
    fclose(f);
}
static void nvs_save(void)
{
    FILE *f = fopen(NVS_PATH, "w");
    if (!f) return;
    for (int i = 0; i < s_nvs_n; i++) fprintf(f, "%s=%s\n", s_nvs[i].key, s_nvs[i].val);
    fclose(f);
}
static nvs_row_t *nvs_find(const char *k, bool create)
{
    for (int i = 0; i < s_nvs_n; i++) if (strcmp(s_nvs[i].key, k) == 0) return &s_nvs[i];
    if (!create || s_nvs_n >= NVS_MAX) return NULL;
    snprintf(s_nvs[s_nvs_n].key, sizeof(s_nvs[0].key), "%s", k);
    s_nvs[s_nvs_n].val[0] = 0;
    return &s_nvs[s_nvs_n++];
}

esp_err_t nvs_open(const char *ns, nvs_open_mode_t mode, nvs_handle_t *out)
{
    (void) ns; (void) mode;
    pthread_mutex_lock(&s_nvs_m);
    nvs_load();
    pthread_mutex_unlock(&s_nvs_m);
    if (out) *out = 1;
    return ESP_OK;
}
void      nvs_close(nvs_handle_t h) { (void) h; }
esp_err_t nvs_commit(nvs_handle_t h)
{
    (void) h;
    pthread_mutex_lock(&s_nvs_m);
    nvs_save();
    pthread_mutex_unlock(&s_nvs_m);
    return ESP_OK;
}
esp_err_t nvs_erase_key(nvs_handle_t h, const char *k)
{
    (void) h;
    pthread_mutex_lock(&s_nvs_m);
    for (int i = 0; i < s_nvs_n; i++) {
        if (strcmp(s_nvs[i].key, k) == 0) { s_nvs[i] = s_nvs[--s_nvs_n]; break; }
    }
    pthread_mutex_unlock(&s_nvs_m);
    return ESP_OK;
}
static esp_err_t nvs_put(const char *k, const char *v)
{
    pthread_mutex_lock(&s_nvs_m);
    nvs_load();
    nvs_row_t *r = nvs_find(k, true);
    if (r) snprintf(r->val, sizeof(r->val), "%s", v);
    pthread_mutex_unlock(&s_nvs_m);
    return r ? ESP_OK : ESP_ERR_NO_MEM;
}
static const char *nvs_get(const char *k)
{
    pthread_mutex_lock(&s_nvs_m);
    nvs_load();
    nvs_row_t *r = nvs_find(k, false);
    pthread_mutex_unlock(&s_nvs_m);
    return r ? r->val : NULL;
}
esp_err_t nvs_set_i32(nvs_handle_t h, const char *k, int32_t v) { (void) h; char b[24]; snprintf(b, sizeof b, "%d", (int) v); return nvs_put(k, b); }
esp_err_t nvs_get_i32(nvs_handle_t h, const char *k, int32_t *v) { (void) h; const char *s = nvs_get(k); if (!s) return ESP_ERR_NVS_NOT_FOUND; *v = atoi(s); return ESP_OK; }
esp_err_t nvs_set_u8 (nvs_handle_t h, const char *k, uint8_t v)  { return nvs_set_i32(h, k, v); }
esp_err_t nvs_get_u8 (nvs_handle_t h, const char *k, uint8_t *v) { int32_t t; esp_err_t e = nvs_get_i32(h, k, &t); if (e == ESP_OK) *v = (uint8_t) t; return e; }
esp_err_t nvs_set_u16(nvs_handle_t h, const char *k, uint16_t v) { return nvs_set_i32(h, k, v); }
esp_err_t nvs_get_u16(nvs_handle_t h, const char *k, uint16_t *v) { int32_t t; esp_err_t e = nvs_get_i32(h, k, &t); if (e == ESP_OK) *v = (uint16_t) t; return e; }
esp_err_t nvs_set_str(nvs_handle_t h, const char *k, const char *v) { (void) h; return nvs_put(k, v); }
esp_err_t nvs_get_str(nvs_handle_t h, const char *k, char *out, size_t *len)
{
    (void) h;
    const char *s = nvs_get(k);
    if (!s) return ESP_ERR_NVS_NOT_FOUND;
    size_t need = strlen(s) + 1;
    if (!out) { *len = need; return ESP_OK; }
    if (*len < need) return ESP_ERR_NVS_NOT_FOUND;
    memcpy(out, s, need);
    *len = need;
    return ESP_OK;
}
esp_err_t nvs_set_blob(nvs_handle_t h, const char *k, const void *v, size_t len)
{
    (void) h;
    char hex[512];
    const uint8_t *b = v;
    if (len * 2 + 1 > sizeof(hex)) return ESP_ERR_INVALID_SIZE;
    for (size_t i = 0; i < len; i++) snprintf(hex + i * 2, 3, "%02x", b[i]);
    hex[len * 2] = 0;
    return nvs_put(k, hex);
}
esp_err_t nvs_get_blob(nvs_handle_t h, const char *k, void *out, size_t *len)
{
    (void) h;
    const char *s = nvs_get(k);
    if (!s) return ESP_ERR_NVS_NOT_FOUND;
    size_t n = strlen(s) / 2;
    if (!out) { *len = n; return ESP_OK; }
    if (*len < n) return ESP_ERR_INVALID_SIZE;
    uint8_t *b = out;
    for (size_t i = 0; i < n; i++) { unsigned t; sscanf(s + i * 2, "%2x", &t); b[i] = (uint8_t) t; }
    *len = n;
    return ESP_OK;
}

/* ---------------- wifi (canned scan results) ---------------- */

static const struct { const char *ssid; int8_t rssi; wifi_auth_mode_t auth; } k_aps[] = {
    { "bitaxe-lab",    -58, WIFI_AUTH_WPA2_PSK },
    { "bitaxe-lab-5G", -63, WIFI_AUTH_WPA2_PSK },
    { "workshop",      -74, WIFI_AUTH_WPA2_PSK },
    { "guest",         -88, WIFI_AUTH_OPEN     },
    { "shed",          -81, WIFI_AUTH_WPA3_PSK },
};
#define AP_COUNT ((int) (sizeof(k_aps) / sizeof(k_aps[0])))

esp_err_t esp_wifi_init(const wifi_init_config_t *c) { (void) c; return ESP_OK; }
esp_err_t esp_wifi_set_mode(wifi_mode_t m) { (void) m; return ESP_OK; }
esp_err_t esp_wifi_set_config(int i, wifi_config_t *c) { (void) i; (void) c; return ESP_OK; }
esp_err_t esp_wifi_get_config(int i, wifi_config_t *c)
{
    (void) i;
    if (c) { memset(c, 0, sizeof(*c)); snprintf((char *) c->sta.ssid, 32, "bitaxe-lab"); }
    return ESP_OK;
}
esp_err_t esp_wifi_start(void) { return ESP_OK; }
esp_err_t esp_wifi_stop(void) { return ESP_OK; }
esp_err_t esp_wifi_connect(void) { return ESP_OK; }
esp_err_t esp_wifi_disconnect(void) { return ESP_OK; }
esp_err_t esp_wifi_scan_start(const wifi_scan_config_t *c, bool block) { (void) c; (void) block; return ESP_OK; }
esp_err_t esp_wifi_scan_stop(void) { return ESP_OK; }
esp_err_t esp_wifi_scan_get_ap_num(uint16_t *n) { if (n) *n = AP_COUNT; return ESP_OK; }
esp_err_t esp_wifi_scan_get_ap_records(uint16_t *n, wifi_ap_record_t *recs)
{
    int want = (n && *n < AP_COUNT) ? *n : AP_COUNT;
    for (int i = 0; i < want; i++) {
        memset(&recs[i], 0, sizeof(recs[i]));
        snprintf((char *) recs[i].ssid, 33, "%s", k_aps[i].ssid);
        recs[i].rssi = k_aps[i].rssi;
        recs[i].authmode = k_aps[i].auth;
        recs[i].primary = (uint8_t) (i + 1);
    }
    if (n) *n = (uint16_t) want;
    return ESP_OK;
}
esp_err_t esp_wifi_sta_get_ap_info(wifi_ap_record_t *r)
{
    if (!r) return ESP_ERR_INVALID_ARG;
    memset(r, 0, sizeof(*r));
    snprintf((char *) r->ssid, 33, "bitaxe-lab");
    r->rssi = -58;
    r->authmode = WIFI_AUTH_WPA2_PSK;
    return ESP_OK;
}
esp_err_t esp_wifi_set_ps(wifi_ps_type_t t) { (void) t; return ESP_OK; }

/* ---------------- HTTP client (shells out to curl) ---------------- */

struct esp_http_client {
    esp_http_client_config_t cfg;
    char   url[512];
    char  *body;
    size_t body_len, read_off;
    int    status;
};

esp_http_client_handle_t esp_http_client_init(const esp_http_client_config_t *cfg)
{
    struct esp_http_client *c = calloc(1, sizeof(*c));
    c->cfg = *cfg;
    if (cfg->url) snprintf(c->url, sizeof(c->url), "%s", cfg->url);
    return c;
}

static esp_err_t http_fetch(struct esp_http_client *c)
{
    free(c->body);
    c->body = NULL;
    c->body_len = 0;
    c->read_off = 0;
    c->status = 0;
    if (getenv("SIM_OFFLINE")) { c->status = 599; return ESP_FAIL; }

    int secs = c->cfg.timeout_ms > 0 ? c->cfg.timeout_ms / 1000 + 1 : 10;
    char cmd[1024];
    snprintf(cmd, sizeof(cmd),
             "curl -sL --max-time %d -w '\\n%%{http_code}' -A gt-touch-sim %s%s%s 2>/dev/null",
             secs, "\"", c->url, "\"");
    FILE *p = popen(cmd, "r");
    if (!p) { c->status = 598; return ESP_FAIL; }

    size_t cap = 8192, len = 0;
    char *buf = malloc(cap);
    size_t n;
    while ((n = fread(buf + len, 1, cap - len - 1, p)) > 0) {
        len += n;
        if (len + 1 >= cap) { cap *= 2; buf = realloc(buf, cap); }
    }
    pclose(p);
    buf[len] = 0;

    char *last_nl = strrchr(buf, '\n');
    if (last_nl) {
        c->status = atoi(last_nl + 1);
        *last_nl = 0;
        len = (size_t) (last_nl - buf);
    }
    c->body = buf;
    c->body_len = len;
    ESP_LOGI(TAG, "GET %s -> %d (%zu bytes)", c->url, c->status, c->body_len);
    return (c->status >= 200 && c->status < 400) ? ESP_OK : ESP_FAIL;
}

esp_err_t esp_http_client_perform(esp_http_client_handle_t c)
{
    esp_err_t e = http_fetch(c);
    if (c->cfg.event_handler && c->body && c->body_len) {
        esp_http_client_event_t ev = {0};
        ev.event_id = HTTP_EVENT_ON_DATA;
        ev.client = c;
        ev.data = c->body;
        ev.data_len = (int) c->body_len;
        ev.user_data = c->cfg.user_data;
        c->cfg.event_handler(&ev);
        ev.event_id = HTTP_EVENT_ON_FINISH;
        ev.data = NULL;
        ev.data_len = 0;
        c->cfg.event_handler(&ev);
    }
    return e;
}
esp_err_t esp_http_client_open(esp_http_client_handle_t c, int w) { (void) w; return http_fetch(c); }
int esp_http_client_fetch_headers(esp_http_client_handle_t c) { return (int) c->body_len; }
int esp_http_client_read(esp_http_client_handle_t c, char *buf, int len)
{
    if (!c->body || c->read_off >= c->body_len) return 0;
    size_t n = c->body_len - c->read_off;
    if ((int) n > len) n = (size_t) len;
    memcpy(buf, c->body + c->read_off, n);
    c->read_off += n;
    return (int) n;
}
int esp_http_client_get_status_code(esp_http_client_handle_t c)    { return c->status; }
int esp_http_client_get_content_length(esp_http_client_handle_t c) { return (int) c->body_len; }
esp_err_t esp_http_client_set_url(esp_http_client_handle_t c, const char *u) { snprintf(c->url, sizeof(c->url), "%s", u); return ESP_OK; }
esp_err_t esp_http_client_set_method(esp_http_client_handle_t c, esp_http_client_method_t m) { (void) c; (void) m; return ESP_OK; }
esp_err_t esp_http_client_set_header(esp_http_client_handle_t c, const char *k, const char *v) { (void) c; (void) k; (void) v; return ESP_OK; }
esp_err_t esp_http_client_close(esp_http_client_handle_t c) { (void) c; return ESP_OK; }
esp_err_t esp_http_client_cleanup(esp_http_client_handle_t c) { if (c) { free(c->body); free(c); } return ESP_OK; }

/* ---------------- UART (BAP lines are injected by the sim, not read here) ---------------- */

int uart_read_bytes(uart_port_t p, void *buf, uint32_t len, uint32_t ticks)
{
    (void) p; (void) buf; (void) len;
    vTaskDelay(ticks ? ticks : 10);
    return 0;
}
