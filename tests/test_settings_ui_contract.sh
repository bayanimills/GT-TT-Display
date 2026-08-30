#!/usr/bin/env bash
set -euo pipefail

repo_dir="$(cd "$(dirname "$0")/.." && pwd)"
settings_file="$repo_dir/main/settings.c"
display_file="$repo_dir/main/display_control.c"
display_header="$repo_dir/main/display_button_visibility.h"

test "$(rg -c 'style_settings_dropdown\(' "$settings_file")" -eq 5
if rg -q 'update_display_schedule_controls' "$settings_file"; then
    echo "schedule controls must remain editable while scheduling is disabled" >&2
    exit 1
fi
rg -q 'display_control_set_power_button_visible\(false\)' "$settings_file"
rg -q 'display_control_set_power_button_visible\(true\)' "$settings_file"
rg -q 'DISPLAY_DEFAULT_OFF_MINUTE \(22U \* 60U\)' "$display_file"
rg -q 'DISPLAY_DEFAULT_ON_MINUTE \(7U \* 60U\)' "$display_file"
rg -q '"Upper Right\\n"' "$settings_file"
rg -q '"Upper Left"' "$settings_file"
if rg -q '"Hidden"' "$settings_file"; then
    echo "button placement must not overload icon visibility" >&2
    exit 1
fi
rg -q 'lv_checkbox_set_text(display_button_visuals_checkbox, "Show display-off button")' "$settings_file"
rg -q '"When hidden, the selected corner still works"' "$settings_file"
rg -q 'power_button_visuals_visible = lv_obj_has_state' "$settings_file"
rg -q 'DISPLAY_NVS_VISUALS_KEY "disp_icon"' "$display_file"
rg -q 'DISPLAY_POWER_BUTTON_HIDDEN_LEGACY = 2' "$display_header"
rg -q 'display_button_visibility_resolve' "$display_file"
rg -q 'lv_obj_set_style_opa(power_button' "$display_file"
rg -q 'lv_obj_set_size\(screen, 20, 14\)' "$display_file"
rg -q 'lv_obj_align\(screen, LV_ALIGN_TOP_MID, 0, 0\)' "$display_file"
if rg -q 'display_off_slash_points|lv_line_create' "$display_file"; then
    echo "display-off control must use a plain monitor without a slash" >&2
    exit 1
fi

echo "settings UI contract tests passed"
