#include "feed_web.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int ascii_tolower(int c)
{
    return c >= 'A' && c <= 'Z' ? c + ('a' - 'A') : c;
}

static bool ascii_equal_ci(const char *a, const char *b)
{
    while (*a && *b)
    {
        if (ascii_tolower((unsigned char)*a) != ascii_tolower((unsigned char)*b))
            return false;
        a++;
        b++;
    }
    return *a == 0 && *b == 0;
}

static bool ipv4_literal(const char *host, unsigned octet[4])
{
    char tail = 0;
    int n = sscanf(host, "%u.%u.%u.%u%c", &octet[0], &octet[1],
                   &octet[2], &octet[3], &tail);
    if (n != 4) return false;
    for (int i = 0; i < 4; i++) if (octet[i] > 255) return false;
    return true;
}

static bool public_ipv4(const unsigned o[4])
{
    if (o[0] == 0 || o[0] == 10 || o[0] == 127 || o[0] >= 224) return false;
    if (o[0] == 100 && o[1] >= 64 && o[1] <= 127) return false;
    if (o[0] == 169 && o[1] == 254) return false;
    if (o[0] == 172 && o[1] >= 16 && o[1] <= 31) return false;
    if (o[0] == 192 && o[1] == 168) return false;
    if (o[0] == 198 && (o[1] == 18 || o[1] == 19)) return false;
    return true;
}

static bool split_url_host(const char *url, const char **host_out,
                           size_t *host_len_out)
{
    const char *p;
    if (strncmp(url, "https://", 8) == 0) p = url + 8;
    else if (strncmp(url, "http://", 7) == 0) p = url + 7;
    else return false;

    const char *authority_end = p + strcspn(p, "/?#");
    if (authority_end == p || memchr(p, '@', (size_t)(authority_end - p)))
        return false;
    /* IPv6 literals and multiple colons are intentionally unsupported. This
     * keeps private/link-local address validation unambiguous. */
    if (memchr(p, '[', (size_t)(authority_end - p)) ||
        memchr(p, ']', (size_t)(authority_end - p))) return false;

    const char *colon = memchr(p, ':', (size_t)(authority_end - p));
    const char *host_end = colon ? colon : authority_end;
    if (colon)
    {
        if (memchr(colon + 1, ':', (size_t)(authority_end - colon - 1)))
            return false;
        if (colon + 1 == authority_end) return false;
        unsigned long port = 0;
        for (const char *q = colon + 1; q < authority_end; q++)
        {
            if (!isdigit((unsigned char)*q)) return false;
            port = port * 10 + (unsigned)(*q - '0');
            if (port > 65535) return false;
        }
        if (port == 0) return false;
    }

    size_t host_len = (size_t)(host_end - p);
    if (host_len == 0 || host_len > 253) return false;
    *host_out = p;
    *host_len_out = host_len;
    return true;
}

bool feed_web_validate_rss_url(const char *url)
{
    if (!url) return false;
    size_t len = strlen(url);
    if (len == 0 || len > FEED_WEB_URL_MAX) return false;
    for (size_t i = 0; i < len; i++)
    {
        unsigned char c = (unsigned char)url[i];
        if (c <= 0x20 || c >= 0x7f || c == '"' || c == '\\' ||
            c == '<' || c == '>' || c == '#') return false;
    }

    const char *host_start;
    size_t host_len;
    if (!split_url_host(url, &host_start, &host_len)) return false;
    char host[254];
    memcpy(host, host_start, host_len);
    host[host_len] = 0;

    if (ascii_equal_ci(host, "localhost")) return false;
    if (host_len >= 6 && ascii_equal_ci(host + host_len - 6, ".local"))
        return false;

    bool only_numeric = true;
    bool saw_dot = false;
    size_t label_start = 0;
    for (size_t i = 0; i < host_len; i++)
    {
        unsigned char c = (unsigned char)host[i];
        if (!(isalnum(c) || c == '-' || c == '.')) return false;
        if (!isdigit(c) && c != '.') only_numeric = false;
        if (c == '.')
        {
            saw_dot = true;
            if (i == label_start || host[i - 1] == '-') return false;
            label_start = i + 1;
        }
        else if (i == label_start && c == '-') return false;
    }
    if (label_start == host_len || host[host_len - 1] == '-') return false;

    unsigned o[4];
    if (ipv4_literal(host, o)) return public_ipv4(o);
    /* Reject alternate all-numeric IP spellings and single-label LAN names. */
    if (only_numeric || !saw_dot) return false;
    return true;
}

