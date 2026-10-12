// UI framework (top bar, navigation, dialogs, helpers) and the home screen.
#include "ui.h"
#include "board.h"
#include <WiFi.h>
#include "net.h"
#include "clock.h"
#include "ui_icons.h"

MrState g;

static lv_obj_t* content;
static lv_obj_t* lblTitle;
static lv_obj_t* btnBack;
static lv_obj_t* btnHome;
static lv_obj_t* lblExt;
static lv_obj_t* lblBed;
static lv_obj_t* lblState;
static lv_obj_t* lblWifi;
static lv_obj_t* lblClock;

static void (*upd)() = nullptr;
static Scr cur = S_COUNT;
static Scr stack[12];
static int sp = 0;

static const char* titles[S_COUNT] = {
    "KlippESP", "Print", "Printing", "Temperature", "Move", "Extrude", "Fans & Speed", "Macros",
    "Console", "Power", "Settings", "Connection", "System", "Z Offset", "About"};

static void (*builders[S_COUNT])(lv_obj_t*) = {
    scr_home, scr_files, scr_printing, scr_temp, scr_move, scr_extrude, scr_fans, scr_macros,
    scr_console, scr_power, scr_settings, scr_connect, scr_system, scr_zoffset, scr_about};

bool is_printing() { return !strcmp(g.pstate, "printing") || !strcmp(g.pstate, "paused"); }

lv_color_t state_color(const char* s) {
    if (!strcmp(s, "printing")) return COL_ACCENT;
    if (!strcmp(s, "paused")) return COL_ORANGE;
    if (!strcmp(s, "error") || !strcmp(s, "shutdown") || !strcmp(s, "offline") || !strcmp(s, "disconnected")) return COL_RED;
    if (!strcmp(s, "complete") || !strcmp(s, "ready")) return COL_GREEN;
    return COL_DIM;
}

void fmt_dur(char* buf, size_t n, float sec) {
    if (sec < 0 || isnan(sec) || isinf(sec)) sec = 0;
    uint32_t s = (uint32_t)sec;
    if (s >= 3600) snprintf(buf, n, "%uh %02um", (unsigned)(s / 3600), (unsigned)((s / 60) % 60));
    else snprintf(buf, n, "%um %02us", (unsigned)(s / 60), (unsigned)(s % 60));
}

// ---------------------------------------------------------------- widgets

lv_obj_t* ui_label(lv_obj_t* parent, const char* txt, const lv_font_t* font, lv_color_t col) {
    lv_obj_t* l = lv_label_create(parent);
    lv_label_set_text(l, txt);
    if (font) lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, col, 0);
    return l;
}

lv_obj_t* ui_btn(lv_obj_t* parent, const char* txt, lv_coord_t w, lv_coord_t h, lv_event_cb_t cb, void* ud, lv_color_t col) {
    lv_obj_t* b = lv_btn_create(parent);
    lv_obj_set_size(b, w, h);
    lv_obj_set_style_bg_color(b, col, 0);
    lv_obj_set_style_radius(b, 10, 0);
    lv_obj_set_style_shadow_width(b, 0, 0);
    lv_obj_set_style_pad_all(b, 4, 0);
    lv_obj_t* l = lv_label_create(b);
    lv_label_set_text(l, txt);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(l);
    if (cb) lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, ud);
    return b;
}

void ui_btn_text(lv_obj_t* btn, const char* txt) { set_text(lv_obj_get_child(btn, 0), txt); }

void ui_select(lv_obj_t* btn, bool on) { set_bg_color(btn, on ? COL_ACCENT : COL_BTN, 0); }

