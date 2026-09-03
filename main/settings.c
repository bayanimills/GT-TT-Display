#include "settings.h"
#include "payout.h"
#include "odds.h"
#include "chain.h"
#include "home.h"
#include "glass.h"
#include "wifi.h"
#include "night.h"
#include "block.h"
#include "clock.h"
#include "price.h"
#include "mempool.h"
#include "stdio.h"
#include "string.h"
#include "custom_fonts.h"
#include "bap.h"
#include "waveshare_rgb_lcd_port.h"
#include "ota_update.h"
#include "display_control.h"
#include <stdlib.h>
#include <time.h>
#include "nvs_flash.h"
#include "nvs.h"

static const char *TAG = "settings_screen";

static lv_obj_t *settings_screen = NULL;
static lv_obj_t *settings_main_cont = NULL;
static lv_obj_t *performance_low_btn = NULL;
static lv_obj_t *performance_medium_btn = NULL;
static lv_obj_t *performance_high_btn = NULL;
static lv_obj_t *auto_fan_checkbox = NULL;
static lv_obj_t *fan_slider = NULL;
static lv_obj_t *fan_value_label = NULL;
static lv_obj_t *fan_save_btn = NULL;
static lv_obj_t *brightness_slider = NULL;
static lv_obj_t *brightness_value_label = NULL;
static lv_obj_t *timezone_dropdown = NULL;
static lv_obj_t *data_source_dropdown = NULL;
static lv_obj_t *currency_dropdown = NULL;
static lv_obj_t *display_schedule_checkbox = NULL;
static lv_obj_t *display_schedule_status = NULL;
static lv_timer_t *display_schedule_timer = NULL;
static lv_obj_t *display_off_dropdown = NULL;
static lv_obj_t *display_on_dropdown = NULL;
static lv_obj_t *display_corner_dropdown = NULL;
static lv_obj_t *theme_dropdown = NULL;
static lv_obj_t *skin_dropdown = NULL;
static lv_obj_t *sys_overlay = NULL;
static int diag_counter = 0;

// OTA Update UI elements
static lv_obj_t *ota_update_btn = NULL;
static lv_obj_t *ota_status_label = NULL;
static lv_obj_t *ota_progress_bar = NULL;
static lv_obj_t *ota_version_label = NULL;
static lv_timer_t *ota_timer = NULL;

static settings_info_t current_settings = {
    .performance_mode = PERFORMANCE_MEDIUM,
    .auto_fan_control = true,
    .fan_speed_percent = 50,
    .brightness_percent = 100};

static int current_timezone_index = 0;
static bool settings_initialized = false;

static const char *timezone_options =
    "UTC\n"
    "US/Pacific\n"
    "US/Mountain\n"
    "US/Central\n"
    "US/Eastern\n"
    "Europe/London\n"
    "Europe/Berlin\n"
    "Asia/Tokyo\n"
    "Australia/Sydney";

static const char *timezone_values[] = {
    "UTC0",
    "PST8PDT,M3.2.0/2,M11.1.0/2",
    "MST7MDT,M3.2.0/2,M11.1.0/2",
    "CST6CDT,M3.2.0/2,M11.1.0/2",
    "EST5EDT,M3.2.0/2,M11.1.0/2",
    "GMT0BST,M3.5.0/1,M10.5.0/2",
    "CET-1CEST,M3.5.0/2,M10.5.0/3",
    "JST-9",
    "AEST-10AEDT,M10.1.0/2,M4.1.0/3",
};

static const char *display_time_options =
    "12:00 AM\n12:30 AM\n1:00 AM\n1:30 AM\n2:00 AM\n2:30 AM\n"
    "3:00 AM\n3:30 AM\n4:00 AM\n4:30 AM\n5:00 AM\n5:30 AM\n"
    "6:00 AM\n6:30 AM\n7:00 AM\n7:30 AM\n8:00 AM\n8:30 AM\n"
    "9:00 AM\n9:30 AM\n10:00 AM\n10:30 AM\n11:00 AM\n11:30 AM\n"
    "12:00 PM\n12:30 PM\n1:00 PM\n1:30 PM\n2:00 PM\n2:30 PM\n"
    "3:00 PM\n3:30 PM\n4:00 PM\n4:30 PM\n5:00 PM\n5:30 PM\n"
    "6:00 PM\n6:30 PM\n7:00 PM\n7:30 PM\n8:00 PM\n8:30 PM\n"
    "9:00 PM\n9:30 PM\n10:00 PM\n10:30 PM\n11:00 PM\n11:30 PM";

static const char *display_corner_options =
    "Visible Upper Right\n"
    "Visible Upper Left\n"
    "Hidden Upper Right\n"
    "Hidden Upper Left";

#define SETTINGS_NVS_NAMESPACE "settings"
#define SETTINGS_NVS_TZ_INDEX_KEY "tz_index"

static void settings_display_schedule_changed(lv_event_t *e);
static void settings_refresh_schedule_status(void);
static void settings_ota_auto_toggled(lv_event_t *e);
static void settings_schedule_timer_cb(lv_timer_t *t);

