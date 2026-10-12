# KlippESP – notes for working on this repo

One code base for several ESP32 Klipper displays. It talks to Moonraker over Wi-Fi and runs Arduino core 3.3.12 /
ESP-IDF 5.5.5 (pioarduino 55.03.312-1), with LVGL 8.3 and ArduinoJson 7. Build with PlatformIO Core 6.2+.

| Environment | Board | Notes |
|---|---|---|
| `waveshare_s3_lcd7` | Waveshare ESP32-S3-Touch-LCD-7: 800×480 RGB, GT911, CH422G, 16 MB flash, 8 MB PSRAM | `custom_sdkconfig` rebuilds the core libs (code in PSRAM) |
| `wt32_sc01` | WT32-SC01 V3.3: ESP32-D0WD, 480×320 ST7796 SPI, FT6336U, 4 MB flash, **no PSRAM** | LovyanGFX 1.2.28 |

Each environment has an `_ota` twin (ArduinoOTA to `upload_port`).

## Build / flash / update

```bash
tools/pio run -e waveshare_s3_lcd7 -t upload     # USB; tools/pio = pio with a per-board PlatformIO directory
tools/pio run -e wt32_sc01_ota -t upload         # over the air
tools/pio run                                    # build every board
curl -F fw=@.pio/build/<env>/firmware.bin http://<display-ip>/update   # browser/HTTP OTA (unreliable on WT32)
```

- **Always build through `tools/pio` when switching boards.** The Waveshare's `custom_sdkconfig` recompiles the
  shared Arduino core libraries. Building any other board in the same PlatformIO directory reinstalls them, even if
  that board has its own `custom_sdkconfig`, and the next Waveshare build then takes ~15 min. `tools/pio` uses
  `~/.platformio` for the Waveshare and `~/.platformio-wt32` for the WT32.
- **Never run two builds in this project at the same time.** The core-library rebuild writes `CMakeLists.txt` /
  `sdkconfig.*` into the project root.
- The update screen during an install comes from the firmware *receiving* it. To see a changed update screen,
  install twice.

## Per-board code

- `src/board.h` selects the board from the `BOARD_*` build flag. It defines `BOARD_NAME`, `LCD_LONG/SHORT`,
  `BOARD_HAS_DIMMING` and `BOARD_ENV`. Exactly one of `board_waveshare7.cpp` / `board_wt32sc01.cpp` compiles.
