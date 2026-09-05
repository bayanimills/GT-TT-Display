#!/usr/bin/env bash
set -euo pipefail

repo_dir="$(cd "$(dirname "$0")/.." && pwd)"
settings_file="$repo_dir/main/settings.c"
display_file="$repo_dir/main/display_control.c"
display_header="$repo_dir/main/display_button_visibility.h"
lvgl_port_file="$repo_dir/main/lvgl_port.c"
glass_file="$repo_dir/main/glass.c"
sdk_defaults="$repo_dir/sdkconfig.defaults"
lv_conf="$repo_dir/components/lvgl__lvgl/lv_conf.h"

test "$(rg -c 'style_settings_dropdown\(' "$settings_file")" -eq 8
rg -q 'display_control_set_power_button_visible\(false\)' "$settings_file"
rg -q 'display_control_set_power_button_visible\(true\)' "$settings_file"
rg -q 'DISPLAY_DEFAULT_OFF_MINUTE \(22U \* 60U\)' "$display_file"
rg -q 'DISPLAY_DEFAULT_ON_MINUTE \(7U \* 60U\)' "$display_file"
rg -q '"Visible Upper Right\\n"' "$settings_file"
rg -q '"Visible Upper Left\\n"' "$settings_file"
rg -q '"Hidden Upper Right\\n"' "$settings_file"
rg -q '"Hidden Upper Left"' "$settings_file"
rg -q '"Hidden keeps the selected corner tappable"' "$settings_file"
rg -Fq 'schedule_details_cont = lv_obj_create(display_section)' "$settings_file"
rg -Fq 'lv_obj_clear_flag(schedule_details_cont, LV_OBJ_FLAG_HIDDEN)' "$settings_file"
rg -Fq 'lv_obj_set_height(display_section, 315)' "$settings_file"
test "$(rg -c 'lv_obj_set_size\(display_(off|on)_dropdown, 300, 44\)' "$settings_file")" -eq 2
rg -Fq 'lv_obj_set_size(display_corner_dropdown, 300, 44)' "$settings_file"
rg -Fq 'lv_obj_set_width(display_corner_hint, 650)' "$settings_file"
rg -Fq 'lv_label_set_long_mode(display_corner_hint, LV_LABEL_LONG_WRAP)' "$settings_file"
rg -Fq 'lv_obj_set_flex_flow(main_cont, LV_FLEX_FLOW_COLUMN)' "$settings_file"
test "$(rg -c 'LV_OBJ_FLAG_SCROLL_MOMENTUM' "$settings_file")" -eq 2
test "$(rg -c 'LV_OBJ_FLAG_SCROLL_ELASTIC' "$settings_file")" -eq 2
test "$(rg -c 'LV_SCROLLBAR_MODE_ACTIVE' "$settings_file")" -eq 2
rg -Fq 'if (h < 56) h = 56' "$settings_file"
rg -Fq 'lv_obj_set_size(sw, 58, 32)' "$settings_file"
rg -Fq 'lv_obj_set_style_anim_time(sw, 140, LV_PART_MAIN)' "$settings_file"
rg -Fq 'fan_manual_cont = lv_obj_create(fan_section)' "$settings_file"
rg -Fq 'lv_obj_add_flag(fan_manual_cont, LV_OBJ_FLAG_HIDDEN)' "$settings_file"
rg -Fq 'restore_details_cont = lv_obj_create(ota_section)' "$settings_file"
rg -Fq 'lv_obj_add_flag(restore_details_cont, LV_OBJ_FLAG_HIDDEN)' "$settings_file"
test "$(rg -c 'lv_obj_scroll_to_view_recursive\(' "$settings_file")" -eq 3
rg -Fq 'lv_obj_set_size(ota_section, 680, 366)' "$settings_file"
rg -Fq '"Include beta releases"' "$settings_file"
rg -Fq '"Manual\nDaily\nWeekly"' "$settings_file"
rg -Fq '"Restore original bitaxeorg firmware"' "$settings_file"
rg -Fq 'lv_obj_set_size(ota_restore_btn, 270, 44)' "$settings_file"
rg -Fq 'settings_show_restore_confirmation' "$settings_file"
rg -q 'display_button_mode_from_config' "$settings_file"
rg -q 'display_button_mode_corner' "$settings_file"
rg -q 'display_button_mode_shows_visuals' "$settings_file"
if rg -q 'display_button_visuals_checkbox' "$settings_file"; then
    echo "button visibility must be represented by the four-mode dropdown" >&2
    exit 1
