// Read-only status dashboard, used when the display has no touch panel.
// One layout engine for landscape (800x480) and portrait (480x800).
#include "ui.h"
#include "ui_icons.h"
#include "net.h"
#include "board.h"
#include <WiFi.h>
#include <vector>

#define COL_COLD  lv_color_hex(0x58a6ff)
#define COL_TILE  lv_color_hex(0x232c3b)
#define COL_LINE  lv_color_hex(0x30363d)

void ui_icons_init() {
    icons_16.fallback = &lv_font_montserrat_16;
    icons_20.fallback = &lv_font_montserrat_20;
    icons_28.fallback = &lv_font_montserrat_28;
    icons_72.fallback = &lv_font_montserrat_48;
}

// ---------------------------------------------------------------- layout

struct Box {
    lv_coord_t x, y, w, h;
};

struct Layout {
    bool portrait;
    lv_coord_t W, headH;
    Box ext, bed, list, main;
    int listCols, listRows;
    lv_coord_t listRowH;
    lv_coord_t ring, tileH, pctY, chipY;
    lv_coord_t hostY, hostRowGap;
};

static Layout L;

static void compute_layout() {
    L.W = lv_disp_get_hor_res(nullptr);
    L.portrait = lv_disp_get_ver_res(nullptr) > L.W;
    if (!L.portrait) {
        L.headH = 56;
        L.ext = {12, 66, 300, 120};
        L.bed = {12, 194, 300, 120};
        L.list = {12, 322, 300, 146};
        L.listCols = 2, L.listRows = 5, L.listRowH = 25;
        L.main = {324, 66, 464, 402};
        L.ring = 200, L.tileH = 78, L.pctY = 66, L.chipY = 134;
        L.hostY = 150, L.hostRowGap = 48;
    } else {
        L.headH = 88;
        L.ext = {12, 96, 456, 120};
        L.bed = {12, 224, 456, 120};
        L.list = {12, 352, 456, 106};
        L.listCols = 3, L.listRows = 4, L.listRowH = 22;
        L.main = {12, 466, 456, 322};
        L.ring = 150, L.tileH = 66, L.pctY = 48, L.chipY = 108;
        L.hostY = 118, L.hostRowGap = 40;
    }
}

// ---------------------------------------------------------------- widgets

struct Gauge {
    lv_obj_t *arc, *icon, *temp, *target, *power;
    int max;
};

struct Tile {
    lv_obj_t *icon, *value;
};

struct Slot {
    lv_obj_t *name, *value;
};

struct HostRow {
    lv_obj_t *value, *bar;
};

#define MAX_SLOTS 12
static lv_coord_t slotNameW;

static lv_obj_t *dState, *dHost, *dNet;
static Gauge gExt, gBed;
static Slot slots[MAX_SLOTS];
static int nSlots;
static lv_obj_t *dPrint, *dIdle;
static lv_obj_t *dRing, *dImg, *dFile, *dPct, *dChip;
static lv_coord_t thumbBox;
static Tile tiles[8];
static lv_obj_t *dBigIcon, *dIdleTitle, *dIdleText;
static lv_obj_t *dHostPanel, *dHostModel, *dHostFoot;
static HostRow hCpu, hMem, hDisk;

