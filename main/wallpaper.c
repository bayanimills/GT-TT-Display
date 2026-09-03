#include <math.h>
#include <string.h>
#include <stdlib.h>
#include "wallpaper.h"
#include "home.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "lvgl_port.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "wallpaper";

#ifndef MALLOC_CAP_SPIRAM
#define MALLOC_CAP_SPIRAM 0
#endif

/* Scene primitives. Coordinates are normalised to the screen width (x in
 * 0..1, y in 0..H/W) so discs stay round whatever the render size, and so the
 * same scene draws the full wallpaper and the picker thumbnails. */
typedef struct {
    float x, y, r;        /* centre and radius (1.0 = screen width) */
    uint32_t rgb;         /* light colour, added to the base */
    float strength;       /* peak intensity */
} glow_t;

typedef struct {
    float x, y, r;
    uint32_t top, bottom; /* vertical gradient across the disc */
    float alpha;
} disc_t;

typedef struct {
    float x0, y0, x1, y1; /* a line; the band is the strip around it */
    float half_w;
    uint32_t rgb;
    float alpha;
} band_t;

typedef struct {
    const char *name;
    uint32_t top, bottom; /* base vertical gradient */
    glow_t glows[3];
    int    glow_count;
    disc_t discs[3];
    int    disc_count;
    band_t bands[3];
    int    band_count;
} scene_t;

/* Glows are always soft, so they carry no detail for a frost pane to blur.
 * The discs and bands are what give each wallpaper crisp structure; their
 * edges are what visibly soften inside a panel. */
static const scene_t k_scenes[] = {
    {
        "Aurora", 0x070B1F, 0x0A1440,
        { { 0.12f, 0.12f, 0.42f, 0x1FA8FF, 0.55f },
          { 0.85f, 0.50f, 0.50f, 0x6D3BFF, 0.60f },
          { 0.50f, 0.66f, 0.35f, 0x18E2C8, 0.28f } }, 3,
        { { 0.76f, 0.19f, 0.30f, 0x2B4BFF, 0x7A3BFF, 0.55f },
          { 0.22f, 0.48f, 0.14f, 0x1CE3D6, 0x1FA8FF, 0.60f } }, 2,
        { { 0.0f, 0.54f, 1.0f, 0.12f, 0.010f, 0xFFFFFF, 0.10f } }, 1,
    },
    {
        "Sunset", 0x160426, 0x2E0822,
        { { 0.20f, 0.54f, 0.50f, 0xFF6A3D, 0.40f },
          { 0.80f, 0.12f, 0.45f, 0xFF3D8A, 0.35f },
          { 0.60f, 0.36f, 0.30f, 0xFFB347, 0.18f } }, 3,
        { { 0.84f, 0.52f, 0.34f, 0xF07A3A, 0xE23560, 0.70f },
          { 0.16f, 0.13f, 0.18f, 0xF5C04E, 0xEE6E36, 0.70f },
          { 0.55f, 0.24f, 0.07f, 0xFFFFFF, 0xFFE0C0, 0.30f } }, 3,
        { { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0, 0.0f } }, 0,
    },
    {
        "Graphite", 0x0B0B0E, 0x1C1D23,
        { { 0.50f, 0.69f, 0.55f, 0xF7931A, 0.32f },
          { 0.10f, 0.06f, 0.50f, 0x4A5060, 0.30f } }, 2,
        { { 0.72f, 0.30f, 0.32f, 0x2A2C34, 0x15161A, 0.85f },
          { 0.72f, 0.30f, 0.28f, 0x121317, 0x1E2026, 0.90f } }, 2,
        { { 0.30f, 0.66f, 0.90f, -0.06f, 0.050f, 0xFFFFFF, 0.06f },
          { 0.45f, 0.66f, 1.05f, -0.06f, 0.018f, 0xFFFFFF, 0.05f } }, 2,
    },
};
#define SCENE_COUNT ((int) (sizeof(k_scenes) / sizeof(k_scenes[0])))

