#include "display_control.h"

#include <time.h>

#include "display_schedule.h"
#include "custom_fonts.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "lvgl.h"
#include "lwip/apps/sntp.h"
#include "nvs.h"
#include "waveshare_rgb_lcd_port.h"
#include "wifi.h"
#include "theme.h"

#define DISPLAY_NVS_NAMESPACE "settings"
#define DISPLAY_NVS_ENABLED_KEY "disp_sched"
#define DISPLAY_NVS_OFF_KEY "disp_off"
#define DISPLAY_NVS_ON_KEY "disp_on"
#define DISPLAY_NVS_CORNER_KEY "disp_corner"
#define DISPLAY_NVS_VISUALS_KEY "disp_icon"
#define DISPLAY_DEFAULT_OFF_MINUTE (22U * 60U)
#define DISPLAY_DEFAULT_ON_MINUTE (7U * 60U)
#define DISPLAY_CONTENT_HORIZONTAL_MARGIN 30
#define DISPLAY_CONTENT_TOP 16
#define DISPLAY_CONTENT_SAFE_INSET 8
#define DISPLAY_POWER_BUTTON_SIZE 44
#define DISPLAY_POWER_BUTTON_HIT_PADDING 6
#define DISPLAY_POWER_BUTTON_HORIZONTAL_INSET \
    (DISPLAY_CONTENT_HORIZONTAL_MARGIN + DISPLAY_CONTENT_SAFE_INSET + DISPLAY_POWER_BUTTON_HIT_PADDING)
#define DISPLAY_POWER_BUTTON_VERTICAL_INSET \
    (DISPLAY_CONTENT_TOP + DISPLAY_CONTENT_SAFE_INSET + DISPLAY_POWER_BUTTON_HIT_PADDING)
#define DISPLAY_TIMER_PERIOD_MS 30000U
#define DISPLAY_WAKE_OVERRIDE_US (10LL * 60LL * 1000LL * 1000LL)
#define VALID_TIME_EPOCH 1672531200

static const char *TAG = "display_control";

static display_control_config_t current_config = {
    .schedule_enabled = false,
    .off_minute = DISPLAY_DEFAULT_OFF_MINUTE,
    .on_minute = DISPLAY_DEFAULT_ON_MINUTE,
    .power_button_corner = DISPLAY_POWER_BUTTON_TOP_RIGHT,
    .power_button_visuals_visible = true,
};

static bool initialized = false;
static bool backlight_on = true;
static bool manual_off = false;
static bool schedule_state_known = false;
static bool last_schedule_off = false;
static bool sntp_started = false;
static int64_t wake_override_until_us = 0;
static lv_timer_t *schedule_timer = NULL;
static lv_obj_t *power_button = NULL;
static lv_obj_t *power_icon_parts[3] = { NULL, NULL, NULL };
static bool button_visibility_requested = true;
static int overlay_depth = 0;
static bool button_dim = false;

static bool display_control_get_local_minute(uint16_t *minute_of_day);
static void display_control_evaluate(void);
static void display_control_create_display_off_icon(lv_obj_t *parent);
static void display_control_position_power_button(void);
static void display_control_refresh_power_button_visibility(void);
static void display_control_apply_skin(void);
static void display_control_load_config(void);
static esp_err_t display_control_save_config(void);
static void display_control_set_backlight(bool enabled);
static void display_control_start_sntp_if_ready(void);
static void display_control_timer_cb(lv_timer_t *timer);
static void display_control_power_clicked(lv_event_t *event);

esp_err_t display_control_init(void)
{
    if (initialized) {
        return ESP_OK;
    }

    display_control_load_config();
    initialized = true;
    backlight_on = true;
    schedule_timer = lv_timer_create(display_control_timer_cb, DISPLAY_TIMER_PERIOD_MS, NULL);
    if (!schedule_timer) {
        ESP_LOGE(TAG, "Failed to create display schedule timer");
        initialized = false;
        return ESP_ERR_NO_MEM;
    }

    display_control_evaluate();
    return ESP_OK;
}