lv_obj_t* ui_tile(lv_obj_t* parent, const char* icon, const char* txt, lv_coord_t w, lv_coord_t h, lv_event_cb_t cb, void* ud, lv_color_t col) {
    lv_obj_t* b = lv_btn_create(parent);
    lv_obj_set_size(b, w, h);
    set_bg_color(b, col, 0);
    lv_obj_set_style_radius(b, 14, 0);
    lv_obj_set_style_shadow_width(b, 0, 0);
    lv_obj_set_flex_flow(b, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(b, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(b, 8, 0);
    ui_label(b, icon, &lv_font_montserrat_32);
    lv_obj_t* l = ui_label(b, txt, &lv_font_montserrat_20);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    if (cb) lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, ud);
    return b;
}

lv_obj_t* ui_card(lv_obj_t* parent, lv_coord_t x, lv_coord_t y, lv_coord_t w, lv_coord_t h) {
    lv_obj_t* c = lv_obj_create(parent);
    lv_obj_set_pos(c, x, y);
    lv_obj_set_size(c, w, h);
    lv_obj_set_style_bg_color(c, COL_CARD, 0);
    lv_obj_set_style_border_width(c, 0, 0);
    lv_obj_set_style_radius(c, 12, 0);
    lv_obj_set_style_pad_all(c, 10, 0);
    lv_obj_clear_flag(c, LV_OBJ_FLAG_SCROLLABLE);
    return c;
}

lv_obj_t* ui_row(lv_obj_t* parent, lv_coord_t w, lv_coord_t h) {
    lv_obj_t* r = lv_obj_create(parent);
    lv_obj_set_size(r, w, h);
    lv_obj_set_style_bg_opa(r, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(r, 0, 0);
    lv_obj_set_style_pad_all(r, 0, 0);
    lv_obj_set_style_pad_column(r, 8, 0);
    lv_obj_set_flex_flow(r, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(r, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(r, LV_OBJ_FLAG_SCROLLABLE);
    return r;
}

// ---------------------------------------------------------------- change-only setters

static inline bool lv_color_eq(lv_color_t a, lv_color_t b) { return a.full == b.full; }

void set_text(lv_obj_t* l, const char* t) {
    const char* cur = lv_label_get_text(l);
    if (!cur || strcmp(cur, t)) lv_label_set_text(l, t);
}

void set_text_color(lv_obj_t* o, lv_color_t c) {
    if (!lv_color_eq(lv_obj_get_style_text_color(o, 0), c)) lv_obj_set_style_text_color(o, c, 0);
}

void set_bg_color(lv_obj_t* o, lv_color_t c, lv_style_selector_t sel) {
    if (!lv_color_eq(lv_obj_get_style_bg_color(o, sel), c)) lv_obj_set_style_bg_color(o, c, sel);
}

void set_arc_color(lv_obj_t* a, lv_color_t c) {
    if (!lv_color_eq(lv_obj_get_style_arc_color(a, LV_PART_INDICATOR), c)) lv_obj_set_style_arc_color(a, c, LV_PART_INDICATOR);
}

void set_visible(lv_obj_t* o, bool on) {
    bool hidden = lv_obj_has_flag(o, LV_OBJ_FLAG_HIDDEN);
    if (on && hidden) lv_obj_clear_flag(o, LV_OBJ_FLAG_HIDDEN);
    else if (!on && !hidden) lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
}

void set_height(lv_obj_t* o, lv_coord_t h) {
    if (lv_obj_get_style_height(o, 0) != h) lv_obj_set_height(o, h);
}

// ---------------------------------------------------------------- firmware update overlay

void ui_ota_progress(int pct) {
    // The RGB panel streams from PSRAM through internal bounce buffers, and code runs from PSRAM, so flash
    // writes no longer disturb the image: show real progress while the update is written.
    static lv_obj_t *ov, *bar, *lbl;
    static int last = -2;
    if (pct <= -100) {  // update failed: back to the normal screens
        if (ov) lv_obj_del(ov);
        ov = nullptr;
        last = -2;
        return;
    }
    if (!ov) {
        lv_coord_t w = lv_disp_get_hor_res(nullptr), h = lv_disp_get_ver_res(nullptr);
        ov = lv_obj_create(lv_layer_top());
        lv_obj_set_size(ov, w, h);
        lv_obj_set_style_bg_color(ov, lv_color_black(), 0);
        lv_obj_set_style_bg_opa(ov, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(ov, 0, 0);
        lv_obj_set_style_radius(ov, 0, 0);
        lv_obj_clear_flag(ov, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_t* t = ui_label(ov, LV_SYMBOL_DOWNLOAD "  Updating firmware", &lv_font_montserrat_32, COL_TEXT);
        lv_obj_align(t, LV_ALIGN_CENTER, 0, -80);
        lv_obj_t* s = ui_label(ov, "Do not unplug.\nThe display restarts when done.", &lv_font_montserrat_20, COL_DIM);
        lv_obj_set_style_text_align(s, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_align(s, LV_ALIGN_CENTER, 0, -26);
        bar = lv_bar_create(ov);
        lv_obj_set_size(bar, LV_MIN(w - 80, 560), 20);
        lv_obj_align(bar, LV_ALIGN_CENTER, 0, 30);
        lv_bar_set_range(bar, 0, 100);
        lbl = ui_label(ov, "", &lv_font_montserrat_32, COL_TEXT);
        lv_obj_align(lbl, LV_ALIGN_CENTER, 0, 80);
    }
    if (pct >= 0 && pct < 100 && last >= 0 && pct - last < 2) return;
    if (pct == last) return;
    last = pct;
    char buf[16];
    snprintf(buf, sizeof(buf), pct < 0 ? "Starting..." : "%d%%", pct);
    lv_label_set_text(lbl, buf);
    lv_bar_set_value(bar, pct < 0 ? 0 : pct, LV_ANIM_OFF);
    lv_refr_now(nullptr);  // the main loop is busy receiving firmware
}

// ---------------------------------------------------------------- toast

static lv_obj_t* toast;
static lv_timer_t* toastTimer;

static void toast_expire(lv_timer_t*) {
    if (toast) lv_obj_del(toast);
    toast = nullptr;
    toastTimer = nullptr;
}

static void toast_click(lv_event_t*) {
    if (!toast) return;
    if (toastTimer) lv_timer_del(toastTimer);
    toastTimer = nullptr;
    lv_obj_del_async(toast);
    toast = nullptr;
}

void ui_toast(const char* msg, bool error) {
    if (!toast) {
        toast = lv_label_create(lv_layer_top());
        lv_obj_set_width(toast, LV_MIN(640, lv_disp_get_hor_res(nullptr) - 32));
        lv_label_set_long_mode(toast, LV_LABEL_LONG_WRAP);
        lv_obj_set_style_pad_all(toast, 12, 0);
        lv_obj_set_style_radius(toast, 10, 0);
        lv_obj_set_style_bg_opa(toast, LV_OPA_90, 0);
        lv_obj_set_style_text_color(toast, COL_TEXT, 0);
        lv_obj_set_style_text_align(toast, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_align(toast, LV_ALIGN_BOTTOM_MID, 0, -16);
        lv_obj_add_flag(toast, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(toast, toast_click, LV_EVENT_CLICKED, nullptr);
    }
    lv_obj_set_style_bg_color(toast, error ? COL_RED : COL_BTN, 0);
    lv_label_set_text(toast, msg);
    if (toastTimer) lv_timer_reset(toastTimer);
    else {
        toastTimer = lv_timer_create(toast_expire, error ? 6000 : 3000, nullptr);
        lv_timer_set_repeat_count(toastTimer, 1);
    }
}

// ---------------------------------------------------------------- confirm dialog

struct ConfirmCtx {
    void (*cb)(void*);
    void* ud;
};

static void confirm_evt(lv_event_t* e) {
    lv_obj_t* mb = lv_event_get_current_target(e);
    ConfirmCtx* c = (ConfirmCtx*)lv_event_get_user_data(e);
    uint16_t id = lv_msgbox_get_active_btn(mb);
    lv_obj_remove_event_cb(mb, confirm_evt);
    if (id == 1 && c->cb) c->cb(c->ud);
    delete c;
    lv_msgbox_close_async(mb);
}

void ui_confirm(const char* title, const char* text, void (*cb)(void*), void* ud) {
    static const char* btns[] = {"Cancel", "OK", ""};
    lv_obj_t* mb = lv_msgbox_create(nullptr, title, text, btns, false);
    lv_obj_set_width(mb, 520);
    lv_obj_center(mb);
    lv_obj_t* bm = lv_msgbox_get_btns(mb);
    lv_obj_set_height(bm, 64);
    lv_btnmatrix_set_btn_ctrl_all(bm, LV_BTNMATRIX_CTRL_CLICK_TRIG);
    lv_obj_set_style_min_width(bm, 360, 0);
    lv_obj_add_event_cb(mb, confirm_evt, LV_EVENT_VALUE_CHANGED, new ConfirmCtx{cb, ud});
}

// ---------------------------------------------------------------- numeric keypad

static lv_obj_t* npOverlay;
static lv_obj_t* npTa;
static void (*npCb)(float);

static void np_evt(lv_event_t* e) {
    lv_obj_t* bm = lv_event_get_target(e);
    const char* t = lv_btnmatrix_get_btn_text(bm, lv_btnmatrix_get_selected_btn(bm));
    if (!t || !npOverlay) return;
    if (!strcmp(t, "OK")) {
        float v = atof(lv_textarea_get_text(npTa));
        if (npCb) npCb(v);
        lv_obj_del_async(npOverlay);
        npOverlay = nullptr;
    } else if (!strcmp(t, "Cancel")) {
        lv_obj_del_async(npOverlay);
        npOverlay = nullptr;
    } else if (!strcmp(t, LV_SYMBOL_BACKSPACE)) {
        lv_textarea_del_char(npTa);
    } else {
        lv_textarea_add_text(npTa, t);
    }
}

void ui_numpad(const char* title, float initial, void (*cb)(float)) {
    static const char* map[] = {"1", "2", "3", "\n", "4", "5", "6", "\n", "7", "8", "9", "\n",
                                ".", "0", LV_SYMBOL_BACKSPACE, "\n", "Cancel", "OK", ""};
    npCb = cb;
    npOverlay = lv_obj_create(lv_layer_top());
    lv_obj_set_size(npOverlay, LCD_W, LCD_H);
    lv_obj_set_style_bg_color(npOverlay, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(npOverlay, LV_OPA_60, 0);
    lv_obj_set_style_border_width(npOverlay, 0, 0);
    lv_obj_set_style_radius(npOverlay, 0, 0);

    lv_obj_t* c = ui_card(npOverlay, 0, 0, 400, 460);
    lv_obj_center(c);
    lv_obj_t* l = ui_label(c, title, &lv_font_montserrat_20);
    lv_obj_align(l, LV_ALIGN_TOP_MID, 0, 0);

    npTa = lv_textarea_create(c);
    lv_textarea_set_one_line(npTa, true);
    lv_textarea_set_accepted_chars(npTa, "0123456789.-");
    lv_obj_set_style_text_font(npTa, &lv_font_montserrat_28, 0);
    lv_obj_set_size(npTa, 370, 56);
    lv_obj_align(npTa, LV_ALIGN_TOP_MID, 0, 30);
    char buf[16];
    snprintf(buf, sizeof(buf), "%g", initial);
    lv_textarea_set_text(npTa, buf);

    lv_obj_t* bm = lv_btnmatrix_create(c);
    lv_btnmatrix_set_map(bm, map);
    lv_obj_set_size(bm, 380, 340);
    lv_obj_align(bm, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_opa(bm, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(bm, 0, 0);
    lv_obj_set_style_text_font(bm, &lv_font_montserrat_24, LV_PART_ITEMS);
    lv_obj_add_event_cb(bm, np_evt, LV_EVENT_VALUE_CHANGED, nullptr);
}

// ---------------------------------------------------------------- navigation

void ui_set_update(void (*fn)()) { upd = fn; }
Scr ui_current() { return cur; }

static void build(Scr s) {
    upd = nullptr;
    mr_console_active(false);
    lv_obj_clean(content);
    cur = s;
    lv_label_set_text(lblTitle, titles[s]);
    if (s == S_HOME) {
        lv_obj_add_flag(btnBack, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(btnHome, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_x(lblTitle, 16);
    } else {
        lv_obj_clear_flag(btnBack, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(btnHome, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_x(lblTitle, 140);
    }
    builders[s](content);
    if (upd) upd();
}

static void nav_async(void* p) { build((Scr)(intptr_t)p); }

void ui_goto(Scr s) {
    if (s == cur) return;
    if (s == S_HOME) sp = 0;
    else if (cur != S_COUNT && sp < 12) stack[sp++] = cur;
    lv_async_call(nav_async, (void*)(intptr_t)s);
}

void ui_back() {
    Scr s = sp > 0 ? stack[--sp] : S_HOME;
    lv_async_call(nav_async, (void*)(intptr_t)s);
}

// ---------------------------------------------------------------- top bar

static void back_cb(lv_event_t*) { ui_back(); }
static void home_cb(lv_event_t*) { ui_goto(S_HOME); }
static void estop_cb(lv_event_t*) {
    mr_post("/printer/emergency_stop");
    ui_toast("Emergency stop sent", true);
}
static void temps_cb(lv_event_t*) { ui_goto(S_TEMP); }

static void build_topbar(lv_obj_t* scr) {
    lv_obj_t* bar = lv_obj_create(scr);
    lv_obj_set_size(bar, LCD_W, 56);
    lv_obj_set_pos(bar, 0, 0);
    lv_obj_set_style_bg_color(bar, COL_BAR, 0);
    lv_obj_set_style_radius(bar, 0, 0);
    lv_obj_set_style_border_width(bar, 0, 0);
    lv_obj_set_style_pad_all(bar, 0, 0);
    lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);

    btnBack = ui_btn(bar, LV_SYMBOL_LEFT, 60, 44, back_cb);
    lv_obj_set_pos(btnBack, 6, 6);
    btnHome = ui_btn(bar, LV_SYMBOL_HOME, 60, 44, home_cb);
    lv_obj_set_pos(btnHome, 72, 6);

    lblTitle = ui_label(bar, "", &lv_font_montserrat_20);
    lv_obj_set_pos(lblTitle, 140, 17);

    lv_obj_t* r = ui_row(bar, 500, 56);
    lv_obj_align(r, LV_ALIGN_RIGHT_MID, -6, 0);
    lv_obj_set_flex_align(r, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(r, 12, 0);
    lv_obj_add_flag(r, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(r, temps_cb, LV_EVENT_CLICKED, nullptr);

    lblExt = ui_label(r, "", &lv_font_montserrat_16);
    lblBed = ui_label(r, "", &lv_font_montserrat_16);
    lblState = ui_label(r, "", &lv_font_montserrat_16);
    lblClock = ui_label(r, "", &lv_font_montserrat_16);
    lblWifi = ui_label(r, LV_SYMBOL_WIFI, &lv_font_montserrat_16);
    lv_obj_t* es = ui_btn(r, "E-STOP", 92, 44, estop_cb, nullptr, COL_RED);
    lv_obj_set_style_text_font(es, &lv_font_montserrat_16, 0);
}

static void update_topbar() {
    char buf[48];
    snprintf(buf, sizeof(buf), LV_SYMBOL_TINT " %.0f/%.0f" DEG, g.ext.temp, g.ext.target);
    set_text(lblExt, g.ready ? buf : "");
    if (g.hasBed && g.ready) {
        snprintf(buf, sizeof(buf), "Bed %.0f/%.0f" DEG, g.bed.temp, g.bed.target);
        set_text(lblBed, buf);
    } else {
        set_text(lblBed, "");
    }
    const char* st = !g.online ? "offline" : !g.ready ? g.klippy : g.pstate;
    set_text(lblState, st);
    set_text_color(lblState, state_color(st));
    set_text_color(lblWifi, g.wifi ? COL_GREEN : COL_RED);
    char hm[16];
    set_text(lblClock, clock_hm(hm, sizeof(hm)) ? hm : "");
}

// ---------------------------------------------------------------- screen sleep

static lv_obj_t* sleepOv;

static void wake_cb(lv_event_t*) {
    if (!sleepOv) return;
    board_backlight(true);
    lv_obj_del_async(sleepOv);
    sleepOv = nullptr;
}

static void update_sleep() {
    if (!cfg.sleepMin) return;
    if (!sleepOv && lv_disp_get_inactive_time(nullptr) > (uint32_t)cfg.sleepMin * 60000UL) {
        board_backlight(false);
        // transparent overlay swallows the wake-up touch so it doesn't press a button
        sleepOv = lv_obj_create(lv_layer_top());
        lv_obj_set_size(sleepOv, LCD_W, LCD_H);
        lv_obj_set_style_bg_opa(sleepOv, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(sleepOv, 0, 0);
        lv_obj_add_event_cb(sleepOv, wake_cb, LV_EVENT_PRESSED, nullptr);
    }
}

// ---------------------------------------------------------------- main timer

static bool dashMode = false;  // no touch panel: read-only status dashboard

static void tick_cb(lv_timer_t*) {
    static uint32_t lastErr = 0;
    static char lastPstate[16] = "";
    static bool first = true;

    mr_get(&g);
    if (dashMode) {
        if (upd) upd();
        return;
    }
    update_topbar();

    if (first) {
        lastErr = g.errSeq;
        first = false;
    }
    if (g.errSeq != lastErr) {
        lastErr = g.errSeq;
        ui_toast(g.err, true);
    }

    // jump to the status screen when a print starts (also when started from Mainsail)
    bool wasPrinting = !strcmp(lastPstate, "printing") || !strcmp(lastPstate, "paused");
    if (g.ready && is_printing() && !wasPrinting && cur != S_PRINTING && cur != S_CONNECT) ui_goto(S_PRINTING);
    if (g.ready) strlcpy(lastPstate, g.pstate, sizeof(lastPstate));

    update_sleep();
    if (upd) upd();
}

extern "C" const lv_img_dsc_t boot_logo;
extern "C" const uint32_t BOOT_LOGO_BG;

void ui_boot_logo() {
    lv_obj_t* scr = lv_scr_act();
    lv_obj_set_style_bg_color(scr, lv_color_hex(BOOT_LOGO_BG), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t* img = lv_img_create(scr);
    lv_img_set_src(img, &boot_logo);
    lv_obj_center(img);
    lv_refr_now(nullptr);
}

static void preview_cb(lv_timer_t*) {
    static const Scr order[] = {S_HOME, S_PRINTING, S_FILES, S_TEMP, S_MOVE, S_EXTRUDE, S_FANS, S_MACROS,
                                S_CONSOLE, S_POWER, S_SETTINGS, S_SYSTEM, S_ZOFFSET, S_CONNECT, S_ABOUT};
    static int i = 0;
    i = (i + 1) % (int)(sizeof(order) / sizeof(order[0]));
    ui_goto(order[i]);
}

void ui_init() {
    lv_obj_clean(lv_scr_act());  // remove the boot logo
    ui_icons_init();
    lv_disp_t* disp = lv_disp_get_default();
    lv_theme_t* th = lv_theme_default_init(disp, COL_ACCENT, COL_RED, true, &lv_font_montserrat_20);
    lv_disp_set_theme(disp, th);

    lv_obj_t* scr = lv_scr_act();
    lv_obj_set_style_bg_color(scr, COL_BG, 0);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    bool dash = cfg.uiMode == 1 || (cfg.uiMode == 0 && !board_has_touch());
    if (dash) {
        dashMode = true;
        mr_get(&g);
        scr_dashboard(scr);
        lv_timer_create(tick_cb, 500, nullptr);
        return;
    }

    build_topbar(scr);

    content = lv_obj_create(scr);
    lv_obj_set_pos(content, 0, 56);
    lv_obj_set_size(content, CONTENT_W, CONTENT_H);
    lv_obj_set_style_bg_opa(content, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(content, 0, 0);
    lv_obj_set_style_radius(content, 0, 0);
    lv_obj_set_style_pad_all(content, 0, 0);
    lv_obj_clear_flag(content, LV_OBJ_FLAG_SCROLLABLE);

    mr_get(&g);
    build(S_HOME);

    lv_timer_create(tick_cb, 500, nullptr);
    // Touch UI forced on a panel without touch: nobody can tap through it, so show each screen in turn.
    if (!board_has_touch()) lv_timer_create(preview_cb, 8000, nullptr);
}

// ================================================================ HOME

static lv_obj_t *hExtT, *hExtTg, *hBedT, *hBedTg, *hExtra, *hStatus, *hBtnRow, *hBtnRestart, *hBtnFw, *hBtnConn, *hPrintTile;

static void home_tile_cb(lv_event_t* e) {
    Scr s = (Scr)(intptr_t)lv_event_get_user_data(e);
    if (s == S_FILES && is_printing()) s = S_PRINTING;
    ui_goto(s);
}
static void restart_cb(lv_event_t*) { mr_post("/printer/restart"); }
static void fwrestart_cb(lv_event_t*) { mr_post("/printer/firmware_restart"); }
static void conn_cb(lv_event_t*) { ui_goto(S_CONNECT); }

static void home_update() {
    char buf[256];
    snprintf(buf, sizeof(buf), "%.1f" DEG, g.ext.temp);
    set_text(hExtT, buf);
    snprintf(buf, sizeof(buf), "/ %.0f" DEG, g.ext.target);
    set_text(hExtTg, buf);
    snprintf(buf, sizeof(buf), "%.1f" DEG, g.bed.temp);
    set_text(hBedT, g.hasBed ? buf : "--");
    snprintf(buf, sizeof(buf), "/ %.0f" DEG, g.bed.target);
    set_text(hBedTg, g.hasBed ? buf : "");

    String ex;
    for (int i = 0; i < g.nExtra && i < 4; i++) {
        snprintf(buf, sizeof(buf), "%s: %.1f" DEG "\n", g.extra[i].label, g.extra[i].temp);
        ex += buf;
    }
    set_text(hExtra, ex.c_str());

    bool showConn = !g.online;
    bool showRestart = g.online && !g.ready;
    if (net_ap_active())
        snprintf(buf, sizeof(buf), "Setup: join Wi-Fi \"%s\" and open http://192.168.4.1", SETUP_AP_SSID);
    else if (!g.wifi) snprintf(buf, sizeof(buf), "Connecting to Wi-Fi %s...", cfg.ssid);
    else if (!g.online) snprintf(buf, sizeof(buf), "Moonraker unreachable at %s:%u", cfg.host, cfg.port);
    else if (!g.ready) snprintf(buf, sizeof(buf), "Klipper %s\n%s", g.klippy, g.klippyMsg);
    else if (is_printing()) snprintf(buf, sizeof(buf), "%s %.0f%%", g.pstate, g.progress * 100);
    else snprintf(buf, sizeof(buf), "%s", g.message[0] ? g.message : "Ready");
    if (g.wifi) {
        size_t n = strlen(buf);
        snprintf(buf + n, sizeof(buf) - n, "\nDisplay: http://%s", WiFi.localIP().toString().c_str());
    }
    set_text(hStatus, buf);
    set_text_color(hStatus, !g.ready ? COL_ORANGE : COL_DIM);

    if (showConn || showRestart) lv_obj_clear_flag(hBtnRow, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(hBtnRow, LV_OBJ_FLAG_HIDDEN);
    if (showConn) lv_obj_clear_flag(hBtnConn, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(hBtnConn, LV_OBJ_FLAG_HIDDEN);
    if (showRestart) {
        lv_obj_clear_flag(hBtnRestart, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(hBtnFw, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(hBtnRestart, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(hBtnFw, LV_OBJ_FLAG_HIDDEN);
    }
    set_text(lv_obj_get_child(hPrintTile, 1), is_printing() ? "Status" : "Print");
}

void scr_home(lv_obj_t* p) {
    lv_obj_t* c = ui_card(p, 8, 8, 262, 408);

    ui_label(c, LV_SYMBOL_TINT " Extruder", &lv_font_montserrat_16, COL_DIM);
    hExtT = ui_label(c, "", &lv_font_montserrat_32);
    lv_obj_set_pos(hExtT, 0, 22);
    hExtTg = ui_label(c, "", &lv_font_montserrat_20, COL_DIM);
    lv_obj_set_pos(hExtTg, 140, 32);

    lv_obj_t* l = ui_label(c, "Bed", &lv_font_montserrat_16, COL_DIM);
    lv_obj_set_pos(l, 0, 72);
    hBedT = ui_label(c, "", &lv_font_montserrat_32);
    lv_obj_set_pos(hBedT, 0, 94);
    hBedTg = ui_label(c, "", &lv_font_montserrat_20, COL_DIM);
    lv_obj_set_pos(hBedTg, 140, 104);

    hExtra = ui_label(c, "", &lv_font_montserrat_16, COL_TEXT);
    lv_obj_set_pos(hExtra, 0, 146);

    hStatus = ui_label(c, "", &lv_font_montserrat_16, COL_DIM);
    lv_label_set_long_mode(hStatus, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(hStatus, 240);
    lv_obj_set_pos(hStatus, 0, 236);

    hBtnRow = ui_row(c, 242, 52);
    lv_obj_align(hBtnRow, LV_ALIGN_BOTTOM_MID, 0, 0);
    hBtnRestart = ui_btn(hBtnRow, "Restart", 116, 48, restart_cb);
    hBtnFw = ui_btn(hBtnRow, "FW Restart", 116, 48, fwrestart_cb, nullptr, COL_ORANGE);
    hBtnConn = ui_btn(hBtnRow, LV_SYMBOL_WIFI " Connection", 240, 48, conn_cb, nullptr, COL_ACCENT);
    lv_obj_set_style_text_font(hBtnRestart, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_font(hBtnFw, &lv_font_montserrat_16, 0);

    lv_obj_t* grid = lv_obj_create(p);
    lv_obj_set_pos(grid, 278, 8);
    lv_obj_set_size(grid, 514, 408);
    lv_obj_set_style_bg_opa(grid, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(grid, 0, 0);
    lv_obj_set_style_pad_all(grid, 0, 0);
    lv_obj_set_style_pad_row(grid, 12, 0);
    lv_obj_set_style_pad_column(grid, 12, 0);
    lv_obj_set_flex_flow(grid, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_clear_flag(grid, LV_OBJ_FLAG_SCROLLABLE);

    struct T { const char* icon; const char* txt; Scr s; };
    static const T tiles[] = {
        {LV_SYMBOL_PLAY, "Print", S_FILES},       {LV_SYMBOL_CHARGE, "Temperature", S_TEMP},
        {LV_SYMBOL_SHUFFLE, "Move", S_MOVE},      {LV_SYMBOL_TINT, "Extrude", S_EXTRUDE},
        {LV_SYMBOL_LOOP, "Fans & Speed", S_FANS}, {LV_SYMBOL_LIST, "Macros", S_MACROS},
        {LV_SYMBOL_KEYBOARD, "Console", S_CONSOLE}, {LV_SYMBOL_POWER, "Power", S_POWER},
        {LV_SYMBOL_SETTINGS, "Settings", S_SETTINGS},
    };
    for (auto& t : tiles) {
        lv_obj_t* b = ui_tile(grid, t.icon, t.txt, 163, 128, home_tile_cb, (void*)(intptr_t)t.s,
                              t.s == S_FILES ? COL_ACCENT : COL_BTN);
        if (t.s == S_FILES) hPrintTile = b;
    }
    ui_set_update(home_update);
}