void feed_web_source_label(const char *url, char *out, size_t out_size)
{
    if (!out || out_size == 0) return;
    snprintf(out, out_size, "RSS");
    if (!url) return;
    const char *host;
    size_t host_len;
    if (!split_url_host(url, &host, &host_len)) return;
    if (host_len > 4 && ascii_tolower((unsigned char)host[0]) == 'w' &&
        ascii_tolower((unsigned char)host[1]) == 'w' &&
        ascii_tolower((unsigned char)host[2]) == 'w' && host[3] == '.')
    {
        host += 4;
        host_len -= 4;
    }
    if (host_len >= out_size) host_len = out_size - 1;
    memcpy(out, host, host_len);
    out[host_len] = 0;
}

static bool local_name_equal(const char *name, size_t len, const char *wanted)
{
    const char *local = name;
    for (size_t i = 0; i < len; i++) if (name[i] == ':') local = name + i + 1;
    size_t local_len = len - (size_t)(local - name);
    return strlen(wanted) == local_len && memcmp(local, wanted, local_len) == 0;
}

/* Find an XML opening/closing tag by local name. Namespace prefixes are
 * accepted; declarations, comments and processing instructions are skipped. */
static const char *find_tag(const char *p, const char *end, const char *name,
                            bool closing, const char **after)
{
    while (p < end)
    {
        const char *lt = memchr(p, '<', (size_t)(end - p));
        if (!lt || lt + 2 >= end) return NULL;
        const char *q = lt + 1;
        bool is_closing = *q == '/';
        if (is_closing) q++;
        if (is_closing != closing || q >= end || *q == '!' || *q == '?')
        {
            p = lt + 1;
            continue;
        }
        const char *name_start = q;
        while (q < end && !isspace((unsigned char)*q) && *q != '>' && *q != '/') q++;
        if (q == name_start || !local_name_equal(name_start,
                                                  (size_t)(q - name_start), name))
        {
            p = lt + 1;
            continue;
        }
        const char *gt = memchr(q, '>', (size_t)(end - q));
        if (!gt) return NULL;
        if (after) *after = gt + 1;
        return lt;
    }
    return NULL;
}

static bool bytes_at(const char *p, const char *end, const char *literal)
{
    size_t n = strlen(literal);
    return (size_t)(end - p) >= n && memcmp(p, literal, n) == 0;
}

static size_t append_byte(char *out, size_t used, size_t cap, unsigned char c)
{
    if (used + 1 < cap) out[used++] = (char)c;
    return used;
}

