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
#include "feed.h"
#include "poolping.h"
#include "wallpaper.h"
#include "lvgl__lvgl/src/extra/libs/qrcode/lv_qrcode.h"
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
static lv_obj_t *fan_section = NULL;
static lv_obj_t *fan_manual_cont = NULL;
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
static lv_obj_t *display_section = NULL;
static lv_obj_t *schedule_details_cont = NULL;
static lv_timer_t *display_schedule_timer = NULL;
static lv_obj_t *display_off_dropdown = NULL;
static lv_obj_t *display_on_dropdown = NULL;
static lv_obj_t *display_corner_label = NULL;
static lv_obj_t *display_corner_dropdown = NULL;
static lv_obj_t *display_corner_hint = NULL;
static lv_obj_t *theme_dropdown = NULL;
static lv_obj_t *skin_dropdown = NULL;
static lv_obj_t *glass_settings_body = NULL;
static lv_obj_t *glass_settings_pane = NULL;
static lv_obj_t *display_dim_slider = NULL;
static lv_obj_t *display_dim_value_label = NULL;
static lv_timer_t *glass_pool_timer = NULL;
static lv_obj_t *glass_pool_values[8] = { NULL };
static lv_obj_t *sys_overlay = NULL;
static int diag_counter = 0;

// OTA Update UI elements
static lv_obj_t *ota_update_btn = NULL;
static lv_obj_t *ota_status_label = NULL;
static lv_obj_t *ota_progress_bar = NULL;
static lv_obj_t *ota_version_label = NULL;
static lv_timer_t *ota_timer = NULL;
static lv_obj_t *ota_restore_btn = NULL;
static lv_obj_t *ota_restore_status_label = NULL;
static lv_obj_t *ota_restore_overlay = NULL;
static lv_obj_t *ota_section = NULL;
static lv_obj_t *restore_details_cont = NULL;
static lv_obj_t *restore_disclosure_btn = NULL;
static bool restore_details_expanded = false;
static lv_obj_t *ota_frequency_dropdown = NULL;

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

static const char *ota_frequency_options = "Manual\nDaily\nWeekly";

#define SETTINGS_NVS_NAMESPACE "settings"
#define SETTINGS_NVS_TZ_INDEX_KEY "tz_index"

static void settings_display_schedule_changed(lv_event_t *e);
static void settings_refresh_schedule_status(void);
static void settings_ota_auto_toggled(lv_event_t *e);
static void settings_ota_beta_toggled(lv_event_t *e);
static void settings_ota_frequency_changed(lv_event_t *e);
static void settings_schedule_timer_cb(lv_timer_t *t);
static void settings_original_restore_clicked(lv_event_t *e);
static void settings_restore_overlay_close(lv_event_t *e);
static void settings_sync_fan_disclosure(void);
static void settings_layout_schedule_controls(void);
static void settings_restore_disclosure_clicked(lv_event_t *e);

typedef enum {
    GLASS_SETTINGS_HUB = 0,
    GLASS_SETTINGS_STYLE,
    GLASS_SETTINGS_POOL,
    GLASS_SETTINGS_DISPLAY,
    GLASS_SETTINGS_SYSTEM,
} glass_settings_page_t;

static glass_settings_page_t glass_settings_page = GLASS_SETTINGS_HUB;
static void glass_settings_render(void);

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

static void settings_sync_fan_disclosure(void)
{
    if (!fan_section || !fan_manual_cont || !fan_save_btn) {
        return;
    }

    if (current_settings.auto_fan_control) {
        lv_obj_add_flag(fan_manual_cont, LV_OBJ_FLAG_HIDDEN);
        lv_obj_align(fan_save_btn, LV_ALIGN_TOP_LEFT, 0, 94);
        lv_obj_set_height(fan_section, 160);
    } else {
        lv_obj_clear_flag(fan_manual_cont, LV_OBJ_FLAG_HIDDEN);
        lv_obj_align(fan_save_btn, LV_ALIGN_TOP_LEFT, 0, 146);
        lv_obj_set_height(fan_section, 210);
    }

    if (settings_main_cont) {
        lv_obj_update_layout(settings_main_cont);
    }
}

