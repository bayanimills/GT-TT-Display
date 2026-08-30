#!/usr/bin/env bash
set -euo pipefail

repo_dir="$(cd "$(dirname "$0")/.." && pwd)"
settings_file="$repo_dir/main/settings.c"
display_file="$repo_dir/main/display_control.c"
display_header="$repo_dir/main/display_control.h"

test "$(rg -c 'style_settings_dropdown\(' "$settings_file")" -eq 5
! rg -q 'update_display_schedule_controls' "$settings_file"
rg -q 'display_control_set_power_button_visible\(false\)' "$settings_file"
rg -q 'display_control_set_power_button_visible\(true\)' "$settings_file"
rg -q 'DISPLAY_DEFAULT_OFF_MINUTE \(22U \* 60U\)' "$display_file"
rg -q 'DISPLAY_DEFAULT_ON_MINUTE \(7U \* 60U\)' "$display_file"
rg -q '"Upper Right\\n"' "$settings_file"
rg -q '"Upper Left\\n"' "$settings_file"
rg -q '"Hidden"' "$settings_file"
rg -q 'DISPLAY_POWER_BUTTON_HIDDEN = 2' "$display_header"
rg -q 'button_visibility_requested.*power_button_corner != DISPLAY_POWER_BUTTON_HIDDEN' "$display_file"

echo "settings UI contract tests passed"