static lv_obj_t* plain(lv_obj_t* parent, lv_coord_t x, lv_coord_t y, lv_coord_t w, lv_coord_t h) {
    lv_obj_t* o = lv_obj_create(parent);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_size(o, w, h);
    lv_obj_set_style_bg_opa(o, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(o, 0, 0);
    lv_obj_set_style_pad_all(o, 0, 0);
    lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    return o;
}

static lv_obj_t* card(lv_obj_t* scr, const Box& b) { return ui_card(scr, b.x, b.y, b.w, b.h); }

static lv_obj_t* make_arc(lv_obj_t* parent, lv_coord_t size, lv_coord_t width, uint16_t rot, uint16_t span) {
    lv_obj_t* a = lv_arc_create(parent);
    lv_obj_set_size(a, size, size);
    lv_arc_set_rotation(a, rot);
    lv_arc_set_bg_angles(a, 0, span);
    lv_obj_remove_style(a, nullptr, LV_PART_KNOB);
    lv_obj_clear_flag(a, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_arc_width(a, width, LV_PART_MAIN);
    lv_obj_set_style_arc_width(a, width, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(a, COL_LINE, LV_PART_MAIN);
    lv_obj_set_style_arc_rounded(a, true, LV_PART_INDICATOR);
    lv_obj_set_style_pad_all(a, 0, 0);
    return a;
}

// Heater card: arc gauge with icon on the left; name, temperature, target and heater power on the right.
static void make_gauge(Gauge& gg, lv_obj_t* scr, const Box& b, const char* icon, const char* name, int max) {
    lv_obj_t* c = card(scr, b);
    lv_coord_t inner = b.w - 20, tx = 112;
    gg.max = max;
    gg.arc = make_arc(c, 100, 10, 135, 270);
    lv_obj_align(gg.arc, LV_ALIGN_LEFT_MID, 0, 0);
    lv_arc_set_range(gg.arc, 0, 1000);  // fixed 0..1000 scale; update_gauge() maps temp/target onto it
    gg.icon = ui_label(gg.arc, icon, &icons_28, COL_DIM);
    lv_obj_center(gg.icon);

    lv_obj_t* n = ui_label(c, name, &lv_font_montserrat_16, COL_DIM);
    lv_obj_set_pos(n, tx, 0);
    gg.temp = ui_label(c, "", &lv_font_montserrat_48);
    lv_obj_set_pos(gg.temp, tx - 2, 14);
    gg.target = ui_label(c, "", &icons_20, COL_DIM);
    lv_obj_set_pos(gg.target, tx, 66);
    gg.power = lv_bar_create(c);
    lv_obj_set_size(gg.power, inner - tx, 6);
    lv_obj_set_pos(gg.power, tx, 94);
    lv_bar_set_range(gg.power, 0, 1000);  // temperature relative to the configured max_temp
}

// Arc: while a heater is on, progress toward the target from room temperature (20 C empty -> target full);
// while off, the temperature on the max scale. Bar: always the temperature relative to the configured max_temp.
static void update_gauge(Gauge& gg, const MrTemp& t, bool present) {
    const float ROOM = 20;
    char buf[48];
    if (!present || !g.ready) {
        set_text(gg.temp, "--");
        set_text(gg.target, present ? "" : "not present");
        lv_arc_set_value(gg.arc, 0);
        lv_bar_set_value(gg.power, 0, LV_ANIM_OFF);
        set_text_color(gg.icon, COL_DIM);
        return;
    }
    bool on = t.target > 0;
    bool atTarget = on && fabsf(t.temp - t.target) < 3;
    // blue = cold, orange = heating (or still hot after switching off), green = at target
    lv_color_t col = atTarget ? COL_GREEN : (on || t.temp > 45) ? COL_ORANGE : COL_COLD;
    float maxT = t.maxTemp > 0 ? t.maxTemp : gg.max;
    int ofMax = (int)constrain(t.temp / maxT * 1000, 0, 1000);

    int arc = ofMax;
    if (on) {
        float span = t.target - ROOM;
        float p = span > 1 ? (t.temp - ROOM) / span : 1;
        arc = atTarget ? 1000 : (int)constrain(p * 1000, 0, 1000);
    }
    lv_arc_set_value(gg.arc, arc);
    set_arc_color(gg.arc, col);
    set_text_color(gg.icon, col);
    lv_bar_set_value(gg.power, ofMax, LV_ANIM_OFF);
    set_bg_color(gg.power, col, LV_PART_INDICATOR);

    snprintf(buf, sizeof(buf), "%.0f" DEG, t.temp);
    set_text(gg.temp, buf);
    if (on) snprintf(buf, sizeof(buf), "%s  %s%.0f" DEG, atTarget ? ICON_OK : ICON_FIRE, L.portrait ? "target " : "", t.target);
    else snprintf(buf, sizeof(buf), "off");
    set_text(gg.target, buf);
    set_text_color(gg.target, on ? col : COL_DIM);
}

static void make_tile(Tile& t, lv_obj_t* parent, lv_coord_t x, lv_coord_t y, lv_coord_t w, lv_coord_t h,
                      const char* icon, const char* caption) {
    lv_obj_t* c = lv_obj_create(parent);
    lv_obj_set_pos(c, x, y);
    lv_obj_set_size(c, w, h);
    set_bg_color(c, COL_TILE, 0);
    lv_obj_set_style_border_width(c, 0, 0);
    lv_obj_set_style_radius(c, 10, 0);
    lv_obj_set_style_pad_all(c, 6, 0);
    lv_obj_clear_flag(c, LV_OBJ_FLAG_SCROLLABLE);
    t.icon = ui_label(c, icon, &icons_20, COL_ACCENT);
    lv_obj_t* cap = ui_label(c, caption, &lv_font_montserrat_14, COL_DIM);
    lv_obj_align(cap, LV_ALIGN_TOP_RIGHT, 0, 2);
    t.value = ui_label(c, "", &lv_font_montserrat_20);
    lv_obj_align(t.value, LV_ALIGN_BOTTOM_LEFT, 0, 0);
}

// ---------------------------------------------------------------- helpers

static const char* wifi_icon() {
    if (!g.wifi) return ICON_WIFI_OFF;
    int r = WiFi.RSSI();
    return r > -55 ? ICON_WIFI_4 : r > -65 ? ICON_WIFI_3 : r > -75 ? ICON_WIFI_2 : ICON_WIFI_1;
}

static bool any_heating() {
    if (g.ext.target > 0 || (g.hasBed && g.bed.target > 0)) return true;
    for (int i = 0; i < g.nExtra; i++)
        if (g.extra[i].settable && g.extra[i].target > 0) return true;
    return false;
}

static float pct(double used, double total) { return total > 0 ? 100.0 * used / total : 0; }

static lv_color_t load_color(float p) { return p < 60 ? COL_GREEN : p < 85 ? COL_ORANGE : COL_RED; }

static void fmt_uptime(char* buf, size_t n, uint32_t s) {
    if (s >= 86400) snprintf(buf, n, "%ud %uh", (unsigned)(s / 86400), (unsigned)(s / 3600 % 24));
    else if (s >= 3600) snprintf(buf, n, "%uh %um", (unsigned)(s / 3600), (unsigned)(s / 60 % 60));
    else snprintf(buf, n, "%um", (unsigned)(s / 60));
}

static void fmt_bytes(char* buf, size_t n, double b) {
    if (b >= 1e9) snprintf(buf, n, "%.1f GB", b / 1e9);
    else snprintf(buf, n, "%.0f MB", b / 1e6);
}

// "hotend_fan" -> "Hotend fan"
static void pretty(char* out, size_t n, const char* name, const char* suffix = nullptr) {
    snprintf(out, n, "%s", name);
    for (char* p = out; *p; p++)
        if (*p == '_') *p = ' ';
    out[0] = toupper((unsigned char)out[0]);
    if (suffix && !strcasestr(out, suffix)) strlcat(out, " ", n), strlcat(out, suffix, n);
}

// ---------------------------------------------------------------- fitting long names

static lv_coord_t text_w(const lv_font_t* f, const char* s) {
    lv_point_t p;
    lv_txt_get_size(&p, s, f, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    return p.x;
}

static bool ieq(const String& a, const char* b) { return a.equalsIgnoreCase(b); }

struct Abbr {
    const char* from;
    const char* to;
};
// applied in order, whole words, case-insensitive
static const Abbr ABBR[] = {
    {"raspberry pi", "Pi"}, {"raspberrypi", "Pi"}, {"raspberry", "Pi"}, {"rpi", "Pi"},
    {"temperature", "Temp"}, {"controller", "Ctrl"}, {"electronics", "Elec."}, {"extruder", "Extr."},
    {"enclosure", "Encl."}, {"motherboard", "Board"}, {"mainboard", "Board"}, {"stepper", "Step."},
    {"drivers", "Drv"}, {"driver", "Drv"}, {"toolhead", "TH"}, {"heatbreak", "HB"}, {"exhaust", "Exh."},
    {"chamber", "Chamb."}, {"auxiliary", "Aux"}, {"cooling", "Cool"},
    // common Klipper/Voron fan names
    {"nevermore", "Filter"}, {"bed fans", "Bed"}, {"bedfans", "Bed"}, {"exhaust", "Exhaust"},
    {"part cooling", "Part"}, {"filter", "Filter"},
};

// Shortens a name until "prefix + name" fits in maxw pixels, staying readable:
// full name -> drop "fan" (the icon shows it) -> abbreviations -> shorten longest words -> cut.
static void fit_name(char* out, size_t n, const lv_font_t* font, lv_coord_t maxw, const char* prefix, const char* raw,
                     bool dropFan) {
    String s = raw;
    s.replace("_", " ");
    s.trim();
    std::vector<String> w;
    auto split = [&] {
        w.clear();
        int st = 0;
        for (int i = 0; i <= (int)s.length(); i++)
            if (i == (int)s.length() || s[i] == ' ') {
                if (i > st) w.push_back(s.substring(st, i));
                st = i + 1;
            }
    };
    auto join = [&] {
        s = "";
        for (size_t i = 0; i < w.size(); i++) s += (i ? " " : "") + w[i];
        if (s.length()) s.setCharAt(0, toupper(s[0]));
    };
    auto fits = [&] {
        snprintf(out, n, "%s%s", prefix, s.c_str());
        return text_w(font, out) <= maxw;
    };
    split();
    join();
    if (fits()) return;
    if (dropFan && w.size() > 1) {
        for (size_t i = 0; i < w.size(); i++)
            if (ieq(w[i], "fan")) w.erase(w.begin() + i--);
        join();
        if (fits()) return;
    }
    for (const Abbr& a : ABBR) {
        String low = " " + s + " ";
        low.toLowerCase();
        int at = low.indexOf(String(" ") + a.from + " ");
        if (at < 0) continue;
        s = s.substring(0, at) + a.to + s.substring(at + strlen(a.from));
        split();
        join();
        if (fits()) return;
    }
    for (;;) {  // shorten the longest word one letter at a time ("Nevermore" -> "Neverm."), keep >= 3 letters
        size_t best = w.size();
        for (size_t i = 0; i < w.size(); i++) {
            size_t letters = w[i].length() - (w[i].endsWith(".") ? 1 : 0);
            if (letters > 3 && (best == w.size() || w[i].length() > w[best].length())) best = i;
        }
        if (best == w.size()) break;
        String base = w[best].endsWith(".") ? w[best].substring(0, w[best].length() - 1) : w[best];
        w[best] = base.substring(0, base.length() - 1) + ".";
        join();
        if (fits()) return;
    }
    while (s.length() > 1 && !fits()) s.remove(s.length() - 1);  // last resort
    fits();
}

static const char* sensor_icon(const char* l) {
    if (strcasestr(l, "pi") || strcasestr(l, "host") || strcasestr(l, "rpi")) return ICON_PI;
    if (strcasestr(l, "mcu") || strcasestr(l, "chip") || strcasestr(l, "board") || strcasestr(l, "driver")) return ICON_CHIP;
    if (strcasestr(l, "chamber") || strcasestr(l, "enclosure")) return ICON_CHAMBER;
    return ICON_THERMO;
}

// ---------------------------------------------------------------- update

static void update_header() {
    char buf[96];
    const char *icon, *text;
    lv_color_t col;
    if (net_ap_active()) { icon = ICON_HOTSPOT; text = "Setup"; col = COL_ACCENT; }
    else if (!g.wifi) { icon = ICON_WIFI_OFF; text = "No Wi-Fi"; col = COL_RED; }
    else if (!g.online) { icon = ICON_LAN_OFF; text = "Offline"; col = COL_RED; }
    else if (!g.ready) {
        bool starting = !strcmp(g.klippy, "startup");
        icon = starting ? ICON_STARTUP : ICON_STOP;
        text = starting ? "Starting" : !strcmp(g.klippy, "shutdown") ? "Shutdown" : "Error";
        col = starting ? COL_ORANGE : COL_RED;
    } else if (!strcmp(g.pstate, "printing")) { icon = ICON_PLAY; text = "Printing"; col = COL_ACCENT; }
    else if (!strcmp(g.pstate, "paused")) { icon = ICON_PAUSE; text = "Paused"; col = COL_ORANGE; }
    else if (!strcmp(g.pstate, "complete")) { icon = ICON_OK; text = "Complete"; col = COL_GREEN; }
    else if (!strcmp(g.pstate, "cancelled")) { icon = ICON_CLOSE; text = "Cancelled"; col = COL_ORANGE; }
    else if (!strcmp(g.pstate, "error")) { icon = ICON_ALERT; text = "Print error"; col = COL_RED; }
    else if (any_heating()) { icon = ICON_FIRE; text = "Heating"; col = COL_ORANGE; }
    else { icon = ICON_OK; text = "Ready"; col = COL_GREEN; }
    snprintf(buf, sizeof(buf), "%s  %s", icon, text);
    set_text(dState, buf);
    set_bg_color(dState, col, 0);

    snprintf(buf, sizeof(buf), ICON_PRINTER "  Printer: %s", cfg.host[0] ? cfg.host : "not configured");
    set_text(dHost, buf);
    if (g.wifi) snprintf(buf, sizeof(buf), "%s  Display: %s", wifi_icon(), WiFi.localIP().toString().c_str());
    else snprintf(buf, sizeof(buf), "%s  Display: --", wifi_icon());
    set_text(dNet, buf);
    set_text_color(dNet, g.wifi ? COL_TEXT : COL_RED);
}

// Named list of sensors, fans and host stats ("icon Name ... value").
static void update_list() {
    int i = 0;
    char name[48], val[24];
    auto put = [&](const char* icon, const char* label, const char* value, lv_color_t iconCol, bool fan = false) {
        if (i >= nSlots) return;
        char pre[16];
        snprintf(pre, sizeof(pre), "%s  ", icon);
        fit_name(name, sizeof(name), &icons_16, slotNameW, pre, label, fan);
        set_text(slots[i].name, name);
        set_text_color(slots[i].name, iconCol);
        set_text(slots[i].value, value);
        i++;
    };
    bool hasPiSensor = false;
    if (g.ready) {
        for (int k = 0; k < g.nExtra; k++) {
            const MrTemp& t = g.extra[k];
            const char* ic = sensor_icon(t.label);
            hasPiSensor |= ic == ICON_PI;
            // value column only fits the temperature; orange marks a heater that is switched on
            snprintf(val, sizeof(val), "%.0f" DEG, t.temp);
            put(ic, t.label, val, t.settable && t.target > 0 ? COL_ORANGE : COL_TEXT, false);
        }
        for (int k = 0; k < g.nFans; k++) {
            const MrFan& f = g.fans[k];
            char lbl[40];
            pretty(lbl, sizeof(lbl), f.label, "fan");
            if (f.speed > 0) snprintf(val, sizeof(val), "%.0f%%", f.speed * 100);
            else snprintf(val, sizeof(val), "off");
            put(f.speed > 0 ? ICON_FAN : ICON_FAN_OFF, lbl, val, f.speed > 0 ? COL_ACCENT : COL_DIM, true);
        }
    }
    const MrHost& h = g.host;
    if (g.online && h.valid) {
        snprintf(val, sizeof(val), "%.0f%%", h.cpu);
        put(ICON_CPU, "CPU", val, load_color(h.cpu));
        float mp = pct(h.memUsed, h.memTotal);
        snprintf(val, sizeof(val), "%.0f%%", mp);
        put(ICON_MEMORY, "Memory", val, load_color(mp));
        if (h.diskTotal) {
            float dp = pct(h.diskUsed, h.diskTotal);
            snprintf(val, sizeof(val), "%.0f%%", dp);
            put(ICON_DISK, "Disk", val, load_color(dp));
        }
        if (!hasPiSensor && !isnan(h.cpuTemp)) {
            snprintf(val, sizeof(val), "%.0f" DEG, h.cpuTemp);
            put(ICON_THERMO, "CPU temp", val, COL_TEXT);
        }
    }
    for (; i < nSlots; i++) {
        set_text(slots[i].name, "");
        set_text(slots[i].value, "");
    }
}

static void update_job() {
    char buf[64], a[24];
    ui_print_thumb(dImg, thumbBox - 16);
    const char* f = strrchr(g.file, '/');
    String fn = f ? f + 1 : g.file;
    int dot = fn.lastIndexOf('.');
    if (dot > 0) fn = fn.substring(0, dot);  // drop .gcode
    fn.replace("_", " ");
    set_text(dFile, fn.c_str());
    snprintf(buf, sizeof(buf), "%.0f%%", g.progress * 100);
    set_text(dPct, buf);
    lv_color_t col = state_color(g.pstate);
    lv_arc_set_value(dRing, (int)(g.progress * 1000));
    set_arc_color(dRing, col);
    set_text_color(dPct, col);

    const char* ic = !strcmp(g.pstate, "printing") ? ICON_PLAY : !strcmp(g.pstate, "paused") ? ICON_PAUSE
                   : !strcmp(g.pstate, "complete") ? ICON_OK : !strcmp(g.pstate, "cancelled") ? ICON_CLOSE : ICON_ALERT;
    snprintf(buf, sizeof(buf), "%s  %s", ic, g.message[0] ? g.message : g.pstate);
    const lv_font_t* cf = text_w(&icons_20, buf) <= lv_obj_get_width(dChip) ? &icons_20 : &icons_16;
    if (lv_obj_get_style_text_font(dChip, 0) != cf) lv_obj_set_style_text_font(dChip, cf, 0);
    set_text(dChip, buf);
    set_text_color(dChip, col);

    float remain = g.progress > 0.005f ? g.printDur / g.progress - g.printDur : 0;
    fmt_dur(a, sizeof(a), g.printDur);
    set_text(tiles[0].value, a);
    fmt_dur(a, sizeof(a), remain);
    set_text(tiles[1].value, is_printing() && g.progress > 0.005f ? a : "-");
    if (g.layers > 0) snprintf(buf, sizeof(buf), "%d/%d", g.layer, g.layers);
    else snprintf(buf, sizeof(buf), "-");
    set_text(tiles[2].value, buf);
    snprintf(buf, sizeof(buf), "%.2f", g.pos[2]);
    set_text(tiles[3].value, buf);
    snprintf(buf, sizeof(buf), "%.0f%%", g.speedFactor * 100);
    set_text(tiles[4].value, buf);
    snprintf(buf, sizeof(buf), "%.0f%%", g.extrudeFactor * 100);
    set_text(tiles[5].value, buf);
    float fan = 0;
    for (int i = 0; i < g.nFans; i++)
        if (g.fans[i].kind == FAN_PART) fan = g.fans[i].speed;
    snprintf(buf, sizeof(buf), "%.0f%%", fan * 100);
    set_text(tiles[6].value, buf);
    set_text_color(tiles[6].icon, fan > 0 ? COL_ACCENT : COL_DIM);
    snprintf(buf, sizeof(buf), "%.2f m", g.filamentUsed / 1000.0f);
    set_text(tiles[7].value, buf);
}

static void set_row(HostRow& r, float p, const char* text) {
    set_text(r.value, text);
    lv_bar_set_value(r.bar, (int)p, LV_ANIM_OFF);
    set_bg_color(r.bar, load_color(p), LV_PART_INDICATOR);
}

static void update_host() {
    const MrHost& h = g.host;
    char buf[96], a[24], b[24];
    // "Raspberry Pi 4 Model B Rev 1.4" -> "Raspberry Pi 4 B", "Debian GNU/Linux 12 (bookworm)" -> "Debian 12"
    String model = h.model[0] ? h.model : "Host";
    model.replace(" Model", "");
    int rev = model.indexOf(" Rev ");
    if (rev > 0) model = model.substring(0, rev);
    String os = h.os;
    os.replace("GNU/Linux ", "");
    int par = os.indexOf(" (");
    if (par > 0) os = os.substring(0, par);
    lv_coord_t w = lv_obj_get_width(dHostModel);
    snprintf(buf, sizeof(buf), ICON_PI "  %s  \xC2\xB7  %s", model.c_str(), os.c_str());
    if (!os.length() || text_w(&icons_20, buf) > w) fit_name(buf, sizeof(buf), &icons_20, w, ICON_PI "  ", model.c_str(), false);
    set_text(dHostModel, buf);

    if (h.cores) snprintf(buf, sizeof(buf), "%.0f%%  (%d cores)", h.cpu, h.cores);
    else snprintf(buf, sizeof(buf), "%.0f%%", h.cpu);
    set_row(hCpu, h.cpu, buf);

    fmt_bytes(a, sizeof(a), h.memUsed * 1024.0);
    fmt_bytes(b, sizeof(b), h.memTotal * 1024.0);
    snprintf(buf, sizeof(buf), "%s / %s", a, b);
    set_row(hMem, pct(h.memUsed, h.memTotal), buf);

    fmt_bytes(a, sizeof(a), (double)h.diskUsed);
    fmt_bytes(b, sizeof(b), (double)h.diskTotal);
    snprintf(buf, sizeof(buf), "%s / %s", a, b);
    set_row(hDisk, pct(h.diskUsed, h.diskTotal), h.diskTotal ? buf : "-");

    String foot;
    if (!isnan(h.cpuTemp)) {
        snprintf(buf, sizeof(buf), ICON_THERMO " CPU %.0f" DEG "C     ", h.cpuTemp);
        foot += buf;
    }
    fmt_uptime(a, sizeof(a), h.uptime);
    snprintf(buf, sizeof(buf), ICON_UPTIME " up %s", a);
    foot += buf;
    if (h.throttled) {
        snprintf(buf, sizeof(buf), "%s" ICON_POWER_ALERT " %s", L.portrait ? "   " : "\n", h.flags[0] ? h.flags : "Throttled");
        foot += buf;
    }
    set_text(dHostFoot, foot.c_str());
    set_text_color(dHostFoot, h.throttled ? COL_ORANGE : COL_DIM);
}

static void update_idle() {
    char buf[320];
    const char *icon, *title;
    lv_color_t col;
    String ip = WiFi.localIP().toString();
    if (net_ap_active()) {
        icon = ICON_HOTSPOT; title = "Wi-Fi setup"; col = COL_ACCENT;
        snprintf(buf, sizeof(buf), "Join Wi-Fi \"%s\" on your phone or PC.\nThe settings page opens automatically\n"
                 "(or browse to http://192.168.4.1).", SETUP_AP_SSID);
    } else if (!g.wifi) {
        icon = ICON_WIFI_OFF; title = "Connecting to Wi-Fi"; col = COL_ORANGE;
        snprintf(buf, sizeof(buf), "Network: %s\nA setup hotspot \"%s\"\nappears if this doesn't succeed.", cfg.ssid, SETUP_AP_SSID);
    } else if (!cfg.host[0]) {
        icon = ICON_PRINTER; title = "Printer not set"; col = COL_ORANGE;
        snprintf(buf, sizeof(buf), "Open http://%s\nand enter the printer's IP address.", ip.c_str());
    } else if (!g.online) {
        icon = ICON_LAN_OFF; title = "Printer offline"; col = COL_RED;
        snprintf(buf, sizeof(buf), "Moonraker not reachable at\n%s:%u", cfg.host, cfg.port);
    } else if (!g.ready) {
        bool starting = !strcmp(g.klippy, "startup");
        icon = starting ? ICON_STARTUP : ICON_STOP;
        title = starting ? "Klipper starting" : !strcmp(g.klippy, "shutdown") ? "Klipper shutdown" : "Klipper error";
        col = starting ? COL_ORANGE : COL_RED;
        snprintf(buf, sizeof(buf), "%s", g.klippyMsg[0] ? g.klippyMsg : g.klippy);
    } else {
        bool heating = any_heating();
        icon = heating ? ICON_FIRE : ICON_OK;
        title = heating ? "Heating" : "Ready";
        col = heating ? COL_ORANGE : COL_GREEN;
        snprintf(buf, sizeof(buf), "%s%s" ICON_POS "  X %.1f   Y %.1f   Z %.2f\n" ICON_HOME "  Homed: %s",
                 g.message[0] ? g.message : "", g.message[0] ? "\n" : "", g.pos[0], g.pos[1], g.pos[2],
                 g.homed[0] ? g.homed : "no");
    }
    set_text(dBigIcon, icon);
    set_text_color(dBigIcon, col);
    set_text(dIdleTitle, title);
    set_text_color(dIdleTitle, col);
    set_text(dIdleText, buf);

    // host panel when the printer is up; otherwise the message gets the full height
    bool hostOn = g.ready && g.host.valid;
    set_visible(dHostPanel, hostOn);
    set_height(dIdleText, hostOn ? L.hostY - 54 : L.main.h - 20 - 48);
    if (hostOn) update_host();
}

static void dash_update() {
    update_header();
    update_gauge(gExt, g.ext, true);
    update_gauge(gBed, g.bed, g.hasBed);
    update_list();

    bool job = g.ready && (is_printing() || !strcmp(g.pstate, "complete") || !strcmp(g.pstate, "cancelled") ||
                           (!strcmp(g.pstate, "error") && g.file[0]));
    set_visible(dPrint, job);
    set_visible(dIdle, !job);
    if (job) update_job();
    else update_idle();
}

// ---------------------------------------------------------------- build

static void build_header(lv_obj_t* scr) {
    lv_obj_t* bar = lv_obj_create(scr);
    lv_obj_set_size(bar, L.W, L.headH);
    lv_obj_set_pos(bar, 0, 0);
    set_bg_color(bar, COL_BAR, 0);
    lv_obj_set_style_radius(bar, 0, 0);
    lv_obj_set_style_border_width(bar, 0, 0);
    lv_obj_set_style_pad_hor(bar, 12, 0);
    lv_obj_set_style_pad_ver(bar, 0, 0);
    lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);

    dState = ui_label(bar, "", &icons_20);
    lv_obj_set_style_bg_opa(dState, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(dState, 8, 0);
    lv_obj_set_style_pad_hor(dState, 12, 0);
    lv_obj_set_style_pad_ver(dState, 6, 0);
    dHost = ui_label(bar, "", &icons_20, COL_DIM);
    lv_label_set_long_mode(dHost, LV_LABEL_LONG_DOT);
    dNet = ui_label(bar, "", &icons_20);
    if (!L.portrait) {
        lv_obj_align(dState, LV_ALIGN_LEFT_MID, 0, 0);
        lv_obj_set_width(dHost, 300);
        lv_obj_align(dHost, LV_ALIGN_LEFT_MID, 190, 0);
        lv_obj_align(dNet, LV_ALIGN_RIGHT_MID, 0, 0);
    } else {  // two rows: state + display IP, then printer IP
        lv_obj_align(dState, LV_ALIGN_TOP_LEFT, 0, 8);
        lv_obj_align(dNet, LV_ALIGN_TOP_RIGHT, 0, 14);
        lv_obj_set_width(dHost, L.W - 24);
        lv_obj_set_style_text_align(dHost, LV_TEXT_ALIGN_RIGHT, 0);  // both IPs right-aligned
        lv_obj_align(dHost, LV_ALIGN_TOP_RIGHT, 0, 54);
    }
}

static void build_list(lv_obj_t* scr) {
    lv_obj_t* c = card(scr, L.list);
    lv_obj_set_style_pad_ver(c, 8, 0);
    lv_coord_t inner = L.list.w - 20, gap = 14;
    lv_coord_t colW = (inner - (L.listCols - 1) * gap) / L.listCols;
    nSlots = LV_MIN(L.listCols * L.listRows, MAX_SLOTS);
    for (int i = 0; i < nSlots; i++) {
        lv_coord_t x = (i / L.listRows) * (colW + gap), y = (i % L.listRows) * L.listRowH;  // fill column by column
        slots[i].name = ui_label(c, "", &icons_16);
        lv_label_set_long_mode(slots[i].name, LV_LABEL_LONG_CLIP);  // fit_name() guarantees it fits
        lv_obj_set_width(slots[i].name, colW - 42);
        slotNameW = colW - 46;
        lv_obj_set_pos(slots[i].name, x, y);
        slots[i].value = ui_label(c, "", &lv_font_montserrat_16);
        lv_obj_set_width(slots[i].value, 48);
        lv_obj_set_style_text_align(slots[i].value, LV_TEXT_ALIGN_RIGHT, 0);
        lv_obj_set_pos(slots[i].value, x + colW - 48, y);
    }
}

static void build_print_panel(lv_obj_t* m, lv_coord_t iw, lv_coord_t ih) {
    dPrint = plain(m, 0, 0, iw, ih);
    dRing = make_arc(dPrint, L.ring, L.portrait ? 12 : 14, 270, 360);
    lv_arc_set_range(dRing, 0, 1000);
    lv_obj_set_pos(dRing, 0, 0);
    thumbBox = L.ring * 72 / 100;
    lv_obj_t* box = plain(dRing, 0, 0, thumbBox, thumbBox);
    set_bg_color(box, COL_BG, 0);
    lv_obj_set_style_bg_opa(box, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(box, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_clip_corner(box, true, 0);
    lv_obj_center(box);
    lv_obj_center(ui_label(box, ICON_CUBE, L.portrait ? &icons_28 : &icons_72, COL_DIM));
    dImg = lv_img_create(box);
    lv_obj_center(dImg);

    lv_coord_t tx = L.ring + 16, tw = iw - tx;
    dFile = ui_label(dPrint, "", &lv_font_montserrat_20);
    lv_label_set_long_mode(dFile, LV_LABEL_LONG_DOT);  // up to 2 lines
    lv_obj_set_size(dFile, tw, 48);
    lv_obj_set_pos(dFile, tx, 0);
    dPct = ui_label(dPrint, "", &lv_font_montserrat_48);
    lv_obj_set_pos(dPct, tx, L.pctY);
    dChip = ui_label(dPrint, "", &icons_20);
    lv_label_set_long_mode(dChip, LV_LABEL_LONG_DOT);
    lv_obj_set_width(dChip, tw);
    lv_obj_set_pos(dChip, tx, L.chipY);

    static const char* icons[8] = {ICON_CLOCK, ICON_HOURGLASS, ICON_LAYERS, ICON_Z, ICON_SPEED, ICON_FLOW, ICON_FAN, ICON_FILAMENT};
    static const char* caps[8] = {"Elapsed", "Remaining", "Layer", "Z mm", "Speed", "Flow", "Part fan", "Filament"};
    lv_coord_t tileW = (iw - 3 * 8) / 4, y0 = L.ring + 12;
    for (int i = 0; i < 8; i++)
        make_tile(tiles[i], dPrint, (i % 4) * (tileW + 8), y0 + (i / 4) * (L.tileH + 8), tileW, L.tileH, icons[i], caps[i]);
}

static void build_idle_panel(lv_obj_t* m, lv_coord_t iw, lv_coord_t ih) {
    dIdle = plain(m, 0, 0, iw, ih);
    dBigIcon = ui_label(dIdle, "", &icons_72);
    lv_obj_set_pos(dBigIcon, 0, 0);
    dIdleTitle = ui_label(dIdle, "", &lv_font_montserrat_32);
    lv_obj_set_pos(dIdleTitle, 88, 4);
    dIdleText = ui_label(dIdle, "", &icons_20, COL_TEXT);
    lv_label_set_long_mode(dIdleText, LV_LABEL_LONG_DOT);
    lv_obj_set_width(dIdleText, iw - 88);
    lv_obj_set_style_text_line_space(dIdleText, 4, 0);
    lv_obj_set_pos(dIdleText, 88, 48);

    // host computer panel
    dHostPanel = plain(dIdle, 0, L.hostY, iw, ih - L.hostY);
    lv_obj_t* sep = lv_obj_create(dHostPanel);
    lv_obj_set_size(sep, iw, 2);
    set_bg_color(sep, COL_LINE, 0);
    lv_obj_set_style_border_width(sep, 0, 0);
    dHostModel = ui_label(dHostPanel, "", &icons_20, COL_DIM);
    lv_label_set_long_mode(dHostModel, LV_LABEL_LONG_CLIP);
    lv_obj_set_width(dHostModel, iw);
    lv_obj_set_pos(dHostModel, 0, 10);
    struct R { HostRow* r; const char* icon; const char* name; };
    const R rows[] = {{&hCpu, ICON_CPU, "CPU"}, {&hMem, ICON_MEMORY, "Memory"}, {&hDisk, ICON_DISK, "Disk"}};
    lv_coord_t y = 0;
    for (int i = 0; i < 3; i++) {
        y = 36 + i * L.hostRowGap;
        char t[48];
        snprintf(t, sizeof(t), "%s  %s", rows[i].icon, rows[i].name);
        lv_obj_set_pos(ui_label(dHostPanel, t, &icons_20, COL_TEXT), 0, y);
        rows[i].r->value = ui_label(dHostPanel, "", &lv_font_montserrat_16, COL_DIM);
        lv_obj_align(rows[i].r->value, LV_ALIGN_TOP_RIGHT, 0, y + 2);
        rows[i].r->bar = lv_bar_create(dHostPanel);
        lv_obj_set_size(rows[i].r->bar, iw, 8);
        lv_obj_set_pos(rows[i].r->bar, 0, y + 26);
        lv_bar_set_range(rows[i].r->bar, 0, 100);
    }
    dHostFoot = ui_label(dHostPanel, "", &icons_20, COL_DIM);
    lv_label_set_long_mode(dHostFoot, LV_LABEL_LONG_DOT);
    lv_obj_set_width(dHostFoot, iw);
    lv_obj_set_pos(dHostFoot, 0, y + 42);
}

void scr_dashboard(lv_obj_t* scr) {
    compute_layout();
    build_header(scr);
    make_gauge(gExt, scr, L.ext, ICON_NOZZLE, "Hotend", 300);
    make_gauge(gBed, scr, L.bed, ICON_BED, "Bed", 120);
    build_list(scr);

    lv_obj_t* m = card(scr, L.main);
    lv_coord_t iw = L.main.w - 20, ih = L.main.h - 20;
    build_print_panel(m, iw, ih);
    build_idle_panel(m, iw, ih);

    ui_set_update(dash_update);
    dash_update();
}
