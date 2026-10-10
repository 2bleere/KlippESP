// Console, settings menu, connection (WiFi + printer address) and system screens.
#include "ui.h"
#include "board.h"
#include <WiFi.h>

#define FW_VERSION "1.0.0"

#define KB_H 150  // on-screen keyboard height; inputs must stay above CONTENT_H - KB_H

static lv_obj_t* make_kb(lv_obj_t* p, lv_event_cb_t cb) {
    lv_obj_t* kb = lv_keyboard_create(p);
    lv_obj_set_size(kb, CONTENT_W, KB_H);
    lv_obj_set_style_text_font(kb, &lv_font_montserrat_14, LV_PART_ITEMS);
    lv_obj_set_style_pad_all(kb, 3, 0);
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
    lv_obj_set_height(ta, 36);
    lv_obj_set_style_text_font(ta, &lv_font_montserrat_14, 0);
    lv_obj_set_style_pad_ver(ta, 8, 0);
    return ta;
}

// ================================================================ CONSOLE

static lv_obj_t *cLogBox, *cLog, *cRow, *cTa, *cKb;
static uint32_t cVer = 0;

static void console_layout(bool kb) {
    if (kb) {
        lv_obj_set_height(cLogBox, CONTENT_H - KB_H - 54);
        lv_obj_set_y(cRow, CONTENT_H - KB_H - 42);
        lv_obj_clear_flag(cKb, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_set_height(cLogBox, CONTENT_H - 54);
        lv_obj_set_y(cRow, CONTENT_H - 42);
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
    lv_obj_set_pos(cLogBox, 6, 6);
    lv_obj_set_size(cLogBox, CONTENT_W - 12, CONTENT_H - 54);
    set_bg_color(cLogBox, COL_CARD, 0);
    lv_obj_set_style_border_width(cLogBox, 0, 0);
    lv_obj_set_style_pad_all(cLogBox, 6, 0);
    lv_obj_set_scroll_dir(cLogBox, LV_DIR_VER);
    cLog = ui_label(cLogBox, "Loading...", &lv_font_montserrat_12);
    lv_label_set_long_mode(cLog, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(cLog, CONTENT_W - 34);

    cRow = ui_row(p, CONTENT_W - 12, 36);
    lv_obj_set_pos(cRow, 6, CONTENT_H - 42);
    cTa = make_ta(cRow, "G-code, e.g. G28 or QUERY_PROBE", "", CONTENT_W - 12 - 84);
    lv_obj_add_event_cb(cTa, console_ta_evt, LV_EVENT_ALL, nullptr);
    lv_obj_t* sb = ui_btn(cRow, "Send", 78, 36, console_send_cb, nullptr, COL_ACCENT);
    lv_obj_set_style_text_font(sb, &lv_font_montserrat_14, 0);

    cKb = make_kb(p, console_kb_evt);
    cVer = 0;
    mr_console_active(true);
    ui_set_update(console_update);
}

// ================================================================ SETTINGS MENU

static void goto_cb(lv_event_t* e) { ui_goto((Scr)(intptr_t)lv_event_get_user_data(e)); }

void scr_settings(lv_obj_t* p) {
    lv_obj_t* g2 = lv_obj_create(p);
    lv_obj_set_pos(g2, 6, 6);
    lv_obj_set_size(g2, CONTENT_W - 12, PORTRAIT ? 236 : 120);
    lv_obj_set_style_bg_opa(g2, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(g2, 0, 0);
    lv_obj_set_style_pad_all(g2, 0, 0);
    lv_obj_set_style_pad_column(g2, 5, 0);
    lv_obj_set_style_pad_row(g2, 5, 0);
    lv_obj_set_flex_flow(g2, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_clear_flag(g2, LV_OBJ_FLAG_SCROLLABLE);
    const lv_coord_t w = PORTRAIT ? (CONTENT_W - 17) / 2 : 113;
    ui_tile(g2, LV_SYMBOL_WIFI, "Connection", w, 110, goto_cb, (void*)(intptr_t)S_CONNECT, COL_ACCENT);
    ui_tile(g2, LV_SYMBOL_SETTINGS, "System", w, 110, goto_cb, (void*)(intptr_t)S_SYSTEM);
    ui_tile(g2, LV_SYMBOL_POWER, "Power devices", w, 110, goto_cb, (void*)(intptr_t)S_POWER);
    ui_tile(g2, LV_SYMBOL_KEYBOARD, "Console", w, 110, goto_cb, (void*)(intptr_t)S_CONSOLE);
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
    const lv_coord_t sw = PORTRAIT ? 300 : 440, sh = PORTRAIT ? 420 : 296;
    lv_obj_t* c = ui_card(kScanOv, 0, 0, sw, sh);
    lv_obj_center(c);
    ui_label(c, n > 0 ? "Select network" : "No networks found", &lv_font_montserrat_16);
    lv_obj_t* close = ui_btn(c, LV_SYMBOL_CLOSE, 44, 32, scan_close);
    lv_obj_align(close, LV_ALIGN_TOP_RIGHT, 0, -4);

    lv_obj_t* list = lv_obj_create(c);
    lv_obj_set_pos(list, 0, 36);
    lv_obj_set_size(list, sw - 16, sh - 52);
    lv_obj_set_style_bg_opa(list, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(list, 0, 0);
    lv_obj_set_style_pad_all(list, 0, 0);
    lv_obj_set_style_pad_row(list, 6, 0);
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);

    int shown = 0;
    for (int i = 0; i < n && shown < 25; i++) {
        String s = WiFi.SSID(i);
        if (!s.length()) continue;
        bool dup = false;
        for (int j = 0; j < i; j++)
            if (WiFi.SSID(j) == s) dup = true;
        if (dup) continue;
        lv_obj_t* b = ui_btn(list, s.c_str(), sw - 26, 40, scan_pick);
        lv_obj_set_style_text_font(b, &lv_font_montserrat_14, 0);
        lv_obj_align(lv_obj_get_child(b, 0), LV_ALIGN_LEFT_MID, 8, 0);
        char r[32];
        snprintf(r, sizeof(r), "%d dBm%s", WiFi.RSSI(i), WiFi.encryptionType(i) == WIFI_AUTH_OPEN ? "  open" : "");
        lv_obj_t* rl = ui_label(b, r, &lv_font_montserrat_12, COL_DIM);
        lv_obj_align(rl, LV_ALIGN_RIGHT_MID, -8, 0);
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
    const lv_coord_t colW = P ? CONTENT_W - 12 : 230;
    const lv_coord_t x2 = P ? 6 : 244, y2 = P ? 120 : 0;  // printer column origin
    lv_obj_t* l = ui_label(p, LV_SYMBOL_WIFI " Wi-Fi", &lv_font_montserrat_14, COL_DIM);
    lv_obj_set_pos(l, 8, 2);
    lv_obj_t* r1 = ui_row(p, colW, 36);
    lv_obj_set_pos(r1, 6, 20);
    lv_obj_set_style_pad_column(r1, 4, 0);
    kSsid = make_ta(r1, "Network (SSID)", cfg.ssid, colW - 48);
    kScanBtn = ui_btn(r1, LV_SYMBOL_REFRESH, 44, 36, scan_cb);
    lv_obj_t* r2 = ui_row(p, colW, 36);
    lv_obj_set_pos(r2, 6, 60);
    lv_obj_set_style_pad_column(r2, 4, 0);
    kPass = make_ta(r2, "Password", cfg.pass, colW - 48);
    lv_textarea_set_password_mode(kPass, true);
    ui_btn(r2, LV_SYMBOL_EYE_OPEN, 44, 36, eye_cb);
    kWifiSt = ui_label(p, "", &lv_font_montserrat_12, COL_DIM);
    lv_obj_set_pos(kWifiSt, 8, 100);
    lv_obj_set_width(kWifiSt, colW - 2);
    lv_label_set_long_mode(kWifiSt, LV_LABEL_LONG_DOT);

    lv_obj_t* l2 = ui_label(p, LV_SYMBOL_SETTINGS " Printer (Moonraker)", &lv_font_montserrat_14, COL_DIM);
    lv_obj_set_pos(l2, x2 + 2, y2 + 2);
    kHost = make_ta(p, "Printer IP or hostname", cfg.host, colW);
    lv_obj_set_pos(kHost, x2, y2 + 20);
    lv_obj_t* r3 = ui_row(p, colW, 36);
    lv_obj_set_pos(r3, x2, y2 + 60);
    lv_obj_set_style_pad_column(r3, 4, 0);
    char port[8];
    snprintf(port, sizeof(port), "%u", cfg.port);
    kPort = make_ta(r3, "Port", port, 70);
    lv_textarea_set_accepted_chars(kPort, "0123456789");
    kKey = make_ta(r3, "API key (optional)", cfg.apikey, colW - 74);
    kMrSt = ui_label(p, "", &lv_font_montserrat_12, COL_DIM);
    lv_obj_set_pos(kMrSt, x2 + 2, y2 + 100);
    lv_obj_set_width(kMrSt, colW - 2);
    lv_label_set_long_mode(kMrSt, LV_LABEL_LONG_DOT);

    lv_obj_t* tas[] = {kSsid, kPass, kHost, kPort, kKey};
    for (lv_obj_t* ta : tas) lv_obj_add_event_cb(ta, conn_ta_evt, LV_EVENT_ALL, nullptr);

    lv_obj_t* c = ui_card(p, 6, CONTENT_H - KB_H, CONTENT_W - 12, KB_H - 6);
    lv_obj_t* hint = ui_label(c,
        P ? "Moonraker port is usually 7125 (80 via\nMainsail). .local names use mDNS. API key\nonly if not a trusted client."
          : "Moonraker usually listens on port 7125 (port 80 if it\n"
            "only works through Mainsail). .local names use mDNS.\n"
            "API key only needed if this display isn't a trusted client.",
        &lv_font_montserrat_12, COL_DIM);
    lv_obj_set_pos(hint, 0, 0);
    lv_obj_t* sb = ui_btn(c, LV_SYMBOL_SAVE "  Save & Connect", 190, 44, save_cb, nullptr, COL_GREEN);
    lv_obj_set_style_text_font(sb, &lv_font_montserrat_14, 0);
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
    snprintf(buf, sizeof(buf), "KlippESP %s\nIP %s  %d dBm\nPrinter %s:%u\nKlipper %s\nFree heap %u KB%s",
             FW_VERSION, WiFi.localIP().toString().c_str(), WiFi.RSSI(), cfg.host, cfg.port,
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

static void bri_cb(lv_event_t* e) {
    lv_obj_t* s = lv_event_get_target(e);
    cfg.brightness = lv_slider_get_value(s);
    board_set_brightness(cfg.brightness);
    if (lv_event_get_code(e) == LV_EVENT_RELEASED) settings_save();
}

void scr_system(lv_obj_t* p) {
    const bool P = PORTRAIT;
    // info + screen options card
    const lv_coord_t cw = P ? CONTENT_W - 12 : 236, ch = P ? 200 : CONTENT_H - 12, iw = cw - 16;
    lv_obj_t* c = ui_card(p, 6, 6, cw, ch);
    sysInfo = ui_label(c, "", &lv_font_montserrat_12);
    lv_label_set_long_mode(sysInfo, LV_LABEL_LONG_DOT);
    lv_obj_set_size(sysInfo, iw, ch - 16 - 56);
    const lv_coord_t dw = (iw - 6) / 2, dy = ch - 16 - 34;
    lv_obj_t* sl = ui_label(c, "Screen off", &lv_font_montserrat_12, COL_DIM);
    lv_obj_set_pos(sl, 0, dy - 18);
    lv_obj_t* ol = ui_label(c, "Orientation", &lv_font_montserrat_12, COL_DIM);
    lv_obj_set_pos(ol, dw + 6, dy - 18);
    sysSleep = lv_dropdown_create(c);
    lv_dropdown_set_options(sysSleep, "Never\n1 min\n5 min\n15 min\n30 min\n60 min");
    lv_obj_set_style_text_font(sysSleep, &lv_font_montserrat_14, 0);
    lv_obj_set_size(sysSleep, dw, 34);
    lv_obj_set_pos(sysSleep, 0, dy);
    for (int i = 0; i < 6; i++)
        if (sleepOpts[i] == cfg.sleepMin) lv_dropdown_set_selected(sysSleep, i);
    lv_obj_add_event_cb(sysSleep, sleep_cb, LV_EVENT_VALUE_CHANGED, nullptr);
    lv_obj_t* rot = lv_dropdown_create(c);
    lv_dropdown_set_options(rot, "Landscape\nPortrait\nLandscape 180\nPortrait 180");
    lv_obj_set_style_text_font(rot, &lv_font_montserrat_14, 0);
    lv_obj_set_size(rot, dw, 34);
    lv_obj_set_pos(rot, dw + 6, dy);
    lv_dropdown_set_selected(rot, cfg.rotation & 3);
    lv_obj_add_event_cb(rot, rot_cb, LV_EVENT_VALUE_CHANGED, nullptr);

    // brightness + actions
    const lv_coord_t rx = P ? 6 : 248, ry = P ? 212 : 6, rw = P ? CONTENT_W - 12 : 226;
    lv_obj_t* r = lv_obj_create(p);
    lv_obj_set_pos(r, rx, ry);
    lv_obj_set_size(r, rw, CONTENT_H - 6 - ry);
    lv_obj_set_style_bg_opa(r, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(r, 0, 0);
    lv_obj_set_style_pad_all(r, 0, 0);
    lv_obj_set_style_pad_row(r, 4, 0);
    lv_obj_set_style_pad_column(r, 4, 0);
    lv_obj_set_flex_flow(r, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_clear_flag(r, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* br = ui_row(r, rw, 32);
    ui_label(br, LV_SYMBOL_IMAGE, &lv_font_montserrat_14, COL_DIM);
    lv_obj_t* bs = lv_slider_create(br);
    lv_obj_set_size(bs, rw - 36, 10);
    lv_obj_set_ext_click_area(bs, 14);
    lv_slider_set_range(bs, 10, 255);
    lv_slider_set_value(bs, cfg.brightness, LV_ANIM_OFF);
    lv_obj_add_event_cb(bs, bri_cb, LV_EVENT_VALUE_CHANGED, nullptr);
    lv_obj_add_event_cb(bs, bri_cb, LV_EVENT_RELEASED, nullptr);

    const lv_coord_t bw = P ? (rw - 4) / 2 : rw;
    auto act = [&](const char* t, lv_event_cb_t cb, void* ud, lv_color_t col) {
        lv_obj_t* b = ui_btn(r, t, bw, 34, cb, ud, col);
        lv_obj_set_style_text_font(b, &lv_font_montserrat_14, 0);
    };
    act(LV_SYMBOL_REFRESH " Restart Klipper", post_cb, (void*)"/printer/restart", COL_BTN);
    act(LV_SYMBOL_REFRESH " Firmware Restart", post_cb, (void*)"/printer/firmware_restart", COL_ORANGE);
    act(LV_SYMBOL_REFRESH " Restart Moonraker", post_cb, (void*)"/server/restart", COL_BTN);
    act(LV_SYMBOL_POWER " Reboot Host", reboot_host_cb, nullptr, COL_RED);
    act(LV_SYMBOL_POWER " Shutdown Host", shutdown_host_cb, nullptr, COL_RED);
    act("Restart Display", reboot_self_cb, nullptr, COL_BTN);
    ui_set_update(system_update);
}
