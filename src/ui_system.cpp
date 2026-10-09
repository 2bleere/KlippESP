// Console, settings menu, connection (WiFi + printer address) and system screens.
#include "ui.h"
#include "board.h"
#include <WiFi.h>

#define FW_VERSION "1.0.0"

static lv_obj_t* make_kb(lv_obj_t* p, lv_event_cb_t cb) {
    lv_obj_t* kb = lv_keyboard_create(p);
    lv_obj_set_size(kb, CONTENT_W, 220);
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
    lv_obj_set_height(ta, 50);
    return ta;
}

// ================================================================ CONSOLE

static lv_obj_t *cLogBox, *cLog, *cRow, *cTa, *cKb;
static uint32_t cVer = 0;

static void console_layout(bool kb) {
    if (kb) {
        lv_obj_set_height(cLogBox, 120);
        lv_obj_set_y(cRow, 134);
        lv_obj_clear_flag(cKb, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_set_height(cLogBox, 340);
        lv_obj_set_y(cRow, 356);
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
    lv_obj_set_pos(cLogBox, 8, 8);
    lv_obj_set_size(cLogBox, 784, 340);
    set_bg_color(cLogBox, COL_CARD, 0);
    lv_obj_set_style_border_width(cLogBox, 0, 0);
    lv_obj_set_scroll_dir(cLogBox, LV_DIR_VER);
    cLog = ui_label(cLogBox, "Loading...", &lv_font_montserrat_14);
    lv_label_set_long_mode(cLog, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(cLog, 750);

    cRow = ui_row(p, 784, 60);
    lv_obj_set_pos(cRow, 8, 356);
    cTa = make_ta(cRow, "Enter G-code (e.g. G28, M115, QUERY_PROBE)", "", 640);
    lv_obj_add_event_cb(cTa, console_ta_evt, LV_EVENT_ALL, nullptr);
    ui_btn(cRow, "Send", 130, 50, console_send_cb, nullptr, COL_ACCENT);

    cKb = make_kb(p, console_kb_evt);
    cVer = 0;
    mr_console_active(true);
    ui_set_update(console_update);
}

// ================================================================ SETTINGS MENU

static void goto_cb(lv_event_t* e) { ui_goto((Scr)(intptr_t)lv_event_get_user_data(e)); }

void scr_settings(lv_obj_t* p) {
    lv_obj_t* g2 = ui_row(p, 784, 200);
    lv_obj_set_pos(g2, 8, 8);
    lv_obj_set_style_pad_column(g2, 12, 0);
    ui_tile(g2, LV_SYMBOL_WIFI, "Connection", 187, 160, goto_cb, (void*)(intptr_t)S_CONNECT, COL_ACCENT);
    ui_tile(g2, LV_SYMBOL_SETTINGS, "System", 187, 160, goto_cb, (void*)(intptr_t)S_SYSTEM);
    ui_tile(g2, LV_SYMBOL_POWER, "Power devices", 187, 160, goto_cb, (void*)(intptr_t)S_POWER);
    ui_tile(g2, LV_SYMBOL_KEYBOARD, "Console", 187, 160, goto_cb, (void*)(intptr_t)S_CONSOLE);
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
    lv_obj_set_size(kScanOv, LCD_W, LCD_H);
    set_bg_color(kScanOv, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(kScanOv, LV_OPA_60, 0);
    lv_obj_set_style_border_width(kScanOv, 0, 0);
    lv_obj_set_style_radius(kScanOv, 0, 0);
    lv_obj_t* c = ui_card(kScanOv, 0, 0, 560, 440);
    lv_obj_center(c);
    ui_label(c, n > 0 ? "Select network" : "No networks found", &lv_font_montserrat_20);
    lv_obj_t* close = ui_btn(c, LV_SYMBOL_CLOSE, 56, 44, scan_close);
    lv_obj_align(close, LV_ALIGN_TOP_RIGHT, 0, -6);

    lv_obj_t* list = lv_obj_create(c);
    lv_obj_set_pos(list, 0, 46);
    lv_obj_set_size(list, 540, 374);
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
        lv_obj_t* b = ui_btn(list, s.c_str(), 530, 56, scan_pick);
        lv_obj_align(lv_obj_get_child(b, 0), LV_ALIGN_LEFT_MID, 8, 0);
        char r[32];
        snprintf(r, sizeof(r), "%d dBm%s", WiFi.RSSI(i), WiFi.encryptionType(i) == WIFI_AUTH_OPEN ? "  open" : "");
        lv_obj_t* rl = ui_label(b, r, &lv_font_montserrat_14, COL_DIM);
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
    // Everything that needs typing sits in the top 200 px so the keyboard never hides it.
    lv_obj_t* l = ui_label(p, LV_SYMBOL_WIFI " Wi-Fi", &lv_font_montserrat_20, COL_DIM);
    lv_obj_set_pos(l, 12, 6);
    lv_obj_t* r1 = ui_row(p, 388, 50);
    lv_obj_set_pos(r1, 8, 36);
    kSsid = make_ta(r1, "Network name (SSID)", cfg.ssid, 300);
    kScanBtn = ui_btn(r1, LV_SYMBOL_REFRESH, 80, 50, scan_cb);
    lv_obj_t* r2 = ui_row(p, 388, 50);
    lv_obj_set_pos(r2, 8, 94);
    kPass = make_ta(r2, "Password", cfg.pass, 300);
    lv_textarea_set_password_mode(kPass, true);
    ui_btn(r2, LV_SYMBOL_EYE_OPEN, 80, 50, eye_cb);
    kWifiSt = ui_label(p, "", &lv_font_montserrat_16, COL_DIM);
    lv_obj_set_pos(kWifiSt, 12, 158);
    lv_obj_set_width(kWifiSt, 384);
    lv_label_set_long_mode(kWifiSt, LV_LABEL_LONG_DOT);

    lv_obj_t* l2 = ui_label(p, LV_SYMBOL_SETTINGS " Printer (Moonraker)", &lv_font_montserrat_20, COL_DIM);
    lv_obj_set_pos(l2, 416, 6);
    kHost = make_ta(p, "Printer IP or hostname", cfg.host, 380);
    lv_obj_set_pos(kHost, 412, 36);
    lv_obj_t* r3 = ui_row(p, 380, 50);
    lv_obj_set_pos(r3, 412, 94);
    char port[8];
    snprintf(port, sizeof(port), "%u", cfg.port);
    kPort = make_ta(r3, "Port", port, 110);
    lv_textarea_set_accepted_chars(kPort, "0123456789");
    kKey = make_ta(r3, "API key (optional)", cfg.apikey, 262);
    kMrSt = ui_label(p, "", &lv_font_montserrat_16, COL_DIM);
    lv_obj_set_pos(kMrSt, 416, 158);
    lv_obj_set_width(kMrSt, 376);
    lv_label_set_long_mode(kMrSt, LV_LABEL_LONG_DOT);

    lv_obj_t* tas[] = {kSsid, kPass, kHost, kPort, kKey};
    for (lv_obj_t* ta : tas) lv_obj_add_event_cb(ta, conn_ta_evt, LV_EVENT_ALL, nullptr);

    lv_obj_t* c = ui_card(p, 8, 200, 784, 216);
    lv_obj_t* hint = ui_label(c,
        "Moonraker usually listens on port 7125. If it only works through Mainsail, use port 80.\n"
        "Hostnames ending in .local are resolved via mDNS. The API key is only needed if\n"
        "the display's IP is not in Moonraker's trusted_clients.",
        &lv_font_montserrat_16, COL_DIM);
    lv_obj_set_pos(hint, 0, 0);
    lv_obj_t* sb = ui_btn(c, LV_SYMBOL_SAVE "  Save & Connect", 320, 70, save_cb, nullptr, COL_GREEN);
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
                 "\n\n%s\nCPU  %.0f%%   Temp %s   Up %uh\nMemory  %u / %u MB\nDisk  %.1f / %.1f GB%s%s",
                 h.model[0] ? h.model : "Host", h.cpu, temp, (unsigned)(h.uptime / 3600),
                 (unsigned)(h.memUsed / 1024), (unsigned)(h.memTotal / 1024), h.diskUsed / 1e9, h.diskTotal / 1e9,
                 h.throttled ? "\n" LV_SYMBOL_WARNING " " : "", h.throttled ? h.flags : "");
    }
    snprintf(buf, sizeof(buf),
             "Display firmware  %s\nIP  %s   RSSI %d dBm\nMoonraker  %s:%u  (%s)\nKlipper  %s\n"
             "Free heap  %u KB   PSRAM %u KB%s",
             FW_VERSION, WiFi.localIP().toString().c_str(), WiFi.RSSI(), cfg.host, cfg.port,
             g.online ? "online" : "offline", g.klippy,
             (unsigned)(ESP.getFreeHeap() / 1024), (unsigned)(ESP.getFreePsram() / 1024), host);
    set_text(sysInfo, buf);
}

void scr_system(lv_obj_t* p) {
    lv_obj_t* c = ui_card(p, 8, 8, 390, 408);
    sysInfo = ui_label(c, "", &lv_font_montserrat_16);
    lv_obj_set_style_text_line_space(sysInfo, 6, 0);
    lv_obj_set_style_text_font(sysInfo, &lv_font_montserrat_14, 0);
    lv_obj_t* sl = ui_label(c, "Screen off after", &lv_font_montserrat_16, COL_DIM);
    lv_obj_set_pos(sl, 0, 290);
    sysSleep = lv_dropdown_create(c);
    lv_dropdown_set_options(sysSleep, "Never\n1 minute\n5 minutes\n15 minutes\n30 minutes\n60 minutes");
    lv_obj_set_width(sysSleep, 370);
    lv_obj_set_pos(sysSleep, 0, 318);
    for (int i = 0; i < 6; i++)
        if (sleepOpts[i] == cfg.sleepMin) lv_dropdown_set_selected(sysSleep, i);
    lv_obj_add_event_cb(sysSleep, sleep_cb, LV_EVENT_VALUE_CHANGED, nullptr);

    lv_obj_t* r = lv_obj_create(p);
    lv_obj_set_pos(r, 406, 8);
    lv_obj_set_size(r, 386, 408);
    lv_obj_set_style_bg_opa(r, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(r, 0, 0);
    lv_obj_set_style_pad_all(r, 0, 0);
    lv_obj_set_style_pad_row(r, 8, 0);
    lv_obj_set_flex_flow(r, LV_FLEX_FLOW_COLUMN);
    lv_obj_clear_flag(r, LV_OBJ_FLAG_SCROLLABLE);
    ui_btn(r, LV_SYMBOL_REFRESH " Restart Klipper", 386, 60, post_cb, (void*)"/printer/restart");
    ui_btn(r, LV_SYMBOL_REFRESH " Firmware Restart", 386, 60, post_cb, (void*)"/printer/firmware_restart", COL_ORANGE);
    ui_btn(r, LV_SYMBOL_REFRESH " Restart Moonraker", 386, 60, post_cb, (void*)"/server/restart");
    ui_btn(r, LV_SYMBOL_POWER " Reboot Host", 386, 60, reboot_host_cb, nullptr, COL_RED);
    ui_btn(r, LV_SYMBOL_POWER " Shutdown Host", 386, 60, shutdown_host_cb, nullptr, COL_RED);
    ui_btn(r, "Restart Display", 386, 52, reboot_self_cb);
    ui_set_update(system_update);
}
