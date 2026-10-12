// Board support: Wireless-Tag WT32-SC01 V3.3 (ST7796 SPI panel, FT6336U touch, PWM backlight), via LovyanGFX.
#include "board.h"
#if defined(BOARD_WT32_SC01)
#include <Arduino.h>
#define LGFX_USE_V1
#include <LovyanGFX.hpp>
#include <lvgl.h>

// ---------------- LovyanGFX device ----------------
class LGFX : public lgfx::LGFX_Device {
    lgfx::Panel_ST7796 panel;
    lgfx::Bus_SPI bus;
    lgfx::Light_PWM light;
    lgfx::Touch_FT5x06 touch;  // FT6336U is FT5x06-compatible

public:
    LGFX() {
        auto b = bus.config();
        b.spi_host = VSPI_HOST;
        b.spi_mode = 0;
        b.freq_write = 40000000;
        b.freq_read = 16000000;
        b.spi_3wire = false;
        b.use_lock = true;
        b.dma_channel = 1;
        b.pin_sclk = 14;
        b.pin_mosi = 13;
        b.pin_miso = -1;
        b.pin_dc = 21;
        bus.config(b);
        panel.setBus(&bus);

        auto p = panel.config();
        p.pin_cs = 15;
        p.pin_rst = 22;
        p.pin_busy = -1;
        p.panel_width = 320;
        p.panel_height = 480;
        p.readable = false;
        p.invert = false;
        p.rgb_order = false;
        p.dlen_16bit = false;
        p.bus_shared = false;
        panel.config(p);

        auto l = light.config();
        l.pin_bl = 23;
        l.invert = false;
        l.freq = 44100;
        l.pwm_channel = 7;
        light.config(l);
        panel.setLight(&light);

        auto t = touch.config();
        t.i2c_port = 1;
        t.i2c_addr = 0x38;
        t.pin_sda = 18;
        t.pin_scl = 19;
        t.pin_int = 39;
        t.freq = 400000;
        t.x_min = 0;
        t.x_max = 319;
        t.y_min = 0;
        t.y_max = 479;
        touch.config(t);
        panel.setTouch(&touch);

        setPanel(&panel);
    }
};

static LGFX lcd;
static uint8_t brightness = 200;
static bool blOn = true;

void board_backlight(bool on) {
    blOn = on;
    lcd.setBrightness(on ? brightness : 0);
}

bool board_backlight_on() { return blOn; }

void board_set_brightness(uint8_t level) {
    brightness = level < 10 ? 10 : level;
    if (blOn) lcd.setBrightness(brightness);
}

bool board_has_touch() { return true; }
const uint16_t* board_framebuffer() { return nullptr; }  // the ST7796 holds the image


// ---------------- LVGL glue ----------------
static void disp_flush(lv_disp_drv_t* drv, const lv_area_t* a, lv_color_t* px) {
    uint32_t w = a->x2 - a->x1 + 1, h = a->y2 - a->y1 + 1;
    lcd.startWrite();
    lcd.setAddrWindow(a->x1, a->y1, w, h);
    lcd.writePixels((lgfx::rgb565_t*)&px->full, w * h);
    lcd.endWrite();
    lv_disp_flush_ready(drv);
}

static void touch_read(lv_indev_drv_t*, lv_indev_data_t* d) {
    int32_t x, y;
    if (lcd.getTouch(&x, &y)) {
        d->point.x = x;
        d->point.y = y;
        d->state = LV_INDEV_STATE_PRESSED;
    } else {
        d->state = LV_INDEV_STATE_RELEASED;
    }
}

void board_init(uint8_t rotation) {
    lcd.init();
    static const uint8_t lgfxRot[4] = {1, 0, 3, 2};  // our 0..3 -> LovyanGFX (0 = native portrait)
    lcd.setRotation(lgfxRot[rotation & 3]);
    lcd.setBrightness(0);
    lcd.fillScreen(TFT_BLACK);

    lv_init();
    // one internal, DMA-capable buffer of ~9600 px (19 KB); RAM is tight without PSRAM
    static lv_disp_draw_buf_t draw_buf;
    const size_t buf_px = LCD_LONG * 20;
    lv_color_t* buf = (lv_color_t*)heap_caps_malloc(buf_px * sizeof(lv_color_t), MALLOC_CAP_DMA | MALLOC_CAP_8BIT);
    lv_disp_draw_buf_init(&draw_buf, buf, nullptr, buf_px);

    static lv_disp_drv_t disp_drv;
    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res = lcd.width();   // after setRotation: 480x320 or 320x480
    disp_drv.ver_res = lcd.height();
    disp_drv.flush_cb = disp_flush;
    disp_drv.draw_buf = &draw_buf;
    lv_disp_drv_register(&disp_drv);

    static lv_indev_drv_t indev_drv;
    lv_indev_drv_init(&indev_drv);
    indev_drv.type = LV_INDEV_TYPE_POINTER;
    indev_drv.read_cb = touch_read;
    lv_indev_drv_register(&indev_drv);

    board_backlight(true);
}

#endif  // BOARD_WT32_SC01
