/**
 * @file ota_screen.h
 *
 * Minimal OTA update screen with reduced LVGL activity
 */

#ifndef OTA_SCREEN_H
#define OTA_SCREEN_H

#ifdef __cplusplus
extern "C" {
#endif

#include "lvgl_port.h"
#include <stdbool.h>

/**
 * @brief Show OTA update screen (call once before OTA starts)
 */
void ota_screen_show(void);

/**
 * @brief Label the overlay for the phase in progress.
 *
 * Downloading writes only to the partition the device is not running from, so
 * losing power part way costs nothing but the transfer. Installing is the
 * commit, and is the only part worth warning anyone about.
 *
 * @param installing true once the download has landed and the boot partition
 *                   is about to be switched
 */
void ota_screen_set_phase(bool installing);

/**
 * @brief Update progress when the reported percentage changes
 * @param progress Progress percentage 0-100
 */
void ota_screen_update_progress(int progress);

/**
 * @brief Show error message on OTA screen
 */
void ota_screen_show_error(const char *error);

/**
 * @brief Hide OTA screen and return to normal operation
 */
void ota_screen_hide(void);

#ifdef __cplusplus
}
#endif

#endif /* OTA_SCREEN_H */
