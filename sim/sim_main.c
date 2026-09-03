/* sim_main.c -- runs the real Turbo Touch screen code on a workstation.
 *
 * The panel is replaced by an 800x480 RGB565 framebuffer that is streamed to
 * stdout; touch and theme commands come back on stdin. server.py wraps that in
 * a browser UI on :8010.
 *
 * Protocol
 *   out : "GTFB" | u32 frame | u16 w | u16 h | w*h*2 bytes RGB565 little-endian
 *   in  : one command per line
 *           T <x> <y> <0|1>   touch at x,y (1 = down)
 *           B <sentence>      feed a raw BAP sentence to the parser
 *           P <index>         select theme preset
 *           S <slot> <rrggbb> override one theme slot
 *           C                 commit theme (persist + repaint)
 *           N <screen>        navigate: home night block clock price mempool wifi settings
 *           K <skin>          select skin (0 classic, 1 glass); home rebuilds on next N home
 *           G <what> <val>    glass: layout 0|1, widgets <hex>, wall <i>, drawer 0|1, sheet 0..4
 *           D off | D mode <0..3>   display off / display-off button mode
 *           R                 force full repaint
 *           Q                 quit
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <pthread.h>
#include <time.h>
#include <sys/select.h>

#include "lvgl.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_lcd_types.h"
#include "esp_lcd_touch.h"

#include "theme.h"
#include "glass.h"
#include "home.h"
#include "night.h"
#include "block.h"
#include "clock.h"
#include "price.h"
#include "mempool.h"
#include "wifi.h"
#include "settings.h"
#include "loading.h"
#include "bap_parser.h"
#include "display_control.h"

#define H_RES 800
#define V_RES 480

static const char *TAG = "sim";

static uint16_t  s_fb[H_RES * V_RES];
static uint32_t  s_frame_no  = 0;
static bool      s_dirty     = true;
static bool      s_running   = true;

static int16_t   s_touch_x = 0, s_touch_y = 0;
static bool      s_touch_down = false;

static pthread_mutex_t s_lvgl_m;

/* ---------------- LVGL glue ---------------- */

static void flush_cb(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *px)
{
    for (int y = area->y1; y <= area->y2; y++) {
        if (y < 0 || y >= V_RES) continue;
        for (int x = area->x1; x <= area->x2; x++) {
            if (x < 0 || x >= H_RES) continue;
            s_fb[y * H_RES + x] = px[(y - area->y1) * (area->x2 - area->x1 + 1) + (x - area->x1)].full;
        }
    }
    s_dirty = true;
    lv_disp_flush_ready(drv);
}

static void touch_read_cb(lv_indev_drv_t *drv, lv_indev_data_t *data)
{
    (void) drv;
    data->point.x = s_touch_x;
    data->point.y = s_touch_y;
    /* Same gate as lvgl_port.c: a touch while dark wakes and is swallowed. */
    data->state   = display_control_filter_touch(s_touch_down) ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}

/* ---------------- port shims the UI expects ---------------- */

bool lvgl_port_lock(int timeout_ms)
{
    (void) timeout_ms;
    return pthread_mutex_lock(&s_lvgl_m) == 0;
}
void lvgl_port_unlock(void) { pthread_mutex_unlock(&s_lvgl_m); }
bool example_lvgl_lock(int timeout_ms) { return lvgl_port_lock(timeout_ms); }
void example_lvgl_unlock(void) { lvgl_port_unlock(); }

esp_err_t lvgl_port_init(esp_lcd_panel_handle_t lcd, esp_lcd_touch_handle_t tp) { (void) lcd; (void) tp; return ESP_OK; }
bool lvgl_port_notify_rgb_vsync(void) { return false; }
void lvgl_port_task_suspend(void) { }
void lvgl_port_task_resume(void) { }

esp_err_t waveshare_esp32_s3_rgb_lcd_init(void) { return ESP_OK; }
esp_err_t wavesahre_rgb_lcd_bl_on(void)  { return ESP_OK; }
esp_err_t wavesahre_rgb_lcd_bl_off(void) { return ESP_OK; }
void      set_ext_to_io(uint8_t v) { (void) v; }