static uint16_t     *s_buf[WALLPAPER_VARIANT_COUNT];
static lv_img_dsc_t  s_dsc[WALLPAPER_VARIANT_COUNT];
static int           s_current = -1;
static uint32_t      s_generation = 0;     /* bumped by release; stale jobs discard */
static bool          s_job_running = false;
static int           s_pending_index = -1;
static wallpaper_done_cb_t s_pending_cb = NULL;

/* 768KB per buffer: PSRAM only, never the internal heap. The sim can be
 * asked to fail the allocation so the flat-pane fallback is exercised. */
static uint16_t *wp_alloc(size_t bytes)
{
#ifndef ESP_PLATFORM
    if (getenv("SIM_FAIL_SPIRAM")) return NULL;
#endif
    return heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM);
}

static const uint8_t k_bayer[8][8] = {
    {  0, 32,  8, 40,  2, 34, 10, 42 },
    { 48, 16, 56, 24, 50, 18, 58, 26 },
    { 12, 44,  4, 36, 14, 46,  6, 38 },
    { 60, 28, 52, 20, 62, 30, 54, 22 },
    {  3, 35, 11, 43,  1, 33,  9, 41 },
    { 51, 19, 59, 27, 49, 17, 57, 25 },
    { 15, 47,  7, 39, 13, 45,  5, 37 },
    { 63, 31, 55, 23, 61, 29, 53, 21 },
};

static inline float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
static inline void unpack(uint32_t rgb, float *r, float *g, float *b)
{
    *r = (float) ((rgb >> 16) & 0xFF);
    *g = (float) ((rgb >> 8) & 0xFF);
    *b = (float) (rgb & 0xFF);
}

/* Gaussian-ish falloff for the glows, tabulated over q = (d/r)^2 in [0, 6).
 * The pixel loop runs 384k times per variant on a 240MHz core, so it must
 * not call expf or sqrtf; everything below works in squared distance. */
#define FALLOFF_N 256
static float k_falloff[FALLOFF_N];
static bool  k_falloff_ready = false;

static void falloff_init(void)
{
    if (k_falloff_ready) return;
    for (int i = 0; i < FALLOFF_N; i++) {
        float q = (float) i * (6.0f / FALLOFF_N);
        k_falloff[i] = expf(-q * 1.6f);
    }
    k_falloff_ready = true;
}

/* Per-render precomputation of everything that does not depend on the pixel. */
typedef struct {
    float x, y, r, r2_in, r2_out, inv_span, inv_2r;   /* disc: squared edge band */
    float tr, tg, tb, br, bg, bb, alpha;
} disc_pre_t;
typedef struct {
    float x0, y0, lx, ly, inv_len, w_in, w_out, inv_span;
    float cr, cg, cb, alpha;
} band_pre_t;
typedef struct {
    float x, y, inv_r2, cr, cg, cb;                /* colour pre-multiplied by strength */
} glow_pre_t;

/* Draw one variant of a scene into a w*h RGB565 buffer. `frost` widens every
 * hard edge and lifts the colour toward white, which is what a real blur of
 * the sharp variant would look like through a lightly tinted pane. */
