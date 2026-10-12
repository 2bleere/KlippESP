<p align="center"><img src="assets/boot_logo_preview.png" width="200" alt="KlippESP – wireless Klipper control"></p>

# KlippESP – WT32-SC01 edition

**v1.2.5** · A KlipperScreen-style touch display for the Wireless-Tag **WT32-SC01 (V3.3)**. It talks to **Moonraker**
(the API Mainsail uses) over Wi-Fi, so nothing needs to be installed on the printer.

> This is the `wt32-sc01` branch. The **Waveshare ESP32-S3-Touch-LCD-7** version (7" touch UI + no-touch status
> dashboard) lives on [`main`](../../tree/main).

**Hardware:** ESP32-D0WD, 4 MB flash, no PSRAM, 3.5" ST7796 **480×320** SPI display, FT6336U capacitive touch, dimmable
backlight.

## Screens

Every screen has a landscape (480×320) and a portrait (320×480) layout. The top bar shows the screen title (a live
state badge on Home and Printing), back / home buttons, the date and time, and a red **E-STOP**.

| Screen | What it does |
|---|---|
| **Home** | Hotend and bed gauges (arc: progress to target; bar: temperature against `max_temp`), a list of sensors and fans, the printer host's CPU temperature, CPU and RAM use, and a 3×3 menu. When the printer is unavailable it shows a status card with only Settings and Power (plus Restart / Firmware restart while Klipper is down) |
| **Print** | G-code files, newest first (up to 60), with date and size; details show time, filament, layers and a Print button |
| **Printing** | Progress ring, finish time, hotend / bed gauges, time, remaining, layer, Z, speed, flow, fan and filament; Pause / Resume, Cancel, Reprint, Tune, Z offset |
| **Temperature** | Per-heater targets, ±1/5/10, keypad, PLA / PETG / ABS / TPU presets, cool down |
| **Move** | XY / Z jog, homing, QGL / Z-tilt / bed mesh when configured, motors off |
| **Extrude** | Preheat, length / speed, extrude / retract, Load / Unload macros |
| **Fans & Speed** | Speed factor, flow, part fan, `fan_generic` sliders |
| **Z Offset** | Live babystepping, reset, save to probe / endstop |
| **Macros · Console** | Run macros (confirmed first), send G-code |
| **Power** | Moonraker power devices. With a single device the Home tile shows its state and toggles it directly (it asks first only while printing). Optional: firmware-restart Klipper after switching the printer on |
| **Settings** | Connection (Wi-Fi + printer), System (orientation, brightness, screen-off timer, restarts, host reboot / shutdown), Power devices, Console, Date & Time, **About** |

**It follows the printer:** a print starting, pausing, finishing, being cancelled or failing opens the Printing page;
Klipper going down returns to Home. The page title always shows the current state.

**Screen off:** after the chosen idle time (Never / 1–60 min); a touch wakes it. It stays on while a print runs or is
paused, and wakes for a firmware update.

**Layers:** when the slicer doesn't send `SET_PRINT_STATS_INFO` (OrcaSlicer by default), the current / total layer is
estimated from Z and the file's layer heights, as Mainsail does.

## Setup

- **Setup hotspot / captive portal:** without Wi-Fi settings, or after 60 s without a connection, the display opens the
  Wi-Fi network **`KlippESP-Setup`**. Join it and the settings page opens by itself (otherwise browse to
  `http://192.168.4.1`).
- **On the display:** Settings → Connection (Wi-Fi scan, password, printer IP, port, API key with an on-screen keyboard).
- **Settings page** (at the display's IP once connected): Wi-Fi network (with a network scan), password, printer IP or
  hostname, Moonraker port (7125), API key, orientation, brightness, screen-off time, *Restart Klipper after power on*,
  time zone, 12/24 h clock, NTP server, firmware update, and an **About** section.
- **Serial (115200 baud):** `ssid=`, `pass=`, `host=`, `port=`, `apikey=`, `rotate=0..3`, `bri=10..255`,
  `sleep=<minutes>`, `pwrrestart=0|1`, `ntp=`, `tz=`, `clock24=0|1`, then `save`; `show` and `status` print the
  current state.

### Moonraker access

The display's IP must be allowed in `moonraker.conf` under `[authorization] trusted_clients` (MainsailOS allows private
ranges by default). Otherwise get an API key from a trusted machine and enter it on the settings page:

```bash
curl http://<printer-ip>:7125/access/api_key
```

To have Klipper come back by itself after a power device switches the printer on (from anywhere, not just this
display), add `restart_klipper_when_powered: True` and `restart_delay: 3` to the device's `[power …]` section.

### Date & time