static void sanitize_title(const char *p, const char *end, char *out, size_t cap)
{
    size_t used = 0;
    bool space = false;
    while (p < end)
    {
        if (bytes_at(p, end, "<![CDATA[")) { p += 9; continue; }
        if (bytes_at(p, end, "]]>")) { p += 3; continue; }
        if (*p == '<')
        {
            const char *gt = memchr(p, '>', (size_t)(end - p));
            if (!gt) break;
            p = gt + 1;
            space = used > 0;
            continue;
        }
        if (*p == '&')
        {
            const char *semi = memchr(p, ';', (size_t)(end - p));
            if (semi && semi - p <= 10)
            {
                unsigned char decoded = 0;
                size_t n = (size_t)(semi - p - 1);
                const char *e = p + 1;
                if (n == 3 && memcmp(e, "amp", 3) == 0) decoded = '&';
                else if (n == 2 && memcmp(e, "lt", 2) == 0) decoded = '<';
                else if (n == 2 && memcmp(e, "gt", 2) == 0) decoded = '>';
                else if (n == 4 && memcmp(e, "quot", 4) == 0) decoded = '"';
                else if (n == 4 && memcmp(e, "apos", 4) == 0) decoded = '\'';
                else if (n == 4 && memcmp(e, "nbsp", 4) == 0) decoded = ' ';
                else if (n >= 2 && *e == '#')
                {
                    char number[10];
                    size_t digits = n - 1;
                    int base = 10;
                    const char *start = e + 1;
                    if (digits > 1 && (*start == 'x' || *start == 'X'))
                    {
                        base = 16;
                        start++;
                        digits--;
                    }
                    if (digits < sizeof(number))
                    {
                        memcpy(number, start, digits);
                        number[digits] = 0;
                        unsigned long v = strtoul(number, NULL, base);
                        if (v >= 0x20 && v <= 0x7e) decoded = (unsigned char)v;
                    }
                }
                p = semi + 1;
                if (!decoded || isspace(decoded)) { space = used > 0; continue; }
                if (space && used + 1 < cap) out[used++] = ' ';
                space = false;
                used = append_byte(out, used, cap, decoded);
                continue;
            }
        }

        unsigned char c = (unsigned char)*p++;
        if (isspace(c) || c < 0x20 || c == 0x7f)
        {
            space = used > 0;
            continue;
        }
        if (space && used + 1 < cap) out[used++] = ' ';
        space = false;
        if (c < 0x80)
        {
            used = append_byte(out, used, cap, c);
            continue;
        }
        /* Preserve valid UTF-8 sequences and replace malformed input. */
        int extra = (c >= 0xC2 && c <= 0xDF) ? 1 :
                    (c >= 0xE0 && c <= 0xEF) ? 2 :
                    (c >= 0xF0 && c <= 0xF4) ? 3 : 0;
        bool valid = extra > 0 && (size_t)(end - p) >= (size_t)extra;
        for (int i = 0; valid && i < extra; i++)
            valid = ((unsigned char)p[i] & 0xC0) == 0x80;
        if (!valid)
        {
            used = append_byte(out, used, cap, '?');
            continue;
        }
        if (used + (size_t)extra + 1 >= cap) break;
        out[used++] = (char)c;
        for (int i = 0; i < extra; i++) out[used++] = *p++;
    }
    while (used > 0 && out[used - 1] == ' ') used--;
    out[used] = 0;
}

size_t feed_web_parse_titles(const char *xml, size_t xml_len,
                             char titles[][FEED_RSS_TITLE_MAX],
                             size_t max_titles)
{
    if (!xml || !titles || max_titles == 0) return 0;
    if (max_titles > FEED_RSS_MAX_ITEMS) max_titles = FEED_RSS_MAX_ITEMS;
    const char *p = xml;
    const char *end = xml + xml_len;
    size_t count = 0;

    while (p < end && count < max_titles)
    {
        const char *after_item = NULL, *after_entry = NULL;
        const char *item = find_tag(p, end, "item", false, &after_item);
        const char *entry = find_tag(p, end, "entry", false, &after_entry);
        const char *open;
        const char *after_open;
        const char *container;
        if (item && (!entry || item < entry))
        {
            open = item; after_open = after_item; container = "item";
        }
        else if (entry)
        {
            open = entry; after_open = after_entry; container = "entry";
        }
        else break;

        const char *after_close = NULL;
        const char *close = find_tag(after_open, end, container, true, &after_close);
        const char *scope_end = close ? close : end;
        const char *after_title = NULL;
        const char *title = find_tag(after_open, scope_end, "title", false,
                                     &after_title);
        if (title)
        {
            const char *after_title_close = NULL;
            const char *title_close = find_tag(after_title, scope_end, "title", true,
                                               &after_title_close);
            if (title_close)
            {
                sanitize_title(after_title, title_close, titles[count],
                               FEED_RSS_TITLE_MAX);
                if (titles[count][0]) count++;
            }
        }
        p = close ? after_close : end;
        (void)open;
    }
    return count;
}