static uint8_t s_brightness = 100;
esp_err_t lcd_backlight_pwm_init(void) { return ESP_OK; }
esp_err_t lcd_backlight_set_brightness(uint8_t p) { s_brightness = p; return ESP_OK; }
uint8_t   lcd_backlight_get_brightness(void) { return s_brightness; }
esp_err_t lcd_backlight_fade_to(uint8_t t, uint32_t ms) { (void) ms; s_brightness = t; return ESP_OK; }
esp_err_t lcd_backlight_enable(void)  { return ESP_OK; }
esp_err_t lcd_backlight_disable(void) { return ESP_OK; }

/* ---------------- screen manager ---------------- */

typedef struct {
    const char *name;
    void      (*create)(void);
    void      (*destroy)(void);
    lv_obj_t *(*get)(void);
} sim_screen_t;

static const sim_screen_t k_screens[] = {
    { "home",     home_screen_create,     home_screen_destroy,     home_get_screen     },
    { "night",    night_screen_create,    night_screen_destroy,    night_get_screen    },
    { "block",    block_screen_create,    block_screen_destroy,    block_get_screen    },
    { "clock",    clock_screen_create,    clock_screen_destroy,    clock_get_screen    },
    { "price",    price_screen_create,    price_screen_destroy,    price_get_screen    },
    { "mempool",  mempool_screen_create,  mempool_screen_destroy,  mempool_get_screen  },
    { "wifi",     wifi_screen_create,     wifi_screen_destroy,     wifi_get_screen     },
    { "settings", settings_screen_create, settings_screen_destroy, settings_get_screen },
};
#define SCREEN_COUNT ((int) (sizeof(k_screens) / sizeof(k_screens[0])))


/* With LV_MEM_CUSTOM 1 LVGL allocates from the general heap and keeps no pool
 * statistics, so this prints n/a; it is kept for builds that switch back to
 * a fixed pool, where headroom is the constraint on what a screen can build. */
static void report_mem(const char *what)
{
    lv_mem_monitor_t m;
    lv_mem_monitor(&m);
    if (m.total_size == 0) {
        /* LV_MEM_CUSTOM: LVGL allocates through malloc and keeps no pool stats. */
        ESP_LOGI(TAG, "mem after %-9s n/a (LV_MEM_CUSTOM, heap-backed)", what);
        return;
    }
    ESP_LOGI(TAG, "mem after %-9s used %6u / %6u B (%2u%%), free %6u, largest free %6u, frag %u%%",
             what,
             (unsigned) (m.total_size - m.free_size), (unsigned) m.total_size,
             (unsigned) m.used_pct, (unsigned) m.free_size,
             (unsigned) m.free_biggest_size, (unsigned) m.frag_pct);
}

static int active_screen_index(void)
{
    lv_obj_t *act = lv_scr_act();
    for (int i = 0; i < SCREEN_COUNT; i++) {
        if (k_screens[i].get && k_screens[i].get() == act) return i;
    }
    return -1;
}

static void sim_goto(int idx)
{
    if (idx < 0 || idx >= SCREEN_COUNT) return;
    int cur = active_screen_index();
    k_screens[idx].create();
    lv_scr_load(k_screens[idx].get());
    if (cur >= 0 && cur != idx) k_screens[cur].destroy();
    lv_obj_invalidate(lv_scr_act());
    s_dirty = true;
    report_mem(k_screens[idx].name);
}

/* Rebuild the active screen so new theme colours take effect.
 *
 * The screens paint their colours at construction time, so a theme change means
 * tearing the screen down and building it again. Deleting the screen that is
 * currently loaded corrupts LVGL's active-screen pointer, so park on a scratch
 * screen for the swap -- the same "load the next one first" order the firmware
 * uses when navigating. */
static void sim_theme_reload(void)
{
    int cur = active_screen_index();
    if (cur < 0) {
        /* Still on the boot/loading screen -- just repaint it. */
        lv_obj_set_style_bg_color(lv_scr_act(), COLOR_BACKGROUND, 0);
        lv_obj_invalidate(lv_scr_act());
        s_dirty = true;
        return;
    }

    lv_obj_t *scratch = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scratch, COLOR_BACKGROUND, 0);
    lv_scr_load(scratch);

    k_screens[cur].destroy();
    k_screens[cur].create();
    lv_scr_load(k_screens[cur].get());
    lv_obj_del(scratch);

    lv_obj_invalidate(lv_scr_act());
    s_dirty = true;
    ESP_LOGI(TAG, "theme -> %s, rebuilt %s", theme_get_name(), k_screens[cur].name);
}

