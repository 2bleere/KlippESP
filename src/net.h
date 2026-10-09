#pragma once
// WiFi management, setup hotspot, web settings page and serial configuration.
// Lets the display be configured without a touch panel.

#define SETUP_AP_SSID "KlippESP-Setup"

void net_begin();
void net_loop();
void net_reconnect();  // after cfg.ssid / cfg.pass changed
bool net_ap_active();