#if defined(ESP_PLATFORM)

#include "wifi.h"
#include "lvgl_port.h"

#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_random.h"
#include "nvs.h"

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#define RSS_NVS_NAMESPACE "rss_feed"
#define RSS_NVS_URL       "url"
#define RSS_NVS_TOKEN     "token"
#define RSS_TOKEN_BYTES   16
#define RSS_TOKEN_HEX_LEN (RSS_TOKEN_BYTES * 2)
#define RSS_RESPONSE_MAX  (32 * 1024)
#define RSS_REFRESH_MS    (15 * 60 * 1000)
#define RSS_RETRY_MS      (60 * 1000)

static const char *TAG = "feed_web";
static char s_rss_url[FEED_WEB_URL_MAX + 1];
static char s_token[RSS_TOKEN_HEX_LEN + 1];
static SemaphoreHandle_t s_lock;
static TaskHandle_t s_task;
static httpd_handle_t s_httpd;
static bool s_initialized;

static bool token_valid(const char *token)
{
    if (!token || strlen(token) != RSS_TOKEN_HEX_LEN) return false;
    for (size_t i = 0; i < RSS_TOKEN_HEX_LEN; i++)
        if (!isxdigit((unsigned char)token[i])) return false;
    return true;
}

static void token_generate(char out[RSS_TOKEN_HEX_LEN + 1])
{
    uint8_t random[RSS_TOKEN_BYTES];
    esp_fill_random(random, sizeof(random));
    for (size_t i = 0; i < sizeof(random); i++)
        snprintf(out + i * 2, 3, "%02x", random[i]);
}

static bool config_load(void)
{
    nvs_handle_t nvs;
    if (nvs_open(RSS_NVS_NAMESPACE, NVS_READWRITE, &nvs) != ESP_OK) return false;

    size_t url_len = sizeof(s_rss_url);
    if (nvs_get_str(nvs, RSS_NVS_URL, s_rss_url, &url_len) != ESP_OK ||
        (s_rss_url[0] && !feed_web_validate_rss_url(s_rss_url)))
    {
        s_rss_url[0] = 0;
        nvs_erase_key(nvs, RSS_NVS_URL);
    }

    size_t token_len = sizeof(s_token);
    if (nvs_get_str(nvs, RSS_NVS_TOKEN, s_token, &token_len) != ESP_OK ||
        !token_valid(s_token))
    {
        token_generate(s_token);
        if (nvs_set_str(nvs, RSS_NVS_TOKEN, s_token) != ESP_OK)
        {
            nvs_close(nvs);
            return false;
        }
    }
    bool ok = nvs_commit(nvs) == ESP_OK;
    nvs_close(nvs);
    return ok;
}

static void config_snapshot(char url[FEED_WEB_URL_MAX + 1],
                            char token[RSS_TOKEN_HEX_LEN + 1])
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    snprintf(url, FEED_WEB_URL_MAX + 1, "%s", s_rss_url);
    if (token) snprintf(token, RSS_TOKEN_HEX_LEN + 1, "%s", s_token);
    xSemaphoreGive(s_lock);
}

static bool config_store_url(const char *url)
{
    if (!url || (url[0] && !feed_web_validate_rss_url(url))) return false;
    nvs_handle_t nvs;
    if (nvs_open(RSS_NVS_NAMESPACE, NVS_READWRITE, &nvs) != ESP_OK) return false;
    esp_err_t err = url[0] ? nvs_set_str(nvs, RSS_NVS_URL, url)
                           : nvs_erase_key(nvs, RSS_NVS_URL);
    if (err == ESP_ERR_NVS_NOT_FOUND && !url[0]) err = ESP_OK;
    if (err == ESP_OK) err = nvs_commit(nvs);
    nvs_close(nvs);
    if (err != ESP_OK) return false;

    xSemaphoreTake(s_lock, portMAX_DELAY);
    snprintf(s_rss_url, sizeof(s_rss_url), "%s", url);
    xSemaphoreGive(s_lock);
    if (s_task) xTaskNotifyGive(s_task);
    return true;
}

