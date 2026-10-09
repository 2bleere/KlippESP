#pragma once
#include <stdint.h>

struct Settings {
    char ssid[33];
    char pass[65];
    char host[64];      // printer IP or hostname (mDNS .local supported)
    uint16_t port;      // Moonraker port, usually 7125 (or 80 behind nginx)
    char apikey[72];    // optional Moonraker API key
    uint16_t sleepMin;  // backlight off after N minutes idle, 0 = never
    uint8_t rotation;   // 0 landscape, 1 portrait, 2 landscape flipped, 3 portrait flipped
};

extern Settings cfg;

void settings_load();
void settings_save();