static void render_scene(const scene_t *sc, uint16_t *out, int w, int h, bool frost)
{
    const float inv_w = 1.0f / (float) w;
    const float edge  = frost ? 0.045f : 1.2f * inv_w;

    falloff_init();

    float tr, tg, tb, br, bg, bb;
    unpack(sc->top, &tr, &tg, &tb);
    unpack(sc->bottom, &br, &bg, &bb);

    disc_pre_t discs[3];
    for (int i = 0; i < sc->disc_count; i++) {
        const disc_t *d = &sc->discs[i];
        disc_pre_t *p = &discs[i];
        float r_in = d->r - edge, r_out = d->r + edge;
        if (r_in < 0.0f) r_in = 0.0f;
        p->x = d->x; p->y = d->y; p->r = d->r;
        p->r2_in = r_in * r_in;
        p->r2_out = r_out * r_out;
        p->inv_span = 1.0f / (p->r2_out - p->r2_in);
        p->inv_2r = 1.0f / (2.0f * d->r);
        unpack(d->top, &p->tr, &p->tg, &p->tb);
        unpack(d->bottom, &p->br, &p->bg, &p->bb);
        p->alpha = d->alpha;
    }

    band_pre_t bands[3];
    for (int i = 0; i < sc->band_count; i++) {
        const band_t *bd = &sc->bands[i];
        band_pre_t *p = &bands[i];
        p->x0 = bd->x0; p->y0 = bd->y0;
        p->lx = bd->x1 - bd->x0; p->ly = bd->y1 - bd->y0;
        p->inv_len = 1.0f / sqrtf(p->lx * p->lx + p->ly * p->ly);
        p->w_in = bd->half_w - edge;
        if (p->w_in < 0.0f) p->w_in = 0.0f;
        p->w_out = bd->half_w + edge;
        p->inv_span = 1.0f / (p->w_out - p->w_in);
        unpack(bd->rgb, &p->cr, &p->cg, &p->cb);
        p->alpha = bd->alpha;
    }

    glow_pre_t glows[3];
    for (int i = 0; i < sc->glow_count; i++) {
        const glow_t *gl = &sc->glows[i];
        glow_pre_t *p = &glows[i];
        p->x = gl->x; p->y = gl->y;
        p->inv_r2 = 1.0f / (gl->r * gl->r);
        unpack(gl->rgb, &p->cr, &p->cg, &p->cb);
        p->cr *= gl->strength; p->cg *= gl->strength; p->cb *= gl->strength;
    }

    for (int py = 0; py < h; py++) {
        const float y = (float) py * inv_w;
        const float v = (float) py / (float) (h - 1);
        uint16_t *row = out + (size_t) py * w;

        for (int px = 0; px < w; px++) {
            const float x = (float) px * inv_w;

            float r = tr + (br - tr) * v;
            float g = tg + (bg - tg) * v;
            float b = tb + (bb - tb) * v;

            for (int i = 0; i < sc->disc_count; i++) {
                const disc_pre_t *d = &discs[i];
                float dx = x - d->x, dy = y - d->y;
                float d2 = dx * dx + dy * dy;
                if (d2 >= d->r2_out) continue;
                float a = d->alpha;
                if (d2 > d->r2_in) {
                    float t = (d2 - d->r2_in) * d->inv_span;
                    a *= 1.0f - t * t * (3.0f - 2.0f * t);
                }
                float t = clampf((dy + d->r) * d->inv_2r, 0.0f, 1.0f);
                float cr = d->tr + (d->br - d->tr) * t;
                float cg = d->tg + (d->bg - d->tg) * t;
                float cb = d->tb + (d->bb - d->tb) * t;
                r += (cr - r) * a; g += (cg - g) * a; b += (cb - b) * a;
            }

            for (int i = 0; i < sc->band_count; i++) {
                const band_pre_t *bd = &bands[i];
                float dist = fabsf((x - bd->x0) * bd->ly - (y - bd->y0) * bd->lx) * bd->inv_len;
                if (dist >= bd->w_out) continue;
                float a = bd->alpha;
                if (dist > bd->w_in) {
                    float t = (dist - bd->w_in) * bd->inv_span;
                    a *= 1.0f - t * t * (3.0f - 2.0f * t);
                }
                r += (bd->cr - r) * a; g += (bd->cg - g) * a; b += (bd->cb - b) * a;
            }

            for (int i = 0; i < sc->glow_count; i++) {
                const glow_pre_t *gl = &glows[i];
                float dx = x - gl->x, dy = y - gl->y;
                float q = (dx * dx + dy * dy) * gl->inv_r2;
                if (q >= 6.0f) continue;
                float k = k_falloff[(int) (q * (FALLOFF_N / 6.0f))];
                r += gl->cr * k; g += gl->cg * k; b += gl->cb * k;
            }

            if (frost) {
                float luma = 0.299f * r + 0.587f * g + 0.114f * b;
                r += (luma - r) * 0.18f; g += (luma - g) * 0.18f; b += (luma - b) * 0.18f;
                r += (255.0f - r) * 0.10f; g += (255.0f - g) * 0.10f; b += (255.0f - b) * 0.10f;
            }

            /* Ordered dither before quantising to 5/6/5 bits, otherwise the
             * gradients band visibly on a 16-bit panel. */
            float dth = ((float) k_bayer[py & 7][px & 7] - 31.5f) / 64.0f;
            int ri = (int) (clampf(r, 0.0f, 255.0f) + dth * 8.0f);
            int gi = (int) (clampf(g, 0.0f, 255.0f) + dth * 4.0f);
            int bi = (int) (clampf(b, 0.0f, 255.0f) + dth * 8.0f);
            ri = ri < 0 ? 0 : (ri > 255 ? 255 : ri);
            gi = gi < 0 ? 0 : (gi > 255 ? 255 : gi);
            bi = bi < 0 ? 0 : (bi > 255 ? 255 : bi);

            row[px] = (uint16_t) (((ri >> 3) << 11) | ((gi >> 2) << 5) | (bi >> 3));
        }
    }
}

