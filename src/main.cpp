// KlipperScreen-style touch interface for the Waveshare ESP32-S3-Touch-LCD-7.
#include <Arduino.h>
#include <WiFi.h>
#include <lvgl.h>
#include "board.h"
#include "settings.h"
#include "moonraker.h"
#include "ui.h"
#include "net.h"
#include "clock.h"
#include "ui_icons.h"

void setup() {
    Serial.begin(115200);
    Serial0.begin(115200);
    settings_load();
    clock_begin();  // timezone, so file dates are local even before NTP syncs

    net_begin();

    board_init(cfg.uiMode == 2 ? (cfg.rotation & 2) : cfg.rotation);  // touch UI layout is landscape-only
    ui_icons_init();
    ui_boot_logo();
    mr_begin();
    // keep the logo up for a moment while Wi-Fi connects
    for (uint32_t t0 = millis(); millis() - t0 < 2500;) {
        lv_timer_handler();
        net_loop();
        delay(5);
    }
    ui_init();
}

void loop() {
    lv_timer_handler();
    net_loop();
    delay(5);
}