/* ---------------- command handling ---------------- */

static void handle_command(char *line)
{
    while (*line == ' ') line++;
    char cmd = *line;
    char *arg = line + 1;
    while (*arg == ' ') arg++;

    switch (cmd) {
    case 'T': {
        int x, y, down;
        if (sscanf(arg, "%d %d %d", &x, &y, &down) == 3) {
            s_touch_x = (int16_t) x;
            s_touch_y = (int16_t) y;
            s_touch_down = down != 0;
        }
        break;
    }
    case 'B':
        bap_parse_and_handle_message(arg);
        break;
    case 'P':
        theme_set_index(atoi(arg));
        break;
    case 'S': {
        int slot; unsigned rgb;
        if (sscanf(arg, "%d %x", &slot, &rgb) == 2) theme_set_slot((theme_slot_t) slot, rgb);
        break;
    }
    case 'C':
        theme_commit();
        break;
    case 'N': {
        char *nl = strchr(arg, '\n');
        if (nl) *nl = 0;
        for (int i = 0; i < SCREEN_COUNT; i++) {
            if (strcmp(arg, k_screens[i].name) == 0) { sim_goto(i); break; }
        }
        break;
    }
    case 'K':
        theme_set_skin((theme_skin_t) atoi(arg));
        display_control_refresh_skin();
        break;
    case 'G': {
        /* Glass skin controls: layout <0|1>, widgets <hexmask>, wall <index>,
         * drawer <0|1>, sheet <0..4>. All go through the same setters the
         * on-device pickers use, so a screenshot exercises the real path. */
        char what[16] = { 0 };
        char val[32] = { 0 };
        if (sscanf(arg, "%15s %31s", what, val) < 1) break;
        if (strcmp(what, "layout") == 0)       glass_set_layout((glass_layout_t) atoi(val));
        else if (strcmp(what, "widgets") == 0) glass_set_widget_mask((uint32_t) strtoul(val, NULL, 16));
        else if (strcmp(what, "wall") == 0)    glass_set_wallpaper(atoi(val));
        else if (strcmp(what, "drawer") == 0)  { if (atoi(val)) glass_drawer_open(); else glass_drawer_close(); }
        else if (strcmp(what, "sheet") == 0)   { int s = atoi(val); if (s) glass_sheet_open((glass_sheet_t) s); else glass_sheet_close(); }
        else if (strcmp(what, "scroll") == 0)  glass_scroll_to(atoi(val));
        s_dirty = true;
        break;
    }
    case 'D': {
        /* Display control: "D off" turns the backlight off as the corner
         * button would; "D mode <0..3>" picks the button mode (visible
         * right/left, hidden right/left) through the same setter settings uses. */
        char what[16] = { 0 };
        int val = 0;
        if (sscanf(arg, "%15s %d", what, &val) < 1) break;
        if (strcmp(what, "off") == 0) {
            display_control_turn_off();
        } else if (strcmp(what, "mode") == 0) {
            display_control_config_t cfg;
            display_control_get_config(&cfg);
            cfg.power_button_corner = display_button_mode_corner((display_power_button_mode_t) val);
            cfg.power_button_visuals_visible = display_button_mode_shows_visuals((display_power_button_mode_t) val);
            display_control_set_config(&cfg);
        }
        s_dirty = true;
        break;
    }
    case 'R':
        lv_obj_invalidate(lv_scr_act());
        s_dirty = true;
        break;
    case 'Q':
        s_running = false;
        break;
    default:
        break;
    }
}

static void poll_stdin(void)
{
    static char buf[1024];
    fd_set fds;
    struct timeval tv = { 0, 0 };
    FD_ZERO(&fds);
    FD_SET(STDIN_FILENO, &fds);
    while (select(STDIN_FILENO + 1, &fds, NULL, NULL, &tv) > 0) {
        if (!fgets(buf, sizeof(buf), stdin)) { s_running = false; return; }
        char *nl = strpbrk(buf, "\r\n");
        if (nl) *nl = 0;
        /* Commands touch LVGL objects, and the price/mempool tasks take the same
         * lock to update labels: hold it here or the two race. */
        if (buf[0]) { lvgl_port_lock(-1); handle_command(buf); lvgl_port_unlock(); }
        FD_ZERO(&fds);
        FD_SET(STDIN_FILENO, &fds);
        tv.tv_sec = 0;
        tv.tv_usec = 0;
    }
}

