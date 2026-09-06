#!/usr/bin/env bash
set -euo pipefail

repo_dir="$(cd "$(dirname "$0")/.." && pwd)"

# User-facing names and deliberately removed clutter.
rg -Fq 'lv_label_set_text(block_title_label, "Blockheight")' "$repo_dir/main/block.c"
! rg -q 'CURRENT TIP|FEE RATE' "$repo_dir/main/block.c"
rg -Fq '"DAYS TO HALVING"' "$repo_dir/main/block.c"
# The currency is set in settings and shown by the prefix, so the price screen
# says it once, not three times, and the CAGR captions name the period that
# won rather than the band that was searched.
rg -Fq 'lv_label_set_text(price_title_label, "Exchange Rate")' "$repo_dir/main/price.c"
! rg -q 'Bitcoin Exchange Rate' "$repo_dir/main/price.c"
! rg -q 'BEST 4-6 YEAR CAGR|BEST 7-10 YEAR CAGR' "$repo_dir/main/price.c"
! rg -q 'Tap price to change currency' "$repo_dir/main/price.c"
! rg -q 'price_suffix_label' "$repo_dir/main/price.c"
rg -Fq '"%u YEAR CAGR"' "$repo_dir/main/price.c"
rg -Fq '"What are the odds?"' "$repo_dir/main/odds.c"
rg -Fq 'lv_label_set_text(source, "bitview.space")' "$repo_dir/main/odds.c"
rg -Fq 'odds_show_hashrate = !odds_show_hashrate' "$repo_dir/main/odds.c"
! rg -q 'waiting  -  .*vMB' "$repo_dir/main/mempool.c"

# Twin clock is the fresh-install default; both cards can rotate metrics.
rg -Fq 'static bool clock_twin_layout = true;' "$repo_dir/main/clock.c"
rg -Fq '"ANALOGUE"' "$repo_dir/main/clock.c"
rg -Fq '"DIGITAL"' "$repo_dir/main/clock.c"
rg -Fq 'clock_stat_kind[slot] =' "$repo_dir/main/clock.c"
# The stat cards are stepped by two lit halves, not by a whole-card tap, so
# the caption no longer has to carry the instruction.
rg -Fq '"MINER HASHRATE"' "$repo_dir/main/clock.c"
! rg -q 'TAP' "$repo_dir/main/clock.c"
rg -Fq 'clock_stat_zone(card, slot, false)' "$repo_dir/main/clock.c"
rg -Fq 'clock_stat_zone(card, slot, true)' "$repo_dir/main/clock.c"
# The date titles the screen and the weekday sits with the face; the timezone
# is reachable on the skin the device actually runs.
rg -Fq 'current_datetitle_text' "$repo_dir/main/clock.c"
rg -Fq 'current_weekday_text' "$repo_dir/main/clock.c"
rg -Fq 'settings_timezone_option_list()' "$repo_dir/main/clock.c"
# Both digital faces use the monospace family, not Montserrat.
rg -Fq 'dejavu_mono_220' "$repo_dir/main/clock.c"
rg -Fq 'dejavu_mono_96' "$repo_dir/main/clock.c"
! rg -q 'clock_build_digital\(clock_display_content, [0-9]+, [0-9]+, [0-9]+, [0-9]+,\s*&montserrat_140' "$repo_dir/main/clock.c"
# No page caption, and the date belongs under the time rather than in the chrome.
! rg -q 'clock_title_label, "CLOCK"' "$repo_dir/main/clock.c"
rg -Fq 'clock_build_fixed_time' "$repo_dir/main/clock.c"

# Aux is one menu entry over two views, and both weather sources are key-free.
rg -Fq '{ "Aux",     "Feed + weather"' "$repo_dir/main/settings.c"
! rg -q '\{ "Feed",    "Recent updates"' "$repo_dir/main/settings.c"
rg -Fq 'AUX_VIEW_WEATHER' "$repo_dir/main/feed.c"
rg -Fq 'locationforecast/2.0/complete' "$repo_dir/main/weather.c"
! rg -q 'locationforecast/2.0/compact' "$repo_dir/main/weather.c"
rg -Fq 'geocoding-api.open-meteo.com' "$repo_dir/main/weather.c"

# Theme is fixed-height, wallpaper-first, immediately selectable and paged.
rg -Fq '{ "Theme",   "Wallpaper + accent"' "$repo_dir/main/settings.c"
rg -Fq 'lv_label_set_text(wall_title, "Wallpaper")' "$repo_dir/main/settings.c"
rg -Fq 'lv_label_set_text(accent_title, "Accent")' "$repo_dir/main/settings.c"
rg -Fq 'glass_settings_theme_page_clicked' "$repo_dir/main/settings.c"
rg -Fq 'lv_obj_add_event_cb(swatch, glass_settings_theme_clicked' "$repo_dir/main/settings.c"

# Pool data and ordering repaint on the same five-second cadence.
rg -Fq 'lv_timer_create(glass_settings_pool_refresh_cb, 5000, NULL)' "$repo_dir/main/settings.c"
rg -Fq '#define SWEEP_INTERVAL_MS  (5 * 1000)' "$repo_dir/main/poolping.c"

# Wi-Fi entered from Settings always exposes a large top-left Back control.
rg -Fq 'lv_label_set_text(back_label, LV_SYMBOL_LEFT "  BACK")' "$repo_dir/main/wifi.c"
rg -Fq 'glass_goto(GLASS_SCREEN_SETTINGS)' "$repo_dir/main/wifi.c"

echo "Glass data UI contract tests passed"
