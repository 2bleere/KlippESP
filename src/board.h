#pragma once
#include <stdint.h>
// Waveshare ESP32-S3-Touch-LCD-7: 800x480 RGB565 panel, GT911 touch, CH422G IO expander.

#define LCD_W 800
#define LCD_H 480

#define PIN_I2C_SDA 8
#define PIN_I2C_SCL 9
#define PIN_TP_INT 4

// CH422G outputs: EXIOn is bit n (EXIO0 is unused on this board)
#define EXIO_TP_RST  (1 << 1)
#define EXIO_LCD_BL  (1 << 2)
#define EXIO_LCD_RST (1 << 3)
#define EXIO_SD_CS   (1 << 4)
#define EXIO_USB_SEL (1 << 5)   // low = USB, high = CAN

// Pixel clock. The panel streams its frame buffer from PSRAM; if PSRAM is busy the stream underruns and
// the image shifts sideways. 12 MHz (~30 fps) leaves headroom; raise it only if the picture stays stable.
#define LCD_PCLK_HZ 12000000

// I2C, IO expander, panel, touch, LVGL driver registration.
// rotation: 0..3 = 0/90/180/270 deg; 90/270 (portrait) is ignored when a touch panel is present.
void board_init(uint8_t rotation);
void board_backlight(bool on);
bool board_backlight_on();
bool board_has_touch();  // GT911 detected at boot
// Stop the RGB panel refresh; the panel keeps showing the last frame. Used during firmware updates,
// where flash writes would otherwise starve the PSRAM frame buffer reads and make the image flicker.
// Only a restart brings the display back.
void board_lcd_freeze();