static void fill_dsc(lv_img_dsc_t *dsc, const uint16_t *buf, int w, int h)
{
    memset(dsc, 0, sizeof(*dsc));
    dsc->header.cf = LV_IMG_CF_TRUE_COLOR;
    dsc->header.always_zero = 0;
    dsc->header.w = (uint32_t) w;
    dsc->header.h = (uint32_t) h;
    dsc->data_size = (uint32_t) (w * h * 2);
    dsc->data = (const uint8_t *) buf;
}

int wallpaper_count(void) { return SCENE_COUNT; }

const char *wallpaper_name(int index)
{
    if (index < 0 || index >= SCENE_COUNT) return "?";
    return k_scenes[index].name;
}

int wallpaper_current(void) { return s_current; }

bool wallpaper_prepare(int index)
{
    if (index < 0 || index >= SCENE_COUNT) index = 0;
    if (index == s_current && s_buf[WALLPAPER_SHARP] && s_buf[WALLPAPER_FROST]) return true;

    const size_t bytes = (size_t) SCREEN_WIDTH * SCREEN_HEIGHT * 2;
    for (int v = 0; v < WALLPAPER_VARIANT_COUNT; v++) {
        if (s_buf[v]) continue;
        s_buf[v] = wp_alloc(bytes);
        if (!s_buf[v]) {
            ESP_LOGW(TAG, "PSRAM allocation failed; no wallpaper");
            wallpaper_release();
            return false;
        }
    }

    render_scene(&k_scenes[index], s_buf[WALLPAPER_SHARP], SCREEN_WIDTH, SCREEN_HEIGHT, false);
    render_scene(&k_scenes[index], s_buf[WALLPAPER_FROST], SCREEN_WIDTH, SCREEN_HEIGHT, true);
    fill_dsc(&s_dsc[WALLPAPER_SHARP], s_buf[WALLPAPER_SHARP], SCREEN_WIDTH, SCREEN_HEIGHT);
    fill_dsc(&s_dsc[WALLPAPER_FROST], s_buf[WALLPAPER_FROST], SCREEN_WIDTH, SCREEN_HEIGHT);
    s_current = index;
    /* The image cache is sized 0 in lv_conf.h, so a re-render with the same
     * data pointer needs no cache invalidation. */
    return true;
}

const lv_img_dsc_t *wallpaper_image(wallpaper_variant_t variant)
{
    if (variant < 0 || variant >= WALLPAPER_VARIANT_COUNT) return NULL;
    if (!s_buf[variant]) return NULL;
    return &s_dsc[variant];
}

void wallpaper_render_thumb(int index, lv_img_dsc_t *dsc, uint16_t *buf, int w, int h)
{
    if (index < 0 || index >= SCENE_COUNT) index = 0;
    render_scene(&k_scenes[index], buf, w, h, false);
    fill_dsc(dsc, buf, w, h);
}

