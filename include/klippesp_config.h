#pragma once
// Feature switches derived from the board selected in platformio.ini (BOARD_* build flag). Included by lv_conf.h,
// so LVGL itself is configured per board too.
#if defined(BOARD_WAVESHARE_S3_LCD7)
#define KLIPPESP_PSRAM 1        // 8 MB PSRAM: LVGL heap and thumbnails live there
#define KLIPPESP_THUMBNAILS 1   // slicer thumbnails (PNG decoding needs PSRAM)
#define KLIPPESP_DASHBOARD 1    // no-touch status dashboard (Interface setting)
#define KLIPPESP_LARGE_FONTS 1  // Montserrat 24/32/48 and icons_16/72 used by the dashboard
#elif defined(BOARD_WT32_SC01)
#define KLIPPESP_PSRAM 0
#define KLIPPESP_THUMBNAILS 0
#define KLIPPESP_DASHBOARD 0
#define KLIPPESP_LARGE_FONTS 0
#endif
