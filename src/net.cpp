#include "net.h"
#include <lvgl.h>
#include "settings.h"
#include "moonraker.h"
#include "ui.h"
#include "clock.h"
#include "board.h"
#include "web_logo.h"
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <Update.h>
#include <ArduinoOTA.h>

static WebServer server(80);
static DNSServer dns;
static bool apActive = false;
static uint32_t connectStart = 0;
static uint32_t lastStaTry = 0;
static uint32_t lastConnected = 0;  // last time the STA link was up (or a connect attempt started)

#define AP_FALLBACK_MS 60000   // start the setup hotspot after this long without Wi-Fi
#define STA_RETRY_MS 15000     // nudge reconnects while disconnected (auto-reconnect can give up)

static void logln(const String& s) { Serial.println(s); }

static void log_status(const char* tag) {
    lv_mem_monitor_t mon;
    lv_mem_monitor(&mon);
    char b[260];
    snprintf(b, sizeof(b), "[%s] up %lus wifi=%s rssi=%d ip=%s ap=%d heap=%u min=%u blk=%u lvgl=%u%% frag=%u%%", tag,
             (unsigned long)(millis() / 1000), WiFi.status() == WL_CONNECTED ? "ok" : "down", WiFi.RSSI(),
             WiFi.localIP().toString().c_str(), apActive, (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getMinFreeHeap(),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT), mon.used_pct, mon.frag_pct);
    logln(b);
}
static String ssidList;  // cached scan results as HTML buttons
static int ssidCount = -1;
static uint32_t scannedAt = 0;

static String esc(const char* s);

static void scan_networks() {
    int n = WiFi.scanNetworks();  // blocking, ~2-5 s; results are sorted by signal strength
    String o;
    int count = 0;
    for (int i = 0; i < n && count < 20; i++) {
        String s = WiFi.SSID(i);
        if (!s.length() || o.indexOf("data-s=\"" + esc(s.c_str()) + "\"") >= 0) continue;
        int q = constrain(2 * (WiFi.RSSI(i) + 100), 0, 100);  // rough signal quality %
        o += "<button type=button class=net data-s=\"" + esc(s.c_str()) + "\" onclick=\"pick(this)\"><span>" +
             esc(s.c_str()) + "</span><small>" + String(q) + "%" + (WiFi.encryptionType(i) == WIFI_AUTH_OPEN ? " open" : "") +
             "</small></button>";
        count++;
    }
    WiFi.scanDelete();
    ssidList = o;
    ssidCount = n < 0 ? -1 : count;
    scannedAt = millis();
}

bool net_ap_active() { return apActive; }

static void start_ap() {
    if (apActive) return;
    // Stop STA retries while the hotspot is up: every retry hops channels and drops the phone.
    WiFi.setAutoReconnect(false);
    WiFi.disconnect();
    WiFi.mode(WIFI_AP_STA);
    scan_networks();
    WiFi.softAP(SETUP_AP_SSID);
    dns.start(53, "*", WiFi.softAPIP());  // answer every lookup with our IP -> captive portal
    apActive = true;
    lastStaTry = millis();
    Serial.printf("Setup hotspot '%s' started, open http://%s\n", SETUP_AP_SSID, WiFi.softAPIP().toString().c_str());
}

static void stop_ap() {
    if (!apActive) return;
    dns.stop();
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(true);
    apActive = false;
    Serial.println("Setup hotspot stopped");
}

void net_reconnect() {
    connectStart = lastStaTry = lastConnected = millis();
    if (!cfg.ssid[0]) return;
    WiFi.disconnect();
    WiFi.begin(cfg.ssid, cfg.pass);
}

static String esc(const char* s) {
    String o;
    for (; *s; s++) {
        switch (*s) {
            case '&': o += "&amp;"; break;
            case '<': o += "&lt;"; break;
            case '>': o += "&gt;"; break;
            case '"': o += "&quot;"; break;
            default: o += *s;
        }
    }
    return o;
}