static bool request_authorized(httpd_req_t *req)
{
    size_t query_len = httpd_req_get_url_query_len(req);
    if (query_len == 0 || query_len > 96) return false;
    char query[97];
    char supplied[RSS_TOKEN_HEX_LEN + 1] = {0};
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) != ESP_OK ||
        httpd_query_key_value(query, "token", supplied, sizeof(supplied)) != ESP_OK)
        return false;

    char expected[RSS_TOKEN_HEX_LEN + 1];
    char unused[FEED_WEB_URL_MAX + 1];
    config_snapshot(unused, expected);
    unsigned diff = (unsigned)(strlen(supplied) ^ strlen(expected));
    for (size_t i = 0; i < RSS_TOKEN_HEX_LEN; i++)
        diff |= (unsigned char)supplied[i] ^ (unsigned char)expected[i];
    return diff == 0;
}

static esp_err_t reply(httpd_req_t *req, const char *status,
                       const char *type, const char *body)
{
    httpd_resp_set_status(req, status);
    httpd_resp_set_type(req, type);
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    httpd_resp_set_hdr(req, "Referrer-Policy", "no-referrer");
    httpd_resp_set_hdr(req, "X-Content-Type-Options", "nosniff");
    httpd_resp_set_hdr(req, "X-Frame-Options", "DENY");
    return httpd_resp_send(req, body, HTTPD_RESP_USE_STRLEN);
}

static const char CONFIG_PAGE[] =
    "<!doctype html><html lang=en><head><meta charset=utf-8>"
    "<meta name=viewport content='width=device-width,initial-scale=1'>"
    "<meta name=referrer content=no-referrer><title>GT Touch Feed</title>"
    "<style>body{font:16px system-ui;max-width:42rem;margin:3rem auto;padding:0 1rem}"
    "label{display:block;font-weight:700;margin-bottom:.5rem}input{box-sizing:border-box;"
    "width:100%;padding:.8rem;font:inherit}button{margin-top:1rem;padding:.8rem 1.2rem;"
    "font:inherit}small,#s{display:block;margin-top:1rem}</style></head><body>"
    "<h1>Activity Feed</h1><form id=f><label for=u>RSS or Atom URL</label>"
    "<input id=u name=url type=url maxlength=255 placeholder=https://example.com/feed.xml>"
    "<button>Save</button></form><small>Clear the field to disable news. HTTPS is recommended."
    " The display shows only the source host and the first four titles.</small><p id=s role=status>"
    "Loading...</p><script>const q=location.search,a='/api/feed'+q,u=document.querySelector('#u'),"
    "s=document.querySelector('#s');fetch(a,{cache:'no-store'}).then(r=>{if(!r.ok)throw 0;return r.json()})"
    ".then(x=>{u.value=x.url||'';s.textContent=x.url?'Configured':'News disabled'})"
    ".catch(()=>s.textContent='Unable to load settings');document.querySelector('#f').onsubmit=async e=>{e.preventDefault();"
    "s.textContent='Saving...';const r=await fetch(a,{method:'POST',headers:{'Content-Type':'text/plain'},"
    "body:u.value.trim()});s.textContent=r.ok?'Saved':await r.text()}</script></body></html>";

static esp_err_t page_get(httpd_req_t *req)
{
    if (!request_authorized(req))
        return reply(req, "403 Forbidden", "text/plain", "Forbidden");
    httpd_resp_set_hdr(req, "Content-Security-Policy",
                       "default-src 'self'; script-src 'unsafe-inline'; style-src 'unsafe-inline'; frame-ancestors 'none'");
    return reply(req, "200 OK", "text/html; charset=utf-8", CONFIG_PAGE);
}