void wallpaper_release(void)
{
    for (int v = 0; v < WALLPAPER_VARIANT_COUNT; v++) {
        if (s_buf[v]) heap_caps_free(s_buf[v]);
        s_buf[v] = NULL;
    }
    s_current = -1;
    s_generation++;
}

typedef struct {
    int index;
    uint32_t generation;
    wallpaper_done_cb_t done;
} wallpaper_job_t;

static void wallpaper_start_job(int index, wallpaper_done_cb_t done);

/* Worker: render into scratch buffers with no lock held, then take the LVGL
 * lock only to swap the buffers in. The swap is a few pointer writes, so the
 * lock is held for microseconds instead of the whole render. */
static void wallpaper_task(void *arg)
{
    wallpaper_job_t job = *(wallpaper_job_t *) arg;
    free(arg);

    const size_t bytes = (size_t) SCREEN_WIDTH * SCREEN_HEIGHT * 2;
    uint16_t *scratch[WALLPAPER_VARIANT_COUNT] = { NULL, NULL };
    bool ok = true;
    for (int v = 0; v < WALLPAPER_VARIANT_COUNT && ok; v++) {
        scratch[v] = wp_alloc(bytes);
        if (!scratch[v]) ok = false;
    }
    if (ok) {
        render_scene(&k_scenes[job.index], scratch[WALLPAPER_SHARP], SCREEN_WIDTH, SCREEN_HEIGHT, false);
        render_scene(&k_scenes[job.index], scratch[WALLPAPER_FROST], SCREEN_WIDTH, SCREEN_HEIGHT, true);
    } else {
        ESP_LOGW(TAG, "PSRAM allocation failed; no wallpaper");
    }

    lvgl_port_lock(-1);
    if (ok && job.generation == s_generation) {
        for (int v = 0; v < WALLPAPER_VARIANT_COUNT; v++) {
            if (s_buf[v]) heap_caps_free(s_buf[v]);
            s_buf[v] = scratch[v];
            fill_dsc(&s_dsc[v], s_buf[v], SCREEN_WIDTH, SCREEN_HEIGHT);
        }
        s_current = job.index;
    } else {
        for (int v = 0; v < WALLPAPER_VARIANT_COUNT; v++) {
            if (scratch[v]) heap_caps_free(scratch[v]);
        }
        if (ok) ok = false;   /* released while rendering: nothing installed */
    }
    s_job_running = false;
    if (job.done) job.done(ok);
    if (s_pending_index >= 0) {
        int next = s_pending_index;
        wallpaper_done_cb_t cb = s_pending_cb;
        s_pending_index = -1;
        s_pending_cb = NULL;
        wallpaper_start_job(next, cb);
    }
    lvgl_port_unlock();
    vTaskDelete(NULL);
}

static void wallpaper_start_job(int index, wallpaper_done_cb_t done)
{
    wallpaper_job_t *job = malloc(sizeof(*job));
    if (!job) { if (done) done(false); return; }
    job->index = index;
    job->generation = s_generation;
    job->done = done;
    s_job_running = true;
    if (xTaskCreate(wallpaper_task, "wallpaper", 4096, job, 3, NULL) != pdPASS) {
        s_job_running = false;
        free(job);
        ESP_LOGW(TAG, "could not start wallpaper task; rendering inline");
        bool ok = wallpaper_prepare(index);
        if (done) done(ok);
    }
}

void wallpaper_prepare_async(int index, wallpaper_done_cb_t done)
{
    if (index < 0 || index >= SCENE_COUNT) index = 0;
    if (index == s_current && s_buf[WALLPAPER_SHARP] && s_buf[WALLPAPER_FROST]) {
        if (done) done(true);
        return;
    }
    if (s_job_running) {
        s_pending_index = index;
        s_pending_cb = done;
        return;
    }
    wallpaper_start_job(index, done);
}
