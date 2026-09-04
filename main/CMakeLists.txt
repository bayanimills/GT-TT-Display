idf_component_register(
    SRCS "ota_update.c" "ota_release.c" "ota_screen.c" "mempool.c" "block.c" "clock.c" "price.c" "Nevan_RUS_96.c" "untyped_96.c" "montserrat_120.c" "montserrat_140.c" "clock-solid-full.c" "cube-solid-full.c" "cubes-solid-full.c" "bap_client.c" "bap_parser.c" "bap_uart.c" "bap_protocol.c" "display_control.c" "display_button_visibility.c" "display_schedule.c" "settings.c" "theme.c" "chain.c" "odds.c" "payout.c" "blockfound.c" "poolping.c" "glass.c" "wallpaper.c" "home.c" "wifi.c" "night.c"
        "loading.c"
        "waveshare_rgb_lcd_port.c"
        "main.c"
        "lvgl_port.c"
        "assets/lvgl_logo.c"
        "assets/temperature_icon.c"
        "assets/fan.c"
        "assets/star.c"
        "assets/glass_icons.c"
        "angelwish_96.c"
    INCLUDE_DIRS "."
    PRIV_REQUIRES mbedtls esp_partition)

idf_component_get_property(lvgl_lib lvgl__lvgl COMPONENT_LIB)
target_compile_options(${lvgl_lib} PRIVATE -Wno-format)
