// Firmware / hardware facts for the About screen and the web page.
// tools/touch_about.py touches this file before each build so the build time is always current.
#include "about.h"
#include "version.h"
#include <Arduino.h>
#include <WiFi.h>
#include <lvgl.h>
#include <esp_idf_version.h>
#include <esp_arduino_version.h>
#include <esp_ota_ops.h>

#if CONFIG_IDF_TARGET_ESP32S3
static const char* BOARD = "Waveshare ESP32-S3-Touch-LCD-7";
#else
static const char* BOARD = "WT32-SC01 V3.3";
#endif

const char* about_build_time() {
    static char buf[24];
    if (!buf[0]) {  // __DATE__ is "Oct 12 2026": reorder to ISO
        static const char* months = "JanFebMarAprMayJunJulAugSepOctNovDec";
        char mon[4] = {__DATE__[0], __DATE__[1], __DATE__[2], 0};
        int m = (strstr(months, mon) - months) / 3 + 1;
        snprintf(buf, sizeof(buf), "%.4s-%02d-%02d %.5s", __DATE__ + 7, m, atoi(__DATE__ + 4), __TIME__);
    }
    return buf;
}

int about_collect(AboutItem* it, int max) {
    int n = 0;
    auto add = [&](const char* k, const char* fmt, auto... a) {
        if (n >= max) return;
        it[n].key = k;
        snprintf(it[n].value, sizeof(it[n].value), fmt, a...);
        n++;
    };
    add("Version", "v%s", FW_VERSION);
    add("Built", "%s", about_build_time());
    add("Board", "%s", BOARD);
    int rev = ESP.getChipRevision();  // IDF 5: major * 100 + minor
    add("Chip", "%s v%d.%d, %d cores, %lu MHz", ESP.getChipModel(), rev / 100, rev % 100, ESP.getChipCores(),
        (unsigned long)ESP.getCpuFreqMHz());
    if (ESP.getPsramSize())
        add("Memory", "%lu MB flash, %lu MB PSRAM", (unsigned long)(ESP.getFlashChipSize() >> 20),
            (unsigned long)(ESP.getPsramSize() >> 20));
    else
        add("Memory", "%lu MB flash, no PSRAM", (unsigned long)(ESP.getFlashChipSize() >> 20));
    const esp_partition_t* app = esp_ota_get_running_partition();
    add("Firmware size", "%lu KB of %lu KB", (unsigned long)(ESP.getSketchSize() >> 10),
        (unsigned long)((app ? app->size : 0) >> 10));
    add("Arduino core", "%d.%d.%d", ESP_ARDUINO_VERSION_MAJOR, ESP_ARDUINO_VERSION_MINOR, ESP_ARDUINO_VERSION_PATCH);
    add("ESP-IDF", "%s", esp_get_idf_version());
    add("LVGL", "%d.%d.%d", LVGL_VERSION_MAJOR, LVGL_VERSION_MINOR, LVGL_VERSION_PATCH);
    add("Wi-Fi MAC", "%s", WiFi.macAddress().c_str());
    uint32_t up = millis() / 1000;
    add("Uptime", "%lud %02luh %02lum", (unsigned long)(up / 86400), (unsigned long)(up / 3600 % 24),
        (unsigned long)(up / 60 % 60));
    add("Free memory", "%lu KB", (unsigned long)(ESP.getFreeHeap() >> 10));
    add("Project", "%s", FW_PROJECT_URL + 8);  // without https://
    return n;
}
