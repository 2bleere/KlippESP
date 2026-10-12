#pragma once
// Firmware / hardware facts for the About screen and the web page (same list on both).
#include <stddef.h>

struct AboutItem {
    const char* key;
    char value[48];
};

#define ABOUT_MAX 16
int about_collect(AboutItem* items, int max);  // returns the number of items filled
const char* about_build_time();                // "2026-10-12 13:24"