void display_control_create_power_button(void)
{
    if (power_button) {
        return;
    }

    power_button = lv_btn_create(lv_layer_top());
    lv_obj_set_size(power_button, DISPLAY_POWER_BUTTON_SIZE, DISPLAY_POWER_BUTTON_SIZE);
    lv_obj_set_ext_click_area(power_button, DISPLAY_POWER_BUTTON_HIT_PADDING);
    display_control_position_power_button();
    lv_obj_set_style_bg_color(power_button, COLOR_CARD_BG, 0);
    lv_obj_set_style_bg_opa(power_button, LV_OPA_70, 0);
    lv_obj_set_style_border_width(power_button, 1, 0);
    lv_obj_set_style_border_color(power_button, COLOR_ACCENT, 0);
    lv_obj_set_style_radius(power_button, 22, 0);
    lv_obj_set_style_shadow_width(power_button, 0, 0);
    lv_obj_add_flag(power_button, LV_OBJ_FLAG_FLOATING);
    lv_obj_add_event_cb(power_button, display_control_power_clicked, LV_EVENT_CLICKED, NULL);

    display_control_create_display_off_icon(power_button);
    display_control_refresh_power_button_visibility();
}

void display_control_refresh_skin(void)
{
    display_control_apply_skin();
}

void display_control_push_overlay(void)
{
    overlay_depth++;
    display_control_refresh_power_button_visibility();
}

void display_control_pop_overlay(void)
{
    if (overlay_depth > 0) overlay_depth--;
    display_control_refresh_power_button_visibility();
}

void display_control_set_power_button_dim(bool dim)
{
    button_dim = dim;
    display_control_apply_skin();
}

void display_control_set_power_button_visible(bool visible)
{
    button_visibility_requested = visible;
    display_control_refresh_power_button_visibility();
}

void display_control_get_config(display_control_config_t *config)
{
    if (config) {
        *config = current_config;
    }
}

esp_err_t display_control_set_config(const display_control_config_t *config)
{
    if (!config || config->off_minute >= 1440U || config->on_minute >= 1440U ||
        config->power_button_corner > DISPLAY_POWER_BUTTON_TOP_LEFT) {
        return ESP_ERR_INVALID_ARG;
    }

    current_config = *config;
    schedule_state_known = false;
    wake_override_until_us = 0;
    display_control_position_power_button();
    display_control_refresh_power_button_visibility();

    esp_err_t result = display_control_save_config();
    display_control_evaluate();
    return result;
}

bool display_control_is_backlight_on(void)
{
    return backlight_on;
}

bool display_control_handle_touch_wake(void)
{
    if (backlight_on) {
        return false;
    }

    manual_off = false;

    uint16_t minute_of_day;
    if (display_control_get_local_minute(&minute_of_day) &&
        display_schedule_should_be_off(current_config.schedule_enabled,
                                       minute_of_day,
                                       current_config.off_minute,
                                       current_config.on_minute)) {
        wake_override_until_us = esp_timer_get_time() + DISPLAY_WAKE_OVERRIDE_US;
    } else {
        wake_override_until_us = 0;
    }

    display_control_set_backlight(true);
    return true;
}

bool display_control_filter_touch(bool pressed)
{
    static bool suppress_until_release = false;
    if (!pressed) {
        suppress_until_release = false;
        return false;
    }
    if (!backlight_on) {
        display_control_handle_touch_wake();
        suppress_until_release = true;
    }
    return !suppress_until_release;
}

void display_control_turn_off(void)
{
    manual_off = true;
    wake_override_until_us = 0;
    display_control_set_backlight(false);
}

static void display_control_load_config(void)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(DISPLAY_NVS_NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        ESP_LOGI(TAG, "Using default display schedule");
        return;
    }

    uint8_t enabled = 0;
    uint8_t corner = DISPLAY_POWER_BUTTON_TOP_RIGHT;
    uint8_t show_visuals = 1U;
    uint16_t off_minute = DISPLAY_DEFAULT_OFF_MINUTE;
    uint16_t on_minute = DISPLAY_DEFAULT_ON_MINUTE;
    bool legacy_hidden = false;

    if (nvs_get_u8(handle, DISPLAY_NVS_ENABLED_KEY, &enabled) == ESP_OK) {
        current_config.schedule_enabled = enabled != 0;
    }
    if (nvs_get_u16(handle, DISPLAY_NVS_OFF_KEY, &off_minute) == ESP_OK && off_minute < 1440U) {
        current_config.off_minute = off_minute;
    }
    if (nvs_get_u16(handle, DISPLAY_NVS_ON_KEY, &on_minute) == ESP_OK && on_minute < 1440U) {
        current_config.on_minute = on_minute;
    }
    if (nvs_get_u8(handle, DISPLAY_NVS_CORNER_KEY, &corner) == ESP_OK) {
        if (corner <= DISPLAY_POWER_BUTTON_TOP_LEFT) {
            current_config.power_button_corner = (display_power_button_corner_t)corner;
        } else if (corner == DISPLAY_POWER_BUTTON_HIDDEN_LEGACY) {
            current_config.power_button_corner = DISPLAY_POWER_BUTTON_TOP_RIGHT;
            current_config.power_button_visuals_visible = false;
            legacy_hidden = true;
        }
    }
    if (!legacy_hidden &&
        nvs_get_u8(handle, DISPLAY_NVS_VISUALS_KEY, &show_visuals) == ESP_OK) {
        current_config.power_button_visuals_visible = show_visuals != 0U;
    }

    nvs_close(handle);
}