static void json_escape(const char *in, char *out, size_t out_size)
{
    size_t used = 0;
    for (; *in && used + 1 < out_size; in++)
    {
        if ((*in == '"' || *in == '\\') && used + 2 < out_size)
            out[used++] = '\\';
        out[used++] = *in;
    }
    out[used] = 0;
}

static esp_err_t api_get(httpd_req_t *req)
{
    if (!request_authorized(req))
        return reply(req, "403 Forbidden", "text/plain", "Forbidden");
    char url[FEED_WEB_URL_MAX + 1], escaped[FEED_WEB_URL_MAX * 2 + 1];
    config_snapshot(url, NULL);
    json_escape(url, escaped, sizeof(escaped));
    char body[sizeof(escaped) + 16];
    snprintf(body, sizeof(body), "{\"url\":\"%s\"}", escaped);
    return reply(req, "200 OK", "application/json", body);
}

static esp_err_t api_post(httpd_req_t *req)
{
    if (!request_authorized(req))
        return reply(req, "403 Forbidden", "text/plain", "Forbidden");
    if (req->content_len > FEED_WEB_URL_MAX)
        return reply(req, "413 Payload Too Large", "text/plain", "URL is too long");

    char url[FEED_WEB_URL_MAX + 1];
    size_t used = 0;
    while (used < req->content_len)
    {
        int got = httpd_req_recv(req, url + used, req->content_len - used);
        if (got <= 0) return reply(req, "400 Bad Request", "text/plain", "Incomplete request");
        used += (size_t)got;
    }
    url[used] = 0;
    while (used > 0 && isspace((unsigned char)url[used - 1])) url[--used] = 0;
    char *start = url;
    while (*start && isspace((unsigned char)*start)) start++;
    if (start != url) memmove(url, start, strlen(start) + 1);

    if (url[0] && !feed_web_validate_rss_url(url))
        return reply(req, "400 Bad Request", "text/plain",
                     "Use a public http:// or https:// URL up to 255 characters");
    if (!config_store_url(url))
        return reply(req, "500 Internal Server Error", "text/plain", "Could not save");
    return reply(req, "200 OK", "text/plain", "Saved");
}

static bool server_start(void)
{
    if (s_httpd) return true;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_uri_handlers = 4;
    if (httpd_start(&s_httpd, &config) != ESP_OK) return false;

    const httpd_uri_t handlers[] = {
        { .uri = "/feed", .method = HTTP_GET, .handler = page_get },
        { .uri = "/api/feed", .method = HTTP_GET, .handler = api_get },
        { .uri = "/api/feed", .method = HTTP_POST, .handler = api_post },
    };
    for (size_t i = 0; i < sizeof(handlers) / sizeof(handlers[0]); i++)
    {
        if (httpd_register_uri_handler(s_httpd, &handlers[i]) != ESP_OK)
        {
            httpd_stop(s_httpd);
            s_httpd = NULL;
            return false;
        }
    }
    ESP_LOGI(TAG, "LAN feed configuration service ready");
    return true;
}

static bool fetch_titles(const char *url,
                         char titles[][FEED_RSS_TITLE_MAX], size_t *title_count)
{
    char *xml = heap_caps_malloc(RSS_RESPONSE_MAX + 1,
                                 MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!xml) xml = heap_caps_malloc(RSS_RESPONSE_MAX + 1, MALLOC_CAP_8BIT);
    if (!xml) return false;

    esp_http_client_config_t config = {
        .url = url,
        .timeout_ms = 10000,
        .buffer_size = 1024,
        .disable_auto_redirect = true,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .user_agent = "GT-Touch-RSS/1",
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) { free(xml); return false; }
    esp_http_client_set_header(client, "Accept", "application/rss+xml, application/atom+xml, application/xml, text/xml");
    esp_http_client_set_header(client, "Accept-Encoding", "identity");

    bool ok = false;
    size_t used = 0;
    if (esp_http_client_open(client, 0) == ESP_OK)
    {
        int64_t declared = esp_http_client_fetch_headers(client);
        int status = esp_http_client_get_status_code(client);
        if (status >= 200 && status < 300 &&
            (declared < 0 || declared <= RSS_RESPONSE_MAX))
        {
            while (used < RSS_RESPONSE_MAX)
            {
                int got = esp_http_client_read(client, xml + used,
                                               RSS_RESPONSE_MAX - used);
                if (got < 0) { used = 0; break; }
                if (got == 0) break;
                used += (size_t)got;
            }
            xml[used] = 0;
            *title_count = feed_web_parse_titles(xml, used, titles,
                                                 FEED_RSS_MAX_ITEMS);
            ok = *title_count > 0;
        }
        esp_http_client_close(client);
    }
    esp_http_client_cleanup(client);
    free(xml);
    return ok;
}

