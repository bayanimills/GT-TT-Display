/**
 * OTA Update Module for GT-TT-Display
 *
 * Provides over-the-air firmware updates from GitHub releases
 *
 * Usage:
 *   ota_check_for_updates();  // Check if newer version available
 *   ota_update_start(url);    // Start OTA update
 */

#pragma once

#include "esp_err.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    OTA_STATUS_IDLE = 0,
    OTA_STATUS_CHECKING,
    OTA_STATUS_UPDATE_AVAILABLE,
    OTA_STATUS_NO_UPDATE,
    OTA_STATUS_DOWNLOADING,
    OTA_STATUS_FLASHING,
    OTA_STATUS_SUCCESS,
    OTA_STATUS_ERROR
} ota_status_t;

typedef struct {
    ota_status_t status;
    int progress_percent;  // 0-100
    char error_msg[128];
    char latest_version[32];
    char current_version[32];
} ota_info_t;


void ota_check_for_updates(void);

/* With rollback enabled a freshly OTA'd image boots PENDING_VERIFY and is
 * reverted on the next boot unless it confirms itself. Call this once the
 * first real screen exists, so a build that crashes constructing it rolls
 * back on its own. No-op on the factory partition. */
void ota_update_confirm_running_image(void);

esp_err_t ota_update_start_latest(void);

esp_err_t ota_update_start(const char *url);

void ota_update_get_info(ota_info_t *info);

bool ota_update_is_running(void);

const char* ota_get_current_version(void);

#ifdef __cplusplus
}
#endif
