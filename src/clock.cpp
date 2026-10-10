#include "clock.h"
#include "settings.h"
#include <Arduino.h>
#include <esp_sntp.h>
#include <string.h>

const TzZone TZ_ZONES[] = {
    {"UTC", "UTC0"},
    {"London, Dublin", "GMT0BST,M3.5.0/1,M10.5.0"},
    {"Lisbon", "WET0WEST,M3.5.0/1,M10.5.0"},
    {"Central Europe (Berlin, Paris, Madrid)", "CET-1CEST,M3.5.0,M10.5.0/3"},
    {"Eastern Europe (Athens, Helsinki)", "EET-2EEST,M3.5.0/3,M10.5.0/4"},
    {"Moscow", "MSK-3"},
    {"Dubai", "<+04>-4"},
    {"India", "IST-5:30"},
    {"China, Singapore", "CST-8"},
    {"Japan, Korea", "JST-9"},
    {"Sydney, Melbourne", "AEST-10AEDT,M10.1.0,M4.1.0/3"},
    {"Auckland", "NZST-12NZDT,M9.5.0,M4.1.0/3"},
    {"Sao Paulo, Buenos Aires", "<-03>3"},
    {"Atlantic (Puerto Rico)", "AST4"},
    {"US Eastern (New York)", "EST5EDT,M3.2.0,M11.1.0"},
    {"Bogota, Lima", "<-05>5"},
    {"US Central (Chicago)", "CST6CDT,M3.2.0,M11.1.0"},
    {"Mexico City", "CST6"},
    {"US Mountain (Denver)", "MST7MDT,M3.2.0,M11.1.0"},
    {"Arizona (Phoenix)", "MST7"},
    {"US Pacific (Los Angeles)", "PST8PDT,M3.2.0,M11.1.0"},
    {"Alaska (Anchorage)", "AKST9AKDT,M3.2.0,M11.1.0"},
    {"Hawaii (Honolulu)", "HST10"},
};
const int TZ_ZONE_COUNT = sizeof(TZ_ZONES) / sizeof(TZ_ZONES[0]);

static void set_tz() {
    setenv("TZ", cfg.tz[0] ? cfg.tz : "UTC0", 1);
    tzset();
}

void clock_begin() { set_tz(); }

void clock_apply() {
    // configTzTime restarts SNTP with the new server and sets TZ
    configTzTime(cfg.tz[0] ? cfg.tz : "UTC0", cfg.ntp[0] ? cfg.ntp : "pool.ntp.org");
    sntp_set_sync_interval(3600 * 1000UL);  // resync hourly
}

bool clock_valid() { return time(nullptr) > 1700000000; }  // NTP has set it (after Nov 2023)

int clock_zone_index() {
    for (int i = 0; i < TZ_ZONE_COUNT; i++)
        if (!strcmp(cfg.tz, TZ_ZONES[i].posix)) return i;
    return -1;
}

const char* clock_zone_name() {
    int i = clock_zone_index();
    return i >= 0 ? TZ_ZONES[i].name : cfg.tz;
}

bool clock_hm(char* buf, size_t n, time_t t) {
    if (!clock_valid()) {
        strlcpy(buf, "--:--", n);
        return false;
    }
    if (!t) t = time(nullptr);
    struct tm tmv;
    localtime_r(&t, &tmv);
    strftime(buf, n, cfg.clock24 ? "%H:%M" : "%l:%M %p", &tmv);
    if (buf[0] == ' ') memmove(buf, buf + 1, strlen(buf));  // "%l" pads with a space
    return true;
}

bool clock_datetime(char* buf, size_t n, time_t t) {
    if (!clock_valid() && !t) {
        strlcpy(buf, "--", n);
        return false;
    }
    if (!t) t = time(nullptr);
    struct tm tmv;
    localtime_r(&t, &tmv);
    strftime(buf, n, cfg.clock24 ? "%Y-%m-%d %H:%M" : "%Y-%m-%d %l:%M %p", &tmv);
    return true;
}
