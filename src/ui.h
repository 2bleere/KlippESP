#pragma once
#include <lvgl.h>
#include "moonraker.h"
#include "settings.h"

#define DEG "\xC2\xB0"

#define COL_BG      lv_color_hex(0x0d1117)
#define COL_BAR     lv_color_hex(0x161b22)
#define COL_CARD    lv_color_hex(0x1c2330)
#define COL_BTN     lv_color_hex(0x263143)
#define COL_ACCENT  lv_color_hex(0x2f81f7)
#define COL_GREEN   lv_color_hex(0x2ea043)
#define COL_ORANGE  lv_color_hex(0xd29922)
#define COL_RED     lv_color_hex(0xda3633)
#define COL_TEXT    lv_color_hex(0xe6edf3)
#define COL_DIM     lv_color_hex(0x8b949e)

// Screen size is decided at boot by the orientation setting (480x320 landscape or 320x480 portrait).
extern bool PORTRAIT;
extern lv_coord_t SW, SH;  // screen width / height
#define TOPBAR_H 40
#define CONTENT_W SW
#define CONTENT_H (SH - TOPBAR_H)

enum Scr {
    S_HOME, S_FILES, S_PRINTING, S_TEMP, S_MOVE, S_EXTRUDE, S_FANS, S_MACROS,
    S_CONSOLE, S_POWER, S_SETTINGS, S_CONNECT, S_SYSTEM, S_ZOFFSET, S_TIME, S_COUNT
};

extern MrState g;  // latest printer snapshot, refreshed by the UI timer

void ui_boot_logo();  // full-screen logo shown while starting up; ui_init() replaces it
void ui_init();
void ui_goto(Scr s);
void ui_back();
Scr ui_current();
void ui_set_update(void (*fn)());  // per-screen refresh callback (called every 500 ms)

// widgets
lv_obj_t* ui_btn(lv_obj_t* parent, const char* txt, lv_coord_t w, lv_coord_t h, lv_event_cb_t cb,
                 void* ud = nullptr, lv_color_t col = COL_BTN);
lv_obj_t* ui_tile(lv_obj_t* parent, const char* icon, const char* txt, lv_coord_t w, lv_coord_t h,
                  lv_event_cb_t cb, void* ud = nullptr, lv_color_t col = COL_BTN);
lv_obj_t* ui_card(lv_obj_t* parent, lv_coord_t x, lv_coord_t y, lv_coord_t w, lv_coord_t h);
lv_obj_t* ui_label(lv_obj_t* parent, const char* txt, const lv_font_t* font = nullptr, lv_color_t col = COL_TEXT);
lv_obj_t* ui_row(lv_obj_t* parent, lv_coord_t w, lv_coord_t h);  // transparent flex row
void ui_btn_text(lv_obj_t* btn, const char* txt);
void ui_select(lv_obj_t* btn, bool on);  // highlight toggle-style buttons

// Change-only setters: LVGL redraws an object whenever a property is set, even to the same value,
// which causes visible flicker on this panel. These skip the call when nothing changed.
void set_text(lv_obj_t* label, const char* txt);
void set_text_color(lv_obj_t* obj, lv_color_t c);
void set_bg_color(lv_obj_t* obj, lv_color_t c, lv_style_selector_t sel = 0);
void set_arc_color(lv_obj_t* arc, lv_color_t c);
void set_visible(lv_obj_t* obj, bool on);
void set_height(lv_obj_t* obj, lv_coord_t h);

// firmware update screen: first call shows a message for 4 s, then turns the screen off until the
// restart; later calls are no-ops; -100 (update failed) restarts the display
void ui_ota_progress(int pct);

void ui_toast(const char* msg, bool error = false);
void ui_confirm(const char* title, const char* text, void (*cb)(void*), void* ud = nullptr);
void ui_numpad(const char* title, float initial, void (*cb)(float));

void fmt_dur(char* buf, size_t n, float sec);
lv_color_t state_color(const char* pstate);

// screen builders
void scr_home(lv_obj_t* p);
void scr_files(lv_obj_t* p);
void scr_printing(lv_obj_t* p);
void scr_zoffset(lv_obj_t* p);
void scr_temp(lv_obj_t* p);
void scr_move(lv_obj_t* p);
void scr_extrude(lv_obj_t* p);
void scr_fans(lv_obj_t* p);
void scr_macros(lv_obj_t* p);
void scr_power(lv_obj_t* p);
void ui_power_open();  // Home Power tile: toggle the only device, or open the list
void scr_console(lv_obj_t* p);
void scr_settings(lv_obj_t* p);
void scr_connect(lv_obj_t* p);
void scr_system(lv_obj_t* p);
void scr_time(lv_obj_t* p);

bool is_printing();  // printing or paused


