// UI framework (top bar, navigation, dialogs, helpers) and the home screen.
#include "ui.h"
#include "board.h"
#include <WiFi.h>
#include "net.h"
#include "clock.h"
#include "ui_icons.h"

MrState g;
bool PORTRAIT = false;
lv_coord_t SW = U(480), SH = U(320);

int32_t UI_K256 = 256;

static void ui_screen_size() {
    SW = lv_disp_get_hor_res(nullptr);
    SH = lv_disp_get_ver_res(nullptr);
    PORTRAIT = SH > SW;
    lv_coord_t lng = LV_MAX(SW, SH), sht = LV_MIN(SW, SH);
    UI_K256 = LV_MAX(256, LV_MIN(lng * 256 / 480, sht * 256 / 320));  // fit the 480x320 design
}

// largest compiled-in font that is no bigger than the scaled design size
static const lv_font_t* pick_font(const lv_font_t* const* fonts, const int* sizes, int n, int px) {
    int want = U(px), best = 0;
    for (int i = 0; i < n; i++)
        if (sizes[i] <= want) best = i;
    return fonts[best];
}

const lv_font_t* FONT(int px) {
    static const lv_font_t* const fonts[] = {&lv_font_montserrat_12, &lv_font_montserrat_14, &lv_font_montserrat_16,
                                             &lv_font_montserrat_20,
#if KLIPPESP_LARGE_FONTS
                                             &lv_font_montserrat_24,
#endif
                                             &lv_font_montserrat_28,
#if KLIPPESP_LARGE_FONTS
                                             &lv_font_montserrat_32, &lv_font_montserrat_48
#endif
    };
    static const int sizes[] = {12, 14, 16, 20,
#if KLIPPESP_LARGE_FONTS
                                24,
#endif
                                28,
#if KLIPPESP_LARGE_FONTS
                                32, 48
#endif
    };
    return pick_font(fonts, sizes, sizeof(sizes) / sizeof(sizes[0]), px);
}

const lv_font_t* ICONS(int px) {
#if KLIPPESP_LARGE_FONTS
    static const lv_font_t* const fonts[] = {&icons_16, &icons_20, &icons_28, &icons_72};
    static const int sizes[] = {16, 20, 28, 72};
#else
    static const lv_font_t* const fonts[] = {&icons_20, &icons_28};
    static const int sizes[] = {20, 28};
#endif
    return pick_font(fonts, sizes, sizeof(sizes) / sizeof(sizes[0]), px);
}

void ui_icons_init() {
    icons_20.fallback = &lv_font_montserrat_20;
    icons_28.fallback = &lv_font_montserrat_28;
#if KLIPPESP_DASHBOARD
    void dashboard_icons_init();
    dashboard_icons_init();
#endif
}

static lv_obj_t* content;
static lv_obj_t* lblTitle;
static lv_obj_t* btnBack;
static lv_obj_t* btnHome;
static lv_obj_t* lblWifi;
static lv_obj_t* lblClock;
static lv_obj_t* topRight;  // clock + wifi + STOP, sized to its content

static void (*upd)() = nullptr;
static Scr cur = S_COUNT;
static Scr stack[12];
static int sp = 0;

static const char* titles[S_COUNT] = {
    "KlippESP", "Print", "Printing", "Temperature", "Move", "Extrude", "Fans & Speed", "Macros",
    "Console", "Power", "Settings", "Connection", "System", "Z Offset", "Date & Time", "About"};