// Phones/PCs probe a known URL after joining a network (generate_204, hotspot-detect.html,
// connecttest.txt, ...). Redirecting anything not addressed to us makes them open the portal.
static bool captive_redirect() {
    if (!apActive) return false;
    String host = server.hostHeader();
    if (host == WiFi.softAPIP().toString() || host == WiFi.localIP().toString()) return false;
    server.sendHeader("Location", "http://" + WiFi.softAPIP().toString() + "/", true);
    server.send(302, "text/plain", "");
    return true;
}

// network list shown inside the picker dialog
static String nets_html() {
    String h = "<p><small>";
    if (ssidCount < 0) h += "No scan yet.";
    else h += String(ssidCount) + " networks found, scanned " + String((millis() - scannedAt) / 1000) + " s ago.";
    return h + "</small></p>" + ssidList;
}

static void handle_scan() {
    scan_networks();
    server.send(200, "text/html", nets_html());
}

static void handle_root() {
    if (captive_redirect()) return;
    String h = F("<!doctype html><html><head><meta charset=utf-8><meta name=viewport content='width=device-width,initial-scale=1'>"
                 "<title>KlippESP</title><link rel=icon type=image/png href=/logo.png><style>body{font-family:sans-serif;max-width:480px;margin:24px auto;padding:0 16px;"
                 "background:#0d1117;color:#e6edf3}label{display:block;margin-top:14px;color:#8b949e}input{width:100%;padding:10px;"
                 "box-sizing:border-box;font-size:16px;background:#1c2330;color:#e6edf3;border:1px solid #30363d;border-radius:6px}"
                 "button{margin-top:20px;width:100%;padding:12px;font-size:17px;background:#2ea043;color:#fff;border:0;border-radius:6px}"
                 "select{width:100%;padding:10px;font-size:16px;background:#1c2330;color:#e6edf3;border:1px solid #30363d;border-radius:6px}"
                 "small{color:#8b949e}.row{display:flex;gap:8px}.row button{margin:0;width:auto;white-space:nowrap;"
                 "background:#263143;font-size:15px}.net{display:flex;justify-content:space-between;margin:6px 0 0;padding:12px;"
                 "font-size:16px;background:#263143;text-align:left}.net small{color:#8b949e;margin-left:12px}"
                 "dialog{background:#1c2330;color:#e6edf3;border:0;border-radius:12px;width:min(440px,90vw);max-height:80vh;"
                 "padding:18px}dialog::backdrop{background:rgba(0,0,0,.6)}dialog h3{margin:0 0 4px}#nl{overflow-y:auto;"
                 "max-height:50vh}.btns{display:flex;gap:8px}.btns button{background:#263143}.btns #rs{background:#2f81f7}"
                 "</style><script>"
                 "function $(i){return document.getElementById(i)}"
                 "function pick(b){$('ssid').value=b.dataset.s;$('dlg').close();$('pass').focus()}"
                 "function rescan(){var b=$('rs');b.disabled=true;b.textContent='Scanning\u2026 (about 5 s)';"
                 "fetch('/scan').then(function(r){return r.text()}).then(function(t){$('nl').innerHTML=t})"
                 ".catch(function(){$('nl').innerHTML='<p>Scan failed, try again.</p>'})"
                 ".finally(function(){b.disabled=false;b.textContent='Rescan'})}"
                 "function nets(){$('dlg').showModal();if(!$('nl').querySelector('.net'))rescan()}"
                 "</script></head><body><div style='text-align:center'><img src=/logo.png width=128 height=128 alt='' "
                 "style='border-radius:18px'></div><h2 style='text-align:center'>KlippESP settings</h2><form method=post action=/save>");
    h += "<label>Wi-Fi network (SSID)</label><div class=row><input name=ssid id=ssid value=\"" + esc(cfg.ssid) +
         "\"><button type=button onclick=nets()>Choose&hellip;</button></div>";
    h += F("<label>Wi-Fi password</label><input name=pass id=pass type=password placeholder='(unchanged)'>");
    h += "<label>Printer IP or hostname</label><input name=host value=\"" + esc(cfg.host) + "\" placeholder='192.168.1.50'>";
    h += "<label>Moonraker port</label><input name=port type=number value=\"" + String(cfg.port) + "\">";
    h += F("<label>API key (optional)</label><input name=apikey type=password placeholder='(unchanged)'>");
    h += F("<label>Display orientation</label><select name=rot>");
    static const char* rots[] = {"Landscape", "Portrait", "Landscape (upside down)", "Portrait (upside down)"};
    for (int i = 0; i < 4; i++)
        h += "<option value=" + String(i) + (cfg.rotation == i ? " selected>" : ">") + rots[i] + "</option>";
    h += F("</select><small>Changing it restarts the display.</small>");
    h += "<label>Brightness</label><input type=range name=bri min=10 max=255 value=" + String(cfg.brightness) + ">";
    // date & time
    int zi = clock_zone_index();
    h += F("<h3 style='margin-top:28px'>Date &amp; time</h3><label>Time zone</label><select name=tzsel id=tzsel "
           "onchange=\"document.getElementById('tzc').style.display=this.value=='custom'?'block':'none'\">");
    for (int i = 0; i < TZ_ZONE_COUNT; i++)
        h += "<option value=" + String(i) + (zi == i ? " selected>" : ">") + TZ_ZONES[i].name + "</option>";
    h += String("<option value=custom") + (zi < 0 ? " selected" : "") + ">Custom (POSIX TZ rule)</option></select>";
    h += "<div id=tzc style='display:" + String(zi < 0 ? "block" : "none") + "'><label>Custom TZ rule</label><input name=tz value=\"" +
         esc(cfg.tz) + "\" placeholder='e.g. EST5EDT,M3.2.0,M11.1.0'></div>";
    h += "<label>Clock</label><select name=c24><option value=1" + String(cfg.clock24 ? " selected" : "") +
         ">24-hour</option><option value=0" + String(cfg.clock24 ? "" : " selected") + ">12-hour</option></select>";
    h += "<label>NTP server</label><input name=ntp value=\"" + esc(cfg.ntp) + "\" placeholder='pool.ntp.org'>";
    char now[32];
    clock_datetime(now, sizeof(now));
    h += "<small>Now: " + String(clock_valid() ? now : "not synced yet") + "</small>";
    h += F("<button>Save &amp; connect</button></form>"
           "<h3 style='margin-top:32px'>Firmware update</h3><form method=post action=/update enctype=multipart/form-data "
           "onsubmit=\"this.querySelector('button').textContent='Uploading\u2026 (about 30 s)';\">"
           "<input type=file name=fw accept=.bin required><button style='background:#2f81f7'>Upload &amp; install</button>"
           "<small>Use .pio/build/waveshare_s3_lcd7/firmware.bin. Settings are kept.</small></form>");
    h += "<dialog id=dlg><h3>Wi-Fi networks</h3><div id=nl>" + nets_html() +
         "</div><div class=btns><button type=button id=rs onclick=rescan()>Rescan</button>"
         "<button type=button onclick=\"$('dlg').close()\">Close</button></div></dialog><p><small>";
    h += WiFi.status() == WL_CONNECTED ? "Connected to " + esc(cfg.ssid) + ", IP " + WiFi.localIP().toString() : String("Not connected to Wi-Fi");
    h += F("</small></p></body></html>");
    server.send(200, "text/html", h);
}

