/* Minimal LVGL 8.3 config; everything not set here uses LVGL defaults. */
#if 1
#ifndef LV_CONF_H
#define LV_CONF_H

#include <stdint.h>

#define LV_COLOR_DEPTH 16
#define LV_COLOR_16_SWAP 0

/* LVGL heap in PSRAM; internal RAM is reserved for Wi-Fi/lwIP (see lv_psram.h) */
#define LV_MEM_CUSTOM 1
#define LV_MEM_CUSTOM_INCLUDE "lv_psram.h"
#define LV_MEM_CUSTOM_ALLOC lv_psram_malloc
#define LV_MEM_CUSTOM_FREE free
#define LV_MEM_CUSTOM_REALLOC lv_psram_realloc

#define LV_TICK_CUSTOM 1
#define LV_TICK_CUSTOM_INCLUDE "Arduino.h"
#define LV_TICK_CUSTOM_SYS_TIME_EXPR (millis())

#define LV_DISP_DEF_REFR_PERIOD 20
#define LV_INDEV_DEF_READ_PERIOD 20
#define LV_DPI_DEF 130

#define LV_USE_LOG 0
#define LV_USE_ASSERT_NULL 0
#define LV_USE_ASSERT_MALLOC 0
#define LV_USE_PERF_MONITOR 0
#define LV_USE_MEM_MONITOR 0

#define LV_IMG_CACHE_DEF_SIZE 2

#define LV_FONT_MONTSERRAT_14 1
#define LV_FONT_MONTSERRAT_16 1
#define LV_FONT_MONTSERRAT_20 1
#define LV_FONT_MONTSERRAT_24 1
#define LV_FONT_MONTSERRAT_28 1
#define LV_FONT_MONTSERRAT_32 1
#define LV_FONT_MONTSERRAT_48 1
#define LV_FONT_DEFAULT &lv_font_montserrat_20

#define LV_USE_PNG 1          /* print thumbnails from Moonraker */

#define LV_BUILD_EXAMPLES 0

#endif
#endif