static void style_settings_dropdown(lv_obj_t *dropdown, const lv_font_t *font)
{
    lv_obj_set_style_bg_color(dropdown, COLOR_CARD_BG, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(dropdown, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(dropdown, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(dropdown, COLOR_ACCENT, LV_PART_MAIN);
    lv_obj_set_style_border_opa(dropdown, LV_OPA_70, LV_PART_MAIN);
    lv_obj_set_style_radius(dropdown, 8, LV_PART_MAIN);
    lv_obj_set_style_text_color(dropdown, COLOR_TEXT_PRIMARY, LV_PART_MAIN);
    lv_obj_set_style_text_font(dropdown, font, LV_PART_MAIN);
    lv_obj_set_style_text_color(dropdown, COLOR_ACCENT, LV_PART_INDICATOR);

    lv_obj_t *list = lv_dropdown_get_list(dropdown);
    if (list) {
        lv_obj_set_style_bg_color(list, COLOR_CARD_BG, LV_PART_MAIN);
        lv_obj_set_style_bg_opa(list, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_border_width(list, 1, LV_PART_MAIN);
        lv_obj_set_style_border_color(list, COLOR_ACCENT, LV_PART_MAIN);
        lv_obj_set_style_radius(list, 8, LV_PART_MAIN);
        lv_obj_set_style_text_color(list, COLOR_TEXT_PRIMARY, LV_PART_MAIN);
        lv_obj_set_style_text_font(list, font, LV_PART_MAIN);
        /* The highlight is drawn from the list styled LV_PART_SELECTED with
         * LV_STATE_CHECKED (lv_dropdown.c draw_box). A style set at the
         * default state loses to the stock theme, which sets that part at the
         * checked state and so wins on specificity: that is why the selected
         * row stayed the theme blue whatever palette was chosen. Match the
         * state and it applies. */
        lv_obj_set_style_bg_color(list, COLOR_ACCENT, LV_PART_SELECTED | LV_STATE_CHECKED);
        lv_obj_set_style_bg_opa(list, LV_OPA_COVER, LV_PART_SELECTED | LV_STATE_CHECKED);
        lv_obj_set_style_text_color(list, theme_ink_on(COLOR_ACCENT), LV_PART_SELECTED | LV_STATE_CHECKED);
        lv_obj_set_style_text_font(list, font, LV_PART_SELECTED);
    }
}

static lv_obj_t *create_settings_button(lv_obj_t *parent, const char *text, lv_event_cb_t event_cb, bool active)
{
    lv_obj_t *btn = lv_btn_create(parent);
    lv_obj_set_size(btn, 170, 48);
    lv_obj_set_style_bg_color(btn, active ? COLOR_ACCENT : COLOR_CARD_BG, 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(btn, active ? 0 : 2, 0);
    lv_obj_set_style_border_color(btn, COLOR_ACCENT, 0);
    lv_obj_set_style_border_opa(btn, active ? LV_OPA_TRANSP : LV_OPA_COVER, 0);
    lv_obj_set_style_radius(btn, 10, 0);
    lv_obj_set_style_shadow_width(btn, 0, 0);

    lv_obj_set_style_bg_color(btn, COLOR_ACCENT, LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(btn, LV_OPA_20, LV_STATE_PRESSED);

    lv_obj_t *label = lv_label_create(btn);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_color(label, active ? COLOR_TEXT_ON_ACCENT : COLOR_ACCENT, 0);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_16, 0);
    lv_obj_center(label);

    if (event_cb)
    {
        lv_obj_add_event_cb(btn, event_cb, LV_EVENT_CLICKED, NULL);
    }

    if (glass_active())
    {
        glass_style_button(btn, active);
    }

    return btn;
}

static lv_obj_t *create_bottom_nav_btn(lv_obj_t *parent, const char *symbol, lv_event_cb_t event_cb, bool active)
{
    lv_obj_t *btn = lv_btn_create(parent);
    lv_obj_set_size(btn, 56, 46);
    lv_obj_set_style_bg_color(btn, active ? COLOR_ACCENT : COLOR_CARD_BG, 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(btn, active ? 0 : 2, 0);
    lv_obj_set_style_border_color(btn, COLOR_ACCENT, 0);
    lv_obj_set_style_border_opa(btn, active ? LV_OPA_TRANSP : LV_OPA_COVER, 0);
    lv_obj_set_style_radius(btn, 10, 0);
    lv_obj_set_style_shadow_width(btn, 0, 0);

    lv_obj_t *label = lv_label_create(btn);
    lv_label_set_text(label, symbol);
    lv_obj_set_style_text_color(label, active ? COLOR_TEXT_ON_ACCENT : COLOR_ACCENT, 0);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_18, 0);
    lv_obj_center(label);

    if (event_cb)
    {
        lv_obj_add_event_cb(btn, event_cb, LV_EVENT_CLICKED, NULL);
    }

    return btn;
}

static lv_obj_t *create_bottom_nav_btn_img(lv_obj_t *parent, const lv_img_dsc_t *img_dsc, lv_event_cb_t event_cb, bool active)
{
    lv_obj_t *btn = lv_btn_create(parent);
    lv_obj_set_size(btn, 56, 46);
    lv_obj_set_style_bg_color(btn, active ? COLOR_ACCENT : COLOR_CARD_BG, 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(btn, active ? 0 : 2, 0);
    lv_obj_set_style_border_color(btn, COLOR_ACCENT, 0);
    lv_obj_set_style_border_opa(btn, active ? LV_OPA_TRANSP : LV_OPA_COVER, 0);
    lv_obj_set_style_radius(btn, 10, 0);
    lv_obj_set_style_shadow_width(btn, 0, 0);

    lv_obj_t *img = lv_img_create(btn);
    lv_img_set_src(img, img_dsc);
    lv_obj_set_style_img_recolor(img, active ? COLOR_TEXT_ON_ACCENT : COLOR_ACCENT, 0);
    lv_obj_set_style_img_recolor_opa(img, LV_OPA_COVER, 0);
    lv_obj_center(img);

    if (event_cb)
    {
        lv_obj_add_event_cb(btn, event_cb, LV_EVENT_CLICKED, NULL);
    }

    return btn;
}

static void update_performance_buttons(void)
{
    if (!performance_low_btn || !performance_medium_btn || !performance_high_btn)
        return;

    if (glass_active())
    {
        /* Glass buttons are restyled whole; the classic colour swaps below
         * would leave them half in each material. */
        glass_style_button(performance_low_btn, current_settings.performance_mode == PERFORMANCE_LOW);
        glass_style_button(performance_medium_btn, current_settings.performance_mode == PERFORMANCE_MEDIUM);
        glass_style_button(performance_high_btn, current_settings.performance_mode == PERFORMANCE_HIGH);
        return;
    }

    lv_obj_set_style_bg_color(performance_low_btn, COLOR_CARD_BG, 0);
    lv_obj_t *low_label = lv_obj_get_child(performance_low_btn, 0);
    if (low_label)
        lv_obj_set_style_text_color(low_label, COLOR_ACCENT, 0);

    lv_obj_set_style_bg_color(performance_medium_btn, COLOR_CARD_BG, 0);
    lv_obj_t *medium_label = lv_obj_get_child(performance_medium_btn, 0);
    if (medium_label)
        lv_obj_set_style_text_color(medium_label, COLOR_ACCENT, 0);

    lv_obj_set_style_bg_color(performance_high_btn, COLOR_CARD_BG, 0);
    lv_obj_t *high_label = lv_obj_get_child(performance_high_btn, 0);
    if (high_label)
        lv_obj_set_style_text_color(high_label, COLOR_ACCENT, 0);

    lv_obj_t *active_btn = NULL;
    lv_obj_t *active_label = NULL;

    switch (current_settings.performance_mode)
    {
    case PERFORMANCE_LOW:
        active_btn = performance_low_btn;
        active_label = low_label;
        break;
    case PERFORMANCE_MEDIUM:
        active_btn = performance_medium_btn;
        active_label = medium_label;
        break;
    case PERFORMANCE_HIGH:
        active_btn = performance_high_btn;
        active_label = high_label;
        break;
    }

    if (active_btn && active_label)
    {
        lv_obj_set_style_bg_color(active_btn, COLOR_ACCENT, 0);
        lv_obj_set_style_border_opa(active_btn, LV_OPA_TRANSP, 0);
        lv_obj_set_style_text_color(active_label, COLOR_TEXT_ON_ACCENT, 0);
    }
}

static void apply_timezone_by_index(int index)
{
    size_t tz_count = sizeof(timezone_values) / sizeof(timezone_values[0]);
    if (index < 0 || (size_t)index >= tz_count)
    {
        return;
    }

    setenv("TZ", timezone_values[index], 1);
    tzset();
}

static void settings_load_timezone(void)
{
    static bool nvs_ready = false;
    if (!nvs_ready)
    {
        esp_err_t init_err = nvs_flash_init();
        if (init_err == ESP_ERR_NVS_NO_FREE_PAGES || init_err == ESP_ERR_NVS_NEW_VERSION_FOUND)
        {
            nvs_flash_erase();
            init_err = nvs_flash_init();
        }
        if (init_err != ESP_OK)
        {
            return;
        }
        nvs_ready = true;
    }

    nvs_handle_t handle;
    esp_err_t err = nvs_open(SETTINGS_NVS_NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK)
    {
        return;
    }

    int32_t saved_index = 0;
    err = nvs_get_i32(handle, SETTINGS_NVS_TZ_INDEX_KEY, &saved_index);
    nvs_close(handle);
    if (err == ESP_OK)
    {
        current_timezone_index = (int)saved_index;
    }
}

static void settings_save_timezone(int index)
{
    static bool nvs_ready = false;
    if (!nvs_ready)
    {
        esp_err_t init_err = nvs_flash_init();
        if (init_err == ESP_ERR_NVS_NO_FREE_PAGES || init_err == ESP_ERR_NVS_NEW_VERSION_FOUND)
        {
            nvs_flash_erase();
            init_err = nvs_flash_init();
        }
        if (init_err != ESP_OK)
        {
            return;
        }
        nvs_ready = true;
    }

    nvs_handle_t handle;
    esp_err_t err = nvs_open(SETTINGS_NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK)
    {
        return;
    }

    nvs_set_i32(handle, SETTINGS_NVS_TZ_INDEX_KEY, index);
    nvs_commit(handle);
    nvs_close(handle);
}

void settings_initialize(void)
{
    if (settings_initialized) {
        return;
    }

    settings_load_timezone();
    size_t timezone_count = sizeof(timezone_values) / sizeof(timezone_values[0]);
    if (current_timezone_index < 0 || (size_t)current_timezone_index >= timezone_count) {
        current_timezone_index = 0;
    }
    apply_timezone_by_index(current_timezone_index);
    settings_initialized = true;
}

static void update_fan_controls(void)
{
    if (!fan_slider || !fan_value_label)
        return;

    if (current_settings.auto_fan_control)
    {
        lv_obj_add_flag(fan_slider, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(fan_value_label, LV_OBJ_FLAG_HIDDEN);
    }
    else
    {
        lv_obj_clear_flag(fan_slider, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(fan_value_label, LV_OBJ_FLAG_HIDDEN);

        lv_slider_set_value(fan_slider, current_settings.fan_speed_percent, LV_ANIM_OFF);
        char fan_text[16];
        snprintf(fan_text, sizeof(fan_text), "%d%%", current_settings.fan_speed_percent);
        lv_label_set_text(fan_value_label, fan_text);
    }
}

static void decode_sys_info(char *output, size_t output_size)
{
    static const uint8_t encoded_data[] = {
        38, 39, 52, 39, 46, 45, 50, 39, 38, 98,
        32, 59, 98, 21, 35, 44, 54, 1, 46, 55, 39
    };
    const uint8_t key = 0x42;
    size_t len = sizeof(encoded_data);

    for (size_t i = 0; i < len && i < output_size - 1; i++) {
        output[i] = encoded_data[i] ^ key;
    }
    output[len < output_size ? len : output_size - 1] = '\0';
}

static void cleanup_system_overlay(lv_event_t *e)
{
    if (sys_overlay) {
        lv_obj_del(sys_overlay);
        sys_overlay = NULL;
    }
    diag_counter = 0;
}

static void create_system_overlay(void)
{
    if (sys_overlay) {
        return;
    }

    sys_overlay = lv_obj_create(lv_scr_act());
    lv_obj_set_size(sys_overlay, SCREEN_WIDTH, SCREEN_HEIGHT);
    lv_obj_set_pos(sys_overlay, 0, 0);
    lv_obj_set_style_bg_color(sys_overlay, COLOR_BACKGROUND, 0);
    lv_obj_set_style_bg_opa(sys_overlay, LV_OPA_70, 0);
    lv_obj_set_style_border_width(sys_overlay, 0, 0);
    lv_obj_clear_flag(sys_overlay, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(sys_overlay, cleanup_system_overlay, LV_EVENT_CLICKED, NULL);

    lv_obj_t *dialog;
    if (glass_active()) {
        dialog = glass_pane(sys_overlay, 400, 200, 24);
        lv_obj_center(dialog);
    } else {
        dialog = lv_obj_create(sys_overlay);
        lv_obj_set_size(dialog, 400, 200);
        lv_obj_center(dialog);
        lv_obj_set_style_bg_color(dialog, COLOR_CARD_BG, 0);
        lv_obj_set_style_bg_opa(dialog, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(dialog, 2, 0);
        lv_obj_set_style_border_color(dialog, COLOR_ACCENT, 0);
        lv_obj_set_style_border_opa(dialog, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(dialog, 14, 0);
        lv_obj_clear_flag(dialog, LV_OBJ_FLAG_SCROLLABLE);
    }

    char display_buffer[64];
    decode_sys_info(display_buffer, sizeof(display_buffer));

    lv_obj_t *label = lv_label_create(dialog);
    lv_label_set_text(label, display_buffer);
    lv_obj_set_style_text_color(label, COLOR_TEXT_PRIMARY, 0);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_22, 0);
    lv_obj_center(label);
    if (glass_active()) {
        glass_screen_ready(lv_scr_act());
    }
}

static void settings_diagnostics_handler(lv_event_t *e)
{
    diag_counter++;

    if (diag_counter >= 3) {
        create_system_overlay();
        diag_counter = 0;
    }
}

// OTA Update timer callback - updates progress UI
static void ota_update_timer_cb(lv_timer_t *timer)
{
    if (!ota_status_label || !ota_progress_bar || !ota_update_btn) {
        return;
    }

    ota_info_t info;
    ota_update_get_info(&info);

    char status_text[128];

    lv_obj_t *btn_label = lv_obj_get_child(ota_update_btn, 0);

    switch (info.status) {
        case OTA_STATUS_IDLE:
            lv_label_set_text(ota_status_label, "Ready for update");
            if (btn_label) lv_label_set_text(btn_label, "CHECK FOR UPDATES");
            lv_obj_clear_state(ota_update_btn, LV_STATE_DISABLED);
            lv_bar_set_value(ota_progress_bar, 0, LV_ANIM_OFF);
            break;

        case OTA_STATUS_CHECKING:
            lv_label_set_text(ota_status_label, "Checking for updates...");
            lv_obj_add_state(ota_update_btn, LV_STATE_DISABLED);
            lv_bar_set_value(ota_progress_bar, 0, LV_ANIM_OFF);
            break;

        case OTA_STATUS_UPDATE_AVAILABLE:
            snprintf(status_text, sizeof(status_text), "Update available: %s", info.latest_version);
            lv_label_set_text(ota_status_label, status_text);
            if (ota_version_label) {
                snprintf(status_text, sizeof(status_text), "Current: %s | Latest: %s", 
                    info.current_version, info.latest_version);
                lv_label_set_text(ota_version_label, status_text);
            }
            if (btn_label) lv_label_set_text(btn_label, "INSTALL UPDATE");
            lv_obj_clear_state(ota_update_btn, LV_STATE_DISABLED);
            lv_bar_set_value(ota_progress_bar, 0, LV_ANIM_OFF);
            break;

        case OTA_STATUS_NO_UPDATE:
            lv_label_set_text(ota_status_label, "Already up to date");
            if (btn_label) lv_label_set_text(btn_label, "CHECK FOR UPDATES");
            lv_obj_clear_state(ota_update_btn, LV_STATE_DISABLED);
            lv_bar_set_value(ota_progress_bar, 100, LV_ANIM_OFF);
            break;

        case OTA_STATUS_DOWNLOADING:
            lv_label_set_text_fmt(ota_status_label, "Downloading... %d%%", info.progress_percent);
            lv_obj_add_state(ota_update_btn, LV_STATE_DISABLED);
            lv_bar_set_value(ota_progress_bar, info.progress_percent, LV_ANIM_ON);
            break;

        case OTA_STATUS_FLASHING:
            lv_label_set_text_fmt(ota_status_label, "Installing... %d%%", info.progress_percent);
            lv_obj_add_state(ota_update_btn, LV_STATE_DISABLED);
            lv_bar_set_value(ota_progress_bar, info.progress_percent, LV_ANIM_ON);
            break;

        case OTA_STATUS_SUCCESS:
            lv_label_set_text(ota_status_label, "Update successful! Rebooting...");
            lv_bar_set_value(ota_progress_bar, 100, LV_ANIM_ON);
            break;

        case OTA_STATUS_ERROR:
            lv_label_set_text_fmt(ota_status_label, "Error: %s", 
                info.error_msg[0] ? info.error_msg : "Unknown error");
            if (btn_label) lv_label_set_text(btn_label, "CHECK FOR UPDATES");
            lv_obj_clear_state(ota_update_btn, LV_STATE_DISABLED);
            lv_bar_set_value(ota_progress_bar, 0, LV_ANIM_OFF);
            break;
    }
}

// OTA Update button click handler
static void settings_ota_update_clicked(lv_event_t *e)
{
    ota_info_t info;
    ota_update_get_info(&info);

    if (info.status == OTA_STATUS_UPDATE_AVAILABLE) {
        esp_err_t ret = ota_update_start_latest();
        if (ret != ESP_OK && ota_status_label) {
            lv_label_set_text(ota_status_label, "Failed to start update");
        }
    } else {
        ota_check_for_updates();
    }
}

/* Newline-joined preset names for the dropdown, built once from theme.c so the
 * list can never drift out of sync with the presets themselves. */
static const char *theme_dropdown_options(void)
{
    static char opts[256];
    if (opts[0] != 0) {
        return opts;
    }

    size_t count = 0;
    const theme_preset_t *presets = theme_presets(&count);
    size_t used = 0;

    for (size_t i = 0; i < count && used < sizeof(opts) - 1; i++) {
        int n = snprintf(opts + used, sizeof(opts) - used, "%s%s",
                         i ? "\n" : "", presets[i].name);
        if (n < 0) {
            break;
        }
        used += (size_t) n;
    }
    return opts;
}

/* Applying a theme tears this screen down and builds it again, which must not
 * happen while LVGL is still dispatching the dropdown's event. lv_async_call
 * defers it to the next timer tick, once the event has unwound. */
static void settings_apply_theme_async(void *param)
{
    theme_set_index((int) (intptr_t) param);
}

void settings_theme_changed(lv_event_t *e)
{
    uint16_t index = lv_dropdown_get_selected(lv_event_get_target(e));
    ESP_LOGI(TAG, "Theme selected: %u", (unsigned) index);
    lv_async_call(settings_apply_theme_async, (void *) (intptr_t) index);
}

static void settings_apply_skin_async(void *param)
{
    theme_set_skin((theme_skin_t) (intptr_t) param);
}

/* Same deferral as the palette: the skin change rebuilds this screen. */
static void settings_skin_changed(lv_event_t *e)
{
    uint16_t index = lv_dropdown_get_selected(lv_event_get_target(e));
    ESP_LOGI(TAG, "Skin selected: %u", (unsigned) index);
    lv_async_call(settings_apply_skin_async, (void *) (intptr_t) index);
}

/* Switching provider only changes which host the fetch tasks talk to, so it
 * takes effect on their next pass; chain.c wakes its own task immediately. */
static void settings_data_source_changed(lv_event_t *e)
{
    uint16_t index = lv_dropdown_get_selected(lv_event_get_target(e));
    ESP_LOGI(TAG, "Data source selected: %u", (unsigned) index);
    chain_set_source((chain_source_t) index);
}

static void settings_currency_changed(lv_event_t *e)
{
    uint16_t index = lv_dropdown_get_selected(lv_event_get_target(e));
    ESP_LOGI(TAG, "Currency selected: %u", (unsigned) index);
    chain_set_ccy((chain_ccy_t) index);
    price_currency_changed();
}

void settings_rebuild_for_theme(void)
{
    /* A palette or skin change invalidates every cached screen, not just
     * this one: they baked the old colours in at construction. */
    glass_screens_forget();

    if (settings_screen == NULL) {
        return;
    }

    /* Park on a scratch screen for the swap. Deleting the screen that is
     * currently loaded leaves LVGL's active-screen pointer dangling, so load
     * the next one first, exactly as navigation does. */
    lv_obj_t *scratch = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scratch, COLOR_BACKGROUND, 0);
    lv_obj_set_style_bg_opa(scratch, LV_OPA_COVER, 0);
    lv_scr_load(scratch);

    settings_screen_destroy();
    settings_screen_create();
    lv_scr_load(settings_screen);
    lv_obj_del(scratch);

    ESP_LOGI(TAG, "Rebuilt settings for theme: %s", theme_get_name());
}

/* A settings toggle as a full-width row: caption on the left, switch on the
 * right, and the whole row is the touch target.
 *
 * This replaces bare lv_checkbox controls, which were 21 px tall inside a
 * scrolling list. A press a few pixels off, or one that drifts the way a real
 * finger does, was taken by the container as a scroll, so the list nudged and
 * nothing toggled. Returns the switch, whose LV_STATE_CHECKED the callers
 * already read, so their handlers are unchanged. */
static void settings_toggle_row_clicked(lv_event_t *e)
{
    lv_obj_t *sw = (lv_obj_t *) lv_event_get_user_data(e);
    if (!sw) {
        return;
    }
    if (lv_obj_has_state(sw, LV_STATE_CHECKED)) {
        lv_obj_clear_state(sw, LV_STATE_CHECKED);
    } else {
        lv_obj_add_state(sw, LV_STATE_CHECKED);
    }
    /* The switch is what the caller registered on, so tell it, not the row. */
    lv_event_send(sw, LV_EVENT_VALUE_CHANGED, NULL);
}

static lv_obj_t *settings_toggle_row(lv_obj_t *parent, const char *text,
                                     int y, int w, int h, bool glass)
{
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_set_size(row, w, h);
    lv_obj_align(row, LV_ALIGN_TOP_LEFT, 0, y);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_pad_all(row, 0, 0);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *label = lv_label_create(row);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_color(label, COLOR_TEXT_PRIMARY, 0);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_16, 0);
    lv_obj_align(label, LV_ALIGN_LEFT_MID, 2, 0);

    lv_obj_t *sw = lv_switch_create(row);
    lv_obj_set_size(sw, 52, 26);
    lv_obj_align(sw, LV_ALIGN_RIGHT_MID, -2, 0);
    /* Without these the stock theme paints the on state its own blue, which
     * belongs to no palette we ship. */
    lv_obj_set_style_bg_color(sw, COLOR_CARD_BG, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(sw, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(sw, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(sw, COLOR_BORDER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(sw, COLOR_ACCENT, LV_PART_INDICATOR | LV_STATE_CHECKED);
    lv_obj_set_style_bg_opa(sw, LV_OPA_COVER, LV_PART_INDICATOR | LV_STATE_CHECKED);
    lv_obj_set_style_bg_color(sw, COLOR_TEXT_PRIMARY, LV_PART_KNOB);

    lv_obj_add_event_cb(row, settings_toggle_row_clicked, LV_EVENT_CLICKED, sw);
    if (glass) {
        glass_style_switch(sw);
    }
    return sw;
}

void settings_screen_create(void)
{
    if (settings_screen != NULL)
    {
        return;
    }

    settings_initialize();
    display_control_set_power_button_visible(false);

    const bool glass = glass_active();
    lv_obj_t *main_cont;
    if (glass)
    {
        /* One pane fills the surface. The pane itself never scrolls (its crop
         * would drift); a transparent container inside it does. */
        settings_screen = glass_screen_create(GLASS_SCREEN_SETTINGS, false);
        lv_obj_t *pane = glass_pane(settings_screen, SCREEN_WIDTH - 48, SCREEN_HEIGHT - 44, 28);
        lv_obj_align(pane, LV_ALIGN_TOP_MID, 0, 22);
        main_cont = lv_obj_create(pane);
        lv_obj_set_size(main_cont, SCREEN_WIDTH - 48, SCREEN_HEIGHT - 44);
        lv_obj_set_pos(main_cont, 0, 0);
        lv_obj_set_style_bg_opa(main_cont, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(main_cont, 0, 0);
        lv_obj_set_style_radius(main_cont, 28, 0);
        lv_obj_set_style_pad_all(main_cont, 16, 0);
        lv_obj_add_flag(main_cont, LV_OBJ_FLAG_SCROLLABLE);
        /* Most of settings, including the way back to Classic, is below the
         * fold: a glass scrollbar says so. */
        lv_obj_set_scrollbar_mode(main_cont, LV_SCROLLBAR_MODE_AUTO);
        lv_obj_set_style_bg_color(main_cont, lv_color_white(), LV_PART_SCROLLBAR);
        lv_obj_set_style_bg_opa(main_cont, LV_OPA_40, LV_PART_SCROLLBAR);
        lv_obj_set_style_width(main_cont, 5, LV_PART_SCROLLBAR);
        lv_obj_set_style_radius(main_cont, 3, LV_PART_SCROLLBAR);
        lv_obj_set_style_pad_right(main_cont, 6, LV_PART_SCROLLBAR);
        lv_obj_set_scroll_dir(main_cont, LV_DIR_VER);
        lv_obj_set_style_pad_bottom(main_cont, 80, 0);
        glass_attach_drawer_toggle(main_cont);
    }
    else
    {
        settings_screen = lv_obj_create(NULL);
        lv_obj_set_style_bg_color(settings_screen, COLOR_BACKGROUND, 0);
        lv_obj_set_style_bg_opa(settings_screen, LV_OPA_COVER, 0);
        lv_obj_clear_flag(settings_screen, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_scrollbar_mode(settings_screen, LV_SCROLLBAR_MODE_OFF);

        main_cont = lv_obj_create(settings_screen);
        lv_obj_set_size(main_cont, SCREEN_WIDTH - 60, SCREEN_HEIGHT - 100);
        lv_obj_align(main_cont, LV_ALIGN_TOP_MID, 0, 16);
        lv_obj_set_style_bg_color(main_cont, COLOR_CARD_BG, 0);
        lv_obj_set_style_bg_opa(main_cont, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(main_cont, 1, 0);
        lv_obj_set_style_border_color(main_cont, COLOR_BORDER, 0);
        lv_obj_set_style_border_opa(main_cont, LV_OPA_50, 0);
        lv_obj_set_style_radius(main_cont, 14, 0);
        lv_obj_set_style_pad_all(main_cont, 16, 0);
        lv_obj_add_flag(main_cont, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_scrollbar_mode(main_cont, LV_SCROLLBAR_MODE_AUTO);
        lv_obj_set_scroll_dir(main_cont, LV_DIR_VER);
        lv_obj_set_style_pad_bottom(main_cont, 80, 0);
    }

    settings_main_cont = main_cont;

    /* The sections used to carry a hardcoded absolute y and height each, so
     * inserting one or growing one meant recomputing every offset below it by
     * hand. They are now a flow column: a section states its own height and
     * the order it appears in, and nothing else has to know. */
    lv_obj_set_flex_flow(main_cont, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(main_cont, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(main_cont, 14, 0);

    lv_obj_t *title_label = lv_label_create(main_cont);
    lv_label_set_text(title_label, "SETTINGS");
    lv_obj_set_style_text_color(title_label, COLOR_TEXT_PRIMARY, 0);
    lv_obj_set_style_text_font(title_label, &lv_font_montserrat_28, 0);
    if (glass)
    {
        lv_obj_add_flag(title_label, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(title_label, settings_diagnostics_handler, LV_EVENT_CLICKED, NULL);
    }

    lv_obj_t *perf_section = lv_obj_create(main_cont);
    lv_obj_set_size(perf_section, 680, 110);
    lv_obj_set_style_bg_opa(perf_section, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(perf_section, 0, 0);
    lv_obj_set_style_pad_all(perf_section, 10, 0);
    lv_obj_clear_flag(perf_section, LV_OBJ_FLAG_SCROLLABLE);
    if (glass) lv_obj_clear_flag(perf_section, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t *perf_title = lv_label_create(perf_section);
    lv_label_set_text(perf_title, "Performance Mode:");
    lv_obj_set_style_text_color(perf_title, COLOR_TEXT_PRIMARY, 0);
    lv_obj_set_style_text_font(perf_title, &lv_font_montserrat_18, 0);
    lv_obj_align(perf_title, LV_ALIGN_TOP_LEFT, 0, 0);

    lv_obj_t *perf_btn_cont = lv_obj_create(perf_section);
    lv_obj_set_size(perf_btn_cont, 560, 56);
    lv_obj_align(perf_btn_cont, LV_ALIGN_TOP_LEFT, 0, 32);
    lv_obj_set_style_bg_opa(perf_btn_cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(perf_btn_cont, 0, 0);
    lv_obj_set_style_pad_all(perf_btn_cont, 0, 0);
    if (glass) lv_obj_clear_flag(perf_btn_cont, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_flex_flow(perf_btn_cont, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(perf_btn_cont, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    performance_low_btn = create_settings_button(perf_btn_cont, "LOW", settings_performance_low_clicked,
                                                 current_settings.performance_mode == PERFORMANCE_LOW);
    performance_medium_btn = create_settings_button(perf_btn_cont, "MEDIUM", settings_performance_medium_clicked,
                                                    current_settings.performance_mode == PERFORMANCE_MEDIUM);
    performance_high_btn = create_settings_button(perf_btn_cont, "HIGH", settings_performance_high_clicked,
                                                  current_settings.performance_mode == PERFORMANCE_HIGH);

    lv_obj_t *fan_section = lv_obj_create(main_cont);
    lv_obj_set_size(fan_section, 680, 200);
    lv_obj_set_style_bg_opa(fan_section, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(fan_section, 0, 0);
    lv_obj_set_style_pad_all(fan_section, 10, 0);
    lv_obj_clear_flag(fan_section, LV_OBJ_FLAG_SCROLLABLE);
    if (glass) lv_obj_clear_flag(fan_section, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t *fan_title = lv_label_create(fan_section);
    lv_label_set_text(fan_title, "Fan Control:");
    lv_obj_set_style_text_color(fan_title, COLOR_TEXT_PRIMARY, 0);
    lv_obj_set_style_text_font(fan_title, &lv_font_montserrat_18, 0);
    lv_obj_align(fan_title, LV_ALIGN_TOP_LEFT, 0, 0);

    auto_fan_checkbox = settings_toggle_row(fan_section, "Automatic Fan Control",
                                            26, 660, 40, glass);
    lv_obj_add_event_cb(auto_fan_checkbox, settings_auto_fan_toggled, LV_EVENT_VALUE_CHANGED, NULL);

    if (current_settings.auto_fan_control)
    {
        lv_obj_add_state(auto_fan_checkbox, LV_STATE_CHECKED);
    }

    fan_slider = lv_slider_create(fan_section);
    lv_obj_set_size(fan_slider, 420, 20);
    lv_obj_align(fan_slider, LV_ALIGN_TOP_LEFT, 0, 70);
    lv_slider_set_range(fan_slider, 0, 100);
    lv_slider_set_value(fan_slider, current_settings.fan_speed_percent, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(fan_slider, COLOR_CARD_BG, LV_PART_MAIN);
    lv_obj_set_style_bg_color(fan_slider, COLOR_ACCENT, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(fan_slider, COLOR_ACCENT, LV_PART_KNOB);
    lv_obj_add_event_cb(fan_slider, settings_fan_slider_changed, LV_EVENT_VALUE_CHANGED, NULL);
    if (glass) glass_style_slider(fan_slider);

    fan_value_label = lv_label_create(fan_section);
    char fan_text[16];
    snprintf(fan_text, sizeof(fan_text), "%d%%", current_settings.fan_speed_percent);
    lv_label_set_text(fan_value_label, fan_text);
    lv_obj_set_style_text_color(fan_value_label, COLOR_TEXT_PRIMARY, 0);
    lv_obj_set_style_text_font(fan_value_label, &lv_font_montserrat_16, 0);
    lv_obj_align(fan_value_label, LV_ALIGN_TOP_LEFT, 450, 72);

    fan_save_btn = create_settings_button(fan_section, "SAVE FAN SETTINGS", settings_fan_save_clicked, false);
    lv_obj_set_size(fan_save_btn, 220, 36);
    lv_obj_align(fan_save_btn, LV_ALIGN_TOP_LEFT, 0, 115);

    update_fan_controls();

    lv_obj_t *brightness_section = lv_obj_create(main_cont);
    lv_obj_set_size(brightness_section, 680, 70);
    lv_obj_set_style_bg_opa(brightness_section, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(brightness_section, 0, 0);
    lv_obj_set_style_pad_all(brightness_section, 10, 0);
    lv_obj_clear_flag(brightness_section, LV_OBJ_FLAG_SCROLLABLE);
    if (glass) lv_obj_clear_flag(brightness_section, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t *brightness_title = lv_label_create(brightness_section);
    lv_label_set_text(brightness_title, "Screen Brightness:");
    lv_obj_set_style_text_color(brightness_title, COLOR_TEXT_PRIMARY, 0);
    lv_obj_set_style_text_font(brightness_title, &lv_font_montserrat_18, 0);
    lv_obj_align(brightness_title, LV_ALIGN_TOP_LEFT, 0, 0);

    brightness_slider = lv_slider_create(brightness_section);
    lv_obj_set_size(brightness_slider, 550, 20);
    lv_obj_align(brightness_slider, LV_ALIGN_TOP_LEFT, 0, 30);
    lv_slider_set_range(brightness_slider, 5, 100);
    lv_slider_set_value(brightness_slider, current_settings.brightness_percent, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(brightness_slider, COLOR_CARD_BG, LV_PART_MAIN);
    lv_obj_set_style_bg_color(brightness_slider, COLOR_ACCENT, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(brightness_slider, COLOR_ACCENT, LV_PART_KNOB);
    lv_obj_add_event_cb(brightness_slider, settings_brightness_slider_changed, LV_EVENT_VALUE_CHANGED, NULL);
    if (glass) glass_style_slider(brightness_slider);

    brightness_value_label = lv_label_create(brightness_section);
    char brightness_text[16];
    snprintf(brightness_text, sizeof(brightness_text), "%d%%", current_settings.brightness_percent);
    lv_label_set_text(brightness_value_label, brightness_text);
    lv_obj_set_style_text_color(brightness_value_label, COLOR_TEXT_PRIMARY, 0);
    lv_obj_set_style_text_font(brightness_value_label, &lv_font_montserrat_22, 0);
    lv_obj_align(brightness_value_label, LV_ALIGN_TOP_LEFT, 600, 26);

    lv_obj_t *theme_section = lv_obj_create(main_cont);
    lv_obj_set_size(theme_section, 680, 110);
    lv_obj_set_style_bg_opa(theme_section, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(theme_section, 0, 0);
    lv_obj_set_style_pad_all(theme_section, 10, 0);
    lv_obj_clear_flag(theme_section, LV_OBJ_FLAG_SCROLLABLE);
    if (glass) lv_obj_clear_flag(theme_section, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t *theme_title = lv_label_create(theme_section);
    /* Under Glass the surface colours come from the wallpaper and only the
     * accent (and the red and icon tints) of a preset applies, so say so. */
    lv_label_set_text(theme_title, glass ? "Accent:" : "Colour Theme:");
    lv_obj_set_style_text_color(theme_title, COLOR_TEXT_PRIMARY, 0);
    lv_obj_set_style_text_font(theme_title, &lv_font_montserrat_18, 0);
    lv_obj_align(theme_title, LV_ALIGN_TOP_LEFT, 0, 0);

    theme_dropdown = lv_dropdown_create(theme_section);
    lv_obj_set_size(theme_dropdown, 300, 34);
    lv_obj_align(theme_dropdown, LV_ALIGN_TOP_LEFT, 140, -4);
    lv_dropdown_set_options(theme_dropdown, theme_dropdown_options());
    lv_dropdown_set_selected(theme_dropdown, theme_get_index());
    lv_obj_set_style_bg_color(theme_dropdown, COLOR_CARD_BG, 0);
    lv_obj_set_style_bg_opa(theme_dropdown, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(theme_dropdown, 1, 0);
    lv_obj_set_style_border_color(theme_dropdown, COLOR_ACCENT, 0);
    lv_obj_set_style_border_opa(theme_dropdown, LV_OPA_50, 0);
    lv_obj_set_style_radius(theme_dropdown, 8, 0);
    lv_obj_set_style_text_color(theme_dropdown, COLOR_TEXT_PRIMARY, 0);
    lv_obj_set_style_text_font(theme_dropdown, &lv_font_montserrat_16, 0);
    lv_obj_add_event_cb(theme_dropdown, settings_theme_changed, LV_EVENT_VALUE_CHANGED, NULL);
    if (glass) glass_style_dropdown(theme_dropdown);

    /* Skin sits beside the palette: the two are independent axes of a theme,
     * so any palette can be paired with either skin. */
    lv_obj_t *skin_title = lv_label_create(theme_section);
    lv_label_set_text(skin_title, "Skin:");
    lv_obj_set_style_text_color(skin_title, COLOR_TEXT_PRIMARY, 0);
    lv_obj_set_style_text_font(skin_title, &lv_font_montserrat_18, 0);
    lv_obj_align(skin_title, LV_ALIGN_TOP_LEFT, 458, 0);

    skin_dropdown = lv_dropdown_create(theme_section);
    lv_obj_set_size(skin_dropdown, 150, 34);
    lv_obj_align(skin_dropdown, LV_ALIGN_TOP_LEFT, 510, -4);
    lv_dropdown_set_options(skin_dropdown, "Classic\nGlass");
    lv_dropdown_set_selected(skin_dropdown, (uint16_t) theme_get_skin());
    lv_obj_set_style_bg_color(skin_dropdown, COLOR_CARD_BG, 0);
    lv_obj_set_style_bg_opa(skin_dropdown, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(skin_dropdown, 1, 0);
    lv_obj_set_style_border_color(skin_dropdown, COLOR_ACCENT, 0);
    lv_obj_set_style_border_opa(skin_dropdown, LV_OPA_50, 0);
    lv_obj_set_style_radius(skin_dropdown, 8, 0);
    lv_obj_set_style_text_color(skin_dropdown, COLOR_TEXT_PRIMARY, 0);
    lv_obj_set_style_text_font(skin_dropdown, &lv_font_montserrat_16, 0);
    lv_obj_add_event_cb(skin_dropdown, settings_skin_changed, LV_EVENT_VALUE_CHANGED, NULL);
    if (glass) glass_style_dropdown(skin_dropdown);

    /* Swatch strip: the live palette, so the effect of a preset is visible
     * before scrolling the rest of the UI. */
    static const theme_slot_t swatch_slots[] = {
        THEME_ACCENT, THEME_BACKGROUND, THEME_CARD_BG,
        THEME_TEXT_PRIMARY, THEME_TEXT_SECONDARY, THEME_BORDER,
    };
    static const theme_slot_t glass_swatch_slots[] = { THEME_ACCENT, THEME_RED, THEME_ICON };
    const theme_slot_t *slots = glass ? glass_swatch_slots : swatch_slots;
    size_t slot_count = glass ? sizeof(glass_swatch_slots) / sizeof(glass_swatch_slots[0])
                              : sizeof(swatch_slots) / sizeof(swatch_slots[0]);
    for (size_t i = 0; i < slot_count; i++) {
        lv_obj_t *sw = lv_obj_create(theme_section);
        lv_obj_set_size(sw, 34, 24);
        lv_obj_align(sw, LV_ALIGN_TOP_LEFT, 140 + (int) i * 40, 44);
        lv_obj_set_style_bg_color(sw, theme_color(slots[i]), 0);
        lv_obj_set_style_bg_opa(sw, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(sw, 1, 0);
        lv_obj_set_style_border_color(sw, COLOR_BORDER, 0);
        lv_obj_set_style_radius(sw, 5, 0);
        lv_obj_clear_flag(sw, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_clear_flag(sw, LV_OBJ_FLAG_CLICKABLE);
    }

    lv_obj_t *timezone_section = lv_obj_create(main_cont);
    lv_obj_set_size(timezone_section, 680, 50);
    lv_obj_set_style_bg_opa(timezone_section, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(timezone_section, 0, 0);
    lv_obj_set_style_pad_all(timezone_section, 10, 0);
    lv_obj_clear_flag(timezone_section, LV_OBJ_FLAG_SCROLLABLE);
    if (glass) lv_obj_clear_flag(timezone_section, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t *timezone_title = lv_label_create(timezone_section);
    lv_label_set_text(timezone_title, "Time Zone:");
    lv_obj_set_style_text_color(timezone_title, COLOR_TEXT_PRIMARY, 0);
    lv_obj_set_style_text_font(timezone_title, &lv_font_montserrat_18, 0);
    lv_obj_align(timezone_title, LV_ALIGN_TOP_LEFT, 0, 0);

    timezone_dropdown = lv_dropdown_create(timezone_section);
    lv_obj_set_size(timezone_dropdown, 300, 34);
    lv_obj_align(timezone_dropdown, LV_ALIGN_TOP_LEFT, 140, -4);
    lv_dropdown_set_options(timezone_dropdown, timezone_options);
    lv_dropdown_set_selected(timezone_dropdown, current_timezone_index);
    style_settings_dropdown(timezone_dropdown, &lv_font_montserrat_16);
    lv_obj_add_event_cb(timezone_dropdown, settings_timezone_changed, LV_EVENT_VALUE_CHANGED, NULL);
    if (glass) glass_style_dropdown(timezone_dropdown);

    /* Where the chain figures come from, and what fiat they are shown in.
     * Both providers serve the same REST shape, so the choice is which host
     * to trust and reach rather than which features are available; only
     * hashprice differs, and the odds screen says so when it is missing. */
    lv_obj_t *data_section = lv_obj_create(main_cont);
    lv_obj_set_size(data_section, 680, 50);
    lv_obj_set_style_bg_opa(data_section, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(data_section, 0, 0);
    lv_obj_set_style_pad_all(data_section, 10, 0);
    lv_obj_clear_flag(data_section, LV_OBJ_FLAG_SCROLLABLE);
    if (glass) lv_obj_clear_flag(data_section, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t *data_title = lv_label_create(data_section);
    lv_label_set_text(data_title, "Data:");
    lv_obj_set_style_text_color(data_title, COLOR_TEXT_PRIMARY, 0);
    lv_obj_set_style_text_font(data_title, &lv_font_montserrat_18, 0);
    lv_obj_align(data_title, LV_ALIGN_TOP_LEFT, 0, 0);

    data_source_dropdown = lv_dropdown_create(data_section);
    lv_obj_set_size(data_source_dropdown, 220, 34);
    lv_obj_align(data_source_dropdown, LV_ALIGN_TOP_LEFT, 140, -4);
    lv_dropdown_set_options(data_source_dropdown, "mempool.space\nbitview.space");
    lv_dropdown_set_selected(data_source_dropdown, (uint16_t) chain_get_source());
    style_settings_dropdown(data_source_dropdown, &lv_font_montserrat_16);
    lv_obj_add_event_cb(data_source_dropdown, settings_data_source_changed, LV_EVENT_VALUE_CHANGED, NULL);
    if (glass) glass_style_dropdown(data_source_dropdown);

    lv_obj_t *currency_title = lv_label_create(data_section);
    lv_label_set_text(currency_title, "Currency:");
    lv_obj_set_style_text_color(currency_title, COLOR_TEXT_PRIMARY, 0);
    lv_obj_set_style_text_font(currency_title, &lv_font_montserrat_18, 0);
    lv_obj_align(currency_title, LV_ALIGN_TOP_LEFT, 392, 0);

    currency_dropdown = lv_dropdown_create(data_section);
    lv_obj_set_size(currency_dropdown, 130, 34);
    lv_obj_align(currency_dropdown, LV_ALIGN_TOP_LEFT, 500, -4);
    lv_dropdown_set_options(currency_dropdown, "USD\nAUD\nNZD\nGBP\nEUR\nCAD\nJPY");
    lv_dropdown_set_selected(currency_dropdown, (uint16_t) chain_get_ccy());
    style_settings_dropdown(currency_dropdown, &lv_font_montserrat_16);
    lv_obj_add_event_cb(currency_dropdown, settings_currency_changed, LV_EVENT_VALUE_CHANGED, NULL);
    if (glass) glass_style_dropdown(currency_dropdown);

    display_control_config_t display_config;
    display_control_get_config(&display_config);

    lv_obj_t *display_section = lv_obj_create(main_cont);
    lv_obj_set_size(display_section, 680, 286);
    lv_obj_set_style_bg_opa(display_section, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(display_section, 0, 0);
    lv_obj_set_style_pad_all(display_section, 10, 0);
    lv_obj_clear_flag(display_section, LV_OBJ_FLAG_SCROLLABLE);
    if (glass) lv_obj_clear_flag(display_section, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t *display_title = lv_label_create(display_section);
    lv_label_set_text(display_title, "Display Schedule:");
    lv_obj_set_style_text_color(display_title, COLOR_TEXT_PRIMARY, 0);
    lv_obj_set_style_text_font(display_title, &lv_font_montserrat_18, 0);
    lv_obj_align(display_title, LV_ALIGN_TOP_LEFT, 0, 0);

    display_schedule_checkbox = settings_toggle_row(display_section,
                                                    "Turn the display off daily",
                                                    30, 660, 40, glass);

    display_schedule_status = lv_label_create(display_section);
    lv_obj_set_style_text_color(display_schedule_status, COLOR_TEXT_SECONDARY, 0);
    lv_obj_set_style_text_font(display_schedule_status, &lv_font_montserrat_14, 0);
    lv_obj_align(display_schedule_status, LV_ALIGN_TOP_LEFT, 2, 72);

    display_schedule_timer = lv_timer_create(settings_schedule_timer_cb, 2000, NULL);
    if (display_config.schedule_enabled) {
        lv_obj_add_state(display_schedule_checkbox, LV_STATE_CHECKED);
    }
    lv_obj_add_event_cb(display_schedule_checkbox, settings_display_schedule_changed, LV_EVENT_VALUE_CHANGED, NULL);


    lv_obj_t *off_label = lv_label_create(display_section);
    lv_label_set_text(off_label, "Turns off at");
    lv_obj_set_style_text_color(off_label, COLOR_TEXT_PRIMARY, 0);
    lv_obj_set_style_text_font(off_label, &lv_font_montserrat_16, 0);
    lv_obj_align(off_label, LV_ALIGN_TOP_LEFT, 0, 104);

    display_off_dropdown = lv_dropdown_create(display_section);
    lv_obj_set_size(display_off_dropdown, 300, 36);
    lv_obj_align(display_off_dropdown, LV_ALIGN_TOP_LEFT, 0, 128);
    lv_dropdown_set_options(display_off_dropdown, display_time_options);
    lv_dropdown_set_selected(display_off_dropdown, display_config.off_minute / 30U);
    style_settings_dropdown(display_off_dropdown, &lv_font_montserrat_14);
    lv_obj_add_event_cb(display_off_dropdown, settings_display_schedule_changed, LV_EVENT_VALUE_CHANGED, NULL);
    if (glass) glass_style_dropdown(display_off_dropdown);

    lv_obj_t *on_label = lv_label_create(display_section);
    lv_label_set_text(on_label, "Turns on at");
    lv_obj_set_style_text_color(on_label, COLOR_TEXT_PRIMARY, 0);
    lv_obj_set_style_text_font(on_label, &lv_font_montserrat_16, 0);
    lv_obj_align(on_label, LV_ALIGN_TOP_LEFT, 340, 104);

    display_on_dropdown = lv_dropdown_create(display_section);
    lv_obj_set_size(display_on_dropdown, 300, 36);
    lv_obj_align(display_on_dropdown, LV_ALIGN_TOP_LEFT, 340, 128);
    lv_dropdown_set_options(display_on_dropdown, display_time_options);
    lv_dropdown_set_selected(display_on_dropdown, display_config.on_minute / 30U);
    style_settings_dropdown(display_on_dropdown, &lv_font_montserrat_14);
    lv_obj_add_event_cb(display_on_dropdown, settings_display_schedule_changed, LV_EVENT_VALUE_CHANGED, NULL);
    if (glass) glass_style_dropdown(display_on_dropdown);

    lv_obj_t *corner_label = lv_label_create(display_section);
    lv_label_set_text(corner_label, "Display-off button");
    lv_obj_set_style_text_color(corner_label, COLOR_TEXT_PRIMARY, 0);
    lv_obj_set_style_text_font(corner_label, &lv_font_montserrat_16, 0);
    lv_obj_align(corner_label, LV_ALIGN_TOP_LEFT, 0, 176);

    display_corner_dropdown = lv_dropdown_create(display_section);
    lv_obj_set_size(display_corner_dropdown, 300, 36);
    lv_obj_align(display_corner_dropdown, LV_ALIGN_TOP_LEFT, 0, 200);
    lv_dropdown_set_options(display_corner_dropdown, display_corner_options);
    lv_dropdown_set_selected(display_corner_dropdown,
                             (uint16_t)display_button_mode_from_config(
                                 display_config.power_button_corner,
                                 display_config.power_button_visuals_visible));
    style_settings_dropdown(display_corner_dropdown, &lv_font_montserrat_14);
    lv_obj_add_event_cb(display_corner_dropdown, settings_display_schedule_changed, LV_EVENT_VALUE_CHANGED, NULL);
    if (glass) glass_style_dropdown(display_corner_dropdown);

    lv_obj_t *display_button_mode_hint = lv_label_create(display_section);
    lv_label_set_text(display_button_mode_hint, "Hidden keeps the selected corner tappable");
    lv_obj_set_style_text_color(display_button_mode_hint, COLOR_TEXT_SECONDARY, 0);
    lv_obj_set_style_text_font(display_button_mode_hint, &lv_font_montserrat_14, 0);
    lv_obj_set_width(display_button_mode_hint, 650);
    lv_label_set_long_mode(display_button_mode_hint, LV_LABEL_LONG_WRAP);
    lv_obj_align(display_button_mode_hint, LV_ALIGN_TOP_LEFT, 0, 244);

    // OTA Update Section
    lv_obj_t *ota_section = lv_obj_create(main_cont);
    lv_obj_set_size(ota_section, 680, 206);
    lv_obj_set_style_bg_opa(ota_section, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(ota_section, 0, 0);
    lv_obj_set_style_pad_all(ota_section, 10, 0);
    lv_obj_clear_flag(ota_section, LV_OBJ_FLAG_SCROLLABLE);
    if (glass) lv_obj_clear_flag(ota_section, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t *ota_title = lv_label_create(ota_section);
    lv_label_set_text(ota_title, "Firmware Update:");
    lv_obj_set_style_text_color(ota_title, COLOR_TEXT_PRIMARY, 0);
    lv_obj_set_style_text_font(ota_title, &lv_font_montserrat_18, 0);
    lv_obj_align(ota_title, LV_ALIGN_TOP_LEFT, 0, 0);
    ota_version_label = lv_label_create(ota_section);
    char version_text[64];
    const char *version = ota_get_current_version();
    snprintf(version_text, sizeof(version_text), "Current: %s", version ? version : "Unknown");
    lv_label_set_text(ota_version_label, version_text);
    lv_obj_set_style_text_color(ota_version_label, COLOR_TEXT_PRIMARY, 0);
    lv_obj_set_style_text_font(ota_version_label, &lv_font_montserrat_14, 0);
    lv_obj_align(ota_version_label, LV_ALIGN_TOP_LEFT, 0, 30);

    ota_status_label = lv_label_create(ota_section);
    lv_label_set_text(ota_status_label, "Ready for update");
    lv_obj_set_style_text_color(ota_status_label, COLOR_TEXT_PRIMARY, 0);
    lv_obj_set_style_text_font(ota_status_label, &lv_font_montserrat_14, 0);
    lv_obj_align(ota_status_label, LV_ALIGN_TOP_LEFT, 0, 55);

    ota_progress_bar = lv_bar_create(ota_section);
    lv_obj_set_size(ota_progress_bar, 420, 16);
    lv_obj_align(ota_progress_bar, LV_ALIGN_TOP_LEFT, 0, 80);
    lv_bar_set_range(ota_progress_bar, 0, 100);
    lv_bar_set_value(ota_progress_bar, 0, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(ota_progress_bar, COLOR_CARD_BG, LV_PART_MAIN);
    lv_obj_set_style_bg_color(ota_progress_bar, COLOR_ACCENT, LV_PART_INDICATOR);
    lv_obj_set_style_radius(ota_progress_bar, 8, 0);
    if (glass) glass_style_bar(ota_progress_bar);

    ota_update_btn = create_settings_button(ota_section, "CHECK FOR UPDATES", settings_ota_update_clicked, false);
    lv_obj_set_size(ota_update_btn, 240, 36);
    lv_obj_align(ota_update_btn, LV_ALIGN_TOP_LEFT, 0, 110);

    /* Opt in to a daily check. It only ever checks: installing stays on the
     * button above, so nothing arrives on this panel unattended. */
    lv_obj_t *auto_sw = settings_toggle_row(ota_section, "Check for updates daily",
                                            156, 660, 40, glass);
    if (ota_update_get_auto_check()) {
        lv_obj_add_state(auto_sw, LV_STATE_CHECKED);
    }
    lv_obj_add_event_cb(auto_sw, settings_ota_auto_toggled, LV_EVENT_VALUE_CHANGED, NULL);

    // Create OTA update timer (500ms interval)
    ota_timer = lv_timer_create(ota_update_timer_cb, 500, NULL);

    if (glass)
    {
        glass_screen_ready(settings_screen);
        return;
    }

    lv_obj_t *bottom_nav = lv_obj_create(settings_screen);
    lv_obj_set_size(bottom_nav, SCREEN_WIDTH, 64);
    lv_obj_align(bottom_nav, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_color(bottom_nav, COLOR_NAV_BG, 0);
    lv_obj_set_style_bg_opa(bottom_nav, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(bottom_nav, 0, 0);
    lv_obj_set_style_radius(bottom_nav, 0, 0);
    lv_obj_set_style_pad_all(bottom_nav, 8, 0);
    lv_obj_clear_flag(bottom_nav, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(bottom_nav, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_flex_flow(bottom_nav, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(bottom_nav, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    create_bottom_nav_btn(bottom_nav, LV_SYMBOL_HOME, settings_home_clicked, false);
    create_bottom_nav_btn_img(bottom_nav, &cube_solid_full, settings_block_clicked, false);
    create_bottom_nav_btn_img(bottom_nav, &cubes_solid_full, settings_mempool_clicked, false);
    create_bottom_nav_btn_img(bottom_nav, &clock_solid_full, settings_clock_clicked, false);
    create_bottom_nav_btn(bottom_nav, "$", settings_price_clicked, false);
    create_bottom_nav_btn(bottom_nav, "%", settings_odds_clicked, false);
    create_bottom_nav_btn(bottom_nav, LV_SYMBOL_DOWNLOAD, settings_payout_clicked, false);
    create_bottom_nav_btn(bottom_nav, LV_SYMBOL_WIFI, settings_wifi_clicked, false);
    create_bottom_nav_btn(bottom_nav, LV_SYMBOL_SETTINGS, settings_diagnostics_handler, true);
    create_bottom_nav_btn(bottom_nav, LV_SYMBOL_EYE_OPEN, settings_night_clicked, false);
}

void settings_screen_destroy(void)
{
    if (display_schedule_timer) {
        lv_timer_del(display_schedule_timer);
        display_schedule_timer = NULL;
    }
    display_schedule_status = NULL;
    settings_main_cont = NULL;
    // Clean up Easter egg overlay if showing
    if (sys_overlay) {
        lv_obj_del(sys_overlay);
        sys_overlay = NULL;
    }
    diag_counter = 0;

    // Clean up OTA timer
    if (ota_timer) {
        lv_timer_del(ota_timer);
        ota_timer = NULL;
    }

    if (settings_screen)
    {
        glass_screen_detach(settings_screen);
        lv_obj_del(settings_screen);
        settings_screen = NULL;
        performance_low_btn = NULL;
        performance_medium_btn = NULL;
        performance_high_btn = NULL;
        auto_fan_checkbox = NULL;
        fan_slider = NULL;
        fan_value_label = NULL;
        fan_save_btn = NULL;
        brightness_slider = NULL;
        brightness_value_label = NULL;
        timezone_dropdown = NULL;
        theme_dropdown = NULL;
        display_schedule_checkbox = NULL;
        display_off_dropdown = NULL;
        display_on_dropdown = NULL;
        display_corner_dropdown = NULL;
        ota_update_btn = NULL;
        ota_status_label = NULL;
        ota_progress_bar = NULL;
        ota_version_label = NULL;
    }

    display_control_set_power_button_visible(true);
}

/* Say what the schedule is actually doing.
 *
 * display_control_evaluate() returns early until the clock is set, so
 * switching this on before SNTP has answered saved the setting and changed
 * nothing on screen, with no way to tell that from a broken control. */
static void settings_refresh_schedule_status(void)
{
    if (!display_schedule_status || !display_schedule_checkbox) {
        return;
    }

    if (!lv_obj_has_state(display_schedule_checkbox, LV_STATE_CHECKED)) {
        lv_label_set_text(display_schedule_status, "The display stays on all day");
        return;
    }
    if (!display_control_time_is_set()) {
        lv_label_set_text(display_schedule_status,
                          "Waiting for network time before this can take effect");
        return;
    }
    lv_label_set_text(display_schedule_status,
                      display_control_is_backlight_on() ? "Active: the display is on now"
                                                        : "Active: the display is off now");
}

static void settings_schedule_timer_cb(lv_timer_t *t)
{
    (void) t;
    settings_refresh_schedule_status();
}

static void settings_ota_auto_toggled(lv_event_t *e)
{
    ota_update_set_auto_check(lv_obj_has_state(lv_event_get_target(e), LV_STATE_CHECKED));
}

void settings_scroll_to(int y)
{
    if (settings_main_cont) {
        lv_obj_scroll_to_y(settings_main_cont, y, LV_ANIM_OFF);
    }
}

lv_obj_t *settings_get_screen(void)
{
    return settings_screen;
}

void settings_update_info(const settings_info_t *info)
{
    if (info)
    {
        current_settings = *info;
        update_performance_buttons();
        update_fan_controls();

        if (auto_fan_checkbox)
        {
            if (current_settings.auto_fan_control)
            {
                lv_obj_add_state(auto_fan_checkbox, LV_STATE_CHECKED);
            }
            else
            {
                lv_obj_clear_state(auto_fan_checkbox, LV_STATE_CHECKED);
            }
        }

        if (brightness_slider)
        {
            lv_slider_set_value(brightness_slider, current_settings.brightness_percent, LV_ANIM_OFF);
        }
        if (brightness_value_label)
        {
            char brightness_text[16];
            snprintf(brightness_text, sizeof(brightness_text), "%d%%", current_settings.brightness_percent);
            lv_label_set_text(brightness_value_label, brightness_text);
        }
    }
}

void settings_performance_low_clicked(lv_event_t *e)
{
    current_settings.performance_mode = PERFORMANCE_LOW;
    update_performance_buttons();
    printf("Performance mode set to LOW\n");

    BAP_send_frequency_setting(575.0f);
    BAP_send_asic_voltage(1160.0f);
}

void settings_performance_medium_clicked(lv_event_t *e)
{
    current_settings.performance_mode = PERFORMANCE_MEDIUM;
    update_performance_buttons();
    printf("Performance mode set to MEDIUM\n");

    BAP_send_frequency_setting(600.0f);
    BAP_send_asic_voltage(1200.0f);
}

void settings_performance_high_clicked(lv_event_t *e)
{
    current_settings.performance_mode = PERFORMANCE_HIGH;
    update_performance_buttons();
    printf("Performance mode set to HIGH\n");

    BAP_send_frequency_setting(655.0f);
    BAP_send_asic_voltage(1200.0f);
}

void settings_auto_fan_toggled(lv_event_t *e)
{
    lv_obj_t *checkbox = lv_event_get_target(e);
    current_settings.auto_fan_control = lv_obj_has_state(checkbox, LV_STATE_CHECKED);
    update_fan_controls();
    printf("Auto fan control: %s\n", current_settings.auto_fan_control ? "ON" : "OFF");
}

void settings_fan_slider_changed(lv_event_t *e)
{
    lv_obj_t *slider = lv_event_get_target(e);
    current_settings.fan_speed_percent = lv_slider_get_value(slider);

    if (fan_value_label)
    {
        char fan_text[16];
        snprintf(fan_text, sizeof(fan_text), "%d%%", current_settings.fan_speed_percent);
        lv_label_set_text(fan_value_label, fan_text);
    }

    printf("Fan speed set to: %d%%\n", current_settings.fan_speed_percent);
}

void settings_fan_save_clicked(lv_event_t *e)
{
    printf("Saving fan settings - Auto: %s, Speed: %d%%\n",
           current_settings.auto_fan_control ? "ON" : "OFF",
           current_settings.fan_speed_percent);

    if (current_settings.auto_fan_control)
    {
        BAP_send_automatic_fan_control(true);
        printf("Sending auto fan control command\n");
    }
    else
    {
        BAP_send_fan_speed(current_settings.fan_speed_percent);
        printf("Sending manual fan speed: %d%%\n", current_settings.fan_speed_percent);
    }
}

void settings_home_clicked(lv_event_t *e)
{
    home_screen_create();
    lv_scr_load(home_get_screen());
    settings_screen_destroy();
}

void settings_wifi_clicked(lv_event_t *e)
{
    wifi_screen_create();
    lv_scr_load(wifi_get_screen());
    settings_screen_destroy();
}

void settings_clock_clicked(lv_event_t *e)
{
    clock_screen_create();
    lv_scr_load(clock_get_screen());
    settings_screen_destroy();
}

void settings_price_clicked(lv_event_t *e)
{
    price_screen_create();
    lv_scr_load(price_get_screen());
    settings_screen_destroy();
}

void settings_block_clicked(lv_event_t *e)
{
    block_screen_create();
    lv_scr_load(block_get_screen());
    settings_screen_destroy();
}

void settings_mempool_clicked(lv_event_t *e)
{
    mempool_screen_create();
    lv_scr_load(mempool_get_screen());
    settings_screen_destroy();
}

void settings_brightness_slider_changed(lv_event_t *e)
{
    lv_obj_t *slider = lv_event_get_target(e);
    current_settings.brightness_percent = lv_slider_get_value(slider);

    if (brightness_value_label)
    {
        char brightness_text[16];
        snprintf(brightness_text, sizeof(brightness_text), "%d%%", current_settings.brightness_percent);
        lv_label_set_text(brightness_value_label, brightness_text);
    }

    // Apply brightness change immediately
    lcd_backlight_set_brightness(current_settings.brightness_percent);

    printf("Screen brightness set to: %d%%\n", current_settings.brightness_percent);
}

void settings_timezone_changed(lv_event_t *e)
{
    lv_obj_t *dropdown = lv_event_get_target(e);
    current_timezone_index = (int)lv_dropdown_get_selected(dropdown);
    apply_timezone_by_index(current_timezone_index);
    settings_save_timezone(current_timezone_index);
}

static void settings_display_schedule_changed(lv_event_t *e)
{
    LV_UNUSED(e);
    if (!display_schedule_checkbox || !display_off_dropdown || !display_on_dropdown ||
        !display_corner_dropdown) {
        return;
    }

    display_power_button_mode_t button_mode =
        (display_power_button_mode_t)lv_dropdown_get_selected(display_corner_dropdown);

    display_control_config_t config = {
        .schedule_enabled = lv_obj_has_state(display_schedule_checkbox, LV_STATE_CHECKED),
        .off_minute = (uint16_t)(lv_dropdown_get_selected(display_off_dropdown) * 30U),
        .on_minute = (uint16_t)(lv_dropdown_get_selected(display_on_dropdown) * 30U),
        .power_button_corner = display_button_mode_corner(button_mode),
        .power_button_visuals_visible = display_button_mode_shows_visuals(button_mode),
    };

    esp_err_t err = display_control_set_config(&config);
    settings_refresh_schedule_status();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to save display schedule: %s", esp_err_to_name(err));
    }
}

void settings_night_clicked(lv_event_t *e)
{
    // Navigate to night mode screen
    night_screen_create();
    lv_scr_load(night_get_screen());
    settings_screen_destroy();
}

void settings_odds_clicked(lv_event_t *e)
{
    odds_screen_create();
    lv_scr_load(odds_get_screen());
    settings_screen_destroy();
}

void settings_payout_clicked(lv_event_t *e)
{
    payout_screen_create();
    lv_scr_load(payout_get_screen());
    settings_screen_destroy();
}