static void handle_save() {
    bool wifiChanged = false;
    if (server.hasArg("ssid") && server.arg("ssid") != cfg.ssid) {
        strlcpy(cfg.ssid, server.arg("ssid").c_str(), sizeof(cfg.ssid));
        wifiChanged = true;
    }
    if (server.arg("pass").length()) {
        strlcpy(cfg.pass, server.arg("pass").c_str(), sizeof(cfg.pass));
        wifiChanged = true;
    }
    String host = server.arg("host");
    host.trim();
    host.replace("http://", "");
    if (host.endsWith("/")) host.remove(host.length() - 1);
    strlcpy(cfg.host, host.c_str(), sizeof(cfg.host));
    int port = server.arg("port").toInt();
    cfg.port = port > 0 && port < 65536 ? port : 7125;
    if (server.arg("apikey").length()) strlcpy(cfg.apikey, server.arg("apikey").c_str(), sizeof(cfg.apikey));
    bool timeChanged = false;
    if (server.hasArg("tzsel")) {
        String sel = server.arg("tzsel");
        String tz = sel == "custom" ? server.arg("tz") : String(TZ_ZONES[constrain(sel.toInt(), 0, TZ_ZONE_COUNT - 1)].posix);
        tz.trim();
        if (tz.length() && tz != cfg.tz) {
            strlcpy(cfg.tz, tz.c_str(), sizeof(cfg.tz));
            timeChanged = true;
        }
    }
    if (server.hasArg("ntp")) {
        String n = server.arg("ntp");
        n.trim();
        if (!n.length()) n = "pool.ntp.org";
        if (n != cfg.ntp) {
            strlcpy(cfg.ntp, n.c_str(), sizeof(cfg.ntp));
            timeChanged = true;
        }
    }
    if (server.hasArg("c24")) cfg.clock24 = server.arg("c24") == "1";
    if (server.hasArg("bri")) {
        cfg.brightness = constrain(server.arg("bri").toInt(), 10, 255);
        board_set_brightness(cfg.brightness);
    }
    bool rotChanged = false;
    if (server.hasArg("rot")) {
        uint8_t r = server.arg("rot").toInt() & 3;
        rotChanged = r != cfg.rotation;
        cfg.rotation = r;
    }
    settings_save();
    mr_reconfigure();
    server.send(200, "text/html",
                "<!doctype html><meta name=viewport content='width=device-width'><body style='font-family:sans-serif;"
                "background:#0d1117;color:#e6edf3;padding:24px'><h3>Saved.</h3><p>" +
                    (rotChanged ? String("The display is restarting with the new orientation. ")
                                : String("")) +
                    (apActive || wifiChanged
                         ? "The display is connecting to <b>" + esc(cfg.ssid) + "</b>. The setup hotspot turns off once "
                           "it's connected; after that the settings page is at the display's IP address (shown on screen)."
                         : String("<a style='color:#2f81f7' href='/'>Back to settings</a>")) +
                    "</p></body>");
    if (timeChanged) clock_apply();
    if (rotChanged) {  // the display layout is built for one orientation at boot
        delay(500);
        ESP.restart();
    }
    if (wifiChanged) {
        delay(300);
        net_reconnect();
    }
}

