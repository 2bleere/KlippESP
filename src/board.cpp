#include "board.h"
#include <Arduino.h>
#include <Wire.h>
#include <Arduino_GFX_Library.h>
#include <lvgl.h>
#include <stdarg.h>
#include "soc/lcd_cam_struct.h"

static Arduino_ESP32RGBPanel* panel;

// log to both native USB (Serial) and the UART port (Serial0)
static void logf(const char* fmt, ...) {
    char buf[160];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    Serial.print(buf);
    Serial0.print(buf);
}
static Arduino_RGB_Display* gfx;

// ---------------- CH422G ----------------
static uint8_t exio = 0;

static void exio_write(uint8_t v) {
    exio = v;
    Wire.beginTransmission(0x38);  // output register IO0-IO7
    Wire.write(v);
    Wire.endTransmission();
}

static void exio_init() {
    Wire.beginTransmission(0x24);  // system parameter: IO_OE = 1 (push-pull outputs)
    Wire.write(0x01);
    Wire.endTransmission();
}

void board_backlight(bool on) {
    exio_write(on ? (exio | EXIO_LCD_BL) : (exio & ~EXIO_LCD_BL));
}

bool board_backlight_on() { return exio & EXIO_LCD_BL; }

static uint8_t gt_addr;
bool board_has_touch() { return gt_addr != 0; }

void board_lcd_freeze() {
    LCD_CAM.lcd_user.lcd_start = 0;   // same as IDF's lcd_ll_stop()
    LCD_CAM.lcd_user.lcd_update = 1;
}

// ---------------- GT911 ----------------

static bool gt_read(uint16_t reg, uint8_t* buf, uint8_t n) {
    Wire.beginTransmission(gt_addr);
    Wire.write(reg >> 8);
    Wire.write(reg & 0xFF);
    if (Wire.endTransmission(false) != 0) return false;
    if (Wire.requestFrom(gt_addr, n) != n) return false;
    for (uint8_t i = 0; i < n; i++) buf[i] = Wire.read();
    return true;
}

static void gt_write8(uint16_t reg, uint8_t v) {
    Wire.beginTransmission(gt_addr);
    Wire.write(reg >> 8);
    Wire.write(reg & 0xFF);
    Wire.write(v);
    Wire.endTransmission();
}

static bool i2c_probe(uint8_t a) {
    Wire.beginTransmission(a);
    return Wire.endTransmission() == 0;
}

static bool gt_probe() {
    if (i2c_probe(0x5D)) gt_addr = 0x5D;
    else if (i2c_probe(0x14)) gt_addr = 0x14;
    else gt_addr = 0;
    return gt_addr != 0;
}

static void touch_read(lv_indev_drv_t*, lv_indev_data_t* d) {
    static int16_t x = 0, y = 0;
    static bool pressed = false;
    static uint32_t lastProbe = 0;
    static uint8_t fails = 0;
    uint8_t st;
    if (!gt_addr) {  // controller missing: retry once a second instead of failing every read
        if (millis() - lastProbe > 1000) {
            lastProbe = millis();
            if (gt_probe()) logf("GT911 found at 0x%02X\n", gt_addr);
        }
        d->state = LV_INDEV_STATE_RELEASED;
        return;
    }
    if (!gt_read(0x814E, &st, 1)) {
        if (++fails > 10) gt_addr = 0;
        d->state = LV_INDEV_STATE_RELEASED;
        return;
    }
    fails = 0;
    if (st & 0x80) {
        uint8_t n = st & 0x0F;
        if (n > 0 && n <= 5) {
            uint8_t p[4];
            if (gt_read(0x8150, p, 4)) {
                x = p[0] | (p[1] << 8);
                y = p[2] | (p[3] << 8);
                if (x >= LCD_W) x = LCD_W - 1;
                if (y >= LCD_H) y = LCD_H - 1;
            }
            pressed = true;
        } else {
            pressed = false;
        }
        gt_write8(0x814E, 0);
    }
    d->point.x = x;
    d->point.y = y;
    d->state = pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}

// ---------------- LVGL display ----------------
static void disp_flush(lv_disp_drv_t* drv, const lv_area_t* a, lv_color_t* px) {
    gfx->draw16bitRGBBitmap(a->x1, a->y1, (uint16_t*)&px->full, a->x2 - a->x1 + 1, a->y2 - a->y1 + 1);
    lv_disp_flush_ready(drv);
}

