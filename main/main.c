#include "waveshare_rgb_lcd_port.h"
#include "loading.h"
#include "theme.h"
#include "settings.h"

#include "esp_log.h"
#include "esp_ota_ops.h"
#include "nvs_flash.h"

static const char *TAG = "main";

/* Bring NVS up before anything reads it. The theme and the saved timezone both
 * live there, so this has to happen before the first screen is built. */
static void storage_init(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        err = nvs_flash_init();
    }
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "NVS init failed (%s); running with defaults", esp_err_to_name(err));
    }
}

/* With CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE a freshly OTA'd image boots as
 * PENDING_VERIFY and is reverted on the next boot unless it confirms itself.
 * We confirm once the panel and the first screen are up, which is the bar for
 * "this build is usable enough to OTA again from". A build that crashes before
 * this point rolls back to the previous slot on its own.
 *
 * No-op on the factory partition, so USB-flashed images are unaffected. */
static void confirm_ota_image(void)
{
    const esp_partition_t *running = esp_ota_get_running_partition();
    esp_ota_img_states_t state;

    if (esp_ota_get_state_partition(running, &state) != ESP_OK) {
        return;
    }
    if (state != ESP_OTA_IMG_PENDING_VERIFY) {
        return;
    }
    if (esp_ota_mark_app_valid_cancel_rollback() == ESP_OK) {
        ESP_LOGI(TAG, "OTA image confirmed on %s", running->label);
    } else {
        ESP_LOGE(TAG, "failed to confirm OTA image; will roll back on reboot");
    }
}

void app_main()
{
    storage_init();
    theme_init();

    /* Screens bake their colours in at construction, so a theme change has to
     * rebuild the screen it was made from. Settings is the only place a theme
     * can be picked, so that is what gets rebuilt. */
    theme_register_reload(settings_rebuild_for_theme);

    waveshare_esp32_s3_rgb_lcd_init();
    wavesahre_rgb_lcd_bl_on();

    ESP_LOGI(TAG, "BAP Touch Display -- Build by WantClue with Love");
    ESP_LOGI(TAG, "theme: %s", theme_get_name());

    // Lock the mutex due to the LVGL APIs are not thread-safe
    if (lvgl_port_lock(-1)) {
        // screen init
        loading();

        // Release the mutex
        lvgl_port_unlock();
    }

    confirm_ota_image();
}
