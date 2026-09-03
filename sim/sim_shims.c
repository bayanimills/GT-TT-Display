/* sim_shims.c -- stand-ins for the modules the sim deliberately does not build:
 * the OTA updater, the BAP UART client and the BAP serial link. The sim feeds
 * BAP sentences straight into the parser instead, so nothing here needs to do
 * real work -- it only has to satisfy the link and behave sanely on screen. */
#include <stdio.h>
#include <string.h>
#include <stdbool.h>

#include "esp_err.h"
#include "esp_log.h"
#include "ota_update.h"
#include "ota_screen.h"
#include "bap_client.h"
#include "bap_uart.h"

static const char *TAG = "sim_shim";

/* ---- OTA ---- */

static ota_info_t s_ota;

void ota_check_for_updates(void)
{
    ESP_LOGI(TAG, "ota check (no-op in sim)");
}
esp_err_t ota_update_start_latest(void) { return ESP_FAIL; }
esp_err_t ota_update_start(const char *url) { (void) url; return ESP_FAIL; }
void ota_update_get_info(ota_info_t *info) { if (info) *info = s_ota; }
bool ota_update_is_running(void) { return false; }
void ota_update_confirm_running_image(void) { ESP_LOGI(TAG, "ota: image confirmed (sim no-op)"); }
const char *ota_get_current_version(void) { return "sim"; }

void ota_screen_show(void) { }
void ota_screen_update_progress(int p) { (void) p; }
void ota_screen_show_error(const char *e) { ESP_LOGE(TAG, "ota error: %s", e ? e : "?"); }
void ota_screen_hide(void) { }

/* ---- BAP client ----
 * The sim is always "connected": sentences arrive over stdin rather than UART. */

esp_err_t bap_client_init(void) { ESP_LOGI(TAG, "bap client (sim: stdin-fed)"); return ESP_OK; }
esp_err_t bap_client_subscribe(const char *p) { ESP_LOGI(TAG, "SUB %s", p ? p : "?"); return ESP_OK; }
esp_err_t bap_client_request(const char *p)   { ESP_LOGI(TAG, "REQ %s", p ? p : "?"); return ESP_OK; }
esp_err_t bap_client_send_frequency_setting(float f) { ESP_LOGI(TAG, "SET frequency %.1f", f); return ESP_OK; }
esp_err_t bap_client_send_asic_voltage(float v)      { ESP_LOGI(TAG, "SET asic_voltage %.0f", v); return ESP_OK; }
esp_err_t bap_client_send_fan_speed(int p)           { ESP_LOGI(TAG, "SET fan_speed %d", p); return ESP_OK; }
esp_err_t bap_client_send_automatic_fan_control(bool e) { ESP_LOGI(TAG, "SET auto_fan %d", (int) e); return ESP_OK; }
esp_err_t bap_client_send_ssid(const char *s)     { ESP_LOGI(TAG, "SET ssid %s", s ? s : "?"); return ESP_OK; }
esp_err_t bap_client_send_password(const char *p) { (void) p; ESP_LOGI(TAG, "SET password ***"); return ESP_OK; }
bool bap_client_is_connected(void) { return true; }
void bap_client_reset_connection_state(void) { }
void bap_client_suspend(void) { }
void bap_client_resume(void) { }

/* ---- BAP UART ---- */

esp_err_t bap_uart_init(void) { return ESP_OK; }
esp_err_t bap_uart_write(const char *d, size_t n) { (void) d; (void) n; return ESP_OK; }
int  bap_uart_read(uint8_t *b, size_t n, uint32_t t) { (void) b; (void) n; (void) t; return 0; }
esp_err_t bap_uart_flush(void) { return ESP_OK; }
esp_err_t bap_uart_deinit(void) { return ESP_OK; }
size_t bap_uart_get_buffer_size(void) { return 1024; }