void board_init(uint8_t rotation) {
    Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
    Wire.setClock(400000);

    // Reset LCD, then bring up the GT911. The touch reset pin isn't documented reliably across
    // board revisions, so try each candidate expander output (never bit5 = USB_SEL) until it answers.
    // INT held low while reset rises selects address 0x5D.
    exio_init();
    const uint8_t base = EXIO_SD_CS | EXIO_LCD_RST;
    exio_write(EXIO_SD_CS);
    delay(20);
    exio_write(base);
    static const uint8_t candidates[] = {EXIO_TP_RST, 1 << 0, 1 << 6, 1 << 7, 0xDF & ~base};
    for (uint8_t rst : candidates) {
        exio_write(base & ~rst);
        delay(100);
        pinMode(PIN_TP_INT, OUTPUT);
        digitalWrite(PIN_TP_INT, LOW);
        delay(100);
        exio_write(base | rst);
        delay(200);
        pinMode(PIN_TP_INT, INPUT);
        delay(100);
        if (gt_probe()) {
            logf("GT911 at 0x%02X (reset mask 0x%02X)\n", gt_addr, rst);
            break;
        }
        logf("GT911 not found with reset mask 0x%02X\n", rst);
    }

    logf("I2C devices:");
    for (uint8_t a = 1; a < 127; a++)
        if (i2c_probe(a) && (a < 0x20 || (a > 0x27 && a < 0x30) || a > 0x3F)) logf(" 0x%02X", a);
    logf("  (expander state 0x%02X)\n", exio);

    panel = new Arduino_ESP32RGBPanel(
        5 /* DE */, 3 /* VSYNC */, 46 /* HSYNC */, 7 /* PCLK */,
        1 /* R3 */, 2 /* R4 */, 42 /* R5 */, 41 /* R6 */, 40 /* R7 */,
        39 /* G2 */, 0 /* G3 */, 45 /* G4 */, 48 /* G5 */, 47 /* G6 */, 21 /* G7 */,
        14 /* B3 */, 38 /* B4 */, 18 /* B5 */, 17 /* B6 */, 10 /* B7 */,
        0 /* hsync_polarity */, 8 /* front */, 4 /* pulse */, 8 /* back */,
        0 /* vsync_polarity */, 8 /* front */, 4 /* pulse */, 8 /* back */,
        1 /* pclk_active_neg */, LCD_PCLK_HZ);
    gfx = new Arduino_RGB_Display(LCD_W, LCD_H, panel, 0, true);
    gfx->begin();
    gfx->fillScreen(0x0000);

    lv_init();
    static lv_disp_draw_buf_t draw_buf;
    // One small internal draw buffer: rendering into PSRAM competes with the panel's frame-buffer DMA
    // (image shifts sideways), while large internal buffers (2x 800x60 = 190 KB) starved Wi-Fi/lwIP.
    const size_t buf_px = LCD_W * 20;
    lv_color_t* buf1 = (lv_color_t*)heap_caps_malloc(buf_px * sizeof(lv_color_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!buf1) buf1 = (lv_color_t*)ps_malloc(buf_px * sizeof(lv_color_t));
    lv_color_t* buf2 = nullptr;
    lv_disp_draw_buf_init(&draw_buf, buf1, buf2, buf_px);

    static lv_disp_drv_t disp_drv;
    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res = LCD_W;
    disp_drv.ver_res = LCD_H;
    disp_drv.flush_cb = disp_flush;
    disp_drv.draw_buf = &draw_buf;
    if (gt_addr && (rotation & 1)) rotation = 0;  // touch UI layout is landscape-only
    if (rotation) {
        disp_drv.sw_rotate = 1;  // LVGL rotates rendered areas before flush; touch input is rotated too
        disp_drv.rotated = (lv_disp_rot_t)rotation;
    }
    lv_disp_drv_register(&disp_drv);

    static lv_indev_drv_t indev_drv;
    lv_indev_drv_init(&indev_drv);
    indev_drv.type = LV_INDEV_TYPE_POINTER;
    indev_drv.read_cb = touch_read;
    lv_indev_drv_register(&indev_drv);

    board_backlight(true);
}