- `include/klippesp_config.h` turns the board into feature switches. `lv_conf.h` includes it, so they apply to LVGL
  too:
  - `KLIPPESP_PSRAM`: LVGL heap in PSRAM via `lv_psram.h`; otherwise a 36 KB LVGL pool.
  - `KLIPPESP_THUMBNAILS`: slicer PNG thumbnails in Moonraker, Files details and the dashboard.
  - `KLIPPESP_DASHBOARD`: `ui_dashboard.cpp` and the Interface setting `cfg.uiMode` (0 auto, 1 dashboard, 2 touch UI,
    which cycles screens when there's no touch).
  - `KLIPPESP_LARGE_FONTS`: Montserrat 24/32/48 and `icons_16/72`.
- To add a board:
  1. Add a `BOARD_*` section in `board.h`, a `board_<name>.cpp` and a block in `klippesp_config.h`.
  2. Add an env in `platformio.ini`.
  3. Map it in `tools/pio` if it uses custom libraries.

## Touch UI scaling

The touch UI is designed for 480×320.
- **Fixed sizes:** every one goes through `U(x)`. Integer scale `UI_K256`: 1.0 on 480×320, 1.5 on 800×480.
- **Fonts:** through `FONT(n)` / `ICONS(n)`, which pick the largest compiled-in font ≤ n×scale.
- **Layout:** derive the remaining space from `CONTENT_W` / `CONTENT_H` rather than hard-coding it. When dividing
  space, subtract the scaled gaps (`(w - 2 * U(5)) / 3`), or rounding wraps flex rows.
- **Orientation:** every builder has a landscape and a portrait branch (`PORTRAIT`). Keep both working.
- **Checking it:** the Waveshare serves `/screenshot.bmp` (rotated like the display). Set Interface to Touch UI and
  every screen cycles every 8 s, so you can capture all of them.

## Memory (WT32: no PSRAM)

- **Free heap:** ~105 KB at runtime (min ~85 KB).
- **LVGL pool:** 36 KB; the worst screen is Home at ~22.5 KB.
- **Long lists:** Files and Macros are virtual lists (`ui_vlist`): only on-screen cells exist and are re-bound on
  scroll. A fully built 60-file list needed ~54 KB and crashed.
- **Moonraker data:** the file list and macro names live only in the Moonraker module (`mr_file`, `mr_macro`) — no
  per-tick copies. JSON is parsed straight from the socket with filters (`httpGetJson`).
- **Task stacks:** sized from measured high-water marks: poll 7 KB, cmd 6 KB, urgent 6 KB.
- `/info` reports heap, LVGL now/peak/frag and stack headroom. Check it after UI changes.

## Architecture

| File | Role |
|---|---|
| `src/main.cpp` | setup: settings → Wi-Fi → board → boot logo → Moonraker tasks → UI; loop: LVGL + `net_loop()` |
| `src/board_*.cpp` | panel, touch, backlight, LVGL driver, rotation, `board_framebuffer()` for screenshots |
| `src/moonraker.*` | FreeRTOS poll task (1 s) + command queue task + urgent one-shot POST tasks; `mr_get()` snapshots state under a mutex; layer estimate; power-on Klipper restart |
| `src/net.*` | Wi-Fi, setup AP + captive portal, settings page, `/scan`, `/update`, `/info`, `/screenshot.bmp`, ArduinoOTA, serial config |
| `src/ui_core.cpp` | touch-UI framework: scaling, top bar, navigation, virtual lists, dialogs, update screen, screen sleep, boot logo |
| `src/ui_status.cpp` | Home and Printing pages |
| `src/ui_print.cpp` / `ui_control.cpp` / `ui_system.cpp` | files + Z offset / temperature, move, extrude, fans, macros, power / console, settings, connection, system, date & time, about |
| `src/ui_dashboard.cpp` | read-only dashboard (`KLIPPESP_DASHBOARD`), own layout engine for 800×480 landscape + portrait |
| `src/about.*`, `src/version.h` | About facts; release version (tags `vX.Y.Z`) |

## Hardware facts and hard-won constraints

- **Waveshare CH422G:** write 0x24←0x01 (outputs), then 0x38←state. **EXIOn = bit n**: TP_RST=1, BL=2, LCD_RST=3,
  SD_CS=4, USB_SEL=5. Setting USB_SEL switches the native USB to CAN and drops USB until a power cycle, so never set
  it.
- **Waveshare panel:** `esp_lcd` RGB driver. The frame buffer is in PSRAM, read through bounce buffers; with XIP from
  PSRAM (`custom_sdkconfig`) flash writes don't disturb the image, so the update screen shows real progress. Pixel
  clock 12 MHz. Keep the LVGL draw buffer in internal RAM: PSRAM bandwidth shakes the image. Internal RAM is scarce;
  big internal buffers starved Wi-Fi.
- **WT32:**
  - **Display:** ST7796 keeps its own frame memory, so there is no screenshot and the update screen is never at risk.
  - **Touch:** FT6336U at 0x38 on SDA 18 / SCL 19 / INT 39.
  - **Backlight:** PWM on GPIO 23.
  - **No light sensor.**
- **Flicker:** LVGL redraws on every property set, even when unchanged. Update widgets via `set_text`,
  `set_text_color`, `set_bg_color`, `set_arc_color`, `set_visible`, `set_height` (change-only).
- **OTA:** the ArduinoOTA receive timeout is 10 s and the web upload socket timeout 20 s. Moonraker polling pauses
  during updates (`mr_pause`). On the WT32 the browser `/update` upload drops mid-way — use PlatformIO OTA.
- **Wi-Fi:** the setup AP starts only after 60 s continuously disconnected. The NVS namespace stays `klcd` (old project
  name), so saved settings survive.
- **Dashboard:** names must always fit — use `fit_name()`. Gauges: arc = progress to target from 20 °C; bar =
  temp / configured `max_temp`.

## Mock Moonraker

`python3 tools/mock_moonraker.py [--port 7125] [--speed 20] [--undervoltage]` – control page at `/`.
Implements every endpoint the firmware uses; keep it in sync when the firmware starts using a new endpoint.

## Conventions

- Match the existing style (4-space indent, `snprintf` into stack buffers, comments explain *why*).
- Icons: add to `ICONS` in `tools/gen_icons.py` and regenerate. Use `ICON_*` macros with `ICONS(n)` in the touch UI
  (`icons_16/72` exist only with large fonts). The icon fonts fall back to Montserrat for plain text.
- Logo: replace `assets/logo_source.png` and run `tools/gen_logo.py` (Pillow). Set `BOOT_SIZE` per board when adding
  one.
