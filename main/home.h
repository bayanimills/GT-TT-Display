#ifndef HOME_H
#define HOME_H

#include "lvgl.h"
#include "theme.h"

// Hardware information structure
typedef struct {
    char model[32];
    char chip[32];
    char efficiency[16];
    char fan_speed[16];
} hardware_info_t;

// Pool information structure
typedef struct {
    char url[128];
    char port[16];
    char worker_name[64];
} pool_info_t;

#define SCREEN_WIDTH 800
#define SCREEN_HEIGHT 480

// Colors -- resolved at runtime from the active theme (see theme.h/theme.c).
// Every screen paints through these macros, so swapping a preset restyles the
// whole UI without touching a single screen file.
#define COLOR_BACKGROUND     theme_color(THEME_BACKGROUND)
#define COLOR_CARD_BG        theme_color(THEME_CARD_BG)
#define COLOR_ACCENT         theme_color(THEME_ACCENT)
#define COLOR_RED            theme_color(THEME_RED)
#define COLOR_TEXT_PRIMARY   theme_color(THEME_TEXT_PRIMARY)
#define COLOR_TEXT_SECONDARY theme_color(THEME_TEXT_SECONDARY)
#define COLOR_TEXT_ON_ACCENT theme_color(THEME_TEXT_ON_ACCENT)
#define COLOR_BORDER         theme_color(THEME_BORDER)
#define COLOR_NAV_BG         theme_color(THEME_NAV_BG)

// Function declarations
void home_screen_create(void);
void home_screen_destroy(void);
void home_update_hashrate(const char* hashrate);
lv_obj_t* home_get_screen(void);

// Hardware data functions
void home_update_hardware_info(const hardware_info_t* hw_info);
void home_update_power(const char* power);
void update_efficiency_display(void);
void home_update_temperature(const char* temperature);
void home_update_device_model(const char* model);
void home_update_asic_model(const char* chip);
void home_update_fan_speed(const char* fan_rpm);
void home_update_shares(const char* shares);
void home_update_best_difficulty(const char* bd);

// Pool data functions
void home_update_pool_info(const pool_info_t* pool_info);

// Event handlers
void home_hardware_clicked(lv_event_t * e);
void home_pool_clicked(lv_event_t * e);
void home_settings_clicked(lv_event_t * e);
void home_night_clicked(lv_event_t * e);
void home_wifi_clicked(lv_event_t * e);
void home_block_clicked(lv_event_t * e);
void home_clock_clicked(lv_event_t * e);
void home_price_clicked(lv_event_t * e);
void home_mempool_clicked(lv_event_t * e);

#endif // HOME_H
