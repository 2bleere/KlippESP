#pragma once
#include <stdint.h>
// Wireless-Tag WT32-SC01 (V3.3): ESP32-D0WD, 3.5" ST7796 320x480 on SPI, FT6336U touch on I2C, PWM backlight.

// Panel is natively 320x480 (portrait). Screen size at runtime depends on the orientation setting.
#define LCD_LONG 480
#define LCD_SHORT 320

// I2C, panel, touch, LVGL driver registration.
// rotation: 0 landscape, 1 portrait, 2 landscape upside down, 3 portrait upside down. Touch follows.
void board_init(uint8_t rotation);
void board_backlight(bool on);
bool board_backlight_on();
void board_set_brightness(uint8_t level);  // 10..255
bool board_has_touch();                    // always true on this board
void board_lcd_freeze();                   // no-op: the ST7796 keeps its own frame memory
