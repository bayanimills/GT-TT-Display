/**
 * OTA Update Module for GT-TT-Display
 *
 * Provides over-the-air firmware updates from GitHub releases
 *
 * Usage:
 *   ota_check_for_updates();  // Check if newer version available
 *   ota_update_start_latest(); // Install the release selected by that check
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

typedef enum {
    OTA_RESTORE_IDLE = 0,
    OTA_RESTORE_CHECKING,
    OTA_RESTORE_READY,
    OTA_RESTORE_ERROR
} ota_restore_status_t;

typedef struct {
    ota_status_t status;
    int progress_percent;  // 0-100
    char error_msg[128];
    char latest_version[32];
    char current_version[32];
    ota_restore_status_t restore_status;
    char original_version[32];
    char restore_error_msg[128];
} ota_info_t;


void ota_check_for_updates(void);

/* Fetch the latest official bitaxeorg display release. This is deliberately a
 * separate action from the fork updater: restoring is normally a downgrade
 * and must never be presented as an ordinary update. */
void ota_check_original_release(void);

/* Install the exact version-pinned OTA asset discovered by the preceding
 * official-release check. Returns ESP_ERR_INVALID_STATE until a valid release
 * has been fetched. The settings UI adds a separate confirmation step. */
esp_err_t ota_restore_original_latest(void);

/* Opt-in daily poll.
 *
 * Off by default, because a mining display should not go looking for new
 * firmware unless its owner asked it to. When on, the version endpoint is
 * checked once a day and nothing more: a release is never installed
 * without someone pressing the button. A bad build that installs itself
 * takes out the screen you would use to notice.
 *
 * Persisted, so the choice survives an update. */
bool ota_update_get_auto_check(void);
void ota_update_set_auto_check(bool enabled);

/* True when a poll or a manual check found a release newer than the
 * running one. Drives the badge, so it must stay cheap. */
bool ota_update_available(void);

/* Start the daily poll timer. Safe to call once, from app_main; it does
 * nothing while the setting is off, and nothing before the radio is up. */
void ota_update_start_auto_check(void);

/* With rollback enabled a freshly OTA'd image boots PENDING_VERIFY and is
 * reverted on the next boot unless it confirms itself. Call this once the
 * first real screen exists, so a build that crashes constructing it rolls
 * back on its own. No-op on the factory partition. */
void ota_update_confirm_running_image(void);

esp_err_t ota_update_start_latest(void);

/* Internal-compatible entry point: the URL must exactly match a release asset
 * selected and validated by a completed fork or official check. */
esp_err_t ota_update_start(const char *url);

void ota_update_get_info(ota_info_t *info);

bool ota_update_is_running(void);

const char* ota_get_current_version(void);

#ifdef __cplusplus
}
#endif
