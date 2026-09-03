#pragma once
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
typedef enum {
    HTTP_EVENT_ERROR = 0, HTTP_EVENT_ON_CONNECTED, HTTP_EVENT_HEADER_SENT,
    HTTP_EVENT_ON_HEADER, HTTP_EVENT_ON_DATA, HTTP_EVENT_ON_FINISH,
    HTTP_EVENT_DISCONNECTED, HTTP_EVENT_REDIRECT
} esp_http_client_event_id_t;
typedef enum { HTTP_METHOD_GET = 0, HTTP_METHOD_POST } esp_http_client_method_t;
typedef enum { HTTP_TRANSPORT_UNKNOWN = 0, HTTP_TRANSPORT_OVER_TCP, HTTP_TRANSPORT_OVER_SSL } esp_http_client_transport_t;
typedef struct esp_http_client *esp_http_client_handle_t;
typedef struct {
    esp_http_client_event_id_t event_id;
    esp_http_client_handle_t   client;
    void *data; int data_len; void *user_data; char *header_key, *header_value;
} esp_http_client_event_t;
typedef esp_err_t (*http_event_handle_cb)(esp_http_client_event_t *evt);
typedef struct {
    const char *url; const char *host; int port; const char *path;
    esp_http_client_method_t method; int timeout_ms;
    int max_redirection_count;   /* ota_update.c sets this */
    http_event_handle_cb event_handler; void *user_data;
    esp_err_t (*crt_bundle_attach)(void *conf);
    int buffer_size; int buffer_size_tx; bool disable_auto_redirect;
    esp_http_client_transport_t transport_type; const char *user_agent;
    bool skip_cert_common_name_check; bool keep_alive_enable;
} esp_http_client_config_t;
esp_http_client_handle_t esp_http_client_init(const esp_http_client_config_t *cfg);
esp_err_t esp_http_client_perform(esp_http_client_handle_t c);
esp_err_t esp_http_client_open(esp_http_client_handle_t c, int write_len);
int       esp_http_client_fetch_headers(esp_http_client_handle_t c);
int       esp_http_client_read(esp_http_client_handle_t c, char *buf, int len);
int       esp_http_client_get_status_code(esp_http_client_handle_t c);
int       esp_http_client_get_content_length(esp_http_client_handle_t c);
esp_err_t esp_http_client_set_url(esp_http_client_handle_t c, const char *url);
esp_err_t esp_http_client_set_method(esp_http_client_handle_t c, esp_http_client_method_t m);
esp_err_t esp_http_client_set_header(esp_http_client_handle_t c, const char *k, const char *v);
esp_err_t esp_http_client_close(esp_http_client_handle_t c);
esp_err_t esp_http_client_cleanup(esp_http_client_handle_t c);