// Serial configuration: lines like "ssid=MyNet", "pass=secret", "host=192.168.1.50", "port=7125",
// "apikey=...", then "save". "show" prints the current settings (without secrets).
static void serial_cmd(String line) {
    line.trim();
    if (!line.length()) return;
    int eq = line.indexOf('=');
    String k = eq > 0 ? line.substring(0, eq) : line, v = eq > 0 ? line.substring(eq + 1) : "";
    k.trim();
    k.toLowerCase();
    if (k == "ssid") strlcpy(cfg.ssid, v.c_str(), sizeof(cfg.ssid));
    else if (k == "pass") strlcpy(cfg.pass, v.c_str(), sizeof(cfg.pass));
    else if (k == "host") strlcpy(cfg.host, v.c_str(), sizeof(cfg.host));
    else if (k == "port") cfg.port = v.toInt() > 0 ? v.toInt() : 7125;
    else if (k == "apikey") strlcpy(cfg.apikey, v.c_str(), sizeof(cfg.apikey));
    else if (k == "rotate") cfg.rotation = v.toInt() & 3;  // takes effect after save + restart
    else if (k == "bri") board_set_brightness(cfg.brightness = constrain(v.toInt(), 10, 255));
    else if (k == "ntp") strlcpy(cfg.ntp, v.c_str(), sizeof(cfg.ntp));
    else if (k == "tz") strlcpy(cfg.tz, v.c_str(), sizeof(cfg.tz));  // POSIX rule
    else if (k == "clock24") cfg.clock24 = v.toInt() != 0;
    else if (k == "save") {
        settings_save();
        mr_reconfigure();
        clock_apply();
        net_reconnect();
        Serial.println("OK saved, reconnecting");
        return;
    } else if (k == "status") {
        log_status("status");
        return;
    } else if (k != "show") {
        Serial.println("ERR keys: ssid= pass= host= port= apikey= rotate=0..3 bri=10..255 ntp= tz= clock24=0|1 save show status");
        return;
    }
    Serial.printf("OK ssid='%s' pass=%s host='%s' port=%u apikey=%s wifi=%s ip=%s\n", cfg.ssid,
                  cfg.pass[0] ? "set" : "empty", cfg.host, cfg.port, cfg.apikey[0] ? "set" : "empty",
                  WiFi.status() == WL_CONNECTED ? "connected" : "disconnected", WiFi.localIP().toString().c_str());
}

