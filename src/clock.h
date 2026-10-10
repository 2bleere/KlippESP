#pragma once
// Wall clock: NTP sync (configurable server) + POSIX timezone, with a list of common zones for the UI/web page.
#include <stddef.h>
#include <time.h>

struct TzZone {
    const char* name;  // shown in the UI
    const char* posix; // TZ rule
};
extern const TzZone TZ_ZONES[];
extern const int TZ_ZONE_COUNT;

void clock_begin();          // set TZ from settings (call once at boot)
void clock_apply();          // (re)start NTP with cfg.ntp / cfg.tz; call when Wi-Fi is up or settings change
bool clock_valid();          // true once NTP has set the time
int clock_zone_index();      // index of cfg.tz in TZ_ZONES, or -1 for a custom rule
const char* clock_zone_name();

// Formatting helpers (local time). Return false (and "--:--") while the time isn't known yet.
bool clock_hm(char* buf, size_t n, time_t t = 0);       // "14:35" / "2:35 PM"
bool clock_datetime(char* buf, size_t n, time_t t = 0); // "2026-10-09 14:35"