static void (*builders[S_COUNT])(lv_obj_t*) = {
    scr_home, scr_files, scr_printing, scr_temp, scr_move, scr_extrude, scr_fans, scr_macros,
    scr_console, scr_power, scr_settings, scr_connect, scr_system, scr_zoffset, scr_time, scr_about};

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
    lv_obj_set_style_radius(b, U(8), 0);
    lv_obj_set_style_shadow_width(b, 0, 0);
    lv_obj_set_style_pad_all(b, U(2), 0);
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
    lv_obj_set_style_radius(b, U(10), 0);
    lv_obj_set_style_shadow_width(b, 0, 0);
    lv_obj_set_style_pad_all(b, U(2), 0);
    lv_obj_set_flex_flow(b, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(b, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(b, U(4), 0);
    ui_label(b, icon, ICONS(28));
    lv_obj_t* l = ui_label(b, txt, FONT(14));
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
    lv_obj_set_style_radius(c, U(10), 0);
    lv_obj_set_style_pad_all(c, U(8), 0);
    lv_obj_clear_flag(c, LV_OBJ_FLAG_SCROLLABLE);
    return c;
}

lv_obj_t* ui_row(lv_obj_t* parent, lv_coord_t w, lv_coord_t h) {
    lv_obj_t* r = lv_obj_create(parent);
    lv_obj_set_size(r, w, h);
    lv_obj_set_style_bg_opa(r, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(r, 0, 0);
    lv_obj_set_style_pad_all(r, 0, 0);
    lv_obj_set_style_pad_column(r, U(6), 0);
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

static void screen_wake();

void ui_ota_progress(int pct) {
    screen_wake();  // a sleeping screen must show the update
    lv_disp_trig_activity(nullptr);
    // The ST7796 keeps its own frame memory, so flash writes don't disturb the image: show real progress.
    static lv_obj_t *ov, *bar, *lbl;
    static int last = -2;
    if (pct <= -100) {  // update failed
        if (ov) lv_obj_del(ov);
        ov = nullptr;
        last = -2;
        return;
    }
    if (!ov) {
        ov = lv_obj_create(lv_layer_top());
        lv_obj_set_size(ov, SW, SH);
        lv_obj_set_style_bg_color(ov, COL_BG, 0);
        lv_obj_set_style_bg_opa(ov, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(ov, 0, 0);
        lv_obj_set_style_radius(ov, 0, 0);
        lv_obj_clear_flag(ov, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_t* t = ui_label(ov, LV_SYMBOL_DOWNLOAD "  Updating firmware", FONT(20), COL_TEXT);
        lv_obj_align(t, LV_ALIGN_CENTER, 0, -U(50));
        lv_obj_t* s = ui_label(ov, "Do not unplug.\nThe display restarts when done.", FONT(14), COL_DIM);
        lv_obj_set_style_text_align(s, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_align(s, LV_ALIGN_CENTER, 0, -U(20));
        bar = lv_bar_create(ov);
        lv_obj_set_size(bar, SW - U(80), U(14));
        lv_obj_align(bar, LV_ALIGN_CENTER, 0, U(18));
        lv_bar_set_range(bar, 0, 100);
        lbl = ui_label(ov, "", FONT(20), COL_TEXT);
        lv_obj_align(lbl, LV_ALIGN_CENTER, 0, U(52));
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

// ---------------------------------------------------------------- virtual list

#define VLIST_MAX_CELLS 40

struct VList {
    lv_obj_t* spacer;
    lv_coord_t pitchX, pitchY, gap;
    int cols, count, nCells;
    vlist_bind_cb bind;
    lv_obj_t* cells[VLIST_MAX_CELLS];
};

static void vlist_layout(lv_obj_t* cont, bool force) {
    VList* v = (VList*)lv_obj_get_user_data(cont);
    if (!v) return;
    int firstRow = LV_MAX(0, lv_obj_get_scroll_y(cont)) / v->pitchY;
    for (int k = 0; k < v->nCells; k++) {
        lv_obj_t* c = v->cells[k];
        int row = firstRow + k / v->cols, col = k % v->cols, idx = row * v->cols + col;
        if (idx >= v->count) {
            set_visible(c, false);
            continue;
        }
        lv_obj_set_pos(c, col * v->pitchX, row * v->pitchY);
        if (force || (int)(intptr_t)lv_obj_get_user_data(c) != idx) {
            lv_obj_set_user_data(c, (void*)(intptr_t)idx);
            v->bind(c, idx);
        }
        set_visible(c, true);
    }
}

static void vlist_event(lv_event_t* e) {
    lv_obj_t* cont = lv_event_get_target(e);
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_SCROLL || code == LV_EVENT_SCROLL_END) {
        vlist_layout(cont, false);
    } else if (code == LV_EVENT_DELETE) {
        free(lv_obj_get_user_data(cont));
        lv_obj_set_user_data(cont, nullptr);
    }
}

lv_obj_t* ui_vlist(lv_obj_t* parent, lv_coord_t x, lv_coord_t y, lv_coord_t w, lv_coord_t h, lv_coord_t cellW,
                   lv_coord_t cellH, lv_coord_t gap, int cols, vlist_make_cb make, vlist_bind_cb bind) {
    const lv_coord_t pad = U(6);
    lv_obj_t* cont = lv_obj_create(parent);
    lv_obj_set_pos(cont, x, y);
    lv_obj_set_size(cont, w, h);
    lv_obj_set_style_bg_opa(cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(cont, 0, 0);
    lv_obj_set_style_radius(cont, 0, 0);
    lv_obj_set_style_pad_all(cont, pad, 0);
    lv_obj_set_scroll_dir(cont, LV_DIR_VER);
    VList* v = (VList*)calloc(1, sizeof(VList));
    if (!v) return cont;
    v->gap = gap;
    v->pitchX = cellW + gap;
    v->pitchY = cellH + gap;
    v->cols = LV_MAX(1, cols);
    v->bind = bind;
    // enough rows to cover the viewport while a row is partly scrolled out at each end
    int rows = (h - 2 * pad) / v->pitchY + 2;
    v->nCells = LV_MIN(rows * v->cols, VLIST_MAX_CELLS);
    v->spacer = lv_obj_create(cont);  // sets the scrollable height for `count` items
    lv_obj_remove_style_all(v->spacer);
    lv_obj_set_size(v->spacer, 1, 1);
    lv_obj_clear_flag(v->spacer, LV_OBJ_FLAG_CLICKABLE);
    for (int k = 0; k < v->nCells; k++) {
        v->cells[k] = make(cont);
        lv_obj_set_user_data(v->cells[k], (void*)(intptr_t)-1);
        lv_obj_add_flag(v->cells[k], LV_OBJ_FLAG_HIDDEN);
    }
    lv_obj_set_user_data(cont, v);
    lv_obj_add_event_cb(cont, vlist_event, LV_EVENT_SCROLL, nullptr);
    lv_obj_add_event_cb(cont, vlist_event, LV_EVENT_SCROLL_END, nullptr);
    lv_obj_add_event_cb(cont, vlist_event, LV_EVENT_DELETE, nullptr);
    return cont;
}

void ui_vlist_set_count(lv_obj_t* cont, int count) {
    VList* v = (VList*)lv_obj_get_user_data(cont);
    if (!v) return;
    v->count = count;
    int rows = (count + v->cols - 1) / v->cols;
    lv_obj_set_pos(v->spacer, 0, LV_MAX(0, rows * v->pitchY - v->gap - 1));  // content ends at the last row
    lv_obj_update_layout(cont);
    if (lv_obj_get_scroll_bottom(cont) < 0) lv_obj_scroll_to_y(cont, LV_MAX(0, lv_obj_get_scroll_y(cont) + lv_obj_get_scroll_bottom(cont)), LV_ANIM_OFF);
    vlist_layout(cont, true);
}

int ui_vlist_index(lv_obj_t* cell) { return (int)(intptr_t)lv_obj_get_user_data(cell); }

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
        lv_obj_set_width(toast, SW - U(40));
        lv_label_set_long_mode(toast, LV_LABEL_LONG_WRAP);
        lv_obj_set_style_text_font(toast, FONT(14), 0);
        lv_obj_set_style_pad_all(toast, U(10), 0);
        lv_obj_set_style_radius(toast, U(10), 0);
        lv_obj_set_style_bg_opa(toast, LV_OPA_90, 0);
        lv_obj_set_style_text_color(toast, COL_TEXT, 0);
        lv_obj_set_style_text_align(toast, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_align(toast, LV_ALIGN_BOTTOM_MID, 0, -U(10));
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
    lv_obj_set_width(mb, LV_MIN(U(380), SW - U(20)));
    lv_obj_set_style_text_font(mb, FONT(14), 0);
    lv_obj_center(mb);
    lv_obj_t* bm = lv_msgbox_get_btns(mb);
    lv_obj_set_height(bm, U(46));
    lv_btnmatrix_set_btn_ctrl_all(bm, LV_BTNMATRIX_CTRL_CLICK_TRIG);
    lv_obj_set_style_min_width(bm, LV_MIN(U(300), SW - U(60)), 0);
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
    lv_obj_set_size(npOverlay, SW, SH);
    lv_obj_set_style_bg_color(npOverlay, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(npOverlay, LV_OPA_60, 0);
    lv_obj_set_style_border_width(npOverlay, 0, 0);
    lv_obj_set_style_radius(npOverlay, 0, 0);

    lv_obj_t* c = ui_card(npOverlay, 0, 0, PORTRAIT ? U(300) : U(440), PORTRAIT ? U(420) : U(300));
    lv_obj_center(c);
    lv_obj_t* l = ui_label(c, title, FONT(16));
    lv_obj_set_pos(l, 0, 0);

    npTa = lv_textarea_create(c);
    lv_textarea_set_one_line(npTa, true);
    lv_textarea_set_accepted_chars(npTa, "0123456789.-");
    lv_obj_set_style_text_font(npTa, FONT(28), 0);
    lv_obj_set_size(npTa, PORTRAIT ? U(284) : U(150), U(54));
    lv_obj_set_pos(npTa, 0, U(30));
    char buf[16];
    snprintf(buf, sizeof(buf), "%g", initial);
    lv_textarea_set_text(npTa, buf);

    lv_obj_t* bm = lv_btnmatrix_create(c);
    lv_btnmatrix_set_map(bm, map);
    if (PORTRAIT) {
        lv_obj_set_size(bm, U(290), U(314));
        lv_obj_align(bm, LV_ALIGN_BOTTOM_MID, 0, U(6));
    } else {
        lv_obj_set_size(bm, U(262), U(284));
        lv_obj_align(bm, LV_ALIGN_RIGHT_MID, U(6), 0);
    }
    lv_obj_set_style_bg_opa(bm, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(bm, 0, 0);
    lv_obj_set_style_pad_all(bm, U(2), 0);
    lv_obj_set_style_text_font(bm, FONT(20), LV_PART_ITEMS);
    lv_obj_add_event_cb(bm, np_evt, LV_EVENT_VALUE_CHANGED, nullptr);
}

// ---------------------------------------------------------------- navigation

void ui_set_update(void (*fn)()) { upd = fn; }
Scr ui_current() { return cur; }

static void update_badge();

static void build(Scr s) {
    upd = nullptr;
    mr_console_active(false);
    lv_obj_clean(content);
    cur = s;
    lv_label_set_text(lblTitle, titles[s]);
    bool badge = s == S_HOME || s == S_PRINTING;  // these show a live state badge instead of a fixed title
    if (s == S_HOME) {
        lv_obj_add_flag(btnBack, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(btnHome, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_clear_flag(btnBack, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(btnHome, LV_OBJ_FLAG_HIDDEN);
    }
    lv_obj_set_pos(lblTitle, s == S_HOME ? U(8) : U(98), badge ? U(7) : U(11));
    // never run into the clock: titles that don't fit end in "..."
    lv_label_set_long_mode(lblTitle, LV_LABEL_LONG_DOT);
    lv_obj_set_width(lblTitle, LV_SIZE_CONTENT);
    lv_obj_update_layout(lblTitle);
    lv_obj_update_layout(topRight);
    lv_coord_t maxw = SW - lv_obj_get_width(topRight) - U(12) - (s == S_HOME ? U(8) : U(98));
    if (lv_obj_get_width(lblTitle) > maxw) lv_obj_set_width(lblTitle, maxw);
    lv_obj_set_style_bg_opa(lblTitle, badge ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
    if (badge) update_badge();
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
static void clock_cb(lv_event_t*) { ui_goto(S_TIME); }

static void build_topbar(lv_obj_t* scr) {
    lv_obj_t* bar = lv_obj_create(scr);
    lv_obj_set_size(bar, SW, TOPBAR_H);
    lv_obj_set_pos(bar, 0, 0);
    lv_obj_set_style_bg_color(bar, COL_BAR, 0);
    lv_obj_set_style_radius(bar, 0, 0);
    lv_obj_set_style_border_width(bar, 0, 0);
    lv_obj_set_style_pad_all(bar, 0, 0);
    lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);

    btnBack = ui_btn(bar, LV_SYMBOL_LEFT, U(42), U(32), back_cb);
    lv_obj_set_pos(btnBack, U(4), U(4));
    btnHome = ui_btn(bar, LV_SYMBOL_HOME, U(42), U(32), home_cb);
    lv_obj_set_pos(btnHome, U(50), U(4));

    lblTitle = ui_label(bar, "", FONT(16));
    lv_obj_set_pos(lblTitle, U(98), U(11));
    lv_obj_set_style_radius(lblTitle, U(8), 0);
    lv_obj_set_style_pad_hor(lblTitle, U(10), 0);
    lv_obj_set_style_pad_ver(lblTitle, U(4), 0);

    // date/time + wifi + E-STOP (temperatures live in the Home / Printing cards); tap the clock for Date & Time
    lv_obj_t* r = topRight = ui_row(bar, U(200), TOPBAR_H);  // width set below from its contents
    lv_obj_align(r, LV_ALIGN_RIGHT_MID, -U(4), 0);
    lv_obj_set_flex_align(r, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(r, U(6), 0);
    lv_obj_add_flag(r, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(r, clock_cb, LV_EVENT_CLICKED, nullptr);

    lblClock = ui_label(r, "", FONT(16));
    // fixed width for the longest text, so the row (and the title space left of it) never changes size
    const char* widest = PORTRAIT ? "12:59 PM" : "Wed 30 Sep  12:59 PM";
    lv_obj_set_width(lblClock, lv_txt_get_width(widest, strlen(widest), FONT(16), 0, LV_TEXT_FLAG_NONE));
    lv_obj_set_style_text_align(lblClock, LV_TEXT_ALIGN_RIGHT, 0);
    lblWifi = ui_label(r, LV_SYMBOL_WIFI, FONT(14));
    lv_obj_t* es = ui_btn(r, "STOP", U(50), U(32), estop_cb, nullptr, COL_RED);
    lv_obj_set_style_text_font(es, FONT(14), 0);
    lv_obj_update_layout(r);
    lv_obj_set_width(r, lv_obj_get_width(lblClock) + lv_obj_get_width(lblWifi) + lv_obj_get_width(es) + 2 * U(6));
}

// Home shows a coloured state badge where other screens show their title.
static void update_badge() {
    const char *icon, *text;
    lv_color_t col;
    bool heating = g.ext.target > 0 || (g.hasBed && g.bed.target > 0);
    if (net_ap_active()) { icon = LV_SYMBOL_WIFI; text = "Setup"; col = COL_ACCENT; }
    else if (!g.wifi) { icon = LV_SYMBOL_WIFI; text = "No Wi-Fi"; col = COL_RED; }
    else if (!g.online) { icon = LV_SYMBOL_CLOSE; text = "Offline"; col = COL_RED; }
    else if (!g.ready) {
        bool starting = !strcmp(g.klippy, "startup");
        icon = starting ? LV_SYMBOL_REFRESH : LV_SYMBOL_WARNING;
        text = starting ? "Starting" : !strcmp(g.klippy, "shutdown") ? "Shutdown" : "Error";
        col = starting ? COL_ORANGE : COL_RED;
    } else if (!strcmp(g.pstate, "printing")) { icon = LV_SYMBOL_PLAY; text = "Printing"; col = COL_ACCENT; }
    else if (!strcmp(g.pstate, "paused")) { icon = LV_SYMBOL_PAUSE; text = "Paused"; col = COL_ORANGE; }
    else if (!strcmp(g.pstate, "complete")) { icon = LV_SYMBOL_OK; text = "Complete"; col = COL_GREEN; }
    else if (!strcmp(g.pstate, "cancelled")) { icon = LV_SYMBOL_CLOSE; text = "Cancelled"; col = COL_ORANGE; }
    else if (!strcmp(g.pstate, "error")) { icon = LV_SYMBOL_WARNING; text = "Print error"; col = COL_RED; }
    else if (heating) { icon = LV_SYMBOL_UP; text = "Heating"; col = COL_ORANGE; }
    else { icon = LV_SYMBOL_OK; text = "Ready"; col = COL_GREEN; }
    char buf[40];
    if (is_printing()) snprintf(buf, sizeof(buf), "%s %s %.0f%%", icon, text, g.progress * 100);
    else snprintf(buf, sizeof(buf), "%s %s", icon, text);
    set_text(lblTitle, buf);
    set_bg_color(lblTitle, col, 0);
}

static void update_topbar() {
    if (cur == S_HOME || cur == S_PRINTING) update_badge();
    set_text_color(lblWifi, g.wifi ? COL_GREEN : COL_RED);
    // "Fri 10 Oct  23:15" (landscape) / "23:15" (portrait); empty until NTP has synced
    char buf[40] = "", hm[16];
    if (clock_hm(hm, sizeof(hm))) {
        if (PORTRAIT) {
            strlcpy(buf, hm, sizeof(buf));
        } else {
            time_t t = time(nullptr);
            struct tm tmv;
            localtime_r(&t, &tmv);
            char d[20];
            strftime(d, sizeof(d), "%a %d %b", &tmv);
            snprintf(buf, sizeof(buf), "%s  %s", d, hm);
        }
    }
    set_text(lblClock, buf);
}

// ---------------------------------------------------------------- screen sleep

static lv_obj_t* sleepOv;

static void wake_cb(lv_event_t*) {
    if (!sleepOv) return;
    board_backlight(true);
    lv_obj_del_async(sleepOv);
    sleepOv = nullptr;
}

static void screen_wake() {
    if (sleepOv) wake_cb(nullptr);
    else if (!board_backlight_on()) board_backlight(true);
}

static void update_sleep() {
    if (!cfg.sleepMin) return;
    // Stay on for the whole print (paused included); the timeout starts counting once the job ends.
    if (g.ready && is_printing()) {
        screen_wake();
        lv_disp_trig_activity(nullptr);
        return;
    }
    if (!sleepOv && lv_disp_get_inactive_time(nullptr) > (uint32_t)cfg.sleepMin * 60000UL) {
        board_backlight(false);
        // transparent overlay swallows the wake-up touch so it doesn't press a button
        sleepOv = lv_obj_create(lv_layer_top());
        lv_obj_set_size(sleepOv, SW, SH);
        lv_obj_set_style_bg_opa(sleepOv, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(sleepOv, 0, 0);
        lv_obj_add_event_cb(sleepOv, wake_cb, LV_EVENT_PRESSED, nullptr);
    }
}

// ---------------------------------------------------------------- main timer

uint32_t ui_lvgl_peak;  // highest LVGL pool use seen (bytes), for sizing LV_MEM_SIZE

static bool dashMode = false;  // read-only status dashboard instead of the touch UI

static void tick_cb(lv_timer_t*) {
    lv_mem_monitor_t mm;
    lv_mem_monitor(&mm);
    if (mm.total_size - mm.free_size > ui_lvgl_peak) ui_lvgl_peak = mm.total_size - mm.free_size;
    static uint32_t lastErr = 0;
    static char lastPstate[20] = "";
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

    // Follow the printer: any job state change (start, pause, resume, complete, cancel, error) shows the status
    // page; Klipper going down shows Home (with Restart buttons); a finished job being cleared returns Home.
    // Started from the touch screen or from Mainsail alike. Not while the user is typing on Connection.
    char key[20];
    strlcpy(key, !g.online ? "offline" : !g.ready ? g.klippy : g.pstate, sizeof(key));
    if (strcmp(key, lastPstate) && lastPstate[0] && cur != S_CONNECT) {
        bool job = g.ready && (is_printing() || !strcmp(g.pstate, "complete") || !strcmp(g.pstate, "cancelled") ||
                               (!strcmp(g.pstate, "error") && g.file[0]));
        if (job && cur != S_PRINTING) ui_goto(S_PRINTING);
        else if (!job && !g.ready && cur == S_PRINTING) ui_goto(S_HOME);  // offline or Klipper down
        else if (!job && g.online && !g.ready && cur != S_HOME) ui_goto(S_HOME);
        else if (!job && g.ready && cur == S_PRINTING) ui_goto(S_HOME);
    }
    strlcpy(lastPstate, key, sizeof(lastPstate));

    update_sleep();
    if (upd) upd();
}

extern "C" const lv_img_dsc_t boot_logo;
extern "C" const uint32_t BOOT_LOGO_BG;

void ui_boot_logo() {
    ui_screen_size();
    lv_obj_t* scr = lv_scr_act();
    lv_obj_set_style_bg_color(scr, lv_color_hex(BOOT_LOGO_BG), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t* img = lv_img_create(scr);
    lv_img_set_src(img, &boot_logo);
    lv_obj_center(img);
    lv_refr_now(nullptr);
}

// Touch UI on a panel without touch (Interface: Touch UI): nobody can tap through it, so show each screen in turn.
static void preview_cb(lv_timer_t*) {
    static const Scr order[] = {S_HOME, S_PRINTING, S_FILES, S_TEMP, S_MOVE, S_EXTRUDE, S_FANS, S_MACROS,
                                S_CONSOLE, S_POWER, S_SETTINGS, S_SYSTEM, S_ZOFFSET, S_TIME, S_CONNECT, S_ABOUT};
    static int i = 0;
    i = (i + 1) % (int)(sizeof(order) / sizeof(order[0]));
    ui_goto(order[i]);
}

bool ui_dashboard_mode() {
#if KLIPPESP_DASHBOARD
    return cfg.uiMode == 1 || (cfg.uiMode == 0 && !board_has_touch());
#else
    return false;
#endif
}

void ui_init() {
    lv_obj_clean(lv_scr_act());  // remove the boot logo
    ui_icons_init();
    dashMode = ui_dashboard_mode();
    lv_disp_t* disp = lv_disp_get_default();
    lv_theme_t* th = lv_theme_default_init(disp, COL_ACCENT, COL_RED, true,
                                           dashMode ? FONT(20) : FONT(16));
    lv_disp_set_theme(disp, th);

    lv_obj_t* scr = lv_scr_act();
    lv_obj_set_style_bg_color(scr, COL_BG, 0);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

#if KLIPPESP_DASHBOARD
    if (dashMode) {
        mr_get(&g);
        scr_dashboard(scr);
        lv_timer_create(tick_cb, 500, nullptr);
        return;
    }
#endif

    build_topbar(scr);

    content = lv_obj_create(scr);
    lv_obj_set_pos(content, 0, TOPBAR_H);
    lv_obj_set_size(content, CONTENT_W, CONTENT_H);
    lv_obj_set_style_bg_opa(content, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(content, 0, 0);
    lv_obj_set_style_radius(content, 0, 0);
    lv_obj_set_style_pad_all(content, 0, 0);
    lv_obj_clear_flag(content, LV_OBJ_FLAG_SCROLLABLE);

    mr_get(&g);
    build(S_HOME);

    lv_timer_create(tick_cb, 500, nullptr);
    if (!board_has_touch()) lv_timer_create(preview_cb, 8000, nullptr);
}
