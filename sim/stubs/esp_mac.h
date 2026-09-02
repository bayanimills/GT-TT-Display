#pragma once
#include <stdint.h>
#include "esp_err.h"
typedef enum { ESP_MAC_WIFI_STA, ESP_MAC_WIFI_SOFTAP } esp_mac_type_t;
static inline esp_err_t esp_read_mac(uint8_t *m, esp_mac_type_t t) {
    (void)t; static const uint8_t d[6] = {0x30,0xED,0xA0,0xAB,0xAF,0xB0};
    for (int i = 0; i < 6; i++) m[i] = d[i];
    return ESP_OK;
}
