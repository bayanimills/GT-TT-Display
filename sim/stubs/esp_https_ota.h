#pragma once
/* Enough of esp_https_ota to syntax-check main/ota_update.c on a host.
 *
 * That file is excluded from the simulator build, since flashing a partition
 * is not something a workstation can usefully pretend to do. The cost of that
 * exclusion was that a typo in it could only be caught by a three minute round
 * trip through CI. These declarations close that gap: they make
 * `gcc -fsyntax-only ../main/ota_update.c` work, and nothing links against
 * them. */
#include "esp_err.h"
#include "esp_http_client.h"
#include "esp_app_desc.h"

/* esp_https_ota_perform() returns this while more data is still coming. */
#define ESP_ERR_HTTPS_OTA_IN_PROGRESS 0x8001

typedef struct esp_https_ota_handle_obj *esp_https_ota_handle_t;

typedef struct {
    const esp_http_client_config_t *http_config;
    void  *http_client_init_cb;
    bool   bulk_flash_erase;
    bool   partial_http_download;
    int    max_http_request_size;
} esp_https_ota_config_t;

esp_err_t esp_https_ota(const esp_https_ota_config_t *config);
esp_err_t esp_https_ota_begin(const esp_https_ota_config_t *config,
                              esp_https_ota_handle_t *handle);
esp_err_t esp_https_ota_perform(esp_https_ota_handle_t handle);
esp_err_t esp_https_ota_finish(esp_https_ota_handle_t handle);
esp_err_t esp_https_ota_abort(esp_https_ota_handle_t handle);
int       esp_https_ota_get_image_len_read(esp_https_ota_handle_t handle);
int       esp_https_ota_get_image_size(esp_https_ota_handle_t handle);
esp_err_t esp_https_ota_get_img_desc(esp_https_ota_handle_t handle, esp_app_desc_t *new_app_info);
bool      esp_https_ota_is_complete_data_received(esp_https_ota_handle_t handle);
