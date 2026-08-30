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

#define DISPLAY_NVS_NAMESPACE "settings"
#define DISPLAY_NVS_ENABLED_KEY "disp_sched"
#define DISPLAY_NVS_OFF_KEY "disp_off"
#define DISPLAY_NVS_ON_KEY "disp_on"
#define DISPLAY_NVS_CORNER_KEY "disp_corner"
#define DISPLAY_DEFAULT_OFF_MINUTE (22U * 60U)
#define DISPLAY_DEFAULT_ON_MINUTE (7U * 60U)
#define DISPLAY_POWER_BUTTON_SIZE 44
#define DISPLAY_POWER_BUTTON_HIT_PADDING 6
#define DISPLAY_POWER_BUTTON_EDGE_INSET 8
#define DISPLAY_TIMER_PERIOD_MS 30000U
#define DISPLAY_WAKE_OVERRIDE_US (10LL * 60LL * 1000LL * 1000LL)
#define VALID_TIME_EPOCH 1672531200

static const char *TAG = "display_control";

static display_control_config_t current_config = {
    .schedule_enabled = false,
    .off_minute = DISPLAY_DEFAULT_OFF_MINUTE,
    .on_minute = DISPLAY_DEFAULT_ON_MINUTE,
    .power_button_corner = DISPLAY_POWER_BUTTON_TOP_RIGHT,
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

static bool display_control_get_local_minute(uint16_t *minute_of_day);
static void display_control_evaluate(void);
static void display_control_position_power_button(void);
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

    lv_obj_t *label = lv_label_create(power_button);
    lv_label_set_text(label, LV_SYMBOL_POWER);
    lv_obj_set_style_text_color(label, COLOR_ACCENT, 0);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_18, 0);
    lv_obj_center(label);
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
    uint16_t off_minute = DISPLAY_DEFAULT_OFF_MINUTE;
    uint16_t on_minute = DISPLAY_DEFAULT_ON_MINUTE;

    if (nvs_get_u8(handle, DISPLAY_NVS_ENABLED_KEY, &enabled) == ESP_OK) {
        current_config.schedule_enabled = enabled != 0;
    }
    if (nvs_get_u16(handle, DISPLAY_NVS_OFF_KEY, &off_minute) == ESP_OK && off_minute < 1440U) {
        current_config.off_minute = off_minute;
    }
    if (nvs_get_u16(handle, DISPLAY_NVS_ON_KEY, &on_minute) == ESP_OK && on_minute < 1440U) {
        current_config.on_minute = on_minute;
    }
    if (nvs_get_u8(handle, DISPLAY_NVS_CORNER_KEY, &corner) == ESP_OK &&
        corner <= DISPLAY_POWER_BUTTON_TOP_LEFT) {
        current_config.power_button_corner = (display_power_button_corner_t)corner;
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
                     DISPLAY_POWER_BUTTON_EDGE_INSET, DISPLAY_POWER_BUTTON_EDGE_INSET);
    } else {
        lv_obj_align(power_button, LV_ALIGN_TOP_RIGHT,
                     -DISPLAY_POWER_BUTTON_EDGE_INSET, DISPLAY_POWER_BUTTON_EDGE_INSET);
    }
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
