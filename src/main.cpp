// KlippESP - KlipperScreen-style touch interface for the WT32-SC01 (ESP32, 480x320).
#include <Arduino.h>
#include <WiFi.h>
#include <lvgl.h>
#include "board.h"
#include "settings.h"
#include "moonraker.h"
#include "ui.h"
#include "net.h"
#include "ui_icons.h"

void setup() {
    Serial.begin(115200);
    settings_load();

    net_begin();

    board_init(cfg.rotation);
    board_set_brightness(cfg.brightness);
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
