#pragma once
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "esp_event.h"
#include "esp_netif.h"
#define WIFI_EVENT ((esp_event_base_t)"WIFI_EVENT")
#define WIFI_EVENT_STA_START        0
#define WIFI_EVENT_STA_DISCONNECTED 1
#define WIFI_EVENT_SCAN_DONE        2
#define ESP_ERR_WIFI_NOT_STARTED    0x3002
typedef enum { WIFI_MODE_NULL = 0, WIFI_MODE_STA, WIFI_MODE_AP, WIFI_MODE_APSTA } wifi_mode_t;
typedef enum { WIFI_AUTH_OPEN = 0, WIFI_AUTH_WEP, WIFI_AUTH_WPA_PSK, WIFI_AUTH_WPA2_PSK,
               WIFI_AUTH_WPA_WPA2_PSK, WIFI_AUTH_WPA3_PSK, WIFI_AUTH_WPA2_WPA3_PSK, WIFI_AUTH_MAX } wifi_auth_mode_t;
typedef enum { WIFI_PS_NONE = 0, WIFI_PS_MIN_MODEM } wifi_ps_type_t;
typedef struct { uint8_t ssid[33]; uint8_t bssid[6]; int8_t rssi; wifi_auth_mode_t authmode; uint8_t primary; } wifi_ap_record_t;
typedef struct { uint8_t ssid[32]; uint8_t password[64]; wifi_auth_mode_t threshold_authmode;
                 struct { wifi_auth_mode_t authmode; } threshold; } wifi_sta_config_t;
typedef union { wifi_sta_config_t sta; } wifi_config_t;
typedef struct { int dummy; } wifi_init_config_t;
typedef enum { WIFI_SCAN_TYPE_ACTIVE = 0, WIFI_SCAN_TYPE_PASSIVE } wifi_scan_type_t;
typedef struct { uint32_t min; uint32_t max; } wifi_active_scan_time_t;
typedef struct { wifi_active_scan_time_t active; uint32_t passive; } wifi_scan_time_t;
typedef struct {
    uint8_t *ssid; uint8_t *bssid; uint8_t channel; bool show_hidden;
    wifi_scan_type_t scan_type; wifi_scan_time_t scan_time;
} wifi_scan_config_t;
#define WIFI_INIT_CONFIG_DEFAULT() ((wifi_init_config_t){0})
esp_err_t esp_wifi_init(const wifi_init_config_t *c);
esp_err_t esp_wifi_set_mode(wifi_mode_t m);
esp_err_t esp_wifi_set_config(int iface, wifi_config_t *c);
esp_err_t esp_wifi_get_config(int iface, wifi_config_t *c);
esp_err_t esp_wifi_start(void);
esp_err_t esp_wifi_stop(void);
esp_err_t esp_wifi_connect(void);
esp_err_t esp_wifi_disconnect(void);
esp_err_t esp_wifi_scan_start(const wifi_scan_config_t *c, bool block);
esp_err_t esp_wifi_scan_stop(void);
esp_err_t esp_wifi_scan_get_ap_num(uint16_t *n);
esp_err_t esp_wifi_scan_get_ap_records(uint16_t *n, wifi_ap_record_t *recs);
esp_err_t esp_wifi_sta_get_ap_info(wifi_ap_record_t *r);
esp_err_t esp_wifi_set_ps(wifi_ps_type_t t);
#define ESP_IF_WIFI_STA 0
#define WIFI_IF_STA     0
