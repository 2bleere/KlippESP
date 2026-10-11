#pragma once
#include <stdint.h>

struct Settings {
    char ssid[33];
    char pass[65];
    char host[64];      // printer IP or hostname (mDNS .local supported)
    uint16_t port;      // Moonraker port, usually 7125 (or 80 behind nginx)
    char apikey[72];    // optional Moonraker API key
    uint16_t sleepMin;  // backlight off after N minutes idle, 0 = never
    uint8_t rotation;   // 0 landscape, 1 portrait, 2 landscape upside down, 3 portrait upside down
    uint8_t brightness; // backlight 10..255
    char ntp[64];       // NTP server
    char tz[64];        // POSIX TZ string, e.g. "EST5EDT,M3.2.0,M11.1.0"
    bool clock24;       // 24 h clock (else 12 h)
    bool powerRestart;  // firmware-restart Klipper after a power device is switched on from the display
};

extern Settings cfg;

void settings_load();
void settings_save();
