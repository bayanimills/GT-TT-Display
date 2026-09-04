#pragma once
/* Enough of esp_ota_ops to syntax-check main/ota_update.c on a host.
 *
 * That file is excluded from the simulator build, because flashing a partition
 * is not something a workstation can usefully pretend to do. The cost of that
 * exclusion was that a typo in it could only be caught by a three minute round
 * trip through CI. These declarations close that gap for
 * `gcc -fsyntax-only`; nothing links against them.
 *
 * esp_app_get_description() stays inline and real, because the simulator does
 * link that one: the settings screen prints the running version. */
#include "esp_err.h"
#include <stddef.h>
#include <stdint.h>

typedef struct { char version[32]; char project_name[32]; } esp_app_desc_t;
static inline const esp_app_desc_t *esp_app_get_description(void) { static esp_app_desc_t d = {"sim", "gt-touch"}; return &d; }

typedef struct {
    uint32_t type;
    uint32_t subtype;
    uint32_t address;
    uint32_t size;
    char     label[17];
} esp_partition_t;

typedef enum {
    ESP_OTA_IMG_NEW = 0,
    ESP_OTA_IMG_PENDING_VERIFY,
    ESP_OTA_IMG_VALID,
    ESP_OTA_IMG_INVALID,
    ESP_OTA_IMG_ABORTED,
    ESP_OTA_IMG_UNDEFINED,
} esp_ota_img_states_t;

const esp_partition_t *esp_ota_get_running_partition(void);
const esp_partition_t *esp_ota_get_next_update_partition(const esp_partition_t *start_from);
esp_err_t esp_partition_read(const esp_partition_t *partition, size_t src_offset,
                             void *dst, size_t size);
esp_err_t esp_ota_get_state_partition(const esp_partition_t *partition,
                                      esp_ota_img_states_t *ota_state);
esp_err_t esp_ota_mark_app_valid_cancel_rollback(void);
esp_err_t esp_ota_mark_app_invalid_rollback_and_reboot(void);
