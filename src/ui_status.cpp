// Graphical status pages for the touch UI (480x320): Home (gauges + named list + menu) and Printing
// (progress ring around a hotend icon, gauges, icon tiles, big action buttons). Gauge behaviour matches the
// main branch's dashboard.
#include "ui.h"
#include "ui_icons.h"
#include "net.h"
#include "clock.h"
#include <vector>

#define COL_COLD lv_color_hex(0x58a6ff)
#define COL_TILE lv_color_hex(0x232c3b)
#define COL_LINE lv_color_hex(0x30363d)

// ---------------------------------------------------------------- shared widgets

static lv_obj_t* make_arc(lv_obj_t* parent, lv_coord_t size, lv_coord_t width, uint16_t rot, uint16_t span) {
    lv_obj_t* a = lv_arc_create(parent);
    lv_obj_set_size(a, size, size);
    lv_arc_set_rotation(a, rot);
    lv_arc_set_bg_angles(a, 0, span);
    lv_arc_set_range(a, 0, 1000);
    lv_arc_set_value(a, 0);
    lv_obj_remove_style(a, nullptr, LV_PART_KNOB);
    lv_obj_clear_flag(a, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_arc_width(a, width, LV_PART_MAIN);
    lv_obj_set_style_arc_width(a, width, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(a, COL_LINE, LV_PART_MAIN);
    lv_obj_set_style_arc_rounded(a, true, LV_PART_INDICATOR);
    lv_obj_set_style_pad_all(a, 0, 0);
    return a;
}

struct Gauge {
    lv_obj_t *arc, *icon, *temp, *target, *bar;
    int max;  // fallback scale when max_temp is unknown
};

// Arc: progress toward the target from room temperature while heating; temperature on the max scale when off.
// Bar: temperature relative to the configured max_temp.
static void update_gauge(Gauge& gg, const MrTemp& t, bool present) {
    const float ROOM = 20;
    char buf[40];
    if (!present || !g.ready) {
        set_text(gg.temp, "--");
        set_text(gg.target, present ? "" : "none");
        lv_arc_set_value(gg.arc, 0);
        if (gg.bar) lv_bar_set_value(gg.bar, 0, LV_ANIM_OFF);
        set_text_color(gg.icon, COL_DIM);
        return;
    }
    bool on = t.target > 0;
    bool atTarget = on && fabsf(t.temp - t.target) < 3;
    lv_color_t col = atTarget ? COL_GREEN : (on || t.temp > 45) ? COL_ORANGE : COL_COLD;
    float maxT = t.maxTemp > 0 ? t.maxTemp : gg.max;
    int ofMax = (int)constrain(t.temp / maxT * 1000, 0, 1000);
    int arc = ofMax;
    if (on) {
        float span = t.target - ROOM;
        arc = atTarget ? 1000 : (int)constrain((span > 1 ? (t.temp - ROOM) / span : 1) * 1000, 0, 1000);
    }
    lv_arc_set_value(gg.arc, arc);
    set_arc_color(gg.arc, col);
    set_text_color(gg.icon, col);
    if (gg.bar) {
        lv_bar_set_value(gg.bar, ofMax, LV_ANIM_OFF);
        set_bg_color(gg.bar, col, LV_PART_INDICATOR);
    }
    snprintf(buf, sizeof(buf), "%.0f" DEG, t.temp);
    set_text(gg.temp, buf);
    if (on) snprintf(buf, sizeof(buf), "%s %.0f" DEG, atTarget ? ICON_OK : ICON_FIRE, t.target);
    else snprintf(buf, sizeof(buf), "off");
    set_text(gg.target, buf);
    set_text_color(gg.target, on ? col : COL_DIM);
}

// "hotend_fan" -> "Hotend fan"; then shorten until it fits maxw px in font (drop "fan", abbreviations, trim)
struct Abbr {
    const char* from;
    const char* to;
};
static const Abbr ABBR[] = {
    {"raspberry pi", "Pi"}, {"raspberrypi", "Pi"}, {"raspberry", "Pi"}, {"rpi", "Pi"}, {"temperature", "Temp"},
    {"controller", "Ctrl"}, {"electronics", "Elec."}, {"extruder", "Extr."}, {"enclosure", "Encl."},
    {"motherboard", "Board"}, {"mainboard", "Board"}, {"stepper", "Step."}, {"drivers", "Drv"}, {"driver", "Drv"},
    {"toolhead", "TH"}, {"heatbreak", "HB"}, {"chamber", "Chamb."}, {"nevermore", "Filter"},
    {"bed fans", "Bed"}, {"part cooling", "Part"},
};

static lv_coord_t text_w(const lv_font_t* f, const char* s) {
    lv_point_t p;
    lv_txt_get_size(&p, s, f, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    return p.x;
}

static void fit_name(char* out, size_t n, const lv_font_t* font, lv_coord_t maxw, const char* raw, bool dropFan) {
    String s = raw;
    s.replace("_", " ");
    s.trim();
    if (s.length()) s.setCharAt(0, toupper(s[0]));
    auto fits = [&] {
        strlcpy(out, s.c_str(), n);
        return text_w(font, out) <= maxw;
    };
    if (fits()) return;
    if (dropFan) {
        String t = " " + s + " ";
        t.replace(" fan ", " ");
        t.replace(" Fan ", " ");
        t.trim();
        if (t.length()) s = t;
        if (fits()) return;
    }
    for (const Abbr& a : ABBR) {
        String low = " " + s + " ";
        low.toLowerCase();
        int at = low.indexOf(String(" ") + a.from + " ");
        if (at < 0) continue;
        s = s.substring(0, at) + a.to + s.substring(at + strlen(a.from));
        if (fits()) return;
    }
    while (s.length() > 3 && !fits()) s.remove(s.length() - 1);
    fits();
}

static float pct(double used, double total) { return total > 0 ? 100.0 * used / total : 0; }
static lv_color_t load_color(float p) { return p < 60 ? COL_GREEN : p < 85 ? COL_ORANGE : COL_RED; }

static bool any_heating() {
    if (g.ext.target > 0 || (g.hasBed && g.bed.target > 0)) return true;
    for (int i = 0; i < g.nExtra; i++)
        if (g.extra[i].settable && g.extra[i].target > 0) return true;
    return false;
}

static void goto_cb(lv_event_t* e) { ui_goto((Scr)(intptr_t)lv_event_get_user_data(e)); }

// ================================================================ HOME

static Gauge hExt, hBed;
static lv_obj_t* hPrintTile;
#define HOME_SLOTS 8  // 2 columns x 4 rows; the last host rows are reserved
static lv_obj_t *hSlotName[HOME_SLOTS], *hSlotVal[HOME_SLOTS];
static lv_coord_t hSlotNameW;
static lv_obj_t *hOnline[3];                        // gauges + list, hidden while the printer is unavailable
static lv_obj_t *hOff, *hOffIcon, *hOffTitle, *hOffText, *hOffBtns;
static lv_obj_t* hTiles[9];
static const Scr HOME_TILE_SCR[9] = {S_FILES, S_TEMP, S_MOVE, S_EXTRUDE, S_FANS, S_MACROS, S_CONSOLE, S_POWER, S_SETTINGS};

static void restart_cb(lv_event_t*) { mr_post("/printer/restart"); }
static void fwrestart_cb(lv_event_t*) { mr_post("/printer/firmware_restart"); }

static lv_obj_t* home_gauge(Gauge& gg, lv_obj_t* p, lv_coord_t y, lv_coord_t w, const char* icon, const char* name, int max) {
    lv_obj_t* c = ui_card(p, 6, y, w, 84);
    gg.max = max;
    gg.arc = make_arc(c, 66, 8, 135, 270);
    lv_obj_align(gg.arc, LV_ALIGN_LEFT_MID, 0, 0);
    gg.icon = ui_label(gg.arc, icon, &icons_20, COL_DIM);
    lv_obj_center(gg.icon);
    lv_obj_t* n = ui_label(c, name, &lv_font_montserrat_12, COL_DIM);
    lv_obj_set_pos(n, 78, 0);
    gg.temp = ui_label(c, "", &lv_font_montserrat_28);
    lv_obj_set_pos(gg.temp, 76, 12);
    gg.target = ui_label(c, "", &icons_20, COL_DIM);
    lv_obj_align(gg.target, LV_ALIGN_TOP_RIGHT, 0, -2);
    gg.bar = lv_bar_create(c);
    lv_obj_set_size(gg.bar, w - 16 - 78, 6);
    lv_obj_set_pos(gg.bar, 78, 56);
    lv_bar_set_range(gg.bar, 0, 1000);
    return c;
}

// Printer unavailable: big status card instead of gauges, and only Settings + Power in the menu.
static void home_offline() {
    const char *icon, *title;
    char text[220];
    lv_color_t col = COL_RED;
    if (net_ap_active()) {
        icon = LV_SYMBOL_WIFI; title = "Wi-Fi setup"; col = COL_ACCENT;
        snprintf(text, sizeof(text), "Join Wi-Fi \"%s\" and open http://192.168.4.1, or use Settings > Connection.", SETUP_AP_SSID);
    } else if (!g.wifi) {
        icon = LV_SYMBOL_WIFI; title = "No Wi-Fi"; col = COL_ORANGE;
        snprintf(text, sizeof(text), "Connecting to %s...", cfg.ssid[0] ? cfg.ssid : "(not configured)");
    } else if (!cfg.host[0]) {
        icon = LV_SYMBOL_SETTINGS; title = "Printer not set"; col = COL_ORANGE;
        snprintf(text, sizeof(text), "Enter the printer's IP in Settings > Connection.");
    } else if (!g.online) {
        icon = LV_SYMBOL_CLOSE; title = "Printer offline";
        snprintf(text, sizeof(text), "Moonraker not reachable at %s:%u", cfg.host, cfg.port);
    } else {
        bool starting = !strcmp(g.klippy, "startup");
        icon = starting ? LV_SYMBOL_REFRESH : LV_SYMBOL_WARNING;
        title = starting ? "Klipper starting" : !strcmp(g.klippy, "shutdown") ? "Klipper shutdown" : "Klipper error";
        col = starting ? COL_ORANGE : COL_RED;
        snprintf(text, sizeof(text), "%s", g.klippyMsg[0] ? g.klippyMsg : g.klippy);
    }
    set_text(hOffIcon, icon);
    set_text_color(hOffIcon, col);
    set_text(hOffTitle, title);
    set_text_color(hOffTitle, col);
    set_text(hOffText, text);
    set_visible(hOffBtns, g.online && !g.ready);
}

static void home_update() {
    bool up = g.ready;
    for (lv_obj_t* o : hOnline) set_visible(o, up);
    set_visible(hOff, !up);
    for (int k = 0; k < 9; k++)
        set_visible(hTiles[k], up || HOME_TILE_SCR[k] == S_SETTINGS || HOME_TILE_SCR[k] == S_POWER);
    if (!up) {
        home_offline();
        return;
    }

    update_gauge(hExt, g.ext, true);
    update_gauge(hBed, g.bed, g.hasBed);

    // named list: sensors, fans, host load
    int i = 0;
    char name[32], val[16];
    auto put = [&](const char* label, const char* v, lv_color_t col, bool fan) {
        if (i >= HOME_SLOTS) return;
        fit_name(name, sizeof(name), &lv_font_montserrat_12, hSlotNameW, label, fan);
        set_text(hSlotName[i], name);
        set_text(hSlotVal[i], v);
        set_text_color(hSlotVal[i], col);
        i++;
    };
    // host (Raspberry Pi etc.) CPU temperature / load / RAM always get their rows at the end
    const MrHost& h = g.host;
    bool hostTemp = h.valid && !isnan(h.cpuTemp);
    int nHost = h.valid ? 2 + (hostTemp ? 1 : 0) : 0;
    const char* hp = strcasestr(h.model, "raspberry") ? "Pi" : "Host";
    for (int k = 0; k < g.nExtra && i < HOME_SLOTS - nHost; k++) {
        const char* lb = g.extra[k].label;
        bool isHostSensor = strcasestr(lb, "pi") || strcasestr(lb, "host") || strcasestr(lb, "rpi");
        if (hostTemp && isHostSensor && !g.extra[k].settable) continue;  // same value as the host row below
        snprintf(val, sizeof(val), "%.0f" DEG, g.extra[k].temp);
        put(lb, val, g.extra[k].settable && g.extra[k].target > 0 ? COL_ORANGE : COL_TEXT, false);
    }
    for (int k = 0; k < g.nFans && i < HOME_SLOTS - nHost; k++) {
        char lbl[40];
        snprintf(lbl, sizeof(lbl), "%s%s", g.fans[k].label, strcasestr(g.fans[k].label, "fan") ? "" : " fan");
        if (g.fans[k].speed > 0) snprintf(val, sizeof(val), "%.0f%%", g.fans[k].speed * 100);
        else snprintf(val, sizeof(val), "off");
        put(lbl, val, g.fans[k].speed > 0 ? COL_ACCENT : COL_DIM, true);
    }
    while (nHost && i < HOME_SLOTS - nHost) {  // keep the host rows in the same place (end of column 2)
        set_text(hSlotName[i], "");
        set_text(hSlotVal[i], "");
        i++;
    }
    if (h.valid) {
        char lbl[16];
        if (hostTemp) {
            snprintf(lbl, sizeof(lbl), "%s temp", hp);
            snprintf(val, sizeof(val), "%.0f" DEG, h.cpuTemp);
            put(lbl, val, h.cpuTemp < 60 ? COL_GREEN : h.cpuTemp < 75 ? COL_ORANGE : COL_RED, false);
        }
        snprintf(lbl, sizeof(lbl), "%s CPU", hp);
        snprintf(val, sizeof(val), "%.0f%%", h.cpu);
        put(lbl, val, load_color(h.cpu), false);
        float mp = pct(h.memUsed, h.memTotal);
        snprintf(lbl, sizeof(lbl), "%s RAM", hp);
        snprintf(val, sizeof(val), "%.0f%%", mp);
        put(lbl, val, load_color(mp), false);
    }
    for (; i < HOME_SLOTS; i++) {
        set_text(hSlotName[i], "");
        set_text(hSlotVal[i], "");
    }
    set_text(lv_obj_get_child(hPrintTile, 1), is_printing() ? "Status" : "Print");
}

void scr_home(lv_obj_t* p) {
    // landscape: status column left, 3x3 menu right; portrait: status on top, 3x3 menu below
    const lv_coord_t pw = PORTRAIT ? CONTENT_W - 12 : 236;           // status panel width
    const lv_coord_t ph = PORTRAIT ? 264 : CONTENT_H - 12;           // status panel height
    hOnline[0] = home_gauge(hExt, p, 6, pw, ICON_NOZZLE, "Hotend", 300);
    hOnline[1] = home_gauge(hBed, p, 96, pw, ICON_BED, "Bed", 120);

    lv_obj_t* lc = ui_card(p, 6, 186, pw, ph - 180);
    hOnline[2] = lc;
    lv_obj_set_style_pad_ver(lc, 5, 0);
    lv_coord_t colW = (pw - 16) / 2;
    hSlotNameW = colW - 46;
    for (int k = 0; k < HOME_SLOTS; k++) {
        lv_coord_t x = (k / 4) * colW, y = (k % 4) * (PORTRAIT ? 18 : 19);  // column by column
        hSlotName[k] = ui_label(lc, "", &lv_font_montserrat_12, COL_DIM);
        lv_obj_set_pos(hSlotName[k], x, y);
        hSlotVal[k] = ui_label(lc, "", &lv_font_montserrat_14);
        lv_obj_set_width(hSlotVal[k], 40);
        lv_obj_set_style_text_align(hSlotVal[k], LV_TEXT_ALIGN_RIGHT, 0);
        lv_obj_set_pos(hSlotVal[k], x + colW - 46, y - 1);
    }

    // offline / Klipper-down status card (same area)
    hOff = ui_card(p, 6, 6, pw, ph);
    hOffIcon = ui_label(hOff, "", &lv_font_montserrat_28);
    hOffTitle = ui_label(hOff, "", &lv_font_montserrat_20);
    lv_obj_set_pos(hOffTitle, 40, 4);
    hOffText = ui_label(hOff, "", &lv_font_montserrat_14, COL_TEXT);
    lv_label_set_long_mode(hOffText, LV_LABEL_LONG_DOT);
    lv_obj_set_size(hOffText, pw - 16, ph - 16 - 44 - 74);
    lv_obj_set_pos(hOffText, 0, 44);
    hOffBtns = ui_row(hOff, pw - 16, 70);
    lv_obj_align(hOffBtns, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_t* rb = ui_btn(hOffBtns, LV_SYMBOL_REFRESH "\nRestart", (pw - 22) / 2, 66, restart_cb);
    lv_obj_t* fb = ui_btn(hOffBtns, LV_SYMBOL_REFRESH "\nFW restart", (pw - 22) / 2, 66, fwrestart_cb, nullptr, COL_ORANGE);
    lv_obj_set_style_text_font(rb, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_font(fb, &lv_font_montserrat_14, 0);
    lv_obj_add_flag(hOff, LV_OBJ_FLAG_HIDDEN);

    // menu
    lv_obj_t* grid = lv_obj_create(p);
    lv_coord_t gx = PORTRAIT ? 6 : 248, gy = PORTRAIT ? 276 : 6;
    lv_coord_t gw = PORTRAIT ? CONTENT_W - 12 : 226, gh = CONTENT_H - gy - 6;
    lv_obj_set_pos(grid, gx, gy);
    lv_obj_set_size(grid, gw, gh);
    lv_obj_set_style_bg_opa(grid, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(grid, 0, 0);
    lv_obj_set_style_pad_all(grid, 0, 0);
    lv_obj_set_style_pad_row(grid, 5, 0);
    lv_obj_set_style_pad_column(grid, 5, 0);
    lv_obj_set_flex_flow(grid, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_clear_flag(grid, LV_OBJ_FLAG_SCROLLABLE);

    static const char* icons[9] = {ICON_PRINTER, ICON_THERMO, ICON_POS, ICON_NOZZLE, ICON_FAN,
                                   LV_SYMBOL_LIST, LV_SYMBOL_KEYBOARD, LV_SYMBOL_POWER, LV_SYMBOL_SETTINGS};
    static const char* names[9] = {"Print", "Temp", "Move", "Extrude", "Fans", "Macros", "Console", "Power", "Settings"};
    lv_coord_t tw = (gw - 10) / 3, th = (gh - 10) / 3;
    for (int k = 0; k < 9; k++) {
        Scr s = HOME_TILE_SCR[k];
        lv_obj_t* b = ui_tile(grid, icons[k], names[k], tw, th, nullptr, nullptr, s == S_FILES ? COL_ACCENT : COL_BTN);
        lv_obj_set_style_text_font(lv_obj_get_child(b, 1), &lv_font_montserrat_12, 0);
        if (PORTRAIT) lv_obj_set_style_pad_row(b, 1, 0);
        lv_obj_add_event_cb(b, [](lv_event_t* e) {
            Scr s = (Scr)(intptr_t)lv_event_get_user_data(e);
            ui_goto(s == S_FILES && is_printing() ? S_PRINTING : s);
        }, LV_EVENT_CLICKED, (void*)(intptr_t)s);
        hTiles[k] = b;
        if (s == S_FILES) hPrintTile = b;
    }
    ui_set_update(home_update);
}

// ================================================================ PRINTING

static lv_obj_t *pRing, *pIcon, *pPct, *pChip, *pName, *pEta;
static Gauge pExt, pBed;
struct Tile {
    lv_obj_t *icon, *value;
};
static Tile pTiles[8];
static lv_obj_t *pBtnPause, *pBtnCancel;

static void pause_cb(lv_event_t*) {
    if (!strcmp(g.pstate, "printing")) mr_post("/printer/print/pause");
    else if (!strcmp(g.pstate, "paused")) mr_post("/printer/print/resume");
    else ui_goto(S_HOME);
}
static void cancel_yes(void*) { mr_post("/printer/print/cancel"); }
static void cancel_cb(lv_event_t*) {
    if (is_printing()) ui_confirm("Cancel print", "Cancel the current print?", cancel_yes);
    else if (g.file[0]) mr_print_file(g.file);  // "Reprint" once the job has finished
}

static void mini_gauge(Gauge& gg, lv_obj_t* c, lv_coord_t x, const char* icon, int max) {
    gg.max = max;
    gg.arc = make_arc(c, 52, 6, 135, 270);
    lv_obj_set_pos(gg.arc, x, 0);
    gg.icon = ui_label(gg.arc, icon, &icons_20, COL_DIM);
    lv_obj_center(gg.icon);
    gg.temp = ui_label(c, "", &lv_font_montserrat_20);
    lv_obj_set_pos(gg.temp, x + 56, -2);
    gg.target = ui_label(c, "", &icons_20, COL_DIM);
    lv_obj_set_pos(gg.target, x + 56, 20);
    gg.bar = lv_bar_create(c);
    lv_obj_set_size(gg.bar, 84, 5);
    lv_obj_set_pos(gg.bar, x + 56, 48);
    lv_bar_set_range(gg.bar, 0, 1000);
}

static void make_tile(Tile& t, lv_obj_t* p, lv_coord_t x, lv_coord_t y, lv_coord_t w, const char* icon, const char* cap) {
    lv_obj_t* c = lv_obj_create(p);
    lv_obj_set_pos(c, x, y);
    lv_obj_set_size(c, w, 52);
    set_bg_color(c, COL_TILE, 0);
    lv_obj_set_style_border_width(c, 0, 0);
    lv_obj_set_style_radius(c, 8, 0);
    lv_obj_set_style_pad_all(c, 4, 0);
    lv_obj_clear_flag(c, LV_OBJ_FLAG_SCROLLABLE);
    t.icon = ui_label(c, icon, &icons_20, COL_ACCENT);
    lv_obj_t* cl = ui_label(c, cap, &lv_font_montserrat_12, COL_DIM);
    lv_obj_align(cl, LV_ALIGN_TOP_RIGHT, 0, 2);
    t.value = ui_label(c, "", &lv_font_montserrat_14);
    lv_obj_align(t.value, LV_ALIGN_BOTTOM_LEFT, 0, 0);
}

static void printing_update() {
    char buf[64], a[24];
    lv_color_t col = state_color(g.pstate);
    set_text_color(pIcon, col);
    lv_arc_set_value(pRing, (int)(g.progress * 1000));
    set_arc_color(pRing, col);
    snprintf(buf, sizeof(buf), "%.0f%%", g.progress * 100);
    set_text(pPct, buf);
    set_text_color(pPct, col);

    bool printing = !strcmp(g.pstate, "printing"), paused = !strcmp(g.pstate, "paused");
    const char* ic = printing ? LV_SYMBOL_PLAY : paused ? LV_SYMBOL_PAUSE : !strcmp(g.pstate, "complete") ? LV_SYMBOL_OK
                   : !strcmp(g.pstate, "cancelled") ? LV_SYMBOL_CLOSE : LV_SYMBOL_WARNING;
    snprintf(buf, sizeof(buf), "%s %s", ic, g.message[0] && is_printing() ? g.message : g.pstate);
    set_text(pChip, buf);
    set_text_color(pChip, col);

    const char* f = strrchr(g.file, '/');
    String fn = f ? f + 1 : g.file;
    int dot = fn.lastIndexOf('.');
    if (dot > 0) fn = fn.substring(0, dot);
    fn.replace("_", " ");
    set_text(pName, fn.length() ? fn.c_str() : "No active print");

    update_gauge(pExt, g.ext, true);
    update_gauge(pBed, g.bed, g.hasBed);

    // estimated finish time (wall clock) while a job runs
    if (is_printing() && g.progress > 0.005f && clock_valid()) {
        char hm[16];
        clock_hm(hm, sizeof(hm), time(nullptr) + (time_t)(g.printDur / g.progress - g.printDur));
        snprintf(buf, sizeof(buf), LV_SYMBOL_BELL " ETA %s", hm);
        set_text(pEta, buf);
    } else {
        set_text(pEta, "");
    }

    float remain = g.progress > 0.005f ? g.printDur / g.progress - g.printDur : 0;
    fmt_dur(a, sizeof(a), g.printDur);
    set_text(pTiles[0].value, a);
    fmt_dur(a, sizeof(a), remain);
    set_text(pTiles[1].value, is_printing() && g.progress > 0.005f ? a : "-");
    if (g.layers > 0) snprintf(buf, sizeof(buf), "%d/%d", g.layer, g.layers);
    else snprintf(buf, sizeof(buf), "-");
    set_text(pTiles[2].value, buf);
    snprintf(buf, sizeof(buf), "%.2f", g.pos[2]);
    set_text(pTiles[3].value, buf);
    snprintf(buf, sizeof(buf), "%.0f%%", g.speedFactor * 100);
    set_text(pTiles[4].value, buf);
    snprintf(buf, sizeof(buf), "%.0f%%", g.extrudeFactor * 100);
    set_text(pTiles[5].value, buf);
    float fan = 0;
    for (int i = 0; i < g.nFans; i++)
        if (g.fans[i].kind == FAN_PART) fan = g.fans[i].speed;
    snprintf(buf, sizeof(buf), "%.0f%%", fan * 100);
    set_text(pTiles[6].value, buf);
    set_text_color(pTiles[6].icon, fan > 0 ? COL_ACCENT : COL_DIM);
    snprintf(buf, sizeof(buf), "%.1f m", g.filamentUsed / 1000.0f);
    set_text(pTiles[7].value, buf);

    ui_btn_text(pBtnPause, printing ? LV_SYMBOL_PAUSE "\nPause" : paused ? LV_SYMBOL_PLAY "\nResume" : LV_SYMBOL_HOME "\nHome");
    set_bg_color(pBtnPause, printing ? COL_ORANGE : paused ? COL_GREEN : COL_BTN, 0);
    ui_btn_text(pBtnCancel, is_printing() ? LV_SYMBOL_STOP "\nCancel" : LV_SYMBOL_REFRESH "\nReprint");
    if (!is_printing() && !g.file[0]) lv_obj_add_state(pBtnCancel, LV_STATE_DISABLED);
    else lv_obj_clear_state(pBtnCancel, LV_STATE_DISABLED);
}

void scr_printing(lv_obj_t* p) {
    // landscape: ring column left, gauges/tiles/actions right; portrait: ring card on top, everything stacked
    const bool P = PORTRAIT;
    lv_obj_t* l = P ? ui_card(p, 6, 6, CONTENT_W - 12, 170) : ui_card(p, 6, 6, 160, 268);
    pRing = make_arc(l, P ? 152 : 144, 10, 270, 360);
    lv_obj_align(pRing, P ? LV_ALIGN_LEFT_MID : LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_t* box = lv_obj_create(pRing);
    lv_obj_set_size(box, 108, 108);
    set_bg_color(box, COL_BG, 0);
    lv_obj_set_style_bg_opa(box, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(box, 0, 0);
    lv_obj_set_style_pad_all(box, 0, 0);
    lv_obj_set_style_radius(box, LV_RADIUS_CIRCLE, 0);
    lv_obj_clear_flag(box, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_center(box);
    // native 56 px glyph: LVGL transforms (zoom) need an off-screen layer that the small pool can't provide
    pIcon = ui_label(box, ICON_NOZZLE, &icons_56, COL_DIM);
    lv_obj_center(pIcon);

    const lv_coord_t tw = P ? CONTENT_W - 12 - 16 - 162 : 144;  // text column width
    pPct = ui_label(l, "", &lv_font_montserrat_28);
    pChip = ui_label(l, "", &lv_font_montserrat_14);
    lv_label_set_long_mode(pChip, LV_LABEL_LONG_DOT);
    lv_obj_set_width(pChip, tw);
    pName = ui_label(l, "", &lv_font_montserrat_12, COL_DIM);
    lv_label_set_long_mode(pName, LV_LABEL_LONG_DOT);
    lv_obj_set_size(pName, tw, P ? 46 : 30);
    pEta = ui_label(l, "", &lv_font_montserrat_14, COL_ACCENT);
    if (P) {
        lv_obj_set_pos(pPct, 162, 12);
        lv_obj_set_pos(pChip, 162, 54);
        lv_obj_set_pos(pName, 162, 82);
        lv_obj_set_pos(pEta, 162, 132);
    } else {
        lv_obj_align(pEta, LV_ALIGN_TOP_MID, 0, 236);
        lv_obj_set_style_text_align(pChip, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_align(pName, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_align(pPct, LV_ALIGN_TOP_MID, 0, 146);
        lv_obj_align(pChip, LV_ALIGN_TOP_MID, 0, 180);
        lv_obj_align(pName, LV_ALIGN_TOP_MID, 0, 204);
    }

    // hotend + bed gauges
    const lv_coord_t rx = P ? 6 : 172, rw = P ? CONTENT_W - 12 : 302;
    lv_coord_t y = P ? 182 : 6;
    lv_obj_t* gc = ui_card(p, rx, y, rw, 70);
    mini_gauge(pExt, gc, 0, ICON_NOZZLE, 300);
    mini_gauge(pBed, gc, (rw - 16) / 2, ICON_BED, 120);
    y += 76;

    // tiles
    static const char* icons[8] = {ICON_CLOCK, ICON_HOURGLASS, ICON_LAYERS, ICON_Z, ICON_SPEED, ICON_FLOW, ICON_FAN, ICON_FILAMENT};
    static const char* caps[8] = {"Time", "Left", "Layer", "Z", "Speed", "Flow", "Fan", "Fil."};
    const lv_coord_t tileW = (rw - 12) / 4;
    for (int i = 0; i < 8; i++) make_tile(pTiles[i], p, rx + (i % 4) * (tileW + 4), y + (i / 4) * 56, tileW, icons[i], caps[i]);
    y += 114;

    // actions
    const lv_coord_t bh = CONTENT_H - 6 - y;
    lv_obj_t* row = ui_row(p, rw, bh);
    lv_obj_set_pos(row, rx, y);
    lv_obj_set_style_pad_column(row, 4, 0);
    pBtnPause = ui_btn(row, "", tileW, bh, pause_cb);
    pBtnCancel = ui_btn(row, "", tileW, bh, cancel_cb, nullptr, COL_RED);
    lv_obj_t* tune = ui_btn(row, LV_SYMBOL_LOOP "\nTune", tileW, bh, goto_cb, (void*)(intptr_t)S_FANS);
    lv_obj_t* zo = ui_btn(row, LV_SYMBOL_UP LV_SYMBOL_DOWN "\nZ offset", tileW, bh, goto_cb, (void*)(intptr_t)S_ZOFFSET);
    lv_obj_t* btns[] = {pBtnPause, pBtnCancel, tune, zo};
    for (lv_obj_t* b : btns) lv_obj_set_style_text_font(b, &lv_font_montserrat_14, 0);

    ui_set_update(printing_update);
}
