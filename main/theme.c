#include <string.h>
#include "theme.h"

#ifdef ESP_PLATFORM
#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"
static const char *TAG = "theme";
#define THEME_NVS_NS   "gttouch"
#define THEME_NVS_IDX  "theme_idx"
#define THEME_NVS_CUS  "theme_custom"
#else
#define ESP_LOGI(...) do {} while (0)
#endif

/* Slot order must match theme_slot_t:
 * background, card, accent, red, text, text2, on-accent, border, nav */
static const theme_preset_t k_presets[] = {
    { "Bitaxe Red",     { 0x050506, 0x0F1218, 0xD4021B, 0xD4021B, 0xFFFFFF, 0xA3A3A3, 0x000000, 0x1A1D24, 0x0C0F14 } },
    { "Bitcoin Orange", { 0x0A0703, 0x17110A, 0xF7931A, 0xE8543F, 0xFFFFFF, 0xB09A80, 0x1A1206, 0x2A2113, 0x120D07 } },
    { "Matrix Green",   { 0x000A03, 0x03150A, 0x39FF14, 0xFF3B30, 0xD9FFD0, 0x6FA867, 0x001505, 0x0C2A14, 0x021207 } },
    { "Cyber Cyan",     { 0x00080D, 0x061620, 0x00E5FF, 0xFF2D6F, 0xEAFDFF, 0x7FA6B3, 0x001C24, 0x0B2C38, 0x041019 } },
    { "Deep Violet",    { 0x08040F, 0x150C24, 0x7C3BFF, 0xFF4D6D, 0xF2ECFF, 0x9E8FC2, 0xFFFFFF, 0x271A3D, 0x100823 } },
    { "Nord",           { 0x2E3440, 0x3B4252, 0x88C0D0, 0xBF616A, 0xECEFF4, 0x9BA6B8, 0x1D232C, 0x4C566A, 0x272D38 } },
    { "Gruvbox",        { 0x1D2021, 0x282828, 0xFE8019, 0xFB4934, 0xEBDBB2, 0xA89984, 0x1D2021, 0x3C3836, 0x232526 } },
    { "Paper (light)",  { 0xF4F2ED, 0xFFFFFF, 0xC2410C, 0xB91C1C, 0x18181B, 0x6B7280, 0xFFFFFF, 0xE2E0DA, 0xEDEAE3 } },
    { "Mono",           { 0x000000, 0x101010, 0xFFFFFF, 0xBBBBBB, 0xFFFFFF, 0x9A9A9A, 0x000000, 0x272727, 0x0A0A0A } },
};
#define PRESET_COUNT ((int)(sizeof(k_presets) / sizeof(k_presets[0])))

static uint32_t s_active[THEME_SLOT_COUNT];
static int      s_index      = 0;
static bool     s_custom     = false;
static bool     s_ready      = false;
static void   (*s_reload_cb)(void) = NULL;

static void apply_preset(int index)
{
    if (index < 0 || index >= PRESET_COUNT) index = 0;
    s_index  = index;
    s_custom = false;
    memcpy(s_active, k_presets[index].slot, sizeof(s_active));
}

static void theme_persist(void)
{
#ifdef ESP_PLATFORM
    nvs_handle_t h;
    if (nvs_open(THEME_NVS_NS, NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_i32(h, THEME_NVS_IDX, s_index);
    if (s_custom) {
        nvs_set_blob(h, THEME_NVS_CUS, s_active, sizeof(s_active));
    } else {
        nvs_erase_key(h, THEME_NVS_CUS);
    }
    nvs_commit(h);
    nvs_close(h);
#endif
}

void theme_init(void)
{
    if (s_ready) return;
    apply_preset(0);
    s_ready = true;

#ifdef ESP_PLATFORM
    nvs_handle_t h;
    if (nvs_open(THEME_NVS_NS, NVS_READONLY, &h) == ESP_OK) {
        int32_t idx = 0;
        if (nvs_get_i32(h, THEME_NVS_IDX, &idx) == ESP_OK) apply_preset((int) idx);

        size_t len = sizeof(s_active);
        uint32_t blob[THEME_SLOT_COUNT];
        if (nvs_get_blob(h, THEME_NVS_CUS, blob, &len) == ESP_OK && len == sizeof(blob)) {
            memcpy(s_active, blob, sizeof(s_active));
            s_custom = true;
        }
        nvs_close(h);
    }
    ESP_LOGI(TAG, "theme=%s custom=%d", theme_get_name(), (int) s_custom);
#endif
}

lv_color_t theme_color(theme_slot_t slot)
{
    if (!s_ready) theme_init();
    if (slot < 0 || slot >= THEME_SLOT_COUNT) slot = THEME_TEXT_PRIMARY;
    return lv_color_hex(s_active[slot]);
}

uint32_t theme_color_hex(theme_slot_t slot)
{
    if (!s_ready) theme_init();
    if (slot < 0 || slot >= THEME_SLOT_COUNT) slot = THEME_TEXT_PRIMARY;
    return s_active[slot];
}

const theme_preset_t *theme_presets(size_t *count)
{
    if (count) *count = PRESET_COUNT;
    return k_presets;
}

int         theme_preset_count(void) { return PRESET_COUNT; }
int         theme_get_index(void)    { if (!s_ready) theme_init(); return s_index; }
const char *theme_get_name(void)     { if (!s_ready) theme_init(); return s_custom ? "Custom" : k_presets[s_index].name; }

void theme_register_reload(void (*reload_cb)(void)) { s_reload_cb = reload_cb; }

void theme_commit(void)
{
    theme_persist();
    if (s_reload_cb) s_reload_cb();
}

void theme_set_index(int index)
{
    if (!s_ready) theme_init();
    apply_preset(index);
    theme_commit();
}

void theme_set_slot(theme_slot_t slot, uint32_t rgb)
{
    if (!s_ready) theme_init();
    if (slot < 0 || slot >= THEME_SLOT_COUNT) return;
    s_active[slot] = rgb & 0xFFFFFF;
    s_custom = true;
}