static void poll_serial(Stream& s, String& buf) {
    while (s.available()) {
        char c = s.read();
        if (c == '\n' || c == '\r') {
            serial_cmd(buf);
            buf = "";
        } else if (buf.length() < 160) {
            buf += c;
        }
    }
}

void net_begin() {
    WiFi.mode(WIFI_STA);
    WiFi.setHostname("klippesp");
    WiFi.setSleep(false);  // modem sleep adds 100+ ms latency to every request
    WiFi.setAutoReconnect(true);
    WiFi.setTxPower(WIFI_POWER_19_5dBm);  // max; the PCB antenna sits behind the panel
    connectStart = lastConnected = lastStaTry = millis();
    WiFi.onEvent([](WiFiEvent_t, WiFiEventInfo_t info) {
        logln("[wifi] disconnected, reason " + String(info.wifi_sta_disconnected.reason));
    }, ARDUINO_EVENT_WIFI_STA_DISCONNECTED);
    WiFi.onEvent([](WiFiEvent_t, WiFiEventInfo_t) { logln("[wifi] got IP " + WiFi.localIP().toString()); },
                 ARDUINO_EVENT_WIFI_STA_GOT_IP);
    if (cfg.ssid[0]) WiFi.begin(cfg.ssid, cfg.pass);
    else start_ap();

    // ArduinoOTA: `pio run -e waveshare_s3_lcd7_ota -t upload` pushes firmware over Wi-Fi
    ArduinoOTA.setHostname("klippesp");
    ArduinoOTA.setMdnsEnabled(false);  // moonraker.cpp owns mDNS (for .local printer names)
    ArduinoOTA.setTimeout(10000);       // ms without data before aborting (default 1 s is too strict over Wi-Fi)
    ArduinoOTA.onStart([] {
        mr_pause(true);
        logln("[ota] ArduinoOTA start");
        ui_ota_progress(-1);
    });
    ArduinoOTA.onProgress([](unsigned int done, unsigned int total) {
        if (total) ui_ota_progress((int)(done * 100ULL / total));
    });
    ArduinoOTA.onEnd([] {
        ui_ota_progress(100);
        logln("[ota] ArduinoOTA done, restarting");
    });
    ArduinoOTA.onError([](ota_error_t e) {
        logln("[ota] ArduinoOTA error " + String((int)e));
        mr_pause(false);
        ui_ota_progress(-100);
    });

    server.on("/", HTTP_GET, handle_root);
    server.on("/save", HTTP_POST, handle_save);
    server.on("/scan", HTTP_GET, handle_scan);
    server.on("/favicon.ico", HTTP_GET, [] { server.send_P(200, "image/png", (PGM_P)WEB_LOGO_PNG, WEB_LOGO_PNG_LEN); });
    server.on("/logo.png", HTTP_GET, [] {
        server.sendHeader("Cache-Control", "max-age=86400");
        server.send_P(200, "image/png", (PGM_P)WEB_LOGO_PNG, WEB_LOGO_PNG_LEN);
    });
    // OTA firmware update: POST multipart "fw" = firmware.bin (also: curl -F fw=@firmware.bin http://<ip>/update)
    server.on(
        "/update", HTTP_POST,
        [] {
            bool ok = !Update.hasError();
            server.sendHeader("Connection", "close");
            server.send(ok ? 200 : 500, "text/html",
                        ok ? "<!doctype html><meta http-equiv=refresh content='15;url=/'><body style='font-family:sans-serif;"
                             "background:#0d1117;color:#e6edf3;padding:24px'><h3>Update installed.</h3><p>Restarting&hellip;"
                             " this page reloads in 15 s.</p>"
                           : (String("Update failed: ") + Update.errorString()).c_str());
            if (ok) {
                delay(500);
                ESP.restart();
            }
        },
        [] {
            HTTPUpload& up = server.upload();
            if (up.status == UPLOAD_FILE_START) {
                logln("[ota] receiving " + up.filename);
                server.client().setTimeout(20);  // seconds; default 1 s aborts on short Wi-Fi/flash-erase stalls
                mr_pause(true);
                ui_ota_progress(-1);
                if (!Update.begin(UPDATE_SIZE_UNKNOWN)) Update.printError(Serial);
            } else if (up.status == UPLOAD_FILE_WRITE) {
                if (Update.write(up.buf, up.currentSize) != up.currentSize) Update.printError(Serial);
                size_t len = server.clientContentLength();
                if (len) ui_ota_progress((int)LV_MIN(99, up.totalSize * 100ULL / len));
            } else if (up.status == UPLOAD_FILE_END) {
                if (Update.end(true)) logln("[ota] done, " + String(up.totalSize) + " bytes");
                else {
                    Update.printError(Serial);
                    mr_pause(false);
                    ui_ota_progress(-100);
                }
            } else if (up.status == UPLOAD_FILE_ABORTED) {
                Update.abort();
                mr_pause(false);
                ui_ota_progress(-100);
                logln("[ota] aborted after " + String(up.totalSize) + " bytes");
            }
        });
    server.onNotFound(handle_root);  // captive-portal style: any URL shows the form
    server.begin();
}