NTP with a configurable server (default `pool.ntp.org`), a list of common time zones with automatic DST (or a custom
POSIX rule on the web page) and a 12/24 h clock. Used for the top-bar clock, the print finish time, "done ~" in file
details and local file dates.

## Build & flash

Install [PlatformIO](https://platformio.org/) **Core 6.2 or newer**, connect the board by USB, then:

```bash
pio run -e wt32_sc01 -t upload
```

The build uses Arduino core **3.3.12 / ESP-IDF 5.5.5** ([pioarduino](https://github.com/pioarduino/platform-espressif32))
and LovyanGFX 1.2.28. It keeps its PlatformIO packages in `~/.platformio-wt32` (set in `platformio.ini`), so it never
replaces the custom-built core libraries of the Waveshare build.

### Updating over Wi-Fi

- **PlatformIO (recommended):** set `upload_port` in `[env:wt32_sc01_ota]` to the display's IP, then
  `pio run -e wt32_sc01_ota -t upload`. The screen shows a progress bar and restarts when done; settings are kept.
- **Browser / curl** (`/update`): **known issue** on this board, the upload drops partway through. Use PlatformIO or USB.

`http://<display-ip>/info` returns the version, ESP-IDF version, build time, uptime, free heap, LVGL pool use and task
stack headroom as JSON.

## Versions and releases

The version is set in [src/version.h](src/version.h) and shown on the About screen, the web page and `/info`.
Releases are tagged `vX.Y.Z-wt32` on this branch and `vX.Y.Z` on `main`.

## Memory (no PSRAM)

About 105 KB of RAM stays free at runtime. To keep it that way:

- LVGL has its own 36 KB pool (worst screen, Home, uses about 22.5 KB).
- The file list and macros are **virtual lists**: only the rows on screen exist and are reused while scrolling, so a
  long list costs the same as a short one.
- Print thumbnails are not shown (a PNG decoder needs ~70 KB); a hotend icon sits in the progress ring instead.
- Check `/info` after UI changes: `lvgl_peak`, `heap_min` and `stack_free_*`.

## Previewing without a printer

`tools/mock_moonraker.py` is a mock Moonraker server (standard library only) with simulated temperatures, prints and
host stats, plus a control page for switching scenarios (idle, heating, printing, paused, complete, error, Klipper
shutdown, offline):

```bash
python3 tools/mock_moonraker.py
```

Point the display's printer IP at the machine running it (allow port 7125 in its firewall).

## Hardware notes

Pins are in [src/board.h](src/board.h) / [src/board.cpp](src/board.cpp).

- **Display:** ST7796 over SPI (SCLK 14, MOSI 13, DC 21, CS 15, RST 22), driven with LovyanGFX. The panel keeps its own
  frame memory, so the image stays put during flash writes.
- **Touch:** FT6336U on I2C (SDA 18, SCL 19, INT 39, address 0x38); follows the screen rotation.
- **Backlight:** PWM on GPIO 23 (brightness 10–255).
- **No light sensor:** the V3.3 board has none on its ADC pins, so brightness is manual.

## Tools

| Script | Purpose |
|---|---|
| `tools/mock_moonraker.py` | Mock printer for previewing every screen |
| `tools/gen_icons.py` | Regenerates the Material Design Icons subset fonts (needs Node.js) |
| `tools/gen_logo.py` | Regenerates the boot and web logo from `assets/logo_source.png` (needs Pillow); crops any artwork on a plain background |
| `tools/touch_about.py` | PlatformIO pre-build step that keeps the About build time current |

## Layout

```
src/main.cpp         start-up: settings, Wi-Fi, display, boot logo, Moonraker, UI
src/board.*          LovyanGFX ST7796 + FT6336U, backlight PWM, LVGL driver, rotation
src/moonraker.*      background polling (filtered JSON streaming), commands, power devices, host stats, layer estimate
src/net.*            Wi-Fi, setup hotspot + captive portal, settings page, OTA, /info, serial config
src/settings.*       settings stored in NVS
src/clock.*          NTP + time zones
src/version.h        release version
src/about.*          facts for the About screen and web page
src/ui_core.cpp      theme, top bar, navigation, virtual lists, dialogs, boot logo, update screen, screen sleep
src/ui_status.cpp    Home and Printing pages
src/ui_print.cpp     files, Z offset
src/ui_control.cpp   temperature, move, extrude, fans, macros, power
src/ui_system.cpp    console, settings, connection, system, date & time, about
src/fonts/           generated icon fonts
include/lv_conf.h    LVGL 8.3 configuration
```

Icons: [Material Design Icons](https://pictogrammers.com/library/mdi/) (Apache-2.0).
