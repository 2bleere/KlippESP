<p align="center"><img src="assets/boot_logo_preview.png" width="200" alt="KlippESP – wireless Klipper control"></p>

# KlippESP

**v1.2.5** · A Klipper printer display for the **Waveshare ESP32-S3-Touch-LCD-7** (800×480). It talks to
**Moonraker** (the API Mainsail uses) over Wi-Fi, so nothing needs to be installed on the printer.

> This is the `main` branch (Waveshare). The **WT32-SC01** (3.5" touch, ESP32 without PSRAM) version lives on the
> [`wt32-sc01`](../../tree/wt32-sc01) branch.

## Interfaces

Chosen with **Interface** on the web settings page (serial `ui=0..2`):

| Setting | Shows |
|---|---|
| **Auto** (default) | Touch UI when the GT911 touch controller answers at boot, otherwise the status dashboard |
| **Status dashboard** | Read-only status display, landscape or portrait |
| **Touch UI** | KlipperScreen-style touch interface (landscape). On a panel without touch it shows each screen in turn every 8 s, so you can preview it |

### Status dashboard

- **Header:** state badge (printing, paused, heating, error, …), printer and display IPs (stacked in landscape), Wi-Fi
  signal, day / date / time.
- **Hotend / Bed:** arc gauges show progress to the target (colour: cold, heating, at target); bars show the
  temperature against the configured `max_temp`; target and heater power.
- **Sensors, fans and host:** named list of chamber / Pi / MCU temperatures, every fan, CPU, memory and disk. Long
  names are shortened so they always fit (e.g. *Nevermore* → *Filter*).
- **Printing:** slicer thumbnail inside the progress ring (a placeholder cube when the file has none), elapsed and
  remaining time with the finish time, layer, Z, speed, flow, part fan and filament. When the slicer doesn't send
  `SET_PRINT_STATS_INFO`, the layer is estimated from Z and the file's layer heights, as Mainsail does.
- **Idle:** large status icon, plus a host panel (board model, OS, CPU/memory/disk, CPU temperature, uptime,
  under-voltage warnings).
- **Problem screens:** Wi-Fi setup, connecting, printer offline, Klipper starting or shut down (with Klipper's message).

### Touch UI

| Screen | What it does |
|---|---|
| **Home** | Temperatures, Klipper state, 9-tile menu; Restart / Firmware restart when Klipper is in error |
| **Print** | G-code files (newest first) with thumbnail, time, filament, layers and a Print button |
| **Printing** | Progress, times, layer, Z, temps, speed / flow / fan; Pause / Resume, Cancel, Reprint, Tune, Z offset. Opens automatically when a print starts |
| **Temperature** | Per-heater targets, ±1/5/10, keypad, PLA / PETG / ABS / TPU presets, cool down |
| **Move** | XY / Z jog, homing, QGL / Z-tilt / bed mesh when configured, motors off |
| **Extrude** | Preheat, length / speed, extrude / retract, Load / Unload macros |
| **Fans & Speed** | Speed factor, flow, part fan, `fan_generic` sliders |
| **Z Offset** | Live babystepping, reset, save to probe / endstop |
| **Macros · Console · Power** | Run macros, send G-code, toggle Moonraker power devices |
| **Settings** | Connection (Wi-Fi + printer on one screen), System (actions, screen-off timer), Power devices, Console, **About** |

A red **E-STOP** is always in the top bar.

## Setup

- **Setup hotspot / captive portal:** without Wi-Fi settings, or after 60 s without a connection, the display opens the
  Wi-Fi network **`KlippESP-Setup`**. Join it and the settings page opens by itself (otherwise browse to
  `http://192.168.4.1`).
- **Settings page** (at the display's IP once connected): Wi-Fi network (with a network scan), password, printer IP or
  hostname, Moonraker port (7125), API key, interface, orientation, time zone, 12/24 h clock, NTP server, firmware
  update, and an **About** section.
- **Serial (115200 baud):** `ssid=`, `pass=`, `host=`, `port=`, `apikey=`, `rotate=0..3`, `ui=0..2`, `ntp=`, `tz=`,
  `clock24=0|1`, then `save`; `show` and `status` print the current state.

### Moonraker access

The display's IP must be allowed in `moonraker.conf` under `[authorization] trusted_clients` (MainsailOS allows private
ranges by default). Otherwise get an API key from a trusted machine and enter it on the settings page:

```bash
curl http://<printer-ip>:7125/access/api_key
```

### Date & time

NTP with a configurable server (default `pool.ntp.org`), a list of common time zones with automatic DST (or a custom
POSIX rule) and a 12/24 h clock. Used for the header clock, the print finish time, "done ~" in file details and local
file dates.

## Build & first flash

Install [PlatformIO](https://platformio.org/) **Core 6.2 or newer**, connect the board by USB, then:

```bash
pio run -e waveshare_s3_lcd7 -t upload
```

If the upload doesn't start, hold **BOOT**, tap **RESET**, release BOOT and retry.

The build uses Arduino core **3.3.12 / ESP-IDF 5.5.5** ([pioarduino](https://github.com/pioarduino/platform-espressif32)).
The first build recompiles the core libraries with custom ESP-IDF options (code runs from PSRAM so the panel keeps
refreshing while flash is written); it downloads ESP-IDF and takes about 15 minutes. Later builds take seconds.

## Updating over Wi-Fi

- **PlatformIO:** set `upload_port` in `[env:waveshare_s3_lcd7_ota]` to the display's IP, then
  `pio run -e waveshare_s3_lcd7_ota -t upload`.
- **Browser:** settings page → **Firmware update** → upload `.pio/build/waveshare_s3_lcd7/firmware.bin`.
- **curl:** `curl -F fw=@.pio/build/waveshare_s3_lcd7/firmware.bin http://<display-ip>/update`

The display shows a progress bar during the update and restarts when done. Settings are kept. These endpoints are open
to your LAN, so keep the display on a trusted network.

`http://<display-ip>/info` returns the version, ESP-IDF version, build time, uptime and free memory as JSON.

## Versions and releases

The version is set in [src/version.h](src/version.h) and shown on the About screen, the web page and `/info`.
Releases are tagged `vX.Y.Z` on `main` (Waveshare) and `vX.Y.Z-wt32` on `wt32-sc01`.

## Previewing without a printer

`tools/mock_moonraker.py` is a mock Moonraker server (standard library only) with simulated temperatures, prints and
host stats, plus a control page for switching scenarios (idle, heating, printing, paused, complete, error, Klipper
shutdown, offline):

```bash
python3 tools/mock_moonraker.py
```

Point the display's printer IP at the machine running it (allow port 7125 in its firewall).

## Hardware notes

Pins and timings are in [src/board.h](src/board.h).

- **Panel:** driven with ESP-IDF's `esp_lcd` RGB driver. The frame buffer is in PSRAM and is read through two small
  bounce buffers in internal RAM; with code running from PSRAM this keeps the image stable during flash writes.
  Pixel clock 12 MHz (`LCD_PCLK_HZ`).
- **CH422G expander:** EXIO*n* is bit *n*; TP_RST = EXIO1, backlight = EXIO2, LCD_RST = EXIO3, SD_CS = EXIO4,
  USB_SEL = EXIO5 (keep it low, or USB disconnects until a power cycle).
- **Brightness:** the backlight is on/off only (no dimming on this board).
- **Wi-Fi:** the PCB antenna sits behind the panel, so placement matters. The serial log prints signal strength and free
  memory every minute.

## Tools

| Script | Purpose |
|---|---|
| `tools/mock_moonraker.py` | Mock printer for previewing every screen |
| `tools/gen_icons.py` | Regenerates the Material Design Icons subset fonts (needs Node.js) |
| `tools/gen_logo.py` | Regenerates the boot and web logo from `assets/logo_source.png` (needs Pillow); crops any artwork on a plain background |
| `tools/touch_about.py` | PlatformIO pre-build step that keeps the About build time current |

## Layout

```
src/main.cpp          start-up: settings, Wi-Fi, display, boot logo, Moonraker, UI
src/board.*           RGB panel (esp_lcd + bounce buffers), GT911 touch, CH422G, LVGL driver, rotation
src/moonraker.*       background polling, commands, thumbnails, power devices, host stats, layer estimate
src/net.*             Wi-Fi, setup hotspot + captive portal, settings page, OTA, /info, serial config
src/settings.*        settings stored in NVS
src/clock.*           NTP + time zones
src/version.h         release version
src/about.*           facts for the About screen and web page
src/ui_dashboard.cpp  status dashboard (landscape + portrait)
src/ui_core.cpp       theme, navigation, dialogs, boot logo, update screen, touch home
src/ui_print.cpp      files, print status, Z offset (touch)
src/ui_control.cpp    temperature, move, extrude, fans, macros, power (touch)
src/ui_system.cpp     console, settings, connection, system, about (touch)
src/fonts/            generated icon fonts
include/lv_conf.h     LVGL 8.3 configuration
```

Icons: [Material Design Icons](https://pictogrammers.com/library/mdi/) (Apache-2.0).
