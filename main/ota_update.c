/**
 * OTA Update Module for GT-TT-Display
 */

#include "ota_update.h"
#include "ota_release.h"
#include "wifi.h"
#include "nvs.h"
#include "ota_screen.h"
#include "settings.h"
#include "bap_client.h"
#include "lvgl_port.h"
#include "waveshare_rgb_lcd_port.h"
#include "esp_log.h"
#include "esp_system.h"   /* esp_restart(); do not rely on a transitive include */
#include "esp_https_ota.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "esp_app_desc.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "mbedtls/sha256.h"
#include <string.h>
#include <stdlib.h>

static const char *TAG = "OTA";

#define OTA_STACK_SIZE 10240
#define OTA_TASK_PRIORITY 1
#define VERSION_CHECK_STACK_SIZE 8192
#define FORK_GITHUB_API_URL "https://api.github.com/repos/bayanimills/GT-TT-Display/releases/latest"
#define ORIGINAL_GITHUB_API_URL "https://api.github.com/repos/bitaxeorg/BAP-GT-TOUCH/releases/latest"
#define GITHUB_RESPONSE_MAX (32 * 1024)

static SemaphoreHandle_t ota_mutex = NULL;
static ota_info_t ota_current_info = {0};

#define OTA_NVS_NAMESPACE  "gtdisplay"
#define OTA_NVS_AUTO_CHECK "ota_auto"
/* Once a day. Often enough to hear about a release the day it lands, rare
 * enough that the panel is not talking to GitHub for no reason. */
#define OTA_AUTO_CHECK_INTERVAL_MS (24 * 60 * 60 * 1000)
/* The first poll waits this long so it does not race the radio at boot. */
#define OTA_AUTO_CHECK_FIRST_MS    (2 * 60 * 1000)

static bool ota_auto_check_enabled = false;
static bool ota_auto_check_loaded = false;
static TaskHandle_t ota_auto_task_handle = NULL;
/* These booleans are reservations as well as running flags. Set them while the
 * mutex is held *before* xTaskCreate, otherwise two callers can both observe a
 * NULL task handle and start competing flash/check operations. */
static bool ota_task_running = false;
static bool version_check_running = false;
static bool original_check_running = false;
static ota_release_t ota_latest_release = {0};
static ota_release_t ota_original_release = {0};

typedef struct {
    ota_release_t release;
} ota_install_request_t;

static void ota_init_mutex(void)
{
    if (ota_mutex == NULL) {
        ota_mutex = xSemaphoreCreateMutex();
        configASSERT(ota_mutex != NULL);
    }
}

static void ota_set_status(ota_status_t status, int progress, const char *error)
{
    ota_init_mutex();
    xSemaphoreTake(ota_mutex, portMAX_DELAY);

    ota_current_info.status = status;
    ota_current_info.progress_percent = progress;

    if (error) {
        strlcpy(ota_current_info.error_msg, error, sizeof(ota_current_info.error_msg));
    } else {
        ota_current_info.error_msg[0] = '\0';
    }

    const char *ver = ota_get_current_version();
    if (ver) {
        strlcpy(ota_current_info.current_version, ver, sizeof(ota_current_info.current_version));
    }

    xSemaphoreGive(ota_mutex);
}

static bool ota_any_operation_running_locked(void)
{
    return ota_task_running || version_check_running || original_check_running;
}

