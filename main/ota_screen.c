/**
 * @file ota_screen.c
 *
 * Minimal OTA screen implementation - reduces LVGL updates during flash writes
 */

#include "ota_screen.h"
#include "custom_fonts.h"
#include "display_control.h"
#include "esp_log.h"
#include <stdio.h>

static const char *TAG = "ota_screen";

static lv_obj_t *ota_screen = NULL;
static lv_obj_t *status_label = NULL;
static lv_obj_t *progress_label = NULL;
static lv_obj_t *progress_bar = NULL;
static lv_obj_t *return_screen = NULL;
static int last_reported_progress = -1;
static bool overlay_pushed = false;

void ota_screen_show(void)
{
    ESP_LOGI(TAG, "Creating OTA update screen");

    lv_obj_t *active = lv_scr_act();
    if (active && active != ota_screen) return_screen = active;

    if (ota_screen) {
        if (lv_scr_act() == ota_screen && return_screen) lv_scr_load(return_screen);
        lv_obj_del(ota_screen);
        ota_screen = NULL;
    }
    if (!overlay_pushed) {
        display_control_push_overlay();
        overlay_pushed = true;
    }

    // Create new screen
    ota_screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(ota_screen, lv_color_hex(0x000000), 0);

    // Title
    lv_obj_t *title = lv_label_create(ota_screen);
    lv_label_set_text(title, "Firmware Update");
    lv_obj_set_style_text_color(title, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_32, 0);
    lv_obj_align(title, LV_ALIGN_CENTER, 0, -80);

    // Status label
    status_label = lv_label_create(ota_screen);
    lv_label_set_text(status_label, "Downloading...");
    lv_obj_set_style_text_color(status_label, lv_color_hex(0xCCCCCC), 0);
    lv_obj_set_style_text_font(status_label, &lv_font_montserrat_24, 0);
    lv_obj_align(status_label, LV_ALIGN_CENTER, 0, -42);

    // Progress label
    progress_label = lv_label_create(ota_screen);
    lv_label_set_text(progress_label, "0%");
    lv_obj_set_style_text_color(progress_label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(progress_label, &lv_font_montserrat_48, 0);
    lv_obj_align(progress_label, LV_ALIGN_CENTER, 0, 14);

    progress_bar = lv_bar_create(ota_screen);
    lv_obj_set_size(progress_bar, 480, 18);
    lv_obj_align(progress_bar, LV_ALIGN_CENTER, 0, 72);
    lv_bar_set_range(progress_bar, 0, 100);
    lv_bar_set_value(progress_bar, 0, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(progress_bar, lv_color_hex(0x252B35), LV_PART_MAIN);
    lv_obj_set_style_bg_color(progress_bar, lv_color_hex(0xF7931A), LV_PART_INDICATOR);
    lv_obj_set_style_radius(progress_bar, 9, LV_PART_MAIN);
    lv_obj_set_style_radius(progress_bar, 9, LV_PART_INDICATOR);

    /* Flash writes can briefly stall the PSRAM-backed panel. The updater keeps
     * the backlight on and pauses LVGL between writes, so describe the visible
     * pause/flicker rather than incorrectly promising a dark screen. */
    lv_obj_t *warning = lv_label_create(ota_screen);
    lv_label_set_text(warning, "The screen may pause or flicker while flash is written.");
    lv_obj_set_style_text_color(warning, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(warning, &lv_font_montserrat_20, 0);
    lv_obj_align(warning, LV_ALIGN_BOTTOM_MID, 0, -66);

    lv_obj_t *warning2 = lv_label_create(ota_screen);
    lv_label_set_text(warning2, "Keep power connected. It will restart automatically.");
    lv_obj_set_style_text_color(warning2, lv_color_hex(0xFF6B6B), 0);
    lv_obj_set_style_text_font(warning2, &lv_font_montserrat_20, 0);
    lv_obj_align(warning2, LV_ALIGN_BOTTOM_MID, 0, -36);

    // Load the screen
    lv_scr_load(ota_screen);

    last_reported_progress = -1;
    ESP_LOGI(TAG, "OTA screen loaded and displayed");
}

void ota_screen_update_progress(int progress)
{
    if (!ota_screen || !progress_label || !status_label || !progress_bar) {
        ESP_LOGE(TAG, "ota_screen_update_progress called but widgets are NULL");
        return;
    }

    if (progress < 0) progress = 0;
    if (progress > 100) progress = 100;
    if (progress == last_reported_progress) return;

    char percent[12];
    snprintf(percent, sizeof(percent), "%d%%", progress);
    lv_label_set_text(progress_label, percent);
    lv_bar_set_value(progress_bar, progress, LV_ANIM_OFF);

    if (progress >= 100) {
        lv_label_set_text(status_label, "Complete! Rebooting...");
        ESP_LOGI(TAG, "OTA complete, rebooting...");
    } else if (progress >= 1) {
        lv_label_set_text(status_label, "Installing firmware...");
        ESP_LOGI(TAG, "OTA progress: %d%%", progress);
    } else {
        // Initial state
        lv_label_set_text(status_label, "Starting update...");
        lv_label_set_text(progress_label, "0%");
        ESP_LOGI(TAG, "OTA starting...");
    }

    last_reported_progress = progress;
}

void ota_screen_show_error(const char *error)
{
    if (!ota_screen || !status_label) {
        return;
    }

    ESP_LOGE(TAG, "OTA Error: %s", error);

    lv_label_set_text(status_label, "Update Failed!");
    lv_obj_set_style_text_color(status_label, lv_color_hex(0xFF0000), 0);

    if (progress_label) {
        lv_label_set_text(progress_label, error);
        lv_obj_set_style_text_font(progress_label, &lv_font_montserrat_18, 0);
    }
    if (progress_bar) {
        lv_obj_set_style_bg_color(progress_bar, lv_color_hex(0xFF4040), LV_PART_INDICATOR);
    }
}

void ota_screen_hide(void)
{
    if (ota_screen) {
        /* Never delete LVGL's active root: on a failed OTA the Settings screen
         * is still alive underneath and should remain usable for retry/help. */
        if (lv_scr_act() == ota_screen && return_screen) lv_scr_load(return_screen);
        lv_obj_del(ota_screen);
        ota_screen = NULL;
        status_label = NULL;
        progress_label = NULL;
        progress_bar = NULL;
        last_reported_progress = -1;
    }
    return_screen = NULL;
    /* The screen can be deleted by a parent/navigation path before hide is
     * called; the overlay-depth reservation must still always be balanced. */
    if (overlay_pushed) {
        display_control_pop_overlay();
        overlay_pushed = false;
    }
}
