// Console, settings menu, connection (WiFi + printer address) and system screens.
#include "ui.h"
#include "board.h"
#include "clock.h"
#include "ui_icons.h"
#include <WiFi.h>

#include "version.h"
#include "about.h"

#define KB_H 150  // on-screen keyboard height; inputs must stay above CONTENT_H - KB_H

static lv_obj_t* make_kb(lv_obj_t* p, lv_event_cb_t cb) {
    lv_obj_t* kb = lv_keyboard_create(p);
    lv_obj_set_size(kb, CONTENT_W, KB_H);
    lv_obj_set_style_text_font(kb, FONT(14), LV_PART_ITEMS);
    lv_obj_set_style_pad_all(kb, U(3), 0);
    lv_obj_align(kb, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_add_flag(kb, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(kb, cb, LV_EVENT_ALL, nullptr);
    return kb;
}

static lv_obj_t* make_ta(lv_obj_t* p, const char* placeholder, const char* text, lv_coord_t w) {
    lv_obj_t* ta = lv_textarea_create(p);
    lv_textarea_set_one_line(ta, true);
    lv_textarea_set_placeholder_text(ta, placeholder);
    lv_textarea_set_text(ta, text);
    lv_obj_set_width(ta, w);
    lv_obj_set_height(ta, U(36));
    lv_obj_set_style_text_font(ta, FONT(14), 0);
    lv_obj_set_style_pad_ver(ta, U(8), 0);
    return ta;
}

// ================================================================ CONSOLE

static lv_obj_t *cLogBox, *cLog, *cRow, *cTa, *cKb;
static uint32_t cVer = 0;

static void console_layout(bool kb) {
    if (kb) {
        lv_obj_set_height(cLogBox, CONTENT_H - KB_H - U(54));
        lv_obj_set_y(cRow, CONTENT_H - KB_H - U(42));
        lv_obj_clear_flag(cKb, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_set_height(cLogBox, CONTENT_H - U(54));
        lv_obj_set_y(cRow, CONTENT_H - U(42));
        lv_obj_add_flag(cKb, LV_OBJ_FLAG_HIDDEN);
    }
    lv_obj_scroll_to_y(cLogBox, LV_COORD_MAX, LV_ANIM_OFF);
}

static void console_send() {
    const char* t = lv_textarea_get_text(cTa);
    if (!t[0]) return;
    mr_gcode(t);
    lv_textarea_set_text(cTa, "");
}

static void console_kb_evt(lv_event_t* e) {
    lv_event_code_t c = lv_event_get_code(e);
    if (c == LV_EVENT_READY) {
        console_send();
        console_layout(false);
    } else if (c == LV_EVENT_CANCEL) {
        console_layout(false);
    }
}

static void console_ta_evt(lv_event_t* e) {
    if (lv_event_get_code(e) == LV_EVENT_FOCUSED || lv_event_get_code(e) == LV_EVENT_CLICKED) {
        lv_keyboard_set_textarea(cKb, cTa);
        console_layout(true);
    }
}

static void console_send_cb(lv_event_t*) {
    console_send();
    console_layout(false);
}

static void console_update() {
    String s;
    uint32_t v = mr_console(s);
    if (v != cVer) {
        cVer = v;
        set_text(cLog, s.c_str());
        lv_obj_scroll_to_y(cLogBox, LV_COORD_MAX, LV_ANIM_OFF);
    }
}

void scr_console(lv_obj_t* p) {
    cLogBox = lv_obj_create(p);
    lv_obj_set_pos(cLogBox, U(6), U(6));
    lv_obj_set_size(cLogBox, CONTENT_W - U(12), CONTENT_H - U(54));
    set_bg_color(cLogBox, COL_CARD, 0);
    lv_obj_set_style_border_width(cLogBox, 0, 0);
    lv_obj_set_style_pad_all(cLogBox, U(6), 0);
    lv_obj_set_scroll_dir(cLogBox, LV_DIR_VER);
    cLog = ui_label(cLogBox, "Loading...", FONT(12));
    lv_label_set_long_mode(cLog, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(cLog, CONTENT_W - U(34));

    cRow = ui_row(p, CONTENT_W - U(12), U(36));
    lv_obj_set_pos(cRow, U(6), CONTENT_H - U(42));
    cTa = make_ta(cRow, "G-code, e.g. G28 or QUERY_PROBE", "", CONTENT_W - U(12) - U(84));
    lv_obj_add_event_cb(cTa, console_ta_evt, LV_EVENT_ALL, nullptr);
    lv_obj_t* sb = ui_btn(cRow, "Send", U(78), U(36), console_send_cb, nullptr, COL_ACCENT);
    lv_obj_set_style_text_font(sb, FONT(14), 0);

    cKb = make_kb(p, console_kb_evt);
    cVer = 0;
    mr_console_active(true);
    ui_set_update(console_update);
}

// ================================================================ SETTINGS MENU

static void goto_cb(lv_event_t* e) { ui_goto((Scr)(intptr_t)lv_event_get_user_data(e)); }

void scr_settings(lv_obj_t* p) {
    lv_obj_t* g2 = lv_obj_create(p);
    lv_obj_set_pos(g2, U(6), U(6));
    lv_obj_set_size(g2, CONTENT_W - U(12), PORTRAIT ? U(350) : U(236));
    lv_obj_set_style_bg_opa(g2, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(g2, 0, 0);
    lv_obj_set_style_pad_all(g2, 0, 0);
    lv_obj_set_style_pad_column(g2, U(5), 0);
    lv_obj_set_style_pad_row(g2, U(5), 0);
    lv_obj_set_flex_flow(g2, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_clear_flag(g2, LV_OBJ_FLAG_SCROLLABLE);
    const lv_coord_t w = PORTRAIT ? (CONTENT_W - U(17)) / 2 : U(113);
    ui_tile(g2, LV_SYMBOL_WIFI, "Connection", w, U(110), goto_cb, (void*)(intptr_t)S_CONNECT, COL_ACCENT);
    ui_tile(g2, LV_SYMBOL_SETTINGS, "System", w, U(110), goto_cb, (void*)(intptr_t)S_SYSTEM);
    ui_tile(g2, LV_SYMBOL_POWER, "Power devices", w, U(110), goto_cb, (void*)(intptr_t)S_POWER);
    ui_tile(g2, LV_SYMBOL_KEYBOARD, "Console", w, U(110), goto_cb, (void*)(intptr_t)S_CONSOLE);
    ui_tile(g2, ICON_CLOCK, "Date & Time", w, U(110), goto_cb, (void*)(intptr_t)S_TIME);
    ui_tile(g2, ICON_CHIP, "About", w, U(110), goto_cb, (void*)(intptr_t)S_ABOUT);
}

// ================================================================ ABOUT

static lv_obj_t* abVal[ABOUT_MAX];
static int abCount;

static void about_update() {
    AboutItem it[ABOUT_MAX];
    int n = about_collect(it, ABOUT_MAX);
    for (int i = 0; i < n && i < abCount; i++) set_text(abVal[i], it[i].value);  // uptime / free memory change
}

void scr_about(lv_obj_t* p) {
    const bool P = PORTRAIT;
    lv_obj_t* c = ui_card(p, U(6), U(6), CONTENT_W - U(12), CONTENT_H - U(12));
    lv_obj_t* t = ui_label(c, "KlippESP v" FW_VERSION, FONT(28));
    lv_obj_set_pos(t, U(4), 0);
    lv_obj_t* s = ui_label(c, "Wireless Klipper control", FONT(14), COL_ACCENT);
    lv_obj_set_pos(s, U(6), U(34));

    lv_obj_t* list = lv_obj_create(c);  // key / value rows, scrolls if the screen is short
    lv_obj_remove_style_all(list);
    lv_obj_set_pos(list, 0, U(60));
    lv_obj_set_size(list, CONTENT_W - U(12) - U(24), CONTENT_H - U(12) - U(24) - U(60));
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(list, U(3), 0);
    lv_obj_set_scroll_dir(list, LV_DIR_VER);

    AboutItem it[ABOUT_MAX];
    abCount = about_collect(it, ABOUT_MAX);
    const lv_coord_t kw = P ? U(110) : U(130), rw = CONTENT_W - U(12) - U(24);
    for (int i = 0; i < abCount; i++) {
        lv_obj_t* r = lv_obj_create(list);
        lv_obj_remove_style_all(r);
        lv_obj_set_size(r, rw, LV_SIZE_CONTENT);
        lv_obj_t* k = ui_label(r, it[i].key, FONT(14), COL_DIM);
        lv_obj_set_width(k, kw);
        abVal[i] = ui_label(r, it[i].value, FONT(14));
        lv_label_set_long_mode(abVal[i], LV_LABEL_LONG_WRAP);
        lv_obj_set_width(abVal[i], rw - kw);
        lv_obj_set_x(abVal[i], kw);
    }
    ui_set_update(about_update);
}

// ================================================================ CONNECTION

static lv_obj_t *kSsid, *kPass, *kHost, *kPort, *kKey, *kKb, *kWifiSt, *kMrSt, *kScanBtn, *kScanOv;
static bool kScanning = false;

static void conn_kb_evt(lv_event_t* e) {
    lv_event_code_t c = lv_event_get_code(e);
    if (c == LV_EVENT_READY || c == LV_EVENT_CANCEL) {
        lv_obj_add_flag(kKb, LV_OBJ_FLAG_HIDDEN);
        lv_obj_t* ta = lv_keyboard_get_textarea(kKb);
        if (ta) lv_obj_clear_state(ta, LV_STATE_FOCUSED);
    }
}

static void conn_ta_evt(lv_event_t* e) {
    lv_event_code_t c = lv_event_get_code(e);
    if (c == LV_EVENT_FOCUSED || c == LV_EVENT_CLICKED) {
        lv_obj_t* ta = lv_event_get_target(e);
        lv_keyboard_set_textarea(kKb, ta);
        lv_keyboard_set_mode(kKb, ta == kPort ? LV_KEYBOARD_MODE_NUMBER : LV_KEYBOARD_MODE_TEXT_LOWER);
        lv_obj_clear_flag(kKb, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(kKb);
    }
}

static void eye_cb(lv_event_t*) { lv_textarea_set_password_mode(kPass, !lv_textarea_get_password_mode(kPass)); }

static void save_cb(lv_event_t*) {
    bool wifiChanged = strcmp(cfg.ssid, lv_textarea_get_text(kSsid)) || strcmp(cfg.pass, lv_textarea_get_text(kPass)) ||
                       WiFi.status() != WL_CONNECTED;
    strlcpy(cfg.ssid, lv_textarea_get_text(kSsid), sizeof(cfg.ssid));
    strlcpy(cfg.pass, lv_textarea_get_text(kPass), sizeof(cfg.pass));
    String host = lv_textarea_get_text(kHost);
    host.trim();
    host.replace("http://", "");
    if (host.endsWith("/")) host.remove(host.length() - 1);
    strlcpy(cfg.host, host.c_str(), sizeof(cfg.host));
    int port = atoi(lv_textarea_get_text(kPort));
    cfg.port = port > 0 && port < 65536 ? port : 7125;
    strlcpy(cfg.apikey, lv_textarea_get_text(kKey), sizeof(cfg.apikey));
    settings_save();
    lv_obj_add_flag(kKb, LV_OBJ_FLAG_HIDDEN);

    if (wifiChanged && cfg.ssid[0]) {
        WiFi.disconnect();
        WiFi.begin(cfg.ssid, cfg.pass);
    }
    mr_reconfigure();
    ui_toast("Settings saved, connecting...");
}

static void scan_pick(lv_event_t* e) {
    if (!kScanOv) return;
    lv_obj_t* b = lv_event_get_target(e);
    lv_textarea_set_text(kSsid, lv_label_get_text(lv_obj_get_child(b, 0)));
    lv_obj_del_async(kScanOv);
    kScanOv = nullptr;
    lv_obj_add_state(kPass, LV_STATE_FOCUSED);
    lv_keyboard_set_textarea(kKb, kPass);
    lv_keyboard_set_mode(kKb, LV_KEYBOARD_MODE_TEXT_LOWER);
    lv_obj_clear_flag(kKb, LV_OBJ_FLAG_HIDDEN);
}

static void scan_close(lv_event_t*) {
    if (!kScanOv) return;
    lv_obj_del_async(kScanOv);
    kScanOv = nullptr;
}

static void show_scan_results(int n) {
    kScanOv = lv_obj_create(lv_layer_top());
    lv_obj_set_size(kScanOv, SW, SH);
    set_bg_color(kScanOv, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(kScanOv, LV_OPA_60, 0);
    lv_obj_set_style_border_width(kScanOv, 0, 0);
    lv_obj_set_style_radius(kScanOv, 0, 0);
    const lv_coord_t sw = PORTRAIT ? U(300) : U(440), sh = PORTRAIT ? U(420) : U(296);
    lv_obj_t* c = ui_card(kScanOv, 0, 0, sw, sh);
    lv_obj_center(c);
    ui_label(c, n > 0 ? "Select network" : "No networks found", FONT(16));
    lv_obj_t* close = ui_btn(c, LV_SYMBOL_CLOSE, U(44), U(32), scan_close);
    lv_obj_align(close, LV_ALIGN_TOP_RIGHT, 0, -U(4));

    lv_obj_t* list = lv_obj_create(c);
    lv_obj_set_pos(list, 0, U(36));
    lv_obj_set_size(list, sw - U(16), sh - U(52));
    lv_obj_set_style_bg_opa(list, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(list, 0, 0);
    lv_obj_set_style_pad_all(list, 0, 0);
    lv_obj_set_style_pad_row(list, U(6), 0);
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);

    int shown = 0;
    for (int i = 0; i < n && shown < 25; i++) {
        String s = WiFi.SSID(i);
        if (!s.length()) continue;
        bool dup = false;
        for (int j = 0; j < i; j++)
            if (WiFi.SSID(j) == s) dup = true;
        if (dup) continue;
        lv_obj_t* b = ui_btn(list, s.c_str(), sw - U(26), U(40), scan_pick);
        lv_obj_set_style_text_font(b, FONT(14), 0);
        lv_obj_align(lv_obj_get_child(b, 0), LV_ALIGN_LEFT_MID, U(8), 0);
        char r[32];
        snprintf(r, sizeof(r), "%d dBm%s", WiFi.RSSI(i), WiFi.encryptionType(i) == WIFI_AUTH_OPEN ? "  open" : "");
        lv_obj_t* rl = ui_label(b, r, FONT(12), COL_DIM);
        lv_obj_align(rl, LV_ALIGN_RIGHT_MID, -U(8), 0);
        shown++;
    }
    WiFi.scanDelete();
}

static void scan_cb(lv_event_t*) {
    if (kScanning) return;
    if (WiFi.status() != WL_CONNECTED) WiFi.disconnect();  // a pending connect attempt blocks scanning
    if (WiFi.scanNetworks(true) == WIFI_SCAN_FAILED) {
        ui_toast("WiFi scan failed", true);
        return;
    }
    kScanning = true;
    ui_btn_text(kScanBtn, "...");
}

static void connect_update() {
    char buf[128];
    if (kScanning) {
        int n = WiFi.scanComplete();
        if (n != WIFI_SCAN_RUNNING) {
            kScanning = false;
            ui_btn_text(kScanBtn, LV_SYMBOL_REFRESH);
            if (n == WIFI_SCAN_FAILED) ui_toast("WiFi scan failed", true);
            else show_scan_results(n);
        }
    }
    if (WiFi.status() == WL_CONNECTED)
        snprintf(buf, sizeof(buf), LV_SYMBOL_OK " Connected  %s  (%d dBm)", WiFi.localIP().toString().c_str(), WiFi.RSSI());
    else
        snprintf(buf, sizeof(buf), cfg.ssid[0] ? "Connecting to %s..." : "Not configured", cfg.ssid);
    set_text(kWifiSt, buf);
    set_text_color(kWifiSt, WiFi.status() == WL_CONNECTED ? COL_GREEN : COL_ORANGE);

    if (g.online) snprintf(buf, sizeof(buf), LV_SYMBOL_OK " Moonraker OK, Klipper %s", g.klippy);
    else snprintf(buf, sizeof(buf), cfg.host[0] ? "Moonraker not reachable" : "Enter the printer's IP address");
    set_text(kMrSt, buf);
    set_text_color(kMrSt, g.online ? COL_GREEN : COL_ORANGE);
}

void scr_connect(lv_obj_t* p) {
    // Everything that needs typing sits above the keyboard (top CONTENT_H - KB_H px).
    const bool P = PORTRAIT;
    const lv_coord_t colW = P ? CONTENT_W - U(12) : U(230);
    const lv_coord_t x2 = P ? U(6) : U(244), y2 = P ? U(120) : 0;  // printer column origin
    lv_obj_t* l = ui_label(p, LV_SYMBOL_WIFI " Wi-Fi", FONT(14), COL_DIM);
    lv_obj_set_pos(l, U(8), U(2));
    lv_obj_t* r1 = ui_row(p, colW, U(36));
    lv_obj_set_pos(r1, U(6), U(20));
    lv_obj_set_style_pad_column(r1, U(4), 0);
    kSsid = make_ta(r1, "Network (SSID)", cfg.ssid, colW - U(48));
    kScanBtn = ui_btn(r1, LV_SYMBOL_REFRESH, U(44), U(36), scan_cb);
    lv_obj_t* r2 = ui_row(p, colW, U(36));
    lv_obj_set_pos(r2, U(6), U(60));
    lv_obj_set_style_pad_column(r2, U(4), 0);
    kPass = make_ta(r2, "Password", cfg.pass, colW - U(48));
    lv_textarea_set_password_mode(kPass, true);
    ui_btn(r2, LV_SYMBOL_EYE_OPEN, U(44), U(36), eye_cb);
    kWifiSt = ui_label(p, "", FONT(12), COL_DIM);
    lv_obj_set_pos(kWifiSt, U(8), U(100));
    lv_obj_set_width(kWifiSt, colW - U(2));
    lv_label_set_long_mode(kWifiSt, LV_LABEL_LONG_DOT);

    lv_obj_t* l2 = ui_label(p, LV_SYMBOL_SETTINGS " Printer (Moonraker)", FONT(14), COL_DIM);
    lv_obj_set_pos(l2, x2 + U(2), y2 + U(2));
    kHost = make_ta(p, "Printer IP or hostname", cfg.host, colW);
    lv_obj_set_pos(kHost, x2, y2 + U(20));
    lv_obj_t* r3 = ui_row(p, colW, U(36));
    lv_obj_set_pos(r3, x2, y2 + U(60));
    lv_obj_set_style_pad_column(r3, U(4), 0);
    char port[8];
    snprintf(port, sizeof(port), "%u", cfg.port);
    kPort = make_ta(r3, "Port", port, U(70));
    lv_textarea_set_accepted_chars(kPort, "0123456789");
    kKey = make_ta(r3, "API key (optional)", cfg.apikey, colW - U(74));
    kMrSt = ui_label(p, "", FONT(12), COL_DIM);
    lv_obj_set_pos(kMrSt, x2 + U(2), y2 + U(100));
    lv_obj_set_width(kMrSt, colW - U(2));
    lv_label_set_long_mode(kMrSt, LV_LABEL_LONG_DOT);

    lv_obj_t* tas[] = {kSsid, kPass, kHost, kPort, kKey};
    for (lv_obj_t* ta : tas) lv_obj_add_event_cb(ta, conn_ta_evt, LV_EVENT_ALL, nullptr);

    lv_obj_t* c = ui_card(p, U(6), CONTENT_H - KB_H, CONTENT_W - U(12), KB_H - U(6));
    lv_obj_t* hint = ui_label(c,
        P ? "Moonraker port is usually 7125 (80 via\nMainsail). .local names use mDNS. API key\nonly if not a trusted client."
          : "Moonraker usually listens on port 7125 (port 80 if it\n"
            "only works through Mainsail). .local names use mDNS.\n"
            "API key only needed if this display isn't a trusted client.",
        FONT(12), COL_DIM);
    lv_obj_set_pos(hint, 0, 0);
    lv_obj_t* sb = ui_btn(c, LV_SYMBOL_SAVE "  Save & Connect", U(190), U(44), save_cb, nullptr, COL_GREEN);
    lv_obj_set_style_text_font(sb, FONT(14), 0);
    lv_obj_align(sb, LV_ALIGN_BOTTOM_RIGHT, 0, 0);

    kKb = make_kb(p, conn_kb_evt);
    kScanning = false;
    ui_set_update(connect_update);
}

// ================================================================ SYSTEM

static lv_obj_t *sysInfo, *sysSleep;
static const uint16_t sleepOpts[] = {0, 1, 5, 15, 30, 60};

static void post_cb(lv_event_t* e) { mr_post((const char*)lv_event_get_user_data(e)); }
static void post_yes(void* ud) { mr_post((const char*)ud); }
static void reboot_host_cb(lv_event_t*) { ui_confirm("Reboot host", "Reboot the printer's host computer?", post_yes, (void*)"/machine/reboot"); }
static void shutdown_host_cb(lv_event_t*) {
    ui_confirm("Shutdown host", "Shut down the printer's host computer?", post_yes, (void*)"/machine/shutdown");
}
static void reboot_self(void*) { ESP.restart(); }
static void reboot_self_cb(lv_event_t*) { ui_confirm("Restart display", "Restart this touchscreen?", reboot_self); }

static void sleep_cb(lv_event_t* e) {
    uint16_t i = lv_dropdown_get_selected(lv_event_get_target(e));
    cfg.sleepMin = sleepOpts[i];
    settings_save();
}

static void system_update() {
    char buf[600], host[260] = "";
    const MrHost& h = g.host;
    if (g.online && h.valid) {
        char temp[16] = "-";
        if (!isnan(h.cpuTemp)) snprintf(temp, sizeof(temp), "%.0f" DEG "C", h.cpuTemp);
        snprintf(host, sizeof(host),
                 "\n%s\nCPU %.0f%%  %s  up %uh\nMem %u / %u MB\nDisk %.1f / %.1f GB%s%s",
                 h.model[0] ? h.model : "Host", h.cpu, temp, (unsigned)(h.uptime / 3600),
                 (unsigned)(h.memUsed / 1024), (unsigned)(h.memTotal / 1024), h.diskUsed / 1e9, h.diskTotal / 1e9,
                 h.throttled ? "\n" LV_SYMBOL_WARNING " " : "", h.throttled ? h.flags : "");
    }
    char now[32];
    if (!clock_datetime(now, sizeof(now))) strlcpy(now, "not synced", sizeof(now));
    snprintf(buf, sizeof(buf), "KlippESP %s\n%s\nIP %s  %d dBm\nPrinter %s:%u\nKlipper %s\nFree heap %u KB%s",
             FW_VERSION, now, WiFi.localIP().toString().c_str(), WiFi.RSSI(), cfg.host, cfg.port,
             g.online ? g.klippy : "offline", (unsigned)(ESP.getFreeHeap() / 1024), host);
    set_text(sysInfo, buf);
}

static uint8_t pendingRot;
static void rot_yes(void*) {
    cfg.rotation = pendingRot;
    settings_save();
    delay(200);
    ESP.restart();
}
static void rot_cb(lv_event_t* e) {
    uint16_t i = lv_dropdown_get_selected(lv_event_get_target(e));
    if (i == cfg.rotation) return;
    pendingRot = i;
    ui_confirm("Orientation", "Restart the display to apply the new orientation?", rot_yes);
}

[[maybe_unused]] static void bri_cb(lv_event_t* e) {
    lv_obj_t* s = lv_event_get_target(e);
    cfg.brightness = lv_slider_get_value(s);
    board_set_brightness(cfg.brightness);
    if (lv_event_get_code(e) == LV_EVENT_RELEASED) settings_save();
}

void scr_system(lv_obj_t* p) {
    const bool P = PORTRAIT;
    // info + screen options card
    const lv_coord_t cw = P ? CONTENT_W - U(12) : U(236), ch = P ? U(200) : CONTENT_H - U(12), iw = cw - U(16);
    lv_obj_t* c = ui_card(p, U(6), U(6), cw, ch);
    sysInfo = ui_label(c, "", FONT(12));
    lv_label_set_long_mode(sysInfo, LV_LABEL_LONG_DOT);
    lv_obj_set_size(sysInfo, iw, ch - U(16) - U(56));
    const lv_coord_t dw = (iw - U(6)) / 2, dy = ch - U(16) - U(34);
    lv_obj_t* sl = ui_label(c, "Screen off", FONT(12), COL_DIM);
    lv_obj_set_pos(sl, 0, dy - U(18));
    lv_obj_t* ol = ui_label(c, "Orientation", FONT(12), COL_DIM);
    lv_obj_set_pos(ol, dw + U(6), dy - U(18));
    sysSleep = lv_dropdown_create(c);
    lv_dropdown_set_options(sysSleep, "Never\n1 min\n5 min\n15 min\n30 min\n60 min");
    lv_obj_set_style_text_font(sysSleep, FONT(14), 0);
    lv_obj_set_size(sysSleep, dw, U(34));
    lv_obj_set_pos(sysSleep, 0, dy);
    for (int i = 0; i < 6; i++)
        if (sleepOpts[i] == cfg.sleepMin) lv_dropdown_set_selected(sysSleep, i);
    lv_obj_add_event_cb(sysSleep, sleep_cb, LV_EVENT_VALUE_CHANGED, nullptr);
    lv_obj_t* rot = lv_dropdown_create(c);
    lv_dropdown_set_options(rot, "Landscape\nPortrait\nLandscape 180\nPortrait 180");
    lv_obj_set_style_text_font(rot, FONT(14), 0);
    lv_obj_set_size(rot, dw, U(34));
    lv_obj_set_pos(rot, dw + U(6), dy);
    lv_dropdown_set_selected(rot, cfg.rotation & 3);
    lv_obj_add_event_cb(rot, rot_cb, LV_EVENT_VALUE_CHANGED, nullptr);

    // brightness + actions
    const lv_coord_t rx = P ? U(6) : U(248), ry = P ? U(212) : U(6), rw = P ? CONTENT_W - U(12) : U(226);
    lv_obj_t* r = lv_obj_create(p);
    lv_obj_set_pos(r, rx, ry);
    lv_obj_set_size(r, rw, CONTENT_H - U(6) - ry);
    lv_obj_set_style_bg_opa(r, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(r, 0, 0);
    lv_obj_set_style_pad_all(r, 0, 0);
    lv_obj_set_style_pad_row(r, U(4), 0);
    lv_obj_set_style_pad_column(r, U(4), 0);
    lv_obj_set_flex_flow(r, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_clear_flag(r, LV_OBJ_FLAG_SCROLLABLE);

#if BOARD_HAS_DIMMING
    lv_obj_t* br = ui_row(r, rw, U(32));
    ui_label(br, LV_SYMBOL_IMAGE, FONT(14), COL_DIM);
    lv_obj_t* bs = lv_slider_create(br);
    lv_obj_set_size(bs, rw - U(36), U(10));
    lv_obj_set_ext_click_area(bs, U(14));
    lv_slider_set_range(bs, 10, 255);
    lv_slider_set_value(bs, cfg.brightness, LV_ANIM_OFF);
    lv_obj_add_event_cb(bs, bri_cb, LV_EVENT_VALUE_CHANGED, nullptr);
    lv_obj_add_event_cb(bs, bri_cb, LV_EVENT_RELEASED, nullptr);
#endif

    const lv_coord_t bw = P ? (rw - U(4)) / 2 : rw;
    auto act = [&](const char* t, lv_event_cb_t cb, void* ud, lv_color_t col) {
        lv_obj_t* b = ui_btn(r, t, bw, U(34), cb, ud, col);
        lv_obj_set_style_text_font(b, FONT(14), 0);
    };
    act(LV_SYMBOL_REFRESH " Restart Klipper", post_cb, (void*)"/printer/restart", COL_BTN);
    act(LV_SYMBOL_REFRESH " Firmware Restart", post_cb, (void*)"/printer/firmware_restart", COL_ORANGE);
    act(LV_SYMBOL_REFRESH " Restart Moonraker", post_cb, (void*)"/server/restart", COL_BTN);
    act(LV_SYMBOL_POWER " Reboot Host", reboot_host_cb, nullptr, COL_RED);
    act(LV_SYMBOL_POWER " Shutdown Host", shutdown_host_cb, nullptr, COL_RED);
    act("Restart Display", reboot_self_cb, nullptr, COL_BTN);
    ui_set_update(system_update);
}

// ================================================================ DATE & TIME

static lv_obj_t *tmClock, *tmDate, *tmStatus, *tmNtp, *tmKb, *tmZone, *tm24;

static void time_kb_evt(lv_event_t* e) {
    lv_event_code_t c = lv_event_get_code(e);
    if (c == LV_EVENT_READY) {
        String n = lv_textarea_get_text(tmNtp);
        n.trim();
        strlcpy(cfg.ntp, n.length() ? n.c_str() : "pool.ntp.org", sizeof(cfg.ntp));
        settings_save();
        clock_apply();
        ui_toast("NTP server saved, syncing...");
    }
    if (c == LV_EVENT_READY || c == LV_EVENT_CANCEL) {
        lv_obj_add_flag(tmKb, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_state(tmNtp, LV_STATE_FOCUSED);
    }
}

static void time_ta_evt(lv_event_t* e) {
    lv_event_code_t c = lv_event_get_code(e);
    if (c == LV_EVENT_FOCUSED || c == LV_EVENT_CLICKED) {
        lv_keyboard_set_textarea(tmKb, tmNtp);
        lv_obj_clear_flag(tmKb, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(tmKb);
    }
}

static void zone_cb(lv_event_t* e) {
    uint16_t i = lv_dropdown_get_selected(lv_event_get_target(e));
    if (i >= TZ_ZONE_COUNT) return;  // "Custom" (set on the web page) keeps its rule
    strlcpy(cfg.tz, TZ_ZONES[i].posix, sizeof(cfg.tz));
    settings_save();
    clock_apply();
}

static void c24_cb(lv_event_t* e) {
    cfg.clock24 = lv_obj_has_state(lv_event_get_target(e), LV_STATE_CHECKED);
    settings_save();
}

static void sync_cb(lv_event_t*) {
    clock_apply();
    ui_toast("Syncing time...");
}

static void time_update() {
    char buf[64];
    if (clock_valid()) {
        time_t t = time(nullptr);
        struct tm tmv;
        localtime_r(&t, &tmv);
        strftime(buf, sizeof(buf), cfg.clock24 ? "%H:%M:%S" : "%l:%M:%S %p", &tmv);
        set_text(tmClock, buf[0] == ' ' ? buf + 1 : buf);
        strftime(buf, sizeof(buf), "%A, %d %B %Y", &tmv);
        set_text(tmDate, buf);
        snprintf(buf, sizeof(buf), LV_SYMBOL_OK " Synced with %s", cfg.ntp);
        set_text(tmStatus, buf);
        set_text_color(tmStatus, COL_GREEN);
    } else {
        set_text(tmClock, "--:--");
        set_text(tmDate, "");
        snprintf(buf, sizeof(buf), g.wifi ? "Waiting for %s..." : "No Wi-Fi - can't sync", cfg.ntp);
        set_text(tmStatus, buf);
        set_text_color(tmStatus, COL_ORANGE);
    }
}

void scr_time(lv_obj_t* p) {
    const bool P = PORTRAIT;
    // everything typed (NTP server) sits above the keyboard; the zone dropdown may sit below it
    const lv_coord_t cw = P ? CONTENT_W - U(12) : U(230);
    lv_obj_t* c = ui_card(p, U(6), U(6), cw, P ? U(96) : U(118));
    tmClock = ui_label(c, "", FONT(28));
    tmDate = ui_label(c, "", FONT(14), COL_DIM);
    lv_obj_set_pos(tmDate, 0, U(36));
    tmStatus = ui_label(c, "", FONT(12), COL_DIM);
    lv_label_set_long_mode(tmStatus, LV_LABEL_LONG_DOT);
    lv_obj_set_width(tmStatus, cw - U(16));
    lv_obj_set_pos(tmStatus, 0, P ? U(60) : U(62));

    const lv_coord_t nx = P ? U(6) : U(244), ny = P ? U(108) : U(2), nw = P ? CONTENT_W - U(12) : U(230);
    lv_obj_t* nl = ui_label(p, "NTP server", FONT(12), COL_DIM);
    lv_obj_set_pos(nl, nx + U(2), ny);
    tmNtp = make_ta(p, "pool.ntp.org", cfg.ntp, nw);
    lv_obj_set_pos(tmNtp, nx, ny + U(18));
    lv_obj_add_event_cb(tmNtp, time_ta_evt, LV_EVENT_ALL, nullptr);
    lv_obj_t* r = ui_row(p, nw, U(36));
    lv_obj_set_pos(r, nx, ny + U(60));
    lv_obj_t* sb = ui_btn(r, LV_SYMBOL_REFRESH " Sync now", U(116), U(34), sync_cb, nullptr, COL_ACCENT);
    lv_obj_set_style_text_font(sb, FONT(14), 0);
    ui_label(r, "24 h", FONT(14), COL_DIM);
    tm24 = lv_switch_create(r);
    lv_obj_set_size(tm24, U(50), U(26));
    if (cfg.clock24) lv_obj_add_state(tm24, LV_STATE_CHECKED);
    lv_obj_add_event_cb(tm24, c24_cb, LV_EVENT_VALUE_CHANGED, nullptr);

    const lv_coord_t zy = P ? U(214) : U(130);
    lv_obj_t* zc = ui_card(p, U(6), zy, CONTENT_W - U(12), P ? U(96) : U(84));
    ui_label(zc, "Time zone", FONT(12), COL_DIM);
    tmZone = lv_dropdown_create(zc);
    String opts;
    for (int i = 0; i < TZ_ZONE_COUNT; i++) opts += String(i ? "\n" : "") + TZ_ZONES[i].name;
    int zi = clock_zone_index();
    if (zi < 0) opts += String("\nCustom: ") + cfg.tz;
    lv_dropdown_set_options(tmZone, opts.c_str());
    lv_dropdown_set_selected(tmZone, zi >= 0 ? zi : TZ_ZONE_COUNT);
    lv_obj_set_style_text_font(tmZone, FONT(14), 0);
    lv_obj_set_style_max_height(lv_dropdown_get_list(tmZone), U(200), 0);
    lv_obj_set_size(tmZone, CONTENT_W - U(28), U(38));
    lv_obj_set_pos(tmZone, 0, U(18));
    lv_obj_add_event_cb(tmZone, zone_cb, LV_EVENT_VALUE_CHANGED, nullptr);

    tmKb = make_kb(p, time_kb_evt);
    ui_set_update(time_update);
}