static int hex_nibble(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

/* GitHub supplies a digest for each uploaded release asset. Verify the exact
 * bytes ESP-IDF wrote before esp_https_ota_finish selects the new partition. */
static bool ota_partition_matches_digest(const esp_partition_t *partition,
                                         size_t image_size, const char *digest)
{
    static const char prefix[] = "sha256:";
    if (!partition || image_size == 0 || image_size > partition->size ||
        !digest || strncmp(digest, prefix, sizeof(prefix) - 1U) != 0 ||
        strlen(digest + sizeof(prefix) - 1U) != 64U) return false;

    uint8_t expected[32];
    const char *hex = digest + sizeof(prefix) - 1U;
    for (size_t i = 0; i < sizeof(expected); i++) {
        int hi = hex_nibble(hex[i * 2U]);
        int lo = hex_nibble(hex[i * 2U + 1U]);
        if (hi < 0 || lo < 0) return false;
        expected[i] = (uint8_t)((hi << 4) | lo);
    }

    uint8_t *chunk = malloc(4096);
    if (!chunk) return false;
    mbedtls_sha256_context ctx;
    mbedtls_sha256_init(&ctx);
    bool ok = mbedtls_sha256_starts(&ctx, 0) == 0;
    for (size_t offset = 0; ok && offset < image_size;) {
        size_t n = image_size - offset;
        if (n > 4096U) n = 4096U;
        if (esp_partition_read(partition, offset, chunk, n) != ESP_OK ||
            mbedtls_sha256_update(&ctx, chunk, n) != 0) {
            ok = false;
            break;
        }
        offset += n;
    }
    uint8_t actual[32] = {0};
    if (ok) ok = mbedtls_sha256_finish(&ctx, actual) == 0;
    mbedtls_sha256_free(&ctx);
    free(chunk);

    uint8_t different = 0;
    for (size_t i = 0; i < sizeof(actual); i++) different |= actual[i] ^ expected[i];
    return ok && different == 0;
}

/* Fetch the complete response, then select a release-pinned OTA asset. GitHub's
 * current release response is about 7.5 KiB; the old 4 KiB reader silently
 * truncated it and happened to work only because tag_name was near the top. */
static esp_err_t fetch_github_release(const char *api_url,
                                      const char *owner, const char *repo,
                                      ota_release_t *release,
                                      const char **friendly_error)
{
    esp_http_client_config_t config = {
        .url = api_url,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms = 15000,
        .buffer_size = 4096,
        .user_agent = "GT-TT-Display-OTA",
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    char *buffer = NULL;
    esp_err_t result = ESP_FAIL;
    if (!client) {
        *friendly_error = "HTTP init failed";
        return ESP_ERR_NO_MEM;
    }

    esp_http_client_set_header(client, "Accept", "application/vnd.github+json");
    esp_http_client_set_header(client, "X-GitHub-Api-Version", "2022-11-28");
    esp_err_t err = esp_http_client_open(client, 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open release API: %s", esp_err_to_name(err));
        *friendly_error = "Connection failed";
        goto cleanup;
    }

    int content_length = esp_http_client_fetch_headers(client);
    int status = esp_http_client_get_status_code(client);
    if (status != 200) {
        ESP_LOGE(TAG, "Release API HTTP status: %d", status);
        *friendly_error = status == 403 ? "GitHub rate limit reached" : "Release server returned error";
        goto cleanup;
    }
    if (content_length > GITHUB_RESPONSE_MAX) {
        *friendly_error = "Release response too large";
        goto cleanup;
    }

    buffer = malloc(GITHUB_RESPONSE_MAX + 1U);
    if (!buffer) {
        *friendly_error = "Out of memory";
        result = ESP_ERR_NO_MEM;
        goto cleanup;
    }

    size_t total = 0;
    for (;;) {
        int n = esp_http_client_read(client, buffer + total,
                                     (int)(GITHUB_RESPONSE_MAX - total));
        if (n < 0) {
            *friendly_error = "Release download failed";
            goto cleanup;
        }
        if (n == 0) break;
        total += (size_t)n;
        if (total == GITHUB_RESPONSE_MAX) {
            char extra;
            n = esp_http_client_read(client, &extra, 1);
            if (n != 0) {
                *friendly_error = n < 0 ? "Release download failed" : "Release response too large";
                goto cleanup;
            }
            break;
        }
    }
    buffer[total] = '\0';

    const esp_partition_t *target = esp_ota_get_next_update_partition(NULL);
    size_t max_size = target ? target->size : (4U * 1024U * 1024U);
    if (!ota_release_parse_github(buffer, total, owner, repo, max_size, release)) {
        *friendly_error = "No valid OTA asset in release";
        goto cleanup;
    }
    result = ESP_OK;

cleanup:
    free(buffer);
    esp_http_client_cleanup(client);
    return result;
}

static void version_check_task(void *param)
{
    (void)param;
    ESP_LOGI(TAG, "Checking fork firmware release...");
    ota_set_status(OTA_STATUS_CHECKING, 0, NULL);

    ota_release_t release;
    const char *friendly_error = "Release check failed";
    esp_err_t err = fetch_github_release(FORK_GITHUB_API_URL,
                                         "bayanimills", "GT-TT-Display",
                                         &release, &friendly_error);
    if (err != ESP_OK) {
        ota_set_status(OTA_STATUS_ERROR, 0, friendly_error);
    } else {
        ota_init_mutex();
        xSemaphoreTake(ota_mutex, portMAX_DELAY);
        strlcpy(ota_current_info.latest_version, release.tag,
                sizeof(ota_current_info.latest_version));
        ota_latest_release = release;
        xSemaphoreGive(ota_mutex);

        const char *current = ota_get_current_version();
        if (ota_release_version_is_newer(release.tag, current)) {
            ota_set_status(OTA_STATUS_UPDATE_AVAILABLE, 0, NULL);
        } else {
            ota_set_status(OTA_STATUS_NO_UPDATE, 0, NULL);
        }
    }

    ota_init_mutex();
    xSemaphoreTake(ota_mutex, portMAX_DELAY);
    version_check_running = false;
    xSemaphoreGive(ota_mutex);
    vTaskDelete(NULL);
}

static void original_check_task(void *param)
{
    (void)param;
    ota_release_t release;
    const char *friendly_error = "Official release check failed";
    esp_err_t err = fetch_github_release(ORIGINAL_GITHUB_API_URL,
                                         "bitaxeorg", "BAP-GT-TOUCH",
                                         &release, &friendly_error);

    ota_init_mutex();
    xSemaphoreTake(ota_mutex, portMAX_DELAY);
    if (err == ESP_OK) {
        strlcpy(ota_current_info.original_version, release.tag,
                sizeof(ota_current_info.original_version));
        ota_current_info.restore_error_msg[0] = '\0';
        ota_current_info.restore_status = OTA_RESTORE_READY;
        ota_original_release = release;
    } else {
        ota_current_info.restore_status = OTA_RESTORE_ERROR;
        strlcpy(ota_current_info.restore_error_msg, friendly_error,
                sizeof(ota_current_info.restore_error_msg));
        memset(&ota_original_release, 0, sizeof(ota_original_release));
    }
    original_check_running = false;
    xSemaphoreGive(ota_mutex);
    vTaskDelete(NULL);
}

static void ota_task(void *param)
{
    ota_install_request_t *request = (ota_install_request_t *)param;
    const char *url = request->release.url;
    esp_err_t err;
    esp_https_ota_handle_t ota_handle = NULL;
    const esp_partition_t *target = esp_ota_get_next_update_partition(NULL);

    ESP_LOGI(TAG, "Starting OTA update from: %s", url);

    if (!target) {
        ota_set_status(OTA_STATUS_ERROR, 0, "No OTA partition available");
        goto cleanup;
    }

    // Suspend BAP client to prevent UART updates during OTA
    bap_client_suspend();

    // Show OTA screen with LVGL lock
    if (lvgl_port_lock(1000)) {
        ESP_LOGW(TAG, "=== SHOWING OTA SCREEN ===");
        ota_screen_show();
        lvgl_port_unlock();
    }

    /* Long enough to read the warning, not just to render it. 100 ms was
     * enough for LVGL to draw the screen and far too little for a person to
     * see it, so the display appeared to die the instant the update was
     * confirmed. */
    vTaskDelay(pdMS_TO_TICKS(4500));
    lvgl_port_task_suspend();

    /* The backlight stays on. It used to be cut here so that flash writes,
     * which stall the PSRAM the panel scans out of, could not tear the
     * picture. The cost was a display that went black the instant the update
     * was confirmed and stayed black for minutes, which reads as a dead
     * device and invites exactly the yank that would ruin it. A frozen or
     * briefly torn progress screen says working; a black one says broken.
     *
     * LVGL stays suspended between writes, so the panel just rescans the same
     * framebuffer; it is woken only to move the bar. */

    ota_set_status(OTA_STATUS_DOWNLOADING, 0, NULL);

    esp_http_client_config_t http_config = {
        .url = url,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms = 30000,
        .buffer_size = 4096,
        .buffer_size_tx = 1024,
        .keep_alive_enable = true,
        .max_redirection_count = 10,
    };

    esp_https_ota_config_t ota_config = {
        .http_config = &http_config,
    };

    err = esp_https_ota_begin(&ota_config, &ota_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_https_ota_begin failed: %s", esp_err_to_name(err));
        ota_set_status(OTA_STATUS_ERROR, 0, "OTA begin failed");
        if (lvgl_port_lock(100)) {
            ota_screen_show_error("Connection failed");
            lvgl_port_unlock();
        }
        vTaskDelay(pdMS_TO_TICKS(3000));
        goto cleanup;
    }

    /* HTTPS proves where the bytes came from; the app descriptor proves they
     * are for this display rather than another ESP32 project in the same org. */
    esp_app_desc_t new_app = {0};
    err = esp_https_ota_get_img_desc(ota_handle, &new_app);
    if (err != ESP_OK || strcmp(new_app.project_name, "lvgl_porting") != 0 ||
        strcmp(new_app.version, request->release.tag) != 0) {
        ESP_LOGE(TAG, "OTA image descriptor mismatch: project=%s version=%s expected=%s",
                 err == ESP_OK ? new_app.project_name : "unreadable",
                 err == ESP_OK ? new_app.version : "unreadable", request->release.tag);
        ota_set_status(OTA_STATUS_ERROR, 0, "Not GT Touch display firmware");
        if (lvgl_port_lock(100)) {
            ota_screen_show_error("Wrong firmware image");
            lvgl_port_unlock();
        }
        esp_https_ota_abort(ota_handle);
        ota_handle = NULL;
        goto cleanup;
    }

    int image_size = esp_https_ota_get_image_size(ota_handle);
    ESP_LOGI(TAG, "Image size: %d bytes", image_size);

    ota_set_status(OTA_STATUS_FLASHING, 0, NULL);

    // Update screen once before flashing starts
    if (lvgl_port_lock(100)) {
        ota_screen_update_progress(1);  // Show we've started flashing
        lvgl_port_unlock();
    }

    int last_shown_progress = -1;

    ESP_LOGI(TAG, "Starting flash write (BAP client suspended)");

    while (1) {
        err = esp_https_ota_perform(ota_handle);
        if (err == ESP_ERR_HTTPS_OTA_IN_PROGRESS) {
            int read_len = esp_https_ota_get_image_len_read(ota_handle);
            int progress = 0;
            if (image_size > 0) {
                progress = (read_len * 100) / image_size;
                if (progress > 100) progress = 100;
            }
            ota_set_status(OTA_STATUS_FLASHING, progress, NULL);

            // Log progress periodically
            if ((read_len & 0xFFFF) < 4096) {
                ESP_LOGI(TAG, "Flash progress: %d%% (%d/%d bytes)", progress, read_len, image_size);
            }

            /* Wake the UI only when the figure has actually moved. Repainting
             * is safe between writes but not free, and doing it every pass
             * would slow the install for no extra information. */
            if (progress != last_shown_progress) {
                last_shown_progress = progress;
                lvgl_port_task_resume();
                if (lvgl_port_lock(100)) {
                    ota_screen_update_progress(progress);
                    lvgl_port_unlock();
                }
                vTaskDelay(pdMS_TO_TICKS(30));
                lvgl_port_task_suspend();
            }

            // Small delay to allow other tasks to run
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }
        if (err == ESP_OK) {
            break;
        }
        ESP_LOGE(TAG, "esp_https_ota_perform failed: %s", esp_err_to_name(err));
        ota_set_status(OTA_STATUS_ERROR, 0, "Download/flash failed");
        if (lvgl_port_lock(100)) {
            ota_screen_show_error("Download/flash failed");
            lvgl_port_unlock();
        }
        esp_https_ota_abort(ota_handle);
        ota_handle = NULL;
        goto cleanup;
    }

    if (!esp_https_ota_is_complete_data_received(ota_handle)) {
        ota_set_status(OTA_STATUS_ERROR, 0, "Incomplete firmware download");
        if (lvgl_port_lock(100)) {
            ota_screen_show_error("Incomplete download");
            lvgl_port_unlock();
        }
        esp_https_ota_abort(ota_handle);
        ota_handle = NULL;
        goto cleanup;
    }

    int received = esp_https_ota_get_image_len_read(ota_handle);
    if (received < 0 || (size_t)received != request->release.size ||
        !ota_partition_matches_digest(target, request->release.size,
                                      request->release.digest)) {
        ota_set_status(OTA_STATUS_ERROR, 0, "Firmware integrity check failed");
        if (lvgl_port_lock(100)) {
            ota_screen_show_error("Integrity check failed");
            lvgl_port_unlock();
        }
        esp_https_ota_abort(ota_handle);
        ota_handle = NULL;
        goto cleanup;
    }

    err = esp_https_ota_finish(ota_handle);
    ota_handle = NULL;
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_https_ota_finish failed: %s", esp_err_to_name(err));
        ota_set_status(OTA_STATUS_ERROR, 0, "OTA validation failed");
        if (lvgl_port_lock(100)) {
            ota_screen_show_error("Validation failed");
            lvgl_port_unlock();
        }
        goto cleanup;
    }

    ota_set_status(OTA_STATUS_SUCCESS, 100, NULL);
    ESP_LOGI(TAG, "OTA update successful! Rebooting in 3 seconds...");

    /* On throughout now, but make sure: a schedule or a corner tap could have
     * turned it off while this ran. */
    lcd_backlight_enable();

    // Resume LVGL task to show completion message
    lvgl_port_task_resume();
    vTaskDelay(pdMS_TO_TICKS(50)); // Brief delay for LVGL to start

    // Show completion
    if (lvgl_port_lock(100)) {
        ota_screen_update_progress(100);
        lvgl_port_unlock();
    }

    vTaskDelay(pdMS_TO_TICKS(3000));
    esp_restart();

cleanup:
    if (ota_handle) {
        esp_https_ota_abort(ota_handle);
    }

    /* On throughout now, but make sure: a schedule or a corner tap could have
     * turned it off while this ran. */
    lcd_backlight_enable();

    // Resume LVGL task first
    lvgl_port_task_resume();

    // Show error screen for a few seconds, then hide it
    vTaskDelay(pdMS_TO_TICKS(3000));
    if (lvgl_port_lock(100)) {
        ota_screen_hide();
        lvgl_port_unlock();
    }

    // Resume BAP client tasks
    bap_client_resume();

    free(request);

    ota_init_mutex();
    xSemaphoreTake(ota_mutex, portMAX_DELAY);
    ota_task_running = false;
    xSemaphoreGive(ota_mutex);

    vTaskDelete(NULL);
}

void ota_update_confirm_running_image(void)
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

static void ota_auto_check_load(void)
{
    if (ota_auto_check_loaded) {
        return;
    }
    ota_auto_check_loaded = true;

    nvs_handle_t h;
    if (nvs_open(OTA_NVS_NAMESPACE, NVS_READONLY, &h) != ESP_OK) {
        return;
    }
    uint8_t v = 0;
    if (nvs_get_u8(h, OTA_NVS_AUTO_CHECK, &v) == ESP_OK) {
        ota_auto_check_enabled = (v != 0);
    }
    nvs_close(h);
}

bool ota_update_get_auto_check(void)
{
    ota_auto_check_load();
    return ota_auto_check_enabled;
}

void ota_update_set_auto_check(bool enabled)
{
    ota_auto_check_load();
    if (enabled == ota_auto_check_enabled) {
        return;
    }
    ota_auto_check_enabled = enabled;

    nvs_handle_t h;
    if (nvs_open(OTA_NVS_NAMESPACE, NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_u8(h, OTA_NVS_AUTO_CHECK, enabled ? 1 : 0);
        nvs_commit(h);
        nvs_close(h);
    }
    ESP_LOGI(TAG, "daily update check %s", enabled ? "on" : "off");

    /* Turning it on should tell you something today, not tomorrow. */
    if (enabled && ota_auto_task_handle) {
        xTaskNotifyGive(ota_auto_task_handle);
    }
}

bool ota_update_available(void)
{
    ota_init_mutex();
    xSemaphoreTake(ota_mutex, portMAX_DELAY);
    bool available = ota_current_info.status == OTA_STATUS_UPDATE_AVAILABLE;
    xSemaphoreGive(ota_mutex);
    return available;
}

/* Polls, never installs. The install stays behind the button in settings. */
static void ota_auto_check_task(void *arg)
{
    (void) arg;
    uint32_t wait_ms = OTA_AUTO_CHECK_FIRST_MS;

    for (;;) {
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(wait_ms));
        wait_ms = OTA_AUTO_CHECK_INTERVAL_MS;

        if (!ota_update_get_auto_check()) {
            continue;
        }
        /* No point asking GitHub before the radio is associated, and the
         * check must not fight an install that is already running. */
        if (!wifi_is_connected() || ota_update_is_running()) {
            wait_ms = 5 * 60 * 1000;
            continue;
        }
        /* Leave a found update on screen rather than re-checking over it. */
        if (ota_update_available()) {
            continue;
        }
        ESP_LOGI(TAG, "daily update check");
        ota_check_for_updates();
    }
}

void ota_update_start_auto_check(void)
{
    if (ota_auto_task_handle) {
        return;
    }
    ota_auto_check_load();
    xTaskCreate(ota_auto_check_task, "ota_auto", 3072, NULL, 3, &ota_auto_task_handle);
}

void ota_check_for_updates(void)
{
    ota_init_mutex();
    xSemaphoreTake(ota_mutex, portMAX_DELAY);
    if (ota_any_operation_running_locked()) {
        ESP_LOGW(TAG, "Update check or OTA already in progress");
        xSemaphoreGive(ota_mutex);
        return;
    }
    version_check_running = true;
    xSemaphoreGive(ota_mutex);

    BaseType_t ret = xTaskCreate(
        version_check_task,
        "version_check",
        VERSION_CHECK_STACK_SIZE,
        NULL,
        OTA_TASK_PRIORITY,
        NULL
    );

    if (ret != pdPASS) {
        xSemaphoreTake(ota_mutex, portMAX_DELAY);
        version_check_running = false;
        xSemaphoreGive(ota_mutex);
        ESP_LOGE(TAG, "Failed to create version check task");
        ota_set_status(OTA_STATUS_ERROR, 0, "Task creation failed");
    }
}

void ota_check_original_release(void)
{
    ota_init_mutex();
    xSemaphoreTake(ota_mutex, portMAX_DELAY);
    if (ota_any_operation_running_locked()) {
        ESP_LOGW(TAG, "Update check or OTA already in progress");
        xSemaphoreGive(ota_mutex);
        return;
    }
    original_check_running = true;
    ota_current_info.restore_status = OTA_RESTORE_CHECKING;
    ota_current_info.restore_error_msg[0] = '\0';
    memset(&ota_original_release, 0, sizeof(ota_original_release));
    xSemaphoreGive(ota_mutex);

    BaseType_t ret = xTaskCreate(original_check_task, "original_check",
                                 VERSION_CHECK_STACK_SIZE, NULL,
                                 OTA_TASK_PRIORITY, NULL);
    if (ret != pdPASS) {
        xSemaphoreTake(ota_mutex, portMAX_DELAY);
        original_check_running = false;
        ota_current_info.restore_status = OTA_RESTORE_ERROR;
        strlcpy(ota_current_info.restore_error_msg, "Task creation failed",
                sizeof(ota_current_info.restore_error_msg));
        xSemaphoreGive(ota_mutex);
    }
}

esp_err_t ota_update_start_latest(void)
{
    ota_release_t release;
    ota_init_mutex();
    xSemaphoreTake(ota_mutex, portMAX_DELAY);
    bool ready = ota_current_info.status == OTA_STATUS_UPDATE_AVAILABLE &&
                 ota_latest_release.url[0] != '\0';
    release = ota_latest_release;
    xSemaphoreGive(ota_mutex);
    return ready ? ota_update_start(release.url) : ESP_ERR_INVALID_STATE;
}

esp_err_t ota_restore_original_latest(void)
{
    ota_release_t release;
    ota_init_mutex();
    xSemaphoreTake(ota_mutex, portMAX_DELAY);
    bool ready = ota_current_info.restore_status == OTA_RESTORE_READY &&
                 ota_original_release.url[0] != '\0';
    release = ota_original_release;
    xSemaphoreGive(ota_mutex);
    esp_err_t ret = ready ? ota_update_start(release.url) : ESP_ERR_INVALID_STATE;
    if (ret != ESP_OK) {
        xSemaphoreTake(ota_mutex, portMAX_DELAY);
        ota_current_info.restore_status = OTA_RESTORE_ERROR;
        strlcpy(ota_current_info.restore_error_msg, "Could not start restore",
                sizeof(ota_current_info.restore_error_msg));
        xSemaphoreGive(ota_mutex);
    }
    return ret;
}

esp_err_t ota_update_start(const char *url)
{
    if (!url || strlen(url) == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!ota_release_url_allowed(url)) {
        ESP_LOGE(TAG, "URL validation failed");
        return ESP_ERR_INVALID_ARG;
    }

    ota_install_request_t *request = calloc(1, sizeof(*request));
    if (!request) {
        return ESP_ERR_NO_MEM;
    }

    ota_init_mutex();
    xSemaphoreTake(ota_mutex, portMAX_DELAY);
    if (ota_current_info.status == OTA_STATUS_UPDATE_AVAILABLE &&
        strcmp(url, ota_latest_release.url) == 0) {
        request->release = ota_latest_release;
    } else if (ota_current_info.restore_status == OTA_RESTORE_READY &&
               strcmp(url, ota_original_release.url) == 0) {
        request->release = ota_original_release;
    }
    else {
        xSemaphoreGive(ota_mutex);
        free(request);
        ESP_LOGE(TAG, "URL was not selected by a completed release check");
        return ESP_ERR_INVALID_STATE;
    }
    if (ota_any_operation_running_locked()) {
        ESP_LOGW(TAG, "OTA update already in progress");
        xSemaphoreGive(ota_mutex);
        free(request);
        return ESP_FAIL;
    }
    ota_task_running = true;
    xSemaphoreGive(ota_mutex);

    BaseType_t ret = xTaskCreate(
        ota_task,
        "ota_task",
        OTA_STACK_SIZE,
        request,
        OTA_TASK_PRIORITY,
        NULL
    );

    if (ret != pdPASS) {
        free(request);
        xSemaphoreTake(ota_mutex, portMAX_DELAY);
        ota_task_running = false;
        xSemaphoreGive(ota_mutex);
        return ESP_FAIL;
    }

    return ESP_OK;
}

void ota_update_get_info(ota_info_t *info)
{
    if (!info) return;

    ota_init_mutex();
    xSemaphoreTake(ota_mutex, portMAX_DELAY);
    memcpy(info, &ota_current_info, sizeof(ota_info_t));
    const char *ver = ota_get_current_version();
    if (ver && info->current_version[0] == '\0') {
        strlcpy(info->current_version, ver, sizeof(info->current_version));
    }
    xSemaphoreGive(ota_mutex);
}

bool ota_update_is_running(void)
{
    ota_init_mutex();
    xSemaphoreTake(ota_mutex, portMAX_DELAY);
    bool running = ota_any_operation_running_locked();
    xSemaphoreGive(ota_mutex);
    return running;
}

const char* ota_get_current_version(void)
{
    const esp_app_desc_t *app_desc = esp_app_get_description();
    return app_desc->version;
}