static void update_fan_controls(void)
{
    if (!fan_slider || !fan_value_label)
        return;

    settings_sync_fan_disclosure();
    if (current_settings.auto_fan_control)
    {
        return;
    }

    lv_slider_set_value(fan_slider, current_settings.fan_speed_percent, LV_ANIM_OFF);
    char fan_text[16];
    snprintf(fan_text, sizeof(fan_text), "%d%%", current_settings.fan_speed_percent);
    lv_label_set_text(fan_value_label, fan_text);
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
    if (ota_restore_overlay) {
        lv_obj_del(ota_restore_overlay);
        ota_restore_overlay = NULL;
        display_control_pop_overlay();
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
    const bool compact_ota = glass_active() && glass_settings_page == GLASS_SETTINGS_SYSTEM;

    switch (info.status) {
        case OTA_STATUS_IDLE:
            lv_label_set_text(ota_status_label, "Ready for update");
            if (btn_label) lv_label_set_text(btn_label, compact_ota ? "UPDATE" : "CHECK FOR UPDATES");
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
            if (btn_label) lv_label_set_text(btn_label, compact_ota ? "INSTALL" : "INSTALL UPDATE");
            lv_obj_clear_state(ota_update_btn, LV_STATE_DISABLED);
            lv_bar_set_value(ota_progress_bar, 0, LV_ANIM_OFF);
            break;

        case OTA_STATUS_NO_UPDATE:
            lv_label_set_text(ota_status_label, "Already up to date");
            if (btn_label) lv_label_set_text(btn_label, compact_ota ? "UPDATE" : "CHECK FOR UPDATES");
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
            if (btn_label) lv_label_set_text(btn_label, compact_ota ? "UPDATE" : "CHECK FOR UPDATES");
            lv_obj_clear_state(ota_update_btn, LV_STATE_DISABLED);
            lv_bar_set_value(ota_progress_bar, 0, LV_ANIM_OFF);
            break;
    }

    if (ota_restore_btn && ota_restore_status_label) {
        lv_obj_t *restore_label = lv_obj_get_child(ota_restore_btn, 0);
        switch (info.restore_status) {
            case OTA_RESTORE_IDLE:
                lv_label_set_text(ota_restore_status_label,
                                  "Official release: check before restoring");
                if (restore_label) lv_label_set_text(restore_label, compact_ota ? "CHECK OFFICIAL" : "CHECK OFFICIAL RELEASE");
                lv_obj_clear_state(ota_restore_btn, LV_STATE_DISABLED);
                break;
            case OTA_RESTORE_CHECKING:
                lv_label_set_text(ota_restore_status_label,
                                  "Checking bitaxeorg for its latest release...");
                if (restore_label) lv_label_set_text(restore_label, "CHECKING...");
                lv_obj_add_state(ota_restore_btn, LV_STATE_DISABLED);
                break;
            case OTA_RESTORE_READY:
                lv_label_set_text_fmt(ota_restore_status_label,
                                      "Latest official release: %s", info.original_version);
                if (restore_label) lv_label_set_text(restore_label, compact_ota ? "RESTORE" : "ATTEMPT OFFICIAL RESTORE");
                lv_obj_clear_state(ota_restore_btn, LV_STATE_DISABLED);
                break;
            case OTA_RESTORE_ERROR:
                lv_label_set_text_fmt(ota_restore_status_label, "Official check failed: %s",
                                      info.restore_error_msg[0] ? info.restore_error_msg : "Unknown error");
                if (restore_label) lv_label_set_text(restore_label, compact_ota ? "TRY AGAIN" : "TRY OFFICIAL CHECK AGAIN");
                lv_obj_clear_state(ota_restore_btn, LV_STATE_DISABLED);
                break;
        }
    }

    /* A check or flash from either channel owns the OTA state machine. Disable
     * both actions so two fingers, the daily poll, and a restore cannot race. */
    if (ota_update_is_running()) {
        lv_obj_add_state(ota_update_btn, LV_STATE_DISABLED);
        if (ota_restore_btn) lv_obj_add_state(ota_restore_btn, LV_STATE_DISABLED);
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

static void settings_restore_overlay_close(lv_event_t *e)
{
    (void)e;
    if (ota_restore_overlay) {
        lv_obj_del(ota_restore_overlay);
        ota_restore_overlay = NULL;
        display_control_pop_overlay();
    }
}

static void settings_restore_confirmed(lv_event_t *e)
{
    (void)e;
    settings_restore_overlay_close(NULL);
    esp_err_t ret = ota_restore_original_latest();
    if (ret != ESP_OK && ota_restore_status_label) {
        lv_label_set_text(ota_restore_status_label, "Could not start the official restore");
    }
}

static void settings_show_restore_confirmation(const ota_info_t *info)
{
    if (ota_restore_overlay || !settings_screen || !info) return;

    display_control_push_overlay();
    ota_restore_overlay = lv_obj_create(lv_scr_act());
    lv_obj_set_size(ota_restore_overlay, SCREEN_WIDTH, SCREEN_HEIGHT);
    lv_obj_set_pos(ota_restore_overlay, 0, 0);
    lv_obj_set_style_bg_color(ota_restore_overlay, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(ota_restore_overlay, LV_OPA_70, 0);
    lv_obj_set_style_border_width(ota_restore_overlay, 0, 0);
    lv_obj_set_style_pad_all(ota_restore_overlay, 0, 0);
    lv_obj_clear_flag(ota_restore_overlay, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(ota_restore_overlay, settings_restore_overlay_close, LV_EVENT_CLICKED, NULL);

    lv_obj_t *dialog;
    if (glass_active()) {
        dialog = glass_pane(ota_restore_overlay, 650, 350, 24);
    } else {
        dialog = lv_obj_create(ota_restore_overlay);
        lv_obj_set_size(dialog, 650, 350);
        lv_obj_set_style_bg_color(dialog, COLOR_CARD_BG, 0);
        lv_obj_set_style_bg_opa(dialog, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(dialog, 2, 0);
        lv_obj_set_style_border_color(dialog, COLOR_ACCENT, 0);
        lv_obj_set_style_radius(dialog, 18, 0);
        lv_obj_set_style_pad_all(dialog, 22, 0);
    }
    lv_obj_center(dialog);
    lv_obj_clear_flag(dialog, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(dialog, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t *title = lv_label_create(dialog);
    lv_label_set_text(title, "ATTEMPT OFFICIAL RESTORE?");
    lv_obj_set_style_text_color(title, COLOR_TEXT_PRIMARY, 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 0);

    lv_obj_t *target = lv_label_create(dialog);
    lv_label_set_text_fmt(target, "Current: %s     Official target: %s",
                          info->current_version[0] ? info->current_version : "Unknown",
                          info->original_version[0] ? info->original_version : "Unknown");
    lv_obj_set_style_text_color(target, COLOR_ACCENT, 0);
    lv_obj_set_style_text_font(target, &lv_font_montserrat_16, 0);
    lv_obj_align(target, LV_ALIGN_TOP_MID, 0, 48);

    lv_obj_t *warning = lv_label_create(dialog);
    lv_label_set_text(warning,
        "This attempts to replace the custom interface with the latest bitaxeorg release.\n"
        "Display settings are kept, but custom screens and themes are removed.\n\n"
        "Important: if custom firmware was installed by USB, its rollback-enabled\n"
        "bootloader may undo this OTA restore after a later reboot. Use the official\n"
        "USB factory image for a guaranteed permanent restore.");
    lv_label_set_long_mode(warning, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(warning, 590);
    lv_obj_set_style_text_align(warning, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(warning, COLOR_TEXT_SECONDARY, 0);
    lv_obj_set_style_text_font(warning, &lv_font_montserrat_14, 0);
    lv_obj_align(warning, LV_ALIGN_TOP_MID, 0, 82);

    lv_obj_t *cancel = create_settings_button(dialog, "CANCEL", settings_restore_overlay_close, false);
    lv_obj_set_size(cancel, 190, 48);
    lv_obj_align(cancel, LV_ALIGN_BOTTOM_LEFT, 18, -4);
    lv_obj_t *restore = create_settings_button(dialog, "TRY RESTORE VIA OTA", settings_restore_confirmed, true);
    lv_obj_set_size(restore, 250, 48);
    lv_obj_align(restore, LV_ALIGN_BOTTOM_RIGHT, -18, -4);

    if (glass_active()) glass_screen_ready(lv_scr_act());
}

static void settings_original_restore_clicked(lv_event_t *e)
{
    (void)e;
    ota_info_t info;
    ota_update_get_info(&info);
    if (info.restore_status == OTA_RESTORE_READY) {
        settings_show_restore_confirmation(&info);
    } else {
        ota_check_original_release();
    }
}

static void settings_restore_disclosure_clicked(lv_event_t *e)
{
    (void)e;
    if (!ota_section || !restore_details_cont || !restore_disclosure_btn) return;

    restore_details_expanded = !restore_details_expanded;
    if (restore_details_expanded) {
        lv_obj_clear_flag(restore_details_cont, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_height(ota_section, 516);
    } else {
        lv_obj_add_flag(restore_details_cont, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_height(ota_section, 366);
    }

    lv_obj_t *label = lv_obj_get_child(restore_disclosure_btn, 0);
    if (label) {
        lv_label_set_text(label, restore_details_expanded ? "HIDE RESTORE OPTIONS"
                                                   : "RESTORE ORIGINAL FIRMWARE...");
    }
    if (settings_main_cont) lv_obj_update_layout(settings_main_cont);
    if (restore_details_expanded) {
        lv_obj_scroll_to_view_recursive(restore_details_cont, LV_ANIM_ON);
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
    if (h < 56) h = 56;
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_set_size(row, w, h);
    lv_obj_align(row, LV_ALIGN_TOP_LEFT, 0, y);
    lv_obj_set_style_bg_color(row, glass ? lv_color_white() : COLOR_BACKGROUND, 0);
    lv_obj_set_style_bg_opa(row, glass ? LV_OPA_10 : LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(row, 1, 0);
    lv_obj_set_style_border_color(row, glass ? lv_color_white() : COLOR_BORDER, 0);
    lv_obj_set_style_border_opa(row, glass ? LV_OPA_30 : LV_OPA_70, 0);
    lv_obj_set_style_radius(row, 14, 0);
    lv_obj_set_style_pad_all(row, 0, 0);
    lv_obj_set_style_bg_color(row, COLOR_ACCENT, LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(row, LV_OPA_20, LV_STATE_PRESSED);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(row, LV_OBJ_FLAG_SCROLL_CHAIN_VER);

    lv_obj_t *label = lv_label_create(row);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_color(label, COLOR_TEXT_PRIMARY, 0);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_18, 0);
    lv_obj_align(label, LV_ALIGN_LEFT_MID, 16, 0);
    lv_obj_clear_flag(label, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *sw = lv_switch_create(row);
    lv_obj_set_size(sw, 58, 32);
    lv_obj_align(sw, LV_ALIGN_RIGHT_MID, -14, 0);
    lv_obj_set_style_anim_time(sw, 140, LV_PART_MAIN);
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

/* Keep schedule times visible even while scheduling is disabled. This lets a
 * user configure a safe window before enabling it instead of immediately
 * applying stale/default times. */
static void settings_layout_schedule_controls(void)
{
    if (!display_section || !schedule_details_cont || !display_schedule_checkbox ||
        !display_corner_dropdown) {
        return;
    }

    lv_obj_clear_flag(schedule_details_cont, LV_OBJ_FLAG_HIDDEN);
    const int corner_y = 202;
    if (display_corner_label) lv_obj_align(display_corner_label, LV_ALIGN_TOP_LEFT, 0, corner_y);
    lv_obj_align(display_corner_dropdown, LV_ALIGN_TOP_LEFT, 0, corner_y + 24);
    if (display_corner_hint) lv_obj_align(display_corner_hint, LV_ALIGN_TOP_LEFT, 0, corner_y + 78);
    lv_obj_set_height(display_section, 315);

    if (settings_main_cont) {
        lv_obj_update_layout(settings_main_cont);
    }
}

/* ---------------- Glass: fixed, full-screen Settings routes ----------------
 *
 * Glass deliberately does not reuse the long Classic preferences list.  A
 * bottom tap replaces the current screen with this hub, and every destination
 * is either a complete screen or a complete, non-scrolling settings page. */

static lv_obj_t *glass_settings_card(lv_obj_t *parent, int x, int y, int w, int h)
{
    lv_obj_t *card = lv_obj_create(parent);
    lv_obj_set_size(card, w, h);
    lv_obj_set_pos(card, x, y);
    lv_obj_set_style_bg_color(card, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_10, 0);
    lv_obj_set_style_border_color(card, lv_color_white(), 0);
    lv_obj_set_style_border_opa(card, LV_OPA_30, 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_radius(card, 18, 0);
    lv_obj_set_style_pad_all(card, 0, 0);
    lv_obj_set_style_shadow_width(card, 0, 0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    return card;
}

static lv_obj_t *glass_settings_action(lv_obj_t *parent, const char *title,
                                        const char *subtitle,
                                        int x, int y, int w, int h,
                                        lv_event_cb_t cb, void *user)
{
    lv_obj_t *card = glass_settings_card(parent, x, y, w, h);
    lv_obj_set_style_bg_color(card, COLOR_ACCENT, LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(card, LV_OPA_30, LV_STATE_PRESSED);
    if (cb) lv_obj_add_event_cb(card, cb, LV_EVENT_CLICKED, user);

    lv_obj_t *name = lv_label_create(card);
    lv_label_set_text(name, title);
    lv_obj_set_style_text_color(name, COLOR_TEXT_PRIMARY, 0);
    lv_obj_set_style_text_font(name, &lv_font_montserrat_18, 0);
    lv_obj_align(name, LV_ALIGN_CENTER, 0, subtitle && subtitle[0] ? -12 : 0);
    lv_obj_clear_flag(name, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);

    if (subtitle && subtitle[0]) {
        lv_obj_t *hint = lv_label_create(card);
        lv_label_set_text(hint, subtitle);
        lv_label_set_long_mode(hint, LV_LABEL_LONG_DOT);
        lv_obj_set_width(hint, w - 18);
        lv_obj_set_style_text_align(hint, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_color(hint, COLOR_TEXT_SECONDARY, 0);
        lv_obj_set_style_text_font(hint, &lv_font_montserrat_12, 0);
        lv_obj_align(hint, LV_ALIGN_CENTER, 0, 17);
        lv_obj_clear_flag(hint, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    }
    return card;
}

static void glass_settings_reset_refs(void)
{
    performance_low_btn = performance_medium_btn = performance_high_btn = NULL;
    auto_fan_checkbox = fan_section = fan_manual_cont = NULL;
    fan_slider = fan_value_label = fan_save_btn = NULL;
    brightness_slider = brightness_value_label = NULL;
    timezone_dropdown = data_source_dropdown = currency_dropdown = NULL;
    display_schedule_checkbox = display_schedule_status = NULL;
    display_section = schedule_details_cont = NULL;
    display_off_dropdown = display_on_dropdown = NULL;
    display_corner_label = display_corner_dropdown = display_corner_hint = NULL;
    display_dim_slider = display_dim_value_label = NULL;
    theme_dropdown = skin_dropdown = NULL;
    ota_update_btn = ota_status_label = ota_progress_bar = ota_version_label = NULL;
    ota_frequency_dropdown = NULL;
    ota_restore_btn = ota_restore_status_label = ota_section = NULL;
    restore_details_cont = restore_disclosure_btn = NULL;
    memset(glass_pool_values, 0, sizeof(glass_pool_values));
}

static void glass_settings_stop_page_tasks(void)
{
    if (display_schedule_timer) {
        lv_timer_del(display_schedule_timer);
        display_schedule_timer = NULL;
    }
    if (glass_pool_timer) {
        lv_timer_del(glass_pool_timer);
        glass_pool_timer = NULL;
    }
    if (ota_timer) {
        lv_timer_del(ota_timer);
        ota_timer = NULL;
    }
}

static void glass_settings_render_async(void *page)
{
    glass_settings_page = (glass_settings_page_t)(intptr_t)page;
    glass_settings_render();
}

static void glass_settings_page_clicked(lv_event_t *e)
{
    lv_async_call(glass_settings_render_async, lv_event_get_user_data(e));
}

static void glass_settings_screen_clicked(lv_event_t *e)
{
    glass_settings_page = GLASS_SETTINGS_HUB;
    glass_goto((glass_screen_t)(intptr_t)lv_event_get_user_data(e));
}

static void glass_settings_back_clicked(lv_event_t *e)
{
    (void)e;
    lv_async_call(glass_settings_render_async, (void *)(intptr_t)GLASS_SETTINGS_HUB);
}

static void glass_settings_header(const char *title)
{
    lv_obj_t *back = create_settings_button(glass_settings_body, LV_SYMBOL_LEFT "  BACK",
                                             glass_settings_back_clicked, false);
    lv_obj_set_size(back, 118, 46);
    lv_obj_set_pos(back, 16, 10);
    lv_obj_add_flag(back, LV_OBJ_FLAG_FLOATING);

    lv_obj_t *label = lv_label_create(glass_settings_body);
    lv_label_set_text(label, title);
    lv_obj_set_style_text_color(label, COLOR_TEXT_PRIMARY, 0);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_24, 0);
    lv_obj_align(label, LV_ALIGN_TOP_MID, 0, 18);
    lv_obj_add_flag(label, LV_OBJ_FLAG_FLOATING);
}

static void glass_settings_build_hub(void)
{
    lv_obj_t *title = lv_label_create(glass_settings_body);
    lv_label_set_text(title, "SETTINGS");
    lv_obj_set_style_text_color(title, COLOR_TEXT_PRIMARY, 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 12);

    typedef struct {
        const char *title;
        const char *hint;
        bool screen;
        intptr_t target;
    } tile_t;
    static const tile_t tile[] = {
        { "Home",    "Widgets",        true,  GLASS_SCREEN_HOME },
        { "Blocks",  "Chain status",   true,  GLASS_SCREEN_BLOCK },
        { "Mempool", "Fees + queue",   true,  GLASS_SCREEN_MEMPOOL },
        { "Clock",   "Time display",   true,  GLASS_SCREEN_CLOCK },
        { "Price",   "Exchange rate",  true,  GLASS_SCREEN_PRICE },
        { "Odds",    "Solo mining",    true,  GLASS_SCREEN_ODDS },
        { "Feed",    "Recent updates", true,  GLASS_SCREEN_FEED },
        { "Wi-Fi",   "Network setup",  true,  GLASS_SCREEN_WIFI },
        { "Style",   "Orange + Glass", false, GLASS_SETTINGS_STYLE },
        { "Pool",    "AxeOS + latency",false, GLASS_SETTINGS_POOL },
        { "Display", "Brightness",     false, GLASS_SETTINGS_DISPLAY },
        { "System",  "Miner + firmware",false,GLASS_SETTINGS_SYSTEM },
    };

    const int x0 = 15, y0 = 48, w = 173, h = 96, gx = 10, gy = 10;
    for (int i = 0; i < 12; i++) {
        glass_settings_action(glass_settings_body, tile[i].title, tile[i].hint,
                              x0 + (i % 4) * (w + gx),
                              y0 + (i / 4) * (h + gy), w, h,
                              tile[i].screen ? glass_settings_screen_clicked
                                             : glass_settings_page_clicked,
                              (void *)tile[i].target);
    }
}

static void glass_settings_orange_clicked(lv_event_t *e)
{
    (void)e;
    lv_async_call(settings_apply_theme_async, (void *)(intptr_t)1);
}

static void glass_settings_wallpaper_clicked(lv_event_t *e)
{
    glass_set_wallpaper((int)(intptr_t)lv_event_get_user_data(e));
}

static void glass_settings_build_style(void)
{
    glass_settings_header("STYLE");

    lv_obj_t *accent = glass_settings_card(glass_settings_body, 18, 68, 716, 94);
    lv_obj_t *accent_title = lv_label_create(accent);
    lv_label_set_text(accent_title, "Accent");
    lv_obj_set_style_text_color(accent_title, COLOR_TEXT_PRIMARY, 0);
    lv_obj_set_style_text_font(accent_title, &lv_font_montserrat_18, 0);
    lv_obj_align(accent_title, LV_ALIGN_LEFT_MID, 18, 0);
    theme_dropdown = lv_dropdown_create(accent);
    lv_obj_set_size(theme_dropdown, 290, 54);
    lv_obj_align(theme_dropdown, LV_ALIGN_LEFT_MID, 112, 0);
    lv_dropdown_set_options(theme_dropdown, theme_dropdown_options());
    lv_dropdown_set_selected(theme_dropdown, theme_get_index());
    glass_style_dropdown(theme_dropdown);
    lv_obj_add_event_cb(theme_dropdown, settings_theme_changed, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_t *orange = create_settings_button(accent, "BITCOIN ORANGE",
                                               glass_settings_orange_clicked,
                                               theme_get_index() == 1);
    lv_obj_set_size(orange, 250, 54);
    lv_obj_align(orange, LV_ALIGN_RIGHT_MID, -16, 0);

    lv_obj_t *walls = glass_settings_card(glass_settings_body, 18, 174, 716, 116);
    lv_obj_t *wall_title = lv_label_create(walls);
    lv_label_set_text(wall_title, "Wallpaper");
    lv_obj_set_style_text_color(wall_title, COLOR_TEXT_PRIMARY, 0);
    lv_obj_set_style_text_font(wall_title, &lv_font_montserrat_18, 0);
    lv_obj_set_pos(wall_title, 16, 10);
    int count = wallpaper_count();
    for (int i = 0; i < count && i < 3; i++) {
        lv_obj_t *btn = create_settings_button(walls, wallpaper_name(i),
                                                NULL,
                                                glass_get_wallpaper() == i);
        lv_obj_set_size(btn, 214, 54);
        lv_obj_set_pos(btn, 16 + i * 230, 48);
        lv_obj_add_event_cb(btn, glass_settings_wallpaper_clicked, LV_EVENT_CLICKED,
                            (void *)(intptr_t)i);
    }

    lv_obj_t *skin = glass_settings_card(glass_settings_body, 18, 302, 716, 84);
    lv_obj_t *skin_title = lv_label_create(skin);
    lv_label_set_text(skin_title, "Interface style");
    lv_obj_set_style_text_color(skin_title, COLOR_TEXT_PRIMARY, 0);
    lv_obj_set_style_text_font(skin_title, &lv_font_montserrat_18, 0);
    lv_obj_align(skin_title, LV_ALIGN_LEFT_MID, 18, 0);
    skin_dropdown = lv_dropdown_create(skin);
    lv_obj_set_size(skin_dropdown, 260, 54);
    lv_obj_align(skin_dropdown, LV_ALIGN_RIGHT_MID, -16, 0);
    lv_dropdown_set_options(skin_dropdown, "Classic\nGlass");
    lv_dropdown_set_selected(skin_dropdown, (uint16_t)theme_get_skin());
    glass_style_dropdown(skin_dropdown);
    lv_obj_add_event_cb(skin_dropdown, settings_skin_changed, LV_EVENT_VALUE_CHANGED, NULL);
}

static void glass_settings_pool_refresh_cb(lv_timer_t *timer)
{
    (void)timer;
    int n = poolping_count();
    for (int rank = 0; rank < n && rank < 8; rank++) {
        if (!glass_pool_values[rank]) continue;
        int idx = poolping_ranked(rank);
        int ms = poolping_latency_ms(idx);
        if (ms == POOLPING_PENDING) lv_label_set_text(glass_pool_values[rank], "...");
        else if (ms == POOLPING_FAILED) lv_label_set_text(glass_pool_values[rank], "NO REPLY");
        else lv_label_set_text_fmt(glass_pool_values[rank], "%d ms", ms);
    }
}

static void glass_settings_build_pool(void)
{
    glass_settings_header("POOL");
    const home_stats_t *stats = home_stats();

    lv_obj_t *current = glass_settings_card(glass_settings_body, 18, 68, 326, 318);
    lv_obj_t *cap = lv_label_create(current);
    lv_label_set_text(cap, "CURRENT POOL");
    lv_obj_set_style_text_color(cap, COLOR_TEXT_SECONDARY, 0);
    lv_obj_set_style_text_font(cap, &lv_font_montserrat_14, 0);
    lv_obj_set_pos(cap, 16, 14);
    lv_obj_t *url = lv_label_create(current);
    lv_label_set_text(url, stats && stats->pool && stats->pool->url[0]
                                ? stats->pool->url : "Waiting for miner");
    lv_label_set_long_mode(url, LV_LABEL_LONG_DOT);
    lv_obj_set_width(url, 292);
    lv_obj_set_style_text_color(url, COLOR_TEXT_PRIMARY, 0);
    lv_obj_set_style_text_font(url, &lv_font_montserrat_20, 0);
    lv_obj_set_pos(url, 16, 40);
    lv_obj_t *port = lv_label_create(current);
    lv_label_set_text_fmt(port, "Port %s", stats && stats->pool && stats->pool->port[0]
                                             ? stats->pool->port : "--");
    lv_obj_set_style_text_color(port, COLOR_TEXT_SECONDARY, 0);
    lv_obj_set_style_text_font(port, &lv_font_montserrat_14, 0);
    lv_obj_set_pos(port, 16, 68);

    const char *ip = wifi_get_current_ip();
    bool ip_ok = ip && ip[0] && strcmp(ip, "0.0.0.0") != 0;
    lv_obj_t *qr_bg = lv_obj_create(current);
    lv_obj_set_size(qr_bg, 148, 148);
    lv_obj_set_pos(qr_bg, 16, 102);
    lv_obj_set_style_bg_color(qr_bg, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(qr_bg, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(qr_bg, 0, 0);
    lv_obj_set_style_radius(qr_bg, 12, 0);
    lv_obj_set_style_pad_all(qr_bg, 0, 0);
    lv_obj_clear_flag(qr_bg, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    if (ip_ok) {
        char dest[96];
        snprintf(dest, sizeof(dest), "http://%s/#/pool", ip);
        lv_obj_t *qr = lv_qrcode_create(qr_bg, 126, lv_color_black(), lv_color_white());
        lv_qrcode_update(qr, dest, strlen(dest));
        lv_obj_center(qr);
    } else {
        lv_obj_t *missing = lv_label_create(qr_bg);
        lv_label_set_text(missing, "NO IP");
        lv_obj_set_style_text_color(missing, lv_color_black(), 0);
        lv_obj_set_style_text_font(missing, &lv_font_montserrat_18, 0);
        lv_obj_center(missing);
    }
    lv_obj_t *qr_hint = lv_label_create(current);
    lv_label_set_text(qr_hint, ip_ok ? "Scan to edit\nin AxeOS" : "Connect Wi-Fi\nto edit");
    lv_obj_set_style_text_color(qr_hint, COLOR_TEXT_SECONDARY, 0);
    lv_obj_set_style_text_font(qr_hint, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_align(qr_hint, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(qr_hint, 130);
    lv_obj_set_pos(qr_hint, 178, 148);
    lv_obj_t *privacy = lv_label_create(current);
    lv_label_set_text(privacy, "Payout address stays private on this display.");
    lv_label_set_long_mode(privacy, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(privacy, 290);
    lv_obj_set_style_text_color(privacy, COLOR_TEXT_SECONDARY, 0);
    lv_obj_set_style_text_font(privacy, &lv_font_montserrat_12, 0);
    lv_obj_set_pos(privacy, 16, 268);

    lv_obj_t *latency = glass_settings_card(glass_settings_body, 356, 68, 378, 318);
    lv_obj_t *lat_title = lv_label_create(latency);
    lv_label_set_text(lat_title, "POOL LATENCY");
    lv_obj_set_style_text_color(lat_title, COLOR_TEXT_SECONDARY, 0);
    lv_obj_set_style_text_font(lat_title, &lv_font_montserrat_14, 0);
    lv_obj_set_pos(lat_title, 16, 14);
    int n = poolping_count();
    for (int rank = 0; rank < n && rank < 6; rank++) {
        int idx = poolping_ranked(rank);
        const pool_entry_t *entry = poolping_entry(idx);
        lv_obj_t *row = lv_obj_create(latency);
        lv_obj_set_size(row, 346, 42);
        lv_obj_set_pos(row, 16, 42 + rank * 45);
        lv_obj_set_style_bg_color(row, lv_color_white(), 0);
        lv_obj_set_style_bg_opa(row, LV_OPA_10, 0);
        lv_obj_set_style_border_width(row, 0, 0);
        lv_obj_set_style_radius(row, 10, 0);
        lv_obj_set_style_pad_all(row, 0, 0);
        lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
        lv_obj_t *name = lv_label_create(row);
        lv_label_set_text(name, entry ? entry->label : "...");
        lv_obj_set_width(name, 225);
        lv_label_set_long_mode(name, LV_LABEL_LONG_DOT);
        lv_obj_set_style_text_color(name, COLOR_TEXT_PRIMARY, 0);
        lv_obj_set_style_text_font(name, &lv_font_montserrat_14, 0);
        lv_obj_align(name, LV_ALIGN_LEFT_MID, 12, 0);
        glass_pool_values[rank] = lv_label_create(row);
        lv_label_set_text(glass_pool_values[rank], "...");
        lv_obj_set_style_text_color(glass_pool_values[rank], COLOR_ACCENT, 0);
        lv_obj_set_style_text_font(glass_pool_values[rank], &lv_font_montserrat_14, 0);
        lv_obj_align(glass_pool_values[rank], LV_ALIGN_RIGHT_MID, -12, 0);
    }
    poolping_refresh_now();
    glass_settings_pool_refresh_cb(NULL);
    glass_pool_timer = lv_timer_create(glass_settings_pool_refresh_cb, 1000, NULL);
}

static void glass_settings_dim_changed(lv_event_t *e)
{
    (void)e;
    if (!display_dim_slider || !display_dim_value_label) return;
    int value = lv_slider_get_value(display_dim_slider);
    lv_label_set_text_fmt(display_dim_value_label, "%d%%", value);
}

static void glass_settings_dim_released(lv_event_t *e)
{
    settings_display_schedule_changed(e);
}

static void glass_settings_build_display(void)
{
    glass_settings_header("DISPLAY");
    display_control_config_t cfg;
    display_control_get_config(&cfg);

    lv_obj_t *bright = glass_settings_card(glass_settings_body, 18, 68, 716, 82);
    lv_obj_t *bright_title = lv_label_create(bright);
    lv_label_set_text(bright_title, "Normal brightness");
    lv_obj_set_style_text_color(bright_title, COLOR_TEXT_PRIMARY, 0);
    lv_obj_set_style_text_font(bright_title, &lv_font_montserrat_18, 0);
    lv_obj_set_pos(bright_title, 16, 10);
    brightness_slider = lv_slider_create(bright);
    lv_obj_set_size(brightness_slider, 420, 22);
    lv_obj_set_ext_click_area(brightness_slider, 14);
    lv_obj_set_pos(brightness_slider, 190, 30);
    lv_slider_set_range(brightness_slider, 5, 100);
    lv_slider_set_value(brightness_slider, current_settings.brightness_percent, LV_ANIM_OFF);
    glass_style_slider(brightness_slider);
    lv_obj_add_event_cb(brightness_slider, settings_brightness_slider_changed, LV_EVENT_VALUE_CHANGED, NULL);
    brightness_value_label = lv_label_create(bright);
    lv_label_set_text_fmt(brightness_value_label, "%d%%", current_settings.brightness_percent);
    lv_obj_set_style_text_color(brightness_value_label, COLOR_ACCENT, 0);
    lv_obj_set_style_text_font(brightness_value_label, &lv_font_montserrat_20, 0);
    lv_obj_align(brightness_value_label, LV_ALIGN_RIGHT_MID, -14, 0);

    display_section = glass_settings_card(glass_settings_body, 18, 162, 716, 150);
    display_schedule_checkbox = settings_toggle_row(display_section,
                                                     "Scheduled brightness reduction",
                                                     8, 680, 54, true);
    if (cfg.schedule_enabled) lv_obj_add_state(display_schedule_checkbox, LV_STATE_CHECKED);
    lv_obj_add_event_cb(display_schedule_checkbox, settings_display_schedule_changed,
                        LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_t *from = lv_label_create(display_section);
    lv_label_set_text(from, "DIM FROM");
    lv_obj_set_style_text_color(from, COLOR_TEXT_SECONDARY, 0);
    lv_obj_set_style_text_font(from, &lv_font_montserrat_12, 0);
    lv_obj_set_pos(from, 16, 72);
    display_off_dropdown = lv_dropdown_create(display_section);
    lv_obj_set_size(display_off_dropdown, 190, 50);
    lv_obj_set_pos(display_off_dropdown, 16, 92);
    lv_dropdown_set_options(display_off_dropdown, display_time_options);
    lv_dropdown_set_selected(display_off_dropdown, cfg.off_minute / 30U);
    glass_style_dropdown(display_off_dropdown);
    lv_obj_add_event_cb(display_off_dropdown, settings_display_schedule_changed, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_t *until = lv_label_create(display_section);
    lv_label_set_text(until, "FULL FROM");
    lv_obj_set_style_text_color(until, COLOR_TEXT_SECONDARY, 0);
    lv_obj_set_style_text_font(until, &lv_font_montserrat_12, 0);
    lv_obj_set_pos(until, 220, 72);
    display_on_dropdown = lv_dropdown_create(display_section);
    lv_obj_set_size(display_on_dropdown, 190, 50);
    lv_obj_set_pos(display_on_dropdown, 220, 92);
    lv_dropdown_set_options(display_on_dropdown, display_time_options);
    lv_dropdown_set_selected(display_on_dropdown, cfg.on_minute / 30U);
    glass_style_dropdown(display_on_dropdown);
    lv_obj_add_event_cb(display_on_dropdown, settings_display_schedule_changed, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_t *dim = lv_label_create(display_section);
    lv_label_set_text(dim, "DIM LEVEL");
    lv_obj_set_style_text_color(dim, COLOR_TEXT_SECONDARY, 0);
    lv_obj_set_style_text_font(dim, &lv_font_montserrat_12, 0);
    lv_obj_set_pos(dim, 428, 72);
    display_dim_slider = lv_slider_create(display_section);
    lv_obj_set_size(display_dim_slider, 190, 22);
    lv_obj_set_ext_click_area(display_dim_slider, 14);
    lv_obj_set_pos(display_dim_slider, 428, 108);
    lv_slider_set_range(display_dim_slider, 5, 60);
    lv_slider_set_value(display_dim_slider, cfg.dim_percent, LV_ANIM_OFF);
    glass_style_slider(display_dim_slider);
    lv_obj_add_event_cb(display_dim_slider, glass_settings_dim_changed, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(display_dim_slider, glass_settings_dim_released, LV_EVENT_RELEASED, NULL);
    display_dim_value_label = lv_label_create(display_section);
    lv_label_set_text_fmt(display_dim_value_label, "%u%%", (unsigned)cfg.dim_percent);
    lv_obj_set_style_text_color(display_dim_value_label, COLOR_ACCENT, 0);
    lv_obj_set_style_text_font(display_dim_value_label, &lv_font_montserrat_16, 0);
    lv_obj_set_pos(display_dim_value_label, 632, 104);

    lv_obj_t *power = glass_settings_card(glass_settings_body, 18, 324, 716, 62);
    lv_obj_t *power_title = lv_label_create(power);
    lv_label_set_text(power_title, "Display-off shortcut");
    lv_obj_set_style_text_color(power_title, COLOR_TEXT_PRIMARY, 0);
    lv_obj_set_style_text_font(power_title, &lv_font_montserrat_16, 0);
    lv_obj_align(power_title, LV_ALIGN_LEFT_MID, 16, 0);
    display_corner_dropdown = lv_dropdown_create(power);
    lv_obj_set_size(display_corner_dropdown, 320, 48);
    lv_obj_align(display_corner_dropdown, LV_ALIGN_RIGHT_MID, -10, 0);
    lv_dropdown_set_options(display_corner_dropdown, display_corner_options);
    lv_dropdown_set_selected(display_corner_dropdown,
        (uint16_t)display_button_mode_from_config(cfg.power_button_corner,
                                                 cfg.power_button_visuals_visible));
    glass_style_dropdown(display_corner_dropdown);
    lv_obj_add_event_cb(display_corner_dropdown, settings_display_schedule_changed, LV_EVENT_VALUE_CHANGED, NULL);
    display_schedule_status = lv_label_create(power);
    lv_label_set_text(display_schedule_status, "");
    lv_obj_add_flag(display_schedule_status, LV_OBJ_FLAG_HIDDEN);
    settings_refresh_schedule_status();
    display_schedule_timer = lv_timer_create(settings_schedule_timer_cb, 2000, NULL);
}

static void glass_settings_build_system(void)
{
    glass_settings_header("SYSTEM");
    lv_obj_t *perf = glass_settings_card(glass_settings_body, 18, 68, 716, 92);
    lv_obj_t *perf_title = lv_label_create(perf);
    lv_label_set_text(perf_title, "Performance");
    lv_obj_set_style_text_color(perf_title, COLOR_TEXT_PRIMARY, 0);
    lv_obj_set_style_text_font(perf_title, &lv_font_montserrat_18, 0);
    lv_obj_set_pos(perf_title, 16, 10);
    performance_low_btn = create_settings_button(perf, "LOW", settings_performance_low_clicked,
                                                  current_settings.performance_mode == PERFORMANCE_LOW);
    performance_medium_btn = create_settings_button(perf, "MEDIUM", settings_performance_medium_clicked,
                                                     current_settings.performance_mode == PERFORMANCE_MEDIUM);
    performance_high_btn = create_settings_button(perf, "HIGH", settings_performance_high_clicked,
                                                   current_settings.performance_mode == PERFORMANCE_HIGH);
    lv_obj_set_size(performance_low_btn, 205, 48);
    lv_obj_set_size(performance_medium_btn, 205, 48);
    lv_obj_set_size(performance_high_btn, 205, 48);
    lv_obj_set_pos(performance_low_btn, 16, 38);
    lv_obj_set_pos(performance_medium_btn, 246, 38);
    lv_obj_set_pos(performance_high_btn, 476, 38);

    lv_obj_t *fan = glass_settings_card(glass_settings_body, 18, 172, 716, 92);
    auto_fan_checkbox = settings_toggle_row(fan, "Automatic fan control", 8, 330, 54, true);
    if (current_settings.auto_fan_control) lv_obj_add_state(auto_fan_checkbox, LV_STATE_CHECKED);
    lv_obj_add_event_cb(auto_fan_checkbox, settings_auto_fan_toggled, LV_EVENT_VALUE_CHANGED, NULL);
    fan_slider = lv_slider_create(fan);
    lv_obj_set_size(fan_slider, 170, 22);
    lv_obj_set_ext_click_area(fan_slider, 14);
    lv_obj_set_pos(fan_slider, 350, 35);
    lv_slider_set_range(fan_slider, 0, 100);
    lv_slider_set_value(fan_slider, current_settings.fan_speed_percent, LV_ANIM_OFF);
    glass_style_slider(fan_slider);
    lv_obj_add_event_cb(fan_slider, settings_fan_slider_changed, LV_EVENT_VALUE_CHANGED, NULL);
    fan_value_label = lv_label_create(fan);
    lv_label_set_text_fmt(fan_value_label, "%d%%", current_settings.fan_speed_percent);
    lv_obj_set_style_text_color(fan_value_label, COLOR_ACCENT, 0);
    lv_obj_set_style_text_font(fan_value_label, &lv_font_montserrat_16, 0);
    lv_obj_set_pos(fan_value_label, 528, 31);
    lv_obj_t *fan_apply = create_settings_button(fan, "APPLY", settings_fan_save_clicked, false);
    lv_obj_set_size(fan_apply, 110, 42);
    lv_obj_set_pos(fan_apply, 590, 25);

    ota_section = glass_settings_card(glass_settings_body, 18, 276, 716, 110);
    ota_version_label = lv_label_create(ota_section);
    lv_label_set_text_fmt(ota_version_label, "Firmware %s - %s", ota_get_current_version(),
                          ota_update_get_beta_enabled() ? "Beta" : "Stable");
    lv_obj_set_style_text_color(ota_version_label, COLOR_TEXT_PRIMARY, 0);
    lv_obj_set_style_text_font(ota_version_label, &lv_font_montserrat_18, 0);
    lv_obj_set_pos(ota_version_label, 16, 10);
    ota_status_label = lv_label_create(ota_section);
    lv_label_set_text(ota_status_label, "Ready for update");
    lv_obj_set_width(ota_status_label, 350);
    lv_label_set_long_mode(ota_status_label, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_color(ota_status_label, COLOR_TEXT_SECONDARY, 0);
    lv_obj_set_style_text_font(ota_status_label, &lv_font_montserrat_12, 0);
    lv_obj_set_pos(ota_status_label, 16, 40);
    ota_progress_bar = lv_bar_create(ota_section);
    lv_obj_set_size(ota_progress_bar, 330, 12);
    lv_obj_set_pos(ota_progress_bar, 16, 76);
    lv_bar_set_range(ota_progress_bar, 0, 100);
    lv_bar_set_value(ota_progress_bar, 0, LV_ANIM_OFF);
    glass_style_bar(ota_progress_bar);

    lv_obj_t *beta_sw = settings_toggle_row(ota_section, "Beta", 2, 166, 56, true);
    lv_obj_set_x(lv_obj_get_parent(beta_sw), 350);
    if (ota_update_get_beta_enabled()) lv_obj_add_state(beta_sw, LV_STATE_CHECKED);
    lv_obj_add_event_cb(beta_sw, settings_ota_beta_toggled, LV_EVENT_VALUE_CHANGED, NULL);

    ota_frequency_dropdown = lv_dropdown_create(ota_section);
    lv_obj_set_size(ota_frequency_dropdown, 170, 48);
    lv_obj_set_pos(ota_frequency_dropdown, 530, 6);
    lv_dropdown_set_options(ota_frequency_dropdown, ota_frequency_options);
    lv_dropdown_set_selected(ota_frequency_dropdown,
                             (uint16_t)ota_update_get_check_frequency());
    glass_style_dropdown(ota_frequency_dropdown);
    lv_obj_add_event_cb(ota_frequency_dropdown, settings_ota_frequency_changed,
                        LV_EVENT_VALUE_CHANGED, NULL);

    ota_update_btn = create_settings_button(ota_section, "UPDATE",
                                             settings_ota_update_clicked, false);
    lv_obj_set_size(ota_update_btn, 166, 44);
    lv_obj_set_pos(ota_update_btn, 350, 62);
    ota_restore_btn = create_settings_button(ota_section, "OFFICIAL RESTORE",
                                              settings_original_restore_clicked, false);
    lv_obj_set_size(ota_restore_btn, 170, 44);
    lv_obj_set_pos(ota_restore_btn, 530, 62);
    ota_restore_status_label = lv_label_create(ota_section);
    lv_label_set_text(ota_restore_status_label, "Official release: check before restoring");
    lv_obj_add_flag(ota_restore_status_label, LV_OBJ_FLAG_HIDDEN);
    ota_timer = lv_timer_create(ota_update_timer_cb, 500, NULL);
}

static void glass_settings_render(void)
{
    if (!glass_settings_body) return;
    glass_settings_stop_page_tasks();
    glass_settings_reset_refs();
    lv_obj_clean(glass_settings_body);
    lv_obj_scroll_to_y(glass_settings_body, 0, LV_ANIM_OFF);
    if (glass_settings_pane) lv_obj_scroll_to_y(glass_settings_pane, 0, LV_ANIM_OFF);
    switch (glass_settings_page) {
        case GLASS_SETTINGS_STYLE:   glass_settings_build_style();   break;
        case GLASS_SETTINGS_POOL:    glass_settings_build_pool();    break;
        case GLASS_SETTINGS_DISPLAY: glass_settings_build_display(); break;
        case GLASS_SETTINGS_SYSTEM:  glass_settings_build_system();  break;
        case GLASS_SETTINGS_HUB:
        default:                     glass_settings_build_hub();     break;
    }
    lv_obj_update_layout(glass_settings_body);
    lv_obj_scroll_to_y(glass_settings_body, 0, LV_ANIM_OFF);
    if (glass_settings_pane) lv_obj_scroll_to_y(glass_settings_pane, 0, LV_ANIM_OFF);
    glass_screen_ready(settings_screen);
}

static void glass_settings_screen_create(void)
{
    settings_screen = glass_screen_create(GLASS_SCREEN_SETTINGS, false);
    glass_settings_pane = glass_pane(settings_screen, SCREEN_WIDTH - 48, 406, 26);
    lv_obj_align(glass_settings_pane, LV_ALIGN_TOP_MID, 0, 18);
    lv_obj_clear_flag(glass_settings_pane, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(glass_settings_pane, LV_SCROLLBAR_MODE_OFF);
    glass_settings_body = lv_obj_create(glass_settings_pane);
    lv_obj_set_size(glass_settings_body, SCREEN_WIDTH - 48, 406);
    lv_obj_set_pos(glass_settings_body, 0, 0);
    lv_obj_set_style_bg_opa(glass_settings_body, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(glass_settings_body, 0, 0);
    lv_obj_set_style_pad_all(glass_settings_body, 0, 0);
    lv_obj_clear_flag(glass_settings_body, LV_OBJ_FLAG_SCROLLABLE);
    glass_settings_render();
}

void settings_glass_show_hub(void)
{
    glass_settings_page = GLASS_SETTINGS_HUB;
    if (glass_settings_body) glass_settings_render();
}

void settings_screen_create(void)
{
    if (settings_screen != NULL)
    {
        return;
    }

    settings_initialize();
    display_control_set_power_button_visible(false);

    if (glass_active()) {
        glass_settings_screen_create();
        return;
    }

    const bool glass = false;
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
        lv_obj_add_flag(main_cont, LV_OBJ_FLAG_SCROLLABLE |
                                   LV_OBJ_FLAG_SCROLL_MOMENTUM |
                                   LV_OBJ_FLAG_SCROLL_ELASTIC);
        /* Most of settings, including the way back to Classic, is below the
         * fold: a glass scrollbar says so. */
        lv_obj_set_scrollbar_mode(main_cont, LV_SCROLLBAR_MODE_ACTIVE);
        lv_obj_set_style_bg_color(main_cont, lv_color_white(), LV_PART_SCROLLBAR);
        lv_obj_set_style_bg_opa(main_cont, LV_OPA_40, LV_PART_SCROLLBAR);
        lv_obj_set_style_width(main_cont, 5, LV_PART_SCROLLBAR);
        lv_obj_set_style_radius(main_cont, 3, LV_PART_SCROLLBAR);
        lv_obj_set_style_pad_right(main_cont, 6, LV_PART_SCROLLBAR);
        lv_obj_set_scroll_dir(main_cont, LV_DIR_VER);
        lv_obj_set_style_pad_bottom(main_cont, 36, 0);
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
        lv_obj_add_flag(main_cont, LV_OBJ_FLAG_SCROLLABLE |
                                   LV_OBJ_FLAG_SCROLL_MOMENTUM |
                                   LV_OBJ_FLAG_SCROLL_ELASTIC);
        lv_obj_set_scrollbar_mode(main_cont, LV_SCROLLBAR_MODE_ACTIVE);
        lv_obj_set_style_bg_color(main_cont, COLOR_TEXT_SECONDARY, LV_PART_SCROLLBAR);
        lv_obj_set_style_bg_opa(main_cont, LV_OPA_50, LV_PART_SCROLLBAR);
        lv_obj_set_style_width(main_cont, 5, LV_PART_SCROLLBAR);
        lv_obj_set_style_radius(main_cont, 3, LV_PART_SCROLLBAR);
        lv_obj_set_style_pad_right(main_cont, 6, LV_PART_SCROLLBAR);
        lv_obj_set_scroll_dir(main_cont, LV_DIR_VER);
        lv_obj_set_style_pad_bottom(main_cont, 24, 0);
    }

    settings_main_cont = main_cont;

    /* The sections used to carry a hardcoded absolute y and height each, so
     * inserting one or growing one meant recomputing every offset below it by
     * hand. They are now a flow column: a section states its own height and
     * the order it appears in, and nothing else has to know. */
    lv_obj_set_flex_flow(main_cont, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(main_cont, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(main_cont, 10, 0);

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

    fan_section = lv_obj_create(main_cont);
    lv_obj_set_size(fan_section, 680, 210);
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
                                            26, 660, 56, glass);
    lv_obj_add_event_cb(auto_fan_checkbox, settings_auto_fan_toggled, LV_EVENT_VALUE_CHANGED, NULL);

    if (current_settings.auto_fan_control)
    {
        lv_obj_add_state(auto_fan_checkbox, LV_STATE_CHECKED);
    }

    /* Manual-only controls live in one disclosure group. Keeping the Apply
     * button outside preserves the existing explicit-save behaviour for both
     * automatic and manual modes. */
    fan_manual_cont = lv_obj_create(fan_section);
    lv_obj_set_size(fan_manual_cont, 660, 44);
    lv_obj_align(fan_manual_cont, LV_ALIGN_TOP_LEFT, 0, 94);
    lv_obj_set_style_bg_opa(fan_manual_cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(fan_manual_cont, 0, 0);
    lv_obj_set_style_pad_all(fan_manual_cont, 0, 0);
    lv_obj_clear_flag(fan_manual_cont, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);

    fan_slider = lv_slider_create(fan_manual_cont);
    lv_obj_set_size(fan_slider, 420, 20);
    lv_obj_set_ext_click_area(fan_slider, 12);
    lv_obj_align(fan_slider, LV_ALIGN_TOP_LEFT, 0, 12);
    lv_slider_set_range(fan_slider, 0, 100);
    lv_slider_set_value(fan_slider, current_settings.fan_speed_percent, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(fan_slider, COLOR_CARD_BG, LV_PART_MAIN);
    lv_obj_set_style_bg_color(fan_slider, COLOR_ACCENT, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(fan_slider, COLOR_ACCENT, LV_PART_KNOB);
    lv_obj_add_event_cb(fan_slider, settings_fan_slider_changed, LV_EVENT_VALUE_CHANGED, NULL);
    if (glass) glass_style_slider(fan_slider);

    fan_value_label = lv_label_create(fan_manual_cont);
    char fan_text[16];
    snprintf(fan_text, sizeof(fan_text), "%d%%", current_settings.fan_speed_percent);
    lv_label_set_text(fan_value_label, fan_text);
    lv_obj_set_style_text_color(fan_value_label, COLOR_TEXT_PRIMARY, 0);
    lv_obj_set_style_text_font(fan_value_label, &lv_font_montserrat_16, 0);
    lv_obj_align(fan_value_label, LV_ALIGN_TOP_LEFT, 450, 14);

    fan_save_btn = create_settings_button(fan_section, "APPLY FAN MODE", settings_fan_save_clicked, false);
    lv_obj_set_size(fan_save_btn, 220, 44);
    lv_obj_align(fan_save_btn, LV_ALIGN_TOP_LEFT, 0, 146);

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
    lv_obj_set_ext_click_area(brightness_slider, 12);
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
    lv_obj_set_size(theme_dropdown, 300, 44);
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
    lv_obj_set_size(skin_dropdown, 150, 44);
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
    lv_obj_set_size(timezone_dropdown, 300, 44);
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
    lv_obj_set_size(data_source_dropdown, 220, 44);
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
    lv_obj_set_size(currency_dropdown, 130, 44);
    lv_obj_align(currency_dropdown, LV_ALIGN_TOP_LEFT, 500, -4);
    lv_dropdown_set_options(currency_dropdown, "USD\nAUD\nNZD\nGBP\nEUR\nCAD\nJPY");
    lv_dropdown_set_selected(currency_dropdown, (uint16_t) chain_get_ccy());
    style_settings_dropdown(currency_dropdown, &lv_font_montserrat_16);
    lv_obj_add_event_cb(currency_dropdown, settings_currency_changed, LV_EVENT_VALUE_CHANGED, NULL);
    if (glass) glass_style_dropdown(currency_dropdown);

    display_control_config_t display_config;
    display_control_get_config(&display_config);

    display_section = lv_obj_create(main_cont);
    lv_obj_set_size(display_section, 680, 300);
    lv_obj_set_style_bg_opa(display_section, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(display_section, 0, 0);
    lv_obj_set_style_pad_all(display_section, 10, 0);
    lv_obj_clear_flag(display_section, LV_OBJ_FLAG_SCROLLABLE);
    if (glass) lv_obj_clear_flag(display_section, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t *display_title = lv_label_create(display_section);
    lv_label_set_text(display_title, "Brightness Schedule:");
    lv_obj_set_style_text_color(display_title, COLOR_TEXT_PRIMARY, 0);
    lv_obj_set_style_text_font(display_title, &lv_font_montserrat_18, 0);
    lv_obj_align(display_title, LV_ALIGN_TOP_LEFT, 0, 0);

    display_schedule_checkbox = settings_toggle_row(display_section,
                                                    "Reduce brightness daily",
                                                    30, 660, 56, glass);

    display_schedule_status = lv_label_create(display_section);
    lv_obj_set_style_text_color(display_schedule_status, COLOR_TEXT_SECONDARY, 0);
    lv_obj_set_style_text_font(display_schedule_status, &lv_font_montserrat_14, 0);
    lv_obj_align(display_schedule_status, LV_ALIGN_TOP_LEFT, 2, 94);

    display_schedule_timer = lv_timer_create(settings_schedule_timer_cb, 2000, NULL);
    if (display_config.schedule_enabled) {
        lv_obj_add_state(display_schedule_checkbox, LV_STATE_CHECKED);
    }
    lv_obj_add_event_cb(display_schedule_checkbox, settings_display_schedule_changed, LV_EVENT_VALUE_CHANGED, NULL);
    settings_refresh_schedule_status();

    schedule_details_cont = lv_obj_create(display_section);
    lv_obj_set_size(schedule_details_cont, 660, 68);
    lv_obj_align(schedule_details_cont, LV_ALIGN_TOP_LEFT, 0, 122);
    lv_obj_set_style_bg_opa(schedule_details_cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(schedule_details_cont, 0, 0);
    lv_obj_set_style_pad_all(schedule_details_cont, 0, 0);
    lv_obj_clear_flag(schedule_details_cont, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *off_label = lv_label_create(schedule_details_cont);
    lv_label_set_text(off_label, "Dims at");
    lv_obj_set_style_text_color(off_label, COLOR_TEXT_PRIMARY, 0);
    lv_obj_set_style_text_font(off_label, &lv_font_montserrat_16, 0);
    lv_obj_align(off_label, LV_ALIGN_TOP_LEFT, 0, 0);

    display_off_dropdown = lv_dropdown_create(schedule_details_cont);
    lv_obj_set_size(display_off_dropdown, 300, 44);
    lv_obj_align(display_off_dropdown, LV_ALIGN_TOP_LEFT, 0, 24);
    lv_dropdown_set_options(display_off_dropdown, display_time_options);
    lv_dropdown_set_selected(display_off_dropdown, display_config.off_minute / 30U);
    style_settings_dropdown(display_off_dropdown, &lv_font_montserrat_14);
    lv_obj_add_event_cb(display_off_dropdown, settings_display_schedule_changed, LV_EVENT_VALUE_CHANGED, NULL);
    if (glass) glass_style_dropdown(display_off_dropdown);

    lv_obj_t *on_label = lv_label_create(schedule_details_cont);
    lv_label_set_text(on_label, "Full brightness at");
    lv_obj_set_style_text_color(on_label, COLOR_TEXT_PRIMARY, 0);
    lv_obj_set_style_text_font(on_label, &lv_font_montserrat_16, 0);
    lv_obj_align(on_label, LV_ALIGN_TOP_LEFT, 340, 0);

    display_on_dropdown = lv_dropdown_create(schedule_details_cont);
    lv_obj_set_size(display_on_dropdown, 300, 44);
    lv_obj_align(display_on_dropdown, LV_ALIGN_TOP_LEFT, 340, 24);
    lv_dropdown_set_options(display_on_dropdown, display_time_options);
    lv_dropdown_set_selected(display_on_dropdown, display_config.on_minute / 30U);
    style_settings_dropdown(display_on_dropdown, &lv_font_montserrat_14);
    lv_obj_add_event_cb(display_on_dropdown, settings_display_schedule_changed, LV_EVENT_VALUE_CHANGED, NULL);
    if (glass) glass_style_dropdown(display_on_dropdown);

    display_corner_label = lv_label_create(display_section);
    lv_label_set_text(display_corner_label, "Display-off button");
    lv_obj_set_style_text_color(display_corner_label, COLOR_TEXT_PRIMARY, 0);
    lv_obj_set_style_text_font(display_corner_label, &lv_font_montserrat_16, 0);
    lv_obj_align(display_corner_label, LV_ALIGN_TOP_LEFT, 0, 202);

    display_corner_dropdown = lv_dropdown_create(display_section);
    lv_obj_set_size(display_corner_dropdown, 300, 44);
    lv_obj_align(display_corner_dropdown, LV_ALIGN_TOP_LEFT, 0, 226);
    lv_dropdown_set_options(display_corner_dropdown, display_corner_options);
    lv_dropdown_set_selected(display_corner_dropdown,
                             (uint16_t)display_button_mode_from_config(
                                 display_config.power_button_corner,
                                 display_config.power_button_visuals_visible));
    style_settings_dropdown(display_corner_dropdown, &lv_font_montserrat_14);
    lv_obj_add_event_cb(display_corner_dropdown, settings_display_schedule_changed, LV_EVENT_VALUE_CHANGED, NULL);
    if (glass) glass_style_dropdown(display_corner_dropdown);

    display_corner_hint = lv_label_create(display_section);
    lv_label_set_text(display_corner_hint, "Hidden keeps the selected corner tappable");
    lv_obj_set_style_text_color(display_corner_hint, COLOR_TEXT_SECONDARY, 0);
    lv_obj_set_style_text_font(display_corner_hint, &lv_font_montserrat_14, 0);
    lv_obj_set_width(display_corner_hint, 650);
    lv_label_set_long_mode(display_corner_hint, LV_LABEL_LONG_WRAP);
    lv_obj_align(display_corner_hint, LV_ALIGN_TOP_LEFT, 0, 280);
    settings_layout_schedule_controls();

    // OTA Update Section
    ota_section = lv_obj_create(main_cont);
    lv_obj_set_size(ota_section, 680, 366);
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
    snprintf(version_text, sizeof(version_text), "Current: %s - %s",
             version ? version : "Unknown",
             ota_update_get_beta_enabled() ? "Beta channel" : "Stable channel");
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
    lv_obj_set_size(ota_update_btn, 240, 44);
    lv_obj_align(ota_update_btn, LV_ALIGN_TOP_LEFT, 0, 110);

    lv_obj_t *beta_sw = settings_toggle_row(ota_section, "Include beta releases",
                                             164, 660, 56, glass);
    if (ota_update_get_beta_enabled()) lv_obj_add_state(beta_sw, LV_STATE_CHECKED);
    lv_obj_add_event_cb(beta_sw, settings_ota_beta_toggled, LV_EVENT_VALUE_CHANGED, NULL);

    lv_obj_t *frequency_label = lv_label_create(ota_section);
    lv_label_set_text(frequency_label, "Automatic check frequency");
    lv_obj_set_style_text_color(frequency_label, COLOR_TEXT_PRIMARY, 0);
    lv_obj_set_style_text_font(frequency_label, &lv_font_montserrat_16, 0);
    lv_obj_align(frequency_label, LV_ALIGN_TOP_LEFT, 16, 240);
    ota_frequency_dropdown = lv_dropdown_create(ota_section);
    lv_obj_set_size(ota_frequency_dropdown, 220, 48);
    lv_obj_align(ota_frequency_dropdown, LV_ALIGN_TOP_RIGHT, -10, 224);
    lv_dropdown_set_options(ota_frequency_dropdown, ota_frequency_options);
    lv_dropdown_set_selected(ota_frequency_dropdown,
                             (uint16_t)ota_update_get_check_frequency());
    style_settings_dropdown(ota_frequency_dropdown, &lv_font_montserrat_16);
    lv_obj_add_event_cb(ota_frequency_dropdown, settings_ota_frequency_changed,
                        LV_EVENT_VALUE_CHANGED, NULL);

    restore_disclosure_btn = create_settings_button(ota_section,
                                                     "RESTORE ORIGINAL FIRMWARE...",
                                                     settings_restore_disclosure_clicked, false);
    lv_obj_set_size(restore_disclosure_btn, 660, 56);
    lv_obj_align(restore_disclosure_btn, LV_ALIGN_TOP_LEFT, 0, 290);

    restore_details_cont = lv_obj_create(ota_section);
    lv_obj_set_size(restore_details_cont, 660, 140);
    lv_obj_align(restore_details_cont, LV_ALIGN_TOP_LEFT, 0, 356);
    lv_obj_set_style_bg_opa(restore_details_cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(restore_details_cont, 0, 0);
    lv_obj_set_style_pad_all(restore_details_cont, 0, 0);
    lv_obj_clear_flag(restore_details_cont, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *restore_title = lv_label_create(restore_details_cont);
    lv_label_set_text(restore_title, "Restore original bitaxeorg firmware");
    lv_obj_set_style_text_color(restore_title, COLOR_TEXT_PRIMARY, 0);
    lv_obj_set_style_text_font(restore_title, &lv_font_montserrat_18, 0);
    lv_obj_align(restore_title, LV_ALIGN_TOP_LEFT, 0, 0);

    ota_restore_status_label = lv_label_create(restore_details_cont);
    lv_label_set_text(ota_restore_status_label, "Official release: check before restoring");
    lv_obj_set_style_text_color(ota_restore_status_label, COLOR_TEXT_SECONDARY, 0);
    lv_obj_set_style_text_font(ota_restore_status_label, &lv_font_montserrat_14, 0);
    lv_obj_align(ota_restore_status_label, LV_ALIGN_TOP_LEFT, 0, 28);

    ota_restore_btn = create_settings_button(restore_details_cont, "CHECK OFFICIAL RELEASE",
                                             settings_original_restore_clicked, false);
    lv_obj_set_size(ota_restore_btn, 270, 44);
    lv_obj_align(ota_restore_btn, LV_ALIGN_TOP_LEFT, 0, 54);

    lv_obj_t *restore_hint = lv_label_create(restore_details_cont);
    lv_label_set_text(restore_hint,
                      "Fetches and pins the latest official OTA image. A confirmation explains rollback compatibility before install.");
    lv_label_set_long_mode(restore_hint, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(restore_hint, 650);
    lv_obj_set_style_text_color(restore_hint, COLOR_TEXT_SECONDARY, 0);
    lv_obj_set_style_text_font(restore_hint, &lv_font_montserrat_14, 0);
    lv_obj_align(restore_hint, LV_ALIGN_TOP_LEFT, 0, 106);
    lv_obj_add_flag(restore_details_cont, LV_OBJ_FLAG_HIDDEN);
    restore_details_expanded = false;

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
    /* This modal increments the global overlay depth. Balance it before its
     * parent screen is deleted during navigation or a theme/skin rebuild. */
    settings_restore_overlay_close(NULL);
    /* Stop page-owned timers before deleting the labels they update. */
    glass_settings_stop_page_tasks();
    memset(glass_pool_values, 0, sizeof(glass_pool_values));
    display_schedule_status = NULL;
    settings_main_cont = NULL;
    // Clean up Easter egg overlay if showing
    if (sys_overlay) {
        lv_obj_del(sys_overlay);
        sys_overlay = NULL;
    }
    diag_counter = 0;

    if (settings_screen)
    {
        glass_screen_detach(settings_screen);
        lv_obj_del(settings_screen);
        settings_screen = NULL;
        performance_low_btn = NULL;
        performance_medium_btn = NULL;
        performance_high_btn = NULL;
        auto_fan_checkbox = NULL;
        fan_section = NULL;
        fan_manual_cont = NULL;
        fan_slider = NULL;
        fan_value_label = NULL;
        fan_save_btn = NULL;
        brightness_slider = NULL;
        brightness_value_label = NULL;
        display_dim_slider = NULL;
        display_dim_value_label = NULL;
        glass_settings_body = NULL;
        glass_settings_pane = NULL;
        timezone_dropdown = NULL;
        theme_dropdown = NULL;
        display_schedule_checkbox = NULL;
        display_section = NULL;
        schedule_details_cont = NULL;
        display_off_dropdown = NULL;
        display_on_dropdown = NULL;
        display_corner_label = NULL;
        display_corner_dropdown = NULL;
        display_corner_hint = NULL;
        ota_update_btn = NULL;
        ota_status_label = NULL;
        ota_progress_bar = NULL;
        ota_version_label = NULL;
        ota_restore_btn = NULL;
        ota_restore_status_label = NULL;
        ota_section = NULL;
        restore_details_cont = NULL;
        restore_disclosure_btn = NULL;
        restore_details_expanded = false;
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
        lv_label_set_text(display_schedule_status, "Full brightness all day");
        return;
    }
    if (!display_control_time_is_set()) {
        lv_label_set_text(display_schedule_status,
                          "Waiting for network time before this can take effect");
        return;
    }
    lv_label_set_text(display_schedule_status,
                      display_control_is_dimmed() ? "Active: reduced brightness now"
                                                  : "Active: full brightness now");
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

static void settings_ota_beta_toggled(lv_event_t *e)
{
    ota_update_set_beta_enabled(
        lv_obj_has_state(lv_event_get_target(e), LV_STATE_CHECKED));
}

static void settings_ota_frequency_changed(lv_event_t *e)
{
    uint16_t selected = lv_dropdown_get_selected(lv_event_get_target(e));
    if (selected > OTA_CHECK_WEEKLY) selected = OTA_CHECK_MANUAL;
    ota_update_set_check_frequency((ota_check_frequency_t)selected);
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
    if (!current_settings.auto_fan_control && fan_manual_cont) {
        lv_obj_scroll_to_view_recursive(fan_manual_cont, LV_ANIM_ON);
    }
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

    // Apply the normal level immediately; an active schedule keeps its lower level.
    display_control_set_brightness((uint8_t)current_settings.brightness_percent);

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
    if (!display_schedule_checkbox || !display_off_dropdown || !display_on_dropdown ||
        !display_corner_dropdown) {
        return;
    }

    display_power_button_mode_t button_mode =
        (display_power_button_mode_t)lv_dropdown_get_selected(display_corner_dropdown);

    display_control_config_t existing;
    display_control_get_config(&existing);
    display_control_config_t config = {
        .schedule_enabled = lv_obj_has_state(display_schedule_checkbox, LV_STATE_CHECKED),
        .off_minute = (uint16_t)(lv_dropdown_get_selected(display_off_dropdown) * 30U),
        .on_minute = (uint16_t)(lv_dropdown_get_selected(display_on_dropdown) * 30U),
        .dim_percent = display_dim_slider
                         ? (uint8_t)lv_slider_get_value(display_dim_slider)
                         : existing.dim_percent,
        .power_button_corner = display_button_mode_corner(button_mode),
        .power_button_visuals_visible = display_button_mode_shows_visuals(button_mode),
    };

    esp_err_t err = display_control_set_config(&config);
    settings_refresh_schedule_status();
    settings_layout_schedule_controls();
    if (lv_event_get_target(e) == display_schedule_checkbox && config.schedule_enabled &&
        schedule_details_cont) {
        lv_obj_scroll_to_view_recursive(schedule_details_cont, LV_ANIM_ON);
    }
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