static void publish_titles(const char *url,
                           const char titles[][FEED_RSS_TITLE_MAX], size_t count)
{
    char source[96];
    feed_web_source_label(url, source, sizeof(source));
    if (lvgl_port_lock(250))
    {
        feed_publish_rss(source, titles, count);
        lvgl_port_unlock();
    }
}

static void clear_titles(void)
{
    if (lvgl_port_lock(250))
    {
        feed_clear_rss();
        lvgl_port_unlock();
    }
}

static void feed_task(void *arg)
{
    (void)arg;
    TickType_t next_fetch = 0;
    bool had_url = false;
    for (;;)
    {
        if (!s_httpd && wifi_get_display_ip()[0]) server_start();

        char url[FEED_WEB_URL_MAX + 1];
        config_snapshot(url, NULL);
        if (!url[0])
        {
            if (had_url) clear_titles();
            had_url = false;
            next_fetch = 0;
        }
        else
        {
            TickType_t now = xTaskGetTickCount();
            if (!had_url) next_fetch = 0;
            had_url = true;
            if ((int32_t)(now - next_fetch) >= 0 && wifi_is_connected())
            {
                char titles[FEED_RSS_MAX_ITEMS][FEED_RSS_TITLE_MAX] = {{0}};
                size_t count = 0;
                if (fetch_titles(url, titles, &count))
                {
                    publish_titles(url, titles, count);
                    next_fetch = now + pdMS_TO_TICKS(RSS_REFRESH_MS);
                    ESP_LOGI(TAG, "RSS refreshed (%u titles)", (unsigned)count);
                }
                else
                {
                    next_fetch = now + pdMS_TO_TICKS(RSS_RETRY_MS);
                    ESP_LOGW(TAG, "RSS refresh failed; URL omitted from log");
                }
            }
        }
        if (ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(5000)) > 0)
            next_fetch = 0;
    }
}

bool feed_web_init(void)
{
    if (s_initialized) return true;
    s_lock = xSemaphoreCreateMutex();
    if (!s_lock) return false;
    if (!config_load()) {
        vSemaphoreDelete(s_lock);
        s_lock = NULL;
        return false;
    }
    if (xTaskCreate(feed_task, "feed_web", 8192, NULL, 3, &s_task) != pdPASS)
    {
        vSemaphoreDelete(s_lock);
        s_lock = NULL;
        return false;
    }
    s_initialized = true;
    return true;
}

bool feed_web_get_config_url(char *out, size_t out_size)
{
    if (!out || out_size == 0 || !s_initialized) return false;
    const char *ip = wifi_get_display_ip();
    if (!ip || !ip[0]) { out[0] = 0; return false; }
    char token[RSS_TOKEN_HEX_LEN + 1];
    char unused[FEED_WEB_URL_MAX + 1];
    config_snapshot(unused, token);
    int n = snprintf(out, out_size, "http://%s/feed?token=%s", ip, token);
    if (n < 0 || (size_t)n >= out_size) { out[0] = 0; return false; }
    return true;
}

#else

/* The host simulator exercises parsing and URL validation but deliberately
 * opens no LAN listener and launches no network task. */
bool feed_web_init(void) { return true; }

bool feed_web_get_config_url(char *out, size_t out_size)
{
    if (out && out_size) out[0] = 0;
    return false;
}

#endif
