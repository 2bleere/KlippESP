# KlippESP – notes for working on this repo

> **Core:** Waveshare on Arduino core 3.3.11 / ESP-IDF 5.5 (pioarduino 55.03.311).
> `custom_sdkconfig` enables XIP from PSRAM + IRAM-safe RGB ISR; `board.cpp` drives the panel through `esp_lcd`
> with bounce buffers (no Arduino_GFX), so flash writes no longer disturb the image and the update screen shows real
> progress. Build with **PlatformIO Core 6.1.19** (`~/.pio61/bin/pio`): Core 6.2 wants SCons 4.11 and fights the
> platform's 4.8 (`ModuleNotFoundError: SCons.Tool.FortranCommon`). If that error appears, reinstall
> `pio pkg install -g -t "platformio/tool-scons@~4.40801.0"`. First build rebuilds the core libs (~15 min).
> `default_16MB.csv` is in the repo because the hybrid build doesn't find the stock one. `/info` returns IDF version,
> build time, uptime and free heap.

ESP32-S3 firmware (Arduino core 3.3 / ESP-IDF 5.5, PlatformIO, LVGL 8.3, esp_lcd RGB driver, ArduinoJson 7) that shows
Klipper status from Moonraker over Wi-Fi on a Waveshare ESP32-S3-Touch-LCD-7 (800×480 RGB565, N16R8).

## Build / flash / update

```bash
~/.pio61/bin/pio run -e waveshare_s3_lcd7                     # build
~/.pio61/bin/pio run -e waveshare_s3_lcd7 -t upload           # first flash over USB
~/.pio61/bin/pio run -e waveshare_s3_lcd7_ota -t upload       # ArduinoOTA to upload_port in platformio.ini
curl -F fw=@.pio/build/waveshare_s3_lcd7/firmware.bin http://<display-ip>/update   # browser/HTTP OTA
```

- The update screen shown during an install comes from the firmware *receiving* it; to see a changed update screen,
  install twice.
- Full USB flash offsets (merged images from other firmware overwrite them): bootloader 0x0, partitions 0x8000,
  boot_app0 0xe000, app 0x10000.

## Architecture

| File | Role |
|---|---|
| `src/main.cpp` | setup: settings → Wi-Fi → board → boot logo (2.5 s) → Moonraker tasks → UI; loop: LVGL + `net_loop()` |
| `src/board.*` | RGB panel (esp_lcd, bounce buffers), CH422G expander, GT911 touch (probed at boot), LVGL driver, rotation, `board_lcd_freeze()` |
| `src/moonraker.*` | FreeRTOS poll task (1 s) + command queue task + urgent one-shot POST tasks; `mr_get()` snapshots state under a mutex |
| `src/net.*` | Wi-Fi, setup AP + captive portal (DNS + probe redirects), settings web page, `/scan`, `/update`, ArduinoOTA, serial config |
| `src/ui_dashboard.cpp` | **no-touch dashboard** (the mode the developer's unit runs: its panel has no touch layer). Layout engine for landscape + portrait (`compute_layout`) |
| `src/ui_core.cpp` etc. | touch UI (KlipperScreen-like), change-only setters, update screen, boot logo |
| `tools/` | `mock_moonraker.py` (scenarios + per-file thumbnails), `gen_icons.py` (MDI fonts), `gen_logo.py` (boot/web logo) |

Mode is chosen at boot: GT911 found → touch UI (landscape only, 180° flip allowed); else dashboard (any rotation).

## Hardware facts and hard-won constraints

- CH422G: write 0x24←0x01 (outputs), then 0x38←state. **EXIOn = bit n**: TP_RST=bit1, BL=bit2, LCD_RST=bit3, SD_CS=bit4,
  USB_SEL=bit5 (high switches the native USB port to CAN – never set it, it drops USB until power-cycle).
- **Internal RAM is scarce.** Big internal draw buffers (2×800×60) starved Wi-Fi/lwIP (9 KB free → disconnects, web hangs).
  LVGL heap is in PSRAM (`include/lv_psram.h`); one 800×20 internal draw buffer. Keep ~200 KB internal free
  (serial `[stat]` line every 60 s prints heap).
- **PSRAM bandwidth:** the panel streams its framebuffer from PSRAM; heavy PSRAM use shifts/shakes the image.
  Pixel clock is 12 MHz; don't put draw buffers in PSRAM.
- **Flash writes vs the panel:** on core 2.x flash writes garbled the RGB panel (no frame memory, MSPI shared), so the
  update screen went dark. Now XIP from PSRAM + IRAM-safe ISR + bounce buffers keep it refreshing: `ui_ota_progress()`
  shows a live bar (redrawn every 2 % via `lv_refr_now`). `board_lcd_freeze()` is a no-op. A failed update removes the overlay.
- **Flicker:** LVGL redraws on every property set even if unchanged. Always update widgets via `set_text`,
  `set_text_color`, `set_bg_color`, `set_arc_color`, `set_visible`, `set_height` (change-only).
- ArduinoOTA receive timeout raised to 10 s and web upload socket timeout to 20 s (defaults of 1 s abort on Wi-Fi /
  flash-erase stalls). Moonraker polling pauses during updates (`mr_pause`).
- Wi-Fi: PCB antenna behind the panel; RSSI swings −55…−82 dBm with placement. Setup AP only after 60 s continuously
  disconnected (not "since boot").
- NVS namespace stays `klcd` (old project name) so saved settings survive.
- Dashboard names must always fit: use `fit_name()` (drops "fan", `ABBR` table, gentle truncation) – no "…" for names.
- Gauges: arc = progress to target from 20 °C (temp on max scale when off); bar = temp / configured `max_temp`
  (read from `configfile.settings`, fallback 300/120).

## Mock Moonraker

`python3 tools/mock_moonraker.py [--port 7125] [--speed 20] [--undervoltage]` – control page at `/`.
Implements every endpoint the firmware uses; keep it in sync when the firmware starts using a new endpoint.

## Conventions

- Match the existing style (4-space indent, `snprintf` into stack buffers, comments explain *why*).
- Icons: add to `ICONS` in `tools/gen_icons.py` and regenerate; use `ICON_*` macros with `icons_16/20/28/72` fonts
  (they fall back to Montserrat for plain text).
