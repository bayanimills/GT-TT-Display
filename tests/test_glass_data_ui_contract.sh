#!/usr/bin/env bash
set -euo pipefail

repo_dir="$(cd "$(dirname "$0")/.." && pwd)"

# User-facing names and deliberately removed clutter.
rg -Fq 'lv_label_set_text(block_title_label, "Blockheight")' "$repo_dir/main/block.c"
! rg -q 'CURRENT TIP|FEE RATE' "$repo_dir/main/block.c"
rg -Fq '"DAYS TO HALVING"' "$repo_dir/main/block.c"
rg -Fq '"Bitcoin Exchange Rate (%s)"' "$repo_dir/main/price.c"
rg -Fq '"BEST 4-6 YEAR CAGR"' "$repo_dir/main/price.c"
rg -Fq '"BEST 7-10 YEAR CAGR"' "$repo_dir/main/price.c"
rg -Fq '"What are the odds?"' "$repo_dir/main/odds.c"
rg -Fq 'lv_label_set_text(source, "bitview.space")' "$repo_dir/main/odds.c"
rg -Fq 'odds_show_hashrate = !odds_show_hashrate' "$repo_dir/main/odds.c"
! rg -q 'waiting  -  .*vMB' "$repo_dir/main/mempool.c"

# Twin clock is the fresh-install default; both cards can rotate metrics.
rg -Fq 'static bool clock_twin_layout = true;' "$repo_dir/main/clock.c"
rg -Fq '"ANALOGUE"' "$repo_dir/main/clock.c"
rg -Fq '"DIGITAL"' "$repo_dir/main/clock.c"
rg -Fq 'clock_stat_kind[slot] = ' "$repo_dir/main/clock.c"
rg -Fq '"MINER HASHRATE  -  TAP"' "$repo_dir/main/clock.c"

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
