#pragma once
#include <stdint.h>
// Board abstraction. Each PlatformIO environment defines one BOARD_* macro, which compiles exactly one of
// board_waveshare7.cpp / board_wt32sc01.cpp. Everything else is shared.

#if defined(BOARD_WAVESHARE_S3_LCD7)
// Waveshare ESP32-S3-Touch-LCD-7: 800x480 RGB565 panel, GT911 touch, CH422G IO expander, 8 MB PSRAM
#define BOARD_NAME "Waveshare ESP32-S3-Touch-LCD-7"
#define LCD_LONG 800
#define LCD_SHORT 480
#define BOARD_HAS_DIMMING 0  // backlight is on/off through the IO expander
#define BOARD_ENV "waveshare_s3_lcd7"  // PlatformIO environment (firmware path hint on the web page)
#elif defined(BOARD_WT32_SC01)
// Wireless-Tag WT32-SC01 (V3.3): ESP32-D0WD, 3.5" ST7796 320x480 on SPI, FT6336U touch, PWM backlight, no PSRAM
#define BOARD_NAME "WT32-SC01 V3.3"
#define LCD_LONG 480
#define LCD_SHORT 320
#define BOARD_HAS_DIMMING 1
#define BOARD_ENV "wt32_sc01"
#else
#error "No board selected: build one of the environments in platformio.ini (BOARD_WAVESHARE_S3_LCD7 / BOARD_WT32_SC01)"
#endif

// Panel, touch and LVGL driver registration.
// rotation: 0 landscape, 1 portrait, 2 landscape upside down, 3 portrait upside down. Touch follows.
void board_init(uint8_t rotation);
void board_backlight(bool on);
bool board_backlight_on();
void board_set_brightness(uint8_t level);  // 10..255; without dimming the backlight just stays on
bool board_has_touch();                    // touch controller answered at boot
// Frame buffer for /screenshot.bmp (RGB565, panel orientation), or nullptr where the panel keeps its own memory.
const uint16_t* board_framebuffer();