static esp_err_t display_control_save_config(void)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(DISPLAY_NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open display settings: %s", esp_err_to_name(err));
        return err;
    }

    err = nvs_set_u8(handle, DISPLAY_NVS_ENABLED_KEY, current_config.schedule_enabled ? 1U : 0U);
    if (err == ESP_OK) {
        err = nvs_set_u16(handle, DISPLAY_NVS_OFF_KEY, current_config.off_minute);
    }
    if (err == ESP_OK) {
        err = nvs_set_u16(handle, DISPLAY_NVS_ON_KEY, current_config.on_minute);
    }
    if (err == ESP_OK) {
        err = nvs_set_u8(handle, DISPLAY_NVS_CORNER_KEY, (uint8_t)current_config.power_button_corner);
    }
    if (err == ESP_OK) {
        err = nvs_set_u8(handle, DISPLAY_NVS_VISUALS_KEY,
                         current_config.power_button_visuals_visible ? 1U : 0U);
    }
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }

    nvs_close(handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to save display settings: %s", esp_err_to_name(err));
    }
    return err;
}

static void display_control_position_power_button(void)
{
    if (!power_button) {
        return;
    }

    if (current_config.power_button_corner == DISPLAY_POWER_BUTTON_TOP_LEFT) {
        lv_obj_align(power_button, LV_ALIGN_TOP_LEFT,
                     DISPLAY_POWER_BUTTON_HORIZONTAL_INSET, DISPLAY_POWER_BUTTON_VERTICAL_INSET);
    } else {
        lv_obj_align(power_button, LV_ALIGN_TOP_RIGHT,
                     -DISPLAY_POWER_BUTTON_HORIZONTAL_INSET, DISPLAY_POWER_BUTTON_VERTICAL_INSET);
    }
}

static void display_control_apply_skin(void)
{
    if (!power_button) {
        return;
    }
    bool glass = theme_get_skin() == THEME_SKIN_GLASS;
    /* Glass: a dark disc with a white rim and glyph, the same material as the
     * drawer buttons, so it reads on any wallpaper. Classic: PR #8's card. */
    bool dim = glass && button_dim;
    lv_obj_set_style_bg_color(power_button, glass ? lv_color_black() : COLOR_CARD_BG, 0);
    lv_obj_set_style_bg_opa(power_button, dim ? LV_OPA_60 : (glass ? LV_OPA_40 : LV_OPA_70), 0);
    lv_obj_set_style_border_color(power_button, glass ? lv_color_white() : COLOR_ACCENT, 0);
    lv_obj_set_style_border_opa(power_button, dim ? LV_OPA_10 : (glass ? LV_OPA_40 : LV_OPA_COVER), 0);
    lv_color_t glyph = glass ? (dim ? lv_color_hex(0x606060) : lv_color_white()) : COLOR_ACCENT;
    if (power_icon_parts[0]) lv_obj_set_style_border_color(power_icon_parts[0], glyph, 0);
    if (power_icon_parts[1]) lv_obj_set_style_bg_color(power_icon_parts[1], glyph, 0);
    if (power_icon_parts[2]) lv_obj_set_style_bg_color(power_icon_parts[2], glyph, 0);
}

