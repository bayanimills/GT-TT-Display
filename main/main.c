#include "waveshare_rgb_lcd_port.h"
#include "loading.h"
#include "theme.h"
#include "settings.h"
#include "chain.h"
#include "poolping.h"
#include "ota_update.h"
#include "display_control.h"
#include "feed_web.h"

#include "esp_log.h"
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
    /* Timezone must be applied before the display schedule can be judged. */
    settings_initialize();

    /* The RSS worker waits for the display's own STA address before opening
     * its token-protected LAN editor or fetching. It is safe to start before
     * the BAP Wi-Fi credentials arrive. */
    if (!feed_web_init()) {
        ESP_LOGW(TAG, "RSS feed service could not start");
    }

    /* Starts the chain refresh task. It waits for the TCP/IP stack and an
     * associated radio before its first fetch, so the odds screen has a
     * snapshot waiting the first time it is opened without the task touching
     * lwIP before there is a stack to touch. */
    chain_init();

    /* Opt-in, and only ever a check: the install stays behind the button
     * in settings. Does nothing while the setting is off. */
    ota_update_start_auto_check();

    poolping_start();

    ESP_LOGI(TAG, "BAP Touch Display -- Build by WantClue with Love");
    ESP_LOGI(TAG, "theme: %s", theme_get_name());

    // Lock the mutex due to the LVGL APIs are not thread-safe
    if (lvgl_port_lock(-1)) {
        // screen init
        loading();
        ESP_ERROR_CHECK(display_control_init());

        // Release the mutex
        lvgl_port_unlock();
    }

}
