/* sim_shims.c -- stand-ins for the modules the sim deliberately does not build:
 * the OTA updater, the BAP UART client and the BAP serial link. The sim feeds
 * BAP sentences straight into the parser instead, so nothing here needs to do
 * real work -- it only has to satisfy the link and behave sanely on screen. */
#include <stdio.h>
#include <string.h>
#include <stdbool.h>

#include "esp_err.h"
#include "esp_log.h"
#include "nvs.h"
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
void ota_check_original_release(void)
{
    s_ota.restore_status = OTA_RESTORE_READY;
    snprintf(s_ota.original_version, sizeof(s_ota.original_version), "v1.1.2");
    ESP_LOGI(TAG, "ota: official release ready (sim)");
}
esp_err_t ota_update_start_latest(void) { return ESP_FAIL; }
esp_err_t ota_restore_original_latest(void) { return ESP_FAIL; }
esp_err_t ota_update_start(const char *url) { (void) url; return ESP_FAIL; }
void ota_update_get_info(ota_info_t *info)
{
    if (!info) return;
    *info = s_ota;
    if (!info->current_version[0]) {
        snprintf(info->current_version, sizeof(info->current_version), "sim");
    }
}
bool ota_update_is_running(void) { return false; }
void ota_update_confirm_running_image(void) { ESP_LOGI(TAG, "ota: image confirmed (sim no-op)"); }
const char *ota_get_current_version(void) { return "sim"; }

/* The daily check is a real preference even here: the settings toggle reads
 * and writes it, and NVS is a file, so it survives a restart of the sim the
 * same way it survives a reboot of the panel. Nothing polls, because there is
 * nothing here to install. */
static bool s_auto_check_loaded = false;
static bool s_auto_check = false;

bool ota_update_get_auto_check(void)
{
    if (!s_auto_check_loaded) {
        s_auto_check_loaded = true;
        nvs_handle_t h;
        if (nvs_open("gtdisplay", NVS_READONLY, &h) == ESP_OK) {
            uint8_t v = 0;
            if (nvs_get_u8(h, "ota_auto", &v) == ESP_OK) s_auto_check = (v != 0);
            nvs_close(h);
        }
    }
    return s_auto_check;
}

void ota_update_set_auto_check(bool enabled)
{
    (void) ota_update_get_auto_check();
    s_auto_check = enabled;
    nvs_handle_t h;
    if (nvs_open("gtdisplay", NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_u8(h, "ota_auto", enabled ? 1 : 0);
        nvs_commit(h);
        nvs_close(h);
    }
    ESP_LOGI(TAG, "ota: daily check %s", enabled ? "on" : "off");
}

bool ota_update_available(void) { return s_ota.status == OTA_STATUS_UPDATE_AVAILABLE; }

/* Let the simulator pretend a release is waiting, so the badge and the
  * settings wording can be checked without publishing one. */
void sim_ota_fake_available(bool on)
{
    s_ota.status = on ? OTA_STATUS_UPDATE_AVAILABLE : OTA_STATUS_IDLE;
}
void sim_ota_fake_original_available(bool on)
{
    s_ota.restore_status = on ? OTA_RESTORE_READY : OTA_RESTORE_IDLE;
    snprintf(s_ota.original_version, sizeof(s_ota.original_version), "%s", on ? "v1.1.2" : "");
}
void ota_update_start_auto_check(void) { }

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