fi
rg -q 'DISPLAY_NVS_VISUALS_KEY "disp_icon"' "$display_file"
rg -q 'DISPLAY_POWER_BUTTON_HIDDEN_LEGACY = 2' "$display_header"
rg -q 'display_button_visibility_resolve' "$display_file"
rg -Fq 'lv_obj_set_style_opa(power_button' "$display_file"
rg -q 'lv_obj_set_size\(screen, 20, 14\)' "$display_file"
rg -q 'lv_obj_align\(screen, LV_ALIGN_TOP_MID, 0, 0\)' "$display_file"
if rg -q 'display_off_slash_points|lv_line_create' "$display_file"; then
    echo "display-off control must use a plain monitor without a slash" >&2
    exit 1
fi

rg -Fq 'indev_drv_tp.scroll_limit = 8' "$lvgl_port_file"
rg -Fq 'indev_drv_tp.scroll_throw = 8' "$lvgl_port_file"
rg -Fq 'TOUCH_RELEASE_DEBOUNCE_READS = 3' "$lvgl_port_file"
rg -Fq 'reported_pressed && ++release_misses < TOUCH_RELEASE_DEBOUNCE_READS' "$lvgl_port_file"
rg -Fxq 'CONFIG_EXAMPLE_LVGL_PORT_TASK_MIN_DELAY_MS=5' "$sdk_defaults"
rg -Fxq 'CONFIG_EXAMPLE_LVGL_PORT_TASK_PRIORITY=4' "$sdk_defaults"
rg -Fxq 'CONFIG_LV_DISP_DEF_REFR_PERIOD=25' "$sdk_defaults"
rg -Fxq 'CONFIG_LV_INDEV_DEF_READ_PERIOD=10' "$sdk_defaults"
rg -Fq '#define LV_DISP_DEF_REFR_PERIOD 25' "$lv_conf"
rg -Fq '#define LV_INDEV_DEF_READ_PERIOD 10' "$lv_conf"
rg -Fq '#define LV_USE_PERF_MONITOR 0' "$lv_conf"

# Bottom navigation has one tap-only event owner. Navigating or resetting input
# during pointer motion can retarget the same touch to the replacement screen.
rg -Fq 'lv_obj_add_event_cb(grab_target, drawer_grabber_cb, LV_EVENT_CLICKED, NULL)' "$glass_file"
rg -Fq 'lv_obj_clear_flag(grab, LV_OBJ_FLAG_CLICKABLE' "$glass_file"
rg -Fq 'lv_async_call(drawer_grabber_navigate_async' "$glass_file"
rg -Fq 'if (kind == GLASS_SCREEN_SETTINGS) return scr;' "$glass_file"
if rg -q 'lv_obj_add_event_cb\((grab_target|grab), drawer_grabber_cb, LV_EVENT_(PRESSED|PRESSING|PRESS_LOST|GESTURE)' "$glass_file"; then
    echo "Glass bottom navigation must react only to the completed parent tap" >&2
    exit 1
fi

# Glass Display is a fixed button/toggle surface: no precision sliders and no
# always-visible schedule controls.
rg -Fq 'lv_label_set_text(bright_title, "Brightness")' "$settings_file"
rg -Fq '"Display Toggle"' "$settings_file"
rg -Fq '"Scheduled Dimming"' "$settings_file"
rg -Fq 'glass_settings_brightness_step' "$settings_file"
rg -Fq 'glass_settings_dim_step' "$settings_file"
rg -Fq 'glass_settings_sync_display_controls' "$settings_file"
rg -Fq 'display_control_preview_brightness((uint8_t)next, 5000U)' "$settings_file"

echo "settings UI contract tests passed"