void net_loop() {
    static String bufSerial;
    poll_serial(Serial, bufSerial);
    if (apActive) dns.processNextRequest();
    server.handleClient();
    static bool otaStarted = false;
    if (!otaStarted && WiFi.status() == WL_CONNECTED) {
        ArduinoOTA.begin();
        otaStarted = true;
    }
    if (otaStarted) ArduinoOTA.handle();

    bool connected = WiFi.status() == WL_CONNECTED;
    static bool wasConnected = false;
    if (connected && !wasConnected) clock_apply();  // (re)start NTP on every (re)connect
    wasConnected = connected;
    static uint32_t connectedAt = 0, lastLog = 0;
    if (!connected) connectedAt = 0;
    else {
        if (!connectedAt) connectedAt = millis();
        lastConnected = millis();
    }
    if (millis() - lastLog > 60000) {
        lastLog = millis();
        log_status("stat");
    }

    if (apActive) {
        // keep the hotspot a few seconds after joining so the "Saved" page can load
        if (connected && millis() - connectedAt > 5000) stop_ap();
        // retry the configured network every 30 s, but only while nobody is on the hotspot
        else if (!connected && cfg.ssid[0] && WiFi.softAPgetStationNum() == 0 && millis() - lastStaTry > 30000) {
            lastStaTry = millis();
            WiFi.begin(cfg.ssid, cfg.pass);
        }
    } else if (!connected) {
        // Short drops are handled by reconnecting; only a long outage (or no config) opens the hotspot.
        if (!cfg.ssid[0] || millis() - lastConnected > AP_FALLBACK_MS) {
            logln("[wifi] no connection for 60 s, starting setup hotspot");
            start_ap();
        } else if (millis() - lastStaTry > STA_RETRY_MS) {
            lastStaTry = millis();
            WiFi.reconnect();
        }
    }
}