static void emit_frame(void)
{
    uint8_t head[12];
    memcpy(head, "GTFB", 4);
    s_frame_no++;
    head[4] = (uint8_t) (s_frame_no);
    head[5] = (uint8_t) (s_frame_no >> 8);
    head[6] = (uint8_t) (s_frame_no >> 16);
    head[7] = (uint8_t) (s_frame_no >> 24);
    head[8]  = (uint8_t) (H_RES & 0xFF);
    head[9]  = (uint8_t) (H_RES >> 8);
    head[10] = (uint8_t) (V_RES & 0xFF);
    head[11] = (uint8_t) (V_RES >> 8);
    fwrite(head, 1, sizeof(head), stdout);
    if (display_control_is_backlight_on()) {
        fwrite(s_fb, 2, H_RES * V_RES, stdout);
    } else {
        /* The panel keeps its pixels when the backlight is off; show the
         * frame at a fifth of its brightness so a dark display is visible
         * in screenshots without pretending the content went away. */
        static uint16_t dim[H_RES * V_RES];
        for (int i = 0; i < H_RES * V_RES; i++) {
            uint16_t v = s_fb[i];
            uint16_t r = ((v >> 11) & 0x1F) / 5, g = ((v >> 5) & 0x3F) / 5, b = (v & 0x1F) / 5;
            dim[i] = (uint16_t) ((r << 11) | (g << 5) | b);
        }
        fwrite(dim, 2, H_RES * V_RES, stdout);
    }
    fflush(stdout);
}

/* ---------------- main ---------------- */

int main(void)
{
    pthread_mutexattr_t ma;
    pthread_mutexattr_init(&ma);
    pthread_mutexattr_settype(&ma, PTHREAD_MUTEX_RECURSIVE);
    pthread_mutex_init(&s_lvgl_m, &ma);

    lv_init();

    static lv_disp_draw_buf_t draw_buf;
    static lv_color_t buf1[H_RES * 60];
    static lv_color_t buf2[H_RES * 60];
    lv_disp_draw_buf_init(&draw_buf, buf1, buf2, H_RES * 60);

    static lv_disp_drv_t disp_drv;
    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res  = H_RES;
    disp_drv.ver_res  = V_RES;
    disp_drv.flush_cb = flush_cb;
    disp_drv.draw_buf = &draw_buf;
    lv_disp_drv_register(&disp_drv);

    static lv_indev_drv_t indev_drv;
    lv_indev_drv_init(&indev_drv);
    indev_drv.type    = LV_INDEV_TYPE_POINTER;
    indev_drv.read_cb = touch_read_cb;
    lv_indev_drv_register(&indev_drv);

    theme_init();
    theme_register_reload(sim_theme_reload);
    ESP_LOGI(TAG, "sim up: %dx%d, theme=%s", H_RES, V_RES, theme_get_name());
    report_mem("boot");

    /* Boot exactly like the device does. */
    settings_initialize();
    if (lvgl_port_lock(-1)) {
        loading();
        display_control_init();
        lvgl_port_unlock();
    }

    struct timespec last;
    clock_gettime(CLOCK_MONOTONIC, &last);

    while (s_running) {
        poll_stdin();

        struct timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);
        uint32_t elapsed = (uint32_t) ((now.tv_sec - last.tv_sec) * 1000 +
                                       (now.tv_nsec - last.tv_nsec) / 1000000);
        if (elapsed > 0) {
            lv_tick_inc(elapsed);
            last = now;
        }

        lvgl_port_lock(-1);
        lv_timer_handler();
        lvgl_port_unlock();

        if (s_dirty) {
            s_dirty = false;
            emit_frame();
        }

        struct timespec nap = { 0, 10 * 1000000L };   /* ~100 Hz */
        nanosleep(&nap, NULL);
    }

    ESP_LOGI(TAG, "sim exiting");
    return 0;
}