static void display_control_refresh_power_button_visibility(void)
{
    if (!power_button) {
        return;
    }
    display_control_apply_skin();

    display_button_visibility_t visibility = display_button_visibility_resolve(
        button_visibility_requested && overlay_depth == 0, current_config.power_button_visuals_visible);

    if (visibility.interactive) {
        lv_obj_clear_flag(power_button, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(power_button, LV_OBJ_FLAG_HIDDEN);
    }

    lv_obj_set_style_opa(power_button,
                         visibility.show_visuals ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
}

static void display_control_create_display_off_icon(lv_obj_t *parent)
{
    lv_obj_t *icon = lv_obj_create(parent);
    lv_obj_set_size(icon, 24, 22);
    lv_obj_center(icon);
    lv_obj_set_style_bg_opa(icon, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(icon, 0, 0);
    lv_obj_set_style_pad_all(icon, 0, 0);
    lv_obj_clear_flag(icon, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t *screen = lv_obj_create(icon);
    lv_obj_set_size(screen, 20, 14);
    lv_obj_align(screen, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(screen, 2, 0);
    lv_obj_set_style_border_color(screen, COLOR_ACCENT, 0);
    lv_obj_set_style_radius(screen, 2, 0);
    lv_obj_set_style_pad_all(screen, 0, 0);
    lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    power_icon_parts[0] = screen;

    lv_obj_t *stand = lv_obj_create(icon);
    lv_obj_set_size(stand, 2, 4);
    lv_obj_align(stand, LV_ALIGN_TOP_MID, 0, 13);
    lv_obj_set_style_bg_color(stand, COLOR_ACCENT, 0);
    lv_obj_set_style_bg_opa(stand, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(stand, 0, 0);
    lv_obj_set_style_radius(stand, 0, 0);
    lv_obj_set_style_pad_all(stand, 0, 0);
    lv_obj_clear_flag(stand, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    power_icon_parts[1] = stand;

    lv_obj_t *foot = lv_obj_create(icon);
    lv_obj_set_size(foot, 10, 2);
    lv_obj_align(foot, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_color(foot, COLOR_ACCENT, 0);
    lv_obj_set_style_bg_opa(foot, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(foot, 0, 0);
    lv_obj_set_style_radius(foot, 0, 0);
    lv_obj_set_style_pad_all(foot, 0, 0);
    lv_obj_clear_flag(foot, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    power_icon_parts[2] = foot;
}

static void display_control_set_backlight(bool enabled)
{
    if (backlight_on == enabled) {
        return;
    }

    esp_err_t err = enabled ? lcd_backlight_enable() : lcd_backlight_disable();
    if (err == ESP_OK) {
        backlight_on = enabled;
        ESP_LOGI(TAG, "Backlight %s", enabled ? "on" : "off");
    } else {
        ESP_LOGE(TAG, "Failed to turn backlight %s: %s", enabled ? "on" : "off", esp_err_to_name(err));
    }
}

static bool display_control_get_local_minute(uint16_t *minute_of_day)
{
    time_t now = time(NULL);
    if (now < VALID_TIME_EPOCH || !minute_of_day) {
        return false;
    }

    struct tm local_time;
    localtime_r(&now, &local_time);
    *minute_of_day = (uint16_t)(local_time.tm_hour * 60 + local_time.tm_min);
    return true;
}

static void display_control_start_sntp_if_ready(void)
{
    if (sntp_started || sntp_enabled()) {
        sntp_started = true;
        return;
    }
    if (!wifi_is_connected()) {
        return;
    }

    sntp_setoperatingmode(SNTP_OPMODE_POLL);
    sntp_setservername(0, "pool.ntp.org");
    sntp_init();
    sntp_started = true;
    ESP_LOGI(TAG, "SNTP started for display schedule");
}

static void display_control_evaluate(void)
{
    display_control_start_sntp_if_ready();

    uint16_t minute_of_day;
    if (!display_control_get_local_minute(&minute_of_day)) {
        return;
    }

    bool schedule_off = display_schedule_should_be_off(current_config.schedule_enabled,
                                                       minute_of_day,
                                                       current_config.off_minute,
                                                       current_config.on_minute);
    bool transitioned_on = schedule_state_known && last_schedule_off && !schedule_off;
    bool transitioned_off = schedule_state_known && !last_schedule_off && schedule_off;

    if (!schedule_state_known && schedule_off) {
        transitioned_off = true;
    }

    schedule_state_known = true;
    last_schedule_off = schedule_off;

    if (transitioned_on) {
        manual_off = false;
        wake_override_until_us = 0;
        display_control_set_backlight(true);
        return;
    }

    if (transitioned_off) {
        manual_off = false;
        wake_override_until_us = 0;
        display_control_set_backlight(false);
        return;
    }

    if (manual_off) {
        display_control_set_backlight(false);
        return;
    }

    if (schedule_off) {
        if (wake_override_until_us > esp_timer_get_time()) {
            display_control_set_backlight(true);
        } else {
            wake_override_until_us = 0;
            display_control_set_backlight(false);
        }
    } else {
        wake_override_until_us = 0;
        display_control_set_backlight(true);
    }
}

static void display_control_timer_cb(lv_timer_t *timer)
{
    LV_UNUSED(timer);
    display_control_evaluate();
}

static void display_control_power_clicked(lv_event_t *event)
{
    LV_UNUSED(event);
    display_control_turn_off();
}
