# Turbo Touch screen simulator

Runs the **real** Turbo Touch UI (`../main/*.c`, LVGL 8.4) on a workstation and
streams the 800x480 framebuffer to a browser on `localhost:8010`. Clicks go back
in as GT911 touch events. Nothing is flashed to hardware.

It builds with **gcc + make + python3 only** — no ESP-IDF, no SDL, no emscripten.

```
browser :8010  ──HTTP──►  server.py  ──stdin/stdout──►  gtsim
   canvas 800x480            │                            │
   theme + touch UI          │                       real home.c / settings.c /
                             │                       wifi.c / night.c ... + LVGL
                             └──polls──► Bitaxe /api/system/info
                                         translated into BAP sentences
```

## Build and run

From WSL / Linux / macOS:

```bash
cd sim
make -j8
python3 server.py                        # demo values
python3 server.py --live 192.168.1.50   # mirror a real Bitaxe
```

Then open <http://localhost:8010>. (On Windows, run the above inside WSL — WSL2
forwards `localhost`, so the browser on Windows reaches it directly.)

## What the browser gives you

- **Theme preset** — the nine presets in `main/theme.c`. Selecting one calls the
  same `theme_set_index()` the device does, and the active screen is rebuilt.
- **Palette** — live per-slot colour pickers mapping 1:1 onto the `COLOR_*`
  macros in `main/home.h`. Edits mark the theme custom.
- **Screen** — jump straight to any screen instead of tapping through.
- **Data** — mirror a real Bitaxe, load demo values, or hand-send a raw BAP
  sentence. Checksums are computed for you.
- **Sim log** — the firmware's own `ESP_LOGI` output, including the real
  `BAP_PARSER` lines.

## Headless screenshots

```bash
python3 shot.py shots                       # every preset, home screen
python3 shot.py shots --screen settings     # a specific screen
python3 shot.py shots --preset 2            # one preset
```

Useful for eyeballing a palette across screens, or attaching before/after
images to a PR.

## How it works

`sim_main.c` replaces the three things that are genuinely board-specific:

| Firmware file | Replaced by |
|---|---|
| `waveshare_rgb_lcd_port.c` (ST7262 RGB panel, GT911, backlight PWM) | an in-memory RGB565 framebuffer |
| `lvgl_port.c` (esp_lvgl_port task + vsync) | a plain LVGL tick/timer loop |
| `main.c` | the same boot sequence, then a stdin command loop |

`sim_rt.c` implements the ESP-IDF surface the screens touch — FreeRTOS tasks and
queues on pthreads, NVS as a flat `sim_nvs.txt`, canned WiFi scan results, and
`esp_http_client` shelling out to `curl`. `sim_shims.c` stands in for the OTA
updater and the BAP UART client, since the sim injects BAP sentences directly
into the real parser instead of reading a serial port.

Everything else — every screen, every font, every asset, and LVGL itself — is
compiled from the repo unchanged. The `stubs/` headers only satisfy `#include`s;
they never reimplement UI behaviour.

### Excluded from the build

`main.c`, `lvgl_port.c`, `waveshare_rgb_lcd_port.c`, `ota_update.c`,
`ota_screen.c`, `bap_uart.c`, `bap_client.c`, and
`assets/logo_background.c` (which `loading.c` `#include`s directly).

### Command protocol

`server.py` speaks this to `gtsim` over stdin; you can also drive it by hand:

```
T <x> <y> <0|1>     touch
B <sentence>        feed a BAP sentence, e.g. $BAP,RES,hashrate,2450.0*3D
P <index>           select theme preset
S <slot> <rrggbb>   override one palette slot
C                   commit theme (persist + rebuild screen)
K <skin>            select skin: 0 classic, 1 glass (home rebuilds)
G <what> <value>    glass skin: layout 0|1, widgets <hexmask>, wall <index>,
                    drawer 0|1, sheet 0..5 (widgets, layout, wallpaper, pool,
                    icons), scroll <px>
N <screen>          home night block clock price mempool wifi settings
D off | D mode <n>  display off (as the corner control); button mode 0..3
R                   force repaint
Q                   quit
```

### Glass skin screenshots

`shot.py` takes raw commands, taps and drags, so the Glass surface and its
pickers can be rendered headlessly:

```bash
python3 shot.py out --preset 0 --cmd "K 1" --name glass-twin
python3 shot.py out --preset 0 --cmd "K 1" --cmd "G layout 0" --name glass-single
python3 shot.py out --preset 0 --cmd "K 1" --cmd "G drawer 1" --name glass-drawer
python3 shot.py out --preset 0 --cmd "K 1" --cmd "G sheet 3" --name glass-wallpapers
python3 shot.py out --preset 0 --cmd "K 1" --touch 400,300 --touch 47,395   # tap to open the drawer, tap Widgets
python3 shot.py out --preset 0 --cmd "K 1" --drag 400,400,400,120           # scroll the widget grid
```

Every screen has a Glass form, so `--screen settings --cmd "K 1"` renders the
glass settings and so on. `--online` lets price and mempool fetch for real
through `curl` (pair it with `--settle 12`), and `SIM_DEBUG=1` in the
environment lets the firmware log through to the terminal.

Glass preferences persist to `sim_nvs.txt` like everything else, so delete it
between runs if you want the defaults back.

Frames come back on stdout as `"GTFB" | u32 frame | u16 w | u16 h | RGB565 LE`.

## Caveats

- The sim renders with LVGL's software renderer at host speed; it is not a
  timing model of the ESP32-S3. Layout, colour and touch behaviour are faithful,
  frame pacing is not.
- `SIM_OFFLINE=1` is set by default so `price.c` / `mempool.c` do not stall on
  network calls. Unset it to let them fetch for real through `curl`.
- `SIM_TASKS=0` disables background task spawning if you are chasing a crash.
