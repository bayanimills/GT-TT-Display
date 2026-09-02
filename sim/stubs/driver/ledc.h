#pragma once
#include <stdint.h>
#include "esp_err.h"
typedef enum { LEDC_LOW_SPEED_MODE = 0, LEDC_HIGH_SPEED_MODE, LEDC_SPEED_MODE_MAX } ledc_mode_t;
typedef enum { LEDC_TIMER_0 = 0, LEDC_TIMER_1, LEDC_TIMER_2, LEDC_TIMER_3 } ledc_timer_t;
typedef enum { LEDC_CHANNEL_0 = 0, LEDC_CHANNEL_1, LEDC_CHANNEL_2, LEDC_CHANNEL_3 } ledc_channel_t;
typedef enum { LEDC_TIMER_8_BIT = 8, LEDC_TIMER_10_BIT = 10, LEDC_TIMER_12_BIT = 12, LEDC_TIMER_13_BIT = 13 } ledc_timer_bit_t;
typedef enum { LEDC_AUTO_CLK = 0 } ledc_clk_cfg_t;
typedef enum { LEDC_INTR_DISABLE = 0, LEDC_INTR_FADE_END } ledc_intr_type_t;
typedef enum { LEDC_FADE_NO_WAIT = 0, LEDC_FADE_WAIT_DONE } ledc_fade_mode_t;
typedef struct { ledc_mode_t speed_mode; ledc_timer_bit_t duty_resolution; ledc_timer_t timer_num; uint32_t freq_hz; ledc_clk_cfg_t clk_cfg; } ledc_timer_config_t;
typedef struct { int gpio_num; ledc_mode_t speed_mode; ledc_channel_t channel; ledc_intr_type_t intr_type; ledc_timer_t timer_sel; uint32_t duty; int hpoint; union { struct { unsigned output_invert : 1; } flags; }; } ledc_channel_config_t;
static inline esp_err_t ledc_timer_config(const ledc_timer_config_t *c) { (void) c; return ESP_OK; }
static inline esp_err_t ledc_channel_config(const ledc_channel_config_t *c) { (void) c; return ESP_OK; }
static inline esp_err_t ledc_set_duty(ledc_mode_t m, ledc_channel_t c, uint32_t d) { (void) m; (void) c; (void) d; return ESP_OK; }
static inline esp_err_t ledc_update_duty(ledc_mode_t m, ledc_channel_t c) { (void) m; (void) c; return ESP_OK; }
static inline uint32_t  ledc_get_duty(ledc_mode_t m, ledc_channel_t c) { (void) m; (void) c; return 255; }
static inline esp_err_t ledc_fade_func_install(int f) { (void) f; return ESP_OK; }
static inline esp_err_t ledc_set_fade_with_time(ledc_mode_t m, ledc_channel_t c, uint32_t d, int t) { (void) m; (void) c; (void) d; (void) t; return ESP_OK; }
static inline esp_err_t ledc_fade_start(ledc_mode_t m, ledc_channel_t c, ledc_fade_mode_t f) { (void) m; (void) c; (void) f; return ESP_OK; }
