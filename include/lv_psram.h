#pragma once
// LVGL heap in PSRAM, so internal RAM stays free for Wi-Fi / lwIP buffers.
#include <stdlib.h>
#include "esp_heap_caps.h"

static inline void* lv_psram_malloc(size_t n) {
    void* p = heap_caps_malloc(n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    return p ? p : malloc(n);
}

static inline void* lv_psram_realloc(void* p, size_t n) {
    void* r = heap_caps_realloc(p, n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    return r ? r : realloc(p, n);
}
