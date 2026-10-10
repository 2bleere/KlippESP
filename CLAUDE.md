# KlippESP – notes for working on this repo

> **This is the `wt32-sc01` branch:** touch UI only, for the WT32-SC01 (classic ESP32, 4 MB flash, NO PSRAM,
> ST7796 480×320 SPI + FT6336U, LovyanGFX). Envs: `wt32_sc01`, `wt32_sc01_ota`. Layout constants: `TOPBAR_H 40`,
> `CONTENT_W 480`, `CONTENT_H 280`; fonts Montserrat 12/14/16/20/28 + icons_20/28. RAM is the constraint (~118 KB free at
> runtime): LVGL has its own 48 KB pool (`lv_conf.h`), Moonraker JSON is parsed straight from the socket with
> filters (`httpGetJson`), one 480×20 DMA draw buffer, file list capped at 60. No print thumbnails here: a PNG
> decoder needs ~70 KB and froze the UI. Watch the serial `[stat]` line (heap, largest block, lvgl %, frag %). The Waveshare-specific notes below
> (PSRAM, CH422G, RGB panel, dashboard) apply to `main`. Port Moonraker/net fixes between branches with cherry-pick.

ESP32-S3 firmware (Arduino core 2.0.14 / ESP-IDF 4.4, PlatformIO, LVGL 8.3, Arduino_GFX 1.4.7, ArduinoJson 7) that shows
Klipper status from Moonraker over Wi-Fi on a Waveshare ESP32-S3-Touch-LCD-7 (800×480 RGB565, N16R8).

## Build / flash / update

```bash
pio run -e waveshare_s3_lcd7                     # build
pio run -e waveshare_s3_lcd7 -t upload           # first flash over USB
pio run -e waveshare_s3_lcd7_ota -t upload       # ArduinoOTA to upload_port in platformio.ini
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
| `src/board.*` | RGB panel, CH422G expander, GT911 touch (probed at boot), LVGL driver, rotation, `board_lcd_freeze()` |
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
- **Flash writes blank the panel** (MSPI shared). This panel also goes black if the LCD refresh is stopped. The update
  screen therefore shows a message 4 s (before any flash write), then backlight off + `board_lcd_freeze()`; a restart
  brings it back. A failed update restarts the device.
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
