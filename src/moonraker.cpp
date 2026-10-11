#include "moonraker.h"
#include "settings.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <ESPmDNS.h>
#include <ArduinoJson.h>
#include <algorithm>
#include <stdarg.h>

static SemaphoreHandle_t mtx;
static QueueHandle_t cmdq;
static MrState S;

static std::vector<MrFile> files;
static uint32_t filesVer = 0;
static MrMeta meta;
static uint32_t metaVer = 0;
static String consoleText;
static uint32_t consoleVer = 0;
static volatile bool consoleOn = false;
static volatile bool reconf = false;
static volatile bool paused = false;

struct Lock {
    Lock() { xSemaphoreTake(mtx, portMAX_DELAY); }
    ~Lock() { xSemaphoreGive(mtx); }
};

enum CmdType : uint8_t { C_GCODE, C_PRINT, C_FILES, C_META, C_POWER };

struct Cmd {
    CmdType t;
    int8_t slot;
    char a[200];
};

// ---------------------------------------------------------------- helpers

static String urlEnc(const char* s, bool keepSlash) {
    static const char* hex = "0123456789ABCDEF";
    String o;
    for (; *s; s++) {
        uint8_t c = *s;
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~' || (keepSlash && c == '/')) {
            o += (char)c;
        } else {
            o += '%';
            o += hex[c >> 4];
            o += hex[c & 15];
        }
    }
    return o;
}

static void setErr(const char* msg) {
    Lock l;
    strlcpy(S.err, msg, sizeof(S.err));
    S.errSeq++;
}

// Resolve "name.local" through mDNS (lwIP does not), cache for a minute.
static SemaphoreHandle_t dnsMtx;

static String resolveHost() {
    String host = cfg.host;
    if (!host.endsWith(".local")) return host;
    struct DnsLock {
        DnsLock() { xSemaphoreTake(dnsMtx, portMAX_DELAY); }
        ~DnsLock() { xSemaphoreGive(dnsMtx); }
    } dl;
    static String cachedFor, cachedIp;
    static uint32_t cachedAt = 0;
    if (cachedFor == host && cachedIp.length() && millis() - cachedAt < 60000) return cachedIp;
    static bool mdnsUp = false;
    if (!mdnsUp) mdnsUp = MDNS.begin("klippesp");
    IPAddress ip = MDNS.queryHost(host.substring(0, host.length() - 6).c_str(), 2000);
    if (ip == IPAddress(0, 0, 0, 0)) return host;
    cachedFor = host;
    cachedIp = ip.toString();
    cachedAt = millis();
    return cachedIp;
}

// Returns HTTP status (>0) or HTTPClient error (<0).
static int http(const char* method, const String& path, const String& body, String* out, uint16_t timeoutMs = 4000) {
    if (WiFi.status() != WL_CONNECTED || !cfg.host[0]) return -1;
    WiFiClient wc;
    HTTPClient h;
    h.setConnectTimeout(3000);
    h.setTimeout(timeoutMs);
    h.setReuse(false);
    if (!h.begin(wc, resolveHost(), cfg.port, path)) return -1;
    if (cfg.apikey[0]) h.addHeader("X-Api-Key", cfg.apikey);
    int code;
    if (!strcmp(method, "GET")) {
        code = h.GET();
    } else {
        h.addHeader("Content-Type", "application/json");
        code = h.sendRequest(method, body);
    }
    if (code > 0 && out) *out = h.getString();
    h.end();
    return code;
}

// GET + parse JSON straight from the socket (no String copy of the body: on this board the large temporary
// buffers fragmented the heap). Returns the HTTP status, 599 if the body isn't valid JSON, < 0 on network errors.
#define HTTP_BAD_JSON 599
static int httpGetJson(const String& path, JsonDocument& d, JsonDocument* filter = nullptr, uint16_t timeoutMs = 4000) {
    if (WiFi.status() != WL_CONNECTED || !cfg.host[0]) return -1;
    WiFiClient wc;
    HTTPClient h;
    h.setConnectTimeout(3000);
    h.setTimeout(timeoutMs);
    h.setReuse(false);
    if (!h.begin(wc, resolveHost(), cfg.port, path)) return -1;
    if (cfg.apikey[0]) h.addHeader("X-Api-Key", cfg.apikey);
    int code = h.GET();
    if (code == 200) {
        DeserializationError e;
        if (h.getSize() >= 0) {  // Content-Length body: parse from the socket
            Stream& st = h.getStream();
            e = filter ? deserializeJson(d, st, DeserializationOption::Filter(*filter)) : deserializeJson(d, st);
        } else {                 // chunked: buffer it
            String body = h.getString();
            e = filter ? deserializeJson(d, body, DeserializationOption::Filter(*filter)) : deserializeJson(d, body);
        }
        if (e) code = HTTP_BAD_JSON;
    }
    h.end();
    return code;
}

// Extract Moonraker's {"error":{"message":...}} text.
static void reportError(int code, const String& body, const char* what) {
    char buf[200];
    JsonDocument d;
    if (code > 0 && deserializeJson(d, body) == DeserializationError::Ok && d["error"]["message"].is<const char*>()) {
        snprintf(buf, sizeof(buf), "%s", (const char*)d["error"]["message"]);
    } else {
        snprintf(buf, sizeof(buf), "%s failed (%d)", what, code);
    }
    setErr(buf);
}

static void enqueue(CmdType t, const char* a, int8_t slot = 0) {
    Cmd c;
    c.t = t;
    c.slot = slot;
    strlcpy(c.a, a ? a : "", sizeof(c.a));
    xQueueSend(cmdq, &c, 0);
}

// ---------------------------------------------------------------- polling

static bool objectsLoaded = false;
static String queryPath;

static bool startsWith(const char* s, const char* p) { return strncmp(s, p, strlen(p)) == 0; }

// Reads extruder / heater_bed max_temp from the Klipper config (filtered: the full config is large).
static void loadMaxTemps(float& ext, float& bed) {
    JsonDocument filter;
    JsonObject f = filter["result"]["status"]["configfile"]["settings"].to<JsonObject>();
    f["extruder"]["max_temp"] = true;
    f["heater_bed"]["max_temp"] = true;
    JsonDocument d;
    if (httpGetJson("/printer/objects/query?configfile=settings", d, &filter, 8000) != 200) return;
    JsonObject st = d["result"]["status"]["configfile"]["settings"];
    ext = st["extruder"]["max_temp"] | 0.0f;
    bed = st["heater_bed"]["max_temp"] | 0.0f;
}

static bool loadObjects() {
    JsonDocument d;
    if (httpGetJson("/printer/objects/list", d) != 200) return false;

    MrState n;
    memset(&n, 0, sizeof(n));
    String q = "/printer/objects/query?webhooks&print_stats&virtual_sdcard&display_status&toolhead&gcode_move&idle_timeout&extruder";

    for (JsonVariant v : d["result"]["objects"].as<JsonArray>()) {
        const char* o = v.as<const char*>();
        if (!o) continue;
        const char* sp = strchr(o, ' ');
        const char* label = sp ? sp + 1 : o;
        if (!strcmp(o, "heater_bed")) {
            n.hasBed = true;
            q += "&heater_bed";
        } else if (!strcmp(o, "probe") || !strcmp(o, "bltouch") || startsWith(o, "probe_eddy_current") || startsWith(o, "beacon") || !strcmp(o, "cartographer")) {
            n.hasProbe = true;
        } else if (!strcmp(o, "quad_gantry_level")) {
            n.hasQgl = true;
        } else if (!strcmp(o, "z_tilt")) {
            n.hasZTilt = true;
        } else if (!strcmp(o, "bed_mesh")) {
            n.hasBedMesh = true;
        } else if (startsWith(o, "gcode_macro ")) {
            if (label[0] != '_' && n.nMacros < MR_MAX_MACROS) strlcpy(n.macros[n.nMacros++], label, 40);
        } else if (startsWith(o, "heater_generic ") || startsWith(o, "temperature_sensor ") || startsWith(o, "temperature_fan ")) {
            if (n.nExtra < MR_MAX_EXTRA) {
                MrTemp& t = n.extra[n.nExtra++];
                strlcpy(t.obj, o, sizeof(t.obj));
                strlcpy(t.label, label, sizeof(t.label));
                t.settable = !startsWith(o, "temperature_sensor ");
                q += "&" + urlEnc(o, false);
            }
        } else if (!strcmp(o, "fan") || startsWith(o, "fan_generic ") || startsWith(o, "heater_fan ") || startsWith(o, "controller_fan ")) {
            if (n.nFans < MR_MAX_FANS) {
                MrFan& f = n.fans[n.nFans++];
                strlcpy(f.obj, o, sizeof(f.obj));
                strlcpy(f.label, !strcmp(o, "fan") ? "Part fan" : label, sizeof(f.label));
                f.kind = !strcmp(o, "fan") ? FAN_PART : startsWith(o, "fan_generic ") ? FAN_GENERIC : FAN_READONLY;
                q += "&" + urlEnc(o, false);
            }
        }
    }
    // sort macros alphabetically
    for (int i = 1; i < n.nMacros; i++)
        for (int j = i; j > 0 && strcmp(n.macros[j - 1], n.macros[j]) > 0; j--) {
            char tmp[40];
            memcpy(tmp, n.macros[j], 40);
            memcpy(n.macros[j], n.macros[j - 1], 40);
            memcpy(n.macros[j - 1], tmp, 40);
        }

    strcpy(n.ext.obj, "extruder");
    strcpy(n.ext.label, "Extruder");
    n.ext.settable = true;
    strcpy(n.bed.obj, "heater_bed");
    strcpy(n.bed.label, "Bed");
    n.bed.settable = true;

    {
        Lock l;
        S.hasBed = n.hasBed;
        S.hasProbe = n.hasProbe;
        S.hasQgl = n.hasQgl;
        S.hasZTilt = n.hasZTilt;
        S.hasBedMesh = n.hasBedMesh;
        memcpy(S.macros, n.macros, sizeof(S.macros));
        S.nMacros = n.nMacros;
        memcpy(S.extra, n.extra, sizeof(S.extra));
        S.nExtra = n.nExtra;
        memcpy(S.fans, n.fans, sizeof(S.fans));
        S.nFans = n.nFans;
        S.ext = n.ext;
        S.bed = n.bed;
    }
    {
        float extMax = 0, bedMax = 0;
        loadMaxTemps(extMax, bedMax);
        Lock l;
        S.ext.maxTemp = extMax;
        S.bed.maxTemp = bedMax;
    }
    queryPath = q;
    return true;
}

static void readTemp(JsonObject o, MrTemp& t) {
    if (o.isNull()) return;
    t.temp = o["temperature"] | t.temp;
    t.target = o["target"] | t.target;
    t.power = o["power"] | t.power;
}

static void parseStatus(JsonObject st) {
    Lock l;
    JsonObject o;

    o = st["webhooks"];
    if (!o.isNull()) {
        strlcpy(S.klippy, o["state"] | S.klippy, sizeof(S.klippy));
        strlcpy(S.klippyMsg, o["state_message"] | "", sizeof(S.klippyMsg));
        S.ready = !strcmp(S.klippy, "ready");
    }
    o = st["print_stats"];
    if (!o.isNull()) {
        strlcpy(S.pstate, o["state"] | S.pstate, sizeof(S.pstate));
        strlcpy(S.file, o["filename"] | S.file, sizeof(S.file));
        S.printDur = o["print_duration"] | S.printDur;
        S.totalDur = o["total_duration"] | S.totalDur;
        S.filamentUsed = o["filament_used"] | S.filamentUsed;
        JsonObject info = o["info"];
        S.layer = info["current_layer"] | 0;
        S.layers = info["total_layer"] | 0;
    }
    o = st["virtual_sdcard"];
    if (!o.isNull()) S.progress = o["progress"] | S.progress;
    o = st["display_status"];
    if (!o.isNull()) strlcpy(S.message, o["message"] | "", sizeof(S.message));
    o = st["toolhead"];
    if (!o.isNull()) {
        JsonArray p = o["position"];
        for (int i = 0; i < 4 && i < (int)p.size(); i++) S.pos[i] = p[i];
        strlcpy(S.homed, o["homed_axes"] | "", sizeof(S.homed));
    }
    o = st["gcode_move"];
    if (!o.isNull()) {
        S.speedFactor = o["speed_factor"] | S.speedFactor;
        S.extrudeFactor = o["extrude_factor"] | S.extrudeFactor;
        S.absolute = o["absolute_coordinates"] | true;
        JsonArray h = o["homing_origin"];
        if (h.size() > 2) S.zOffset = h[2];
    }
    o = st["idle_timeout"];
    if (!o.isNull()) strlcpy(S.idle, o["state"] | "", sizeof(S.idle));

    o = st["extruder"];
    readTemp(o, S.ext);
    if (!o.isNull()) S.canExtrude = o["can_extrude"] | false;
    readTemp(st["heater_bed"], S.bed);
    for (int i = 0; i < S.nExtra; i++) readTemp(st[S.extra[i].obj], S.extra[i]);
    for (int i = 0; i < S.nFans; i++) {
        JsonObject f = st[S.fans[i].obj];
        if (!f.isNull()) S.fans[i].speed = f["speed"] | S.fans[i].speed;
    }
}

static void pollPrinterInfo() {
    JsonDocument d;
    int code = httpGetJson("/printer/info", d);
    Lock l;
    S.online = code > 0;
    S.ready = false;
    if (code != 200) {
        strcpy(S.klippy, code > 0 ? "disconnected" : "offline");
        S.klippyMsg[0] = 0;
        return;
    }
    strlcpy(S.klippy, d["result"]["state"] | "unknown", sizeof(S.klippy));
    strlcpy(S.klippyMsg, d["result"]["state_message"] | "", sizeof(S.klippyMsg));
}

static void pollStatus() {
    JsonDocument d;
    int code = httpGetJson(queryPath, d);
    if (code != 200) {
        objectsLoaded = false;  // klippy restarted or Moonraker gone; re-discover
        Lock l;
        S.online = code > 0;
        S.ready = false;
        return;
    }
    { Lock l; S.online = true; }
    parseStatus(d["result"]["status"]);
}

static void pollConsole() {
    JsonDocument d;
    if (httpGetJson("/server/gcode_store?count=40", d) != 200) return;
    String t;
    for (JsonObject e : d["result"]["gcode_store"].as<JsonArray>()) {
        const char* m = e["message"] | "";
        if (!strcmp(e["type"] | "", "command")) t += "> ";
        t += m;
        t += '\n';
    }
    Lock l;
    if (t != consoleText) {
        consoleText = t;
        consoleVer++;
    }
}

static void pollPower() {
    JsonDocument d;
    if (httpGetJson("/machine/device_power/devices", d) != 200) {
        Lock l;
        S.nPower = 0;
        return;
    }
    Lock l;
    S.nPower = 0;
    for (JsonObject o : d["result"]["devices"].as<JsonArray>()) {
        if (S.nPower >= MR_MAX_POWER) break;
        MrPower& p = S.power[S.nPower++];
        strlcpy(p.name, o["device"] | "?", sizeof(p.name));
        p.on = !strcmp(o["status"] | "", "on");
    }
}

// ---------------------------------------------------------------- host stats

static bool sysInfoLoaded = false;

static void pollProcStats() {
    JsonDocument filter;
    JsonObject f = filter["result"].to<JsonObject>();
    f["system_cpu_usage"]["cpu"] = true;
    f["system_memory"] = true;
    f["cpu_temp"] = true;
    f["system_uptime"] = true;
    f["throttled_state"] = true;
    JsonDocument d;
    if (httpGetJson("/machine/proc_stats", d, &filter) != 200) return;
    JsonObject r = d["result"];
    Lock l;
    MrHost& h = S.host;
    h.valid = true;
    h.cpu = r["system_cpu_usage"]["cpu"] | 0.0f;
    h.cpuTemp = r["cpu_temp"].is<float>() ? r["cpu_temp"].as<float>() : NAN;
    uint32_t total = r["system_memory"]["total"] | 0, avail = r["system_memory"]["available"] | 0;
    h.memTotal = total;
    h.memUsed = total > avail ? total - avail : 0;
    h.uptime = r["system_uptime"] | 0.0f;
    h.throttled = r["throttled_state"]["bits"] | 0;
    String fl;
    for (JsonVariant v : r["throttled_state"]["flags"].as<JsonArray>()) {
        if (fl.length()) fl += ", ";
        fl += v.as<const char*>();
    }
    strlcpy(h.flags, fl.c_str(), sizeof(h.flags));
}

static void pollDisk() {
    JsonDocument filter;
    filter["result"]["disk_usage"] = true;
    JsonDocument d;
    if (httpGetJson("/server/files/directory?path=gcodes&extended=false", d, &filter, 6000) != 200) return;
    JsonObject du = d["result"]["disk_usage"];
    Lock l;
    S.host.diskTotal = du["total"] | 0ULL;
    S.host.diskUsed = du["used"] | 0ULL;
}

static void loadSystemInfo() {
    JsonDocument filter;
    JsonObject f = filter["result"]["system_info"].to<JsonObject>();
    f["cpu_info"] = true;
    f["distribution"]["name"] = true;
    JsonDocument d;
    if (httpGetJson("/machine/system_info", d, &filter, 6000) != 200) return;
    JsonObject si = d["result"]["system_info"];
    JsonObject ci = si["cpu_info"];
    const char* model = ci["model"] | "";
    if (!model[0]) model = ci["hardware_desc"] | "";
    if (!model[0]) model = ci["cpu_desc"] | "";
    Lock l;
    strlcpy(S.host.model, model, sizeof(S.host.model));
    strlcpy(S.host.os, si["distribution"]["name"] | "", sizeof(S.host.os));
    S.host.cores = ci["cpu_count"] | 0;
    sysInfoLoaded = true;
}

// host stats are available from Moonraker even while Klipper is down
static void pollHost(uint32_t tick) {
    if (!sysInfoLoaded) loadSystemInfo();
    if (tick % 3 == 0) pollProcStats();
    if (tick % 30 == 0) pollDisk();
}

static void pollTask(void*) {
    uint32_t tick = 0;
    for (;;) {
        if (paused) {
            vTaskDelay(pdMS_TO_TICKS(200));
            continue;
        }
        uint32_t t0 = millis();
        bool wifi = WiFi.status() == WL_CONNECTED;
        { Lock l; S.wifi = wifi; }
        if (reconf) {
            reconf = false;
            objectsLoaded = false;
            sysInfoLoaded = false;
            Lock l;
            S.online = S.ready = false;
            strcpy(S.klippy, "offline");
        }
        if (!wifi || !cfg.host[0]) {
            Lock l;
            S.online = S.ready = false;
            strcpy(S.klippy, "offline");
        } else if (!objectsLoaded) {
            pollPrinterInfo();
            bool online;
            { Lock l; online = S.online; }
            if (online) pollHost(tick);
            else sysInfoLoaded = false;
            // power devices matter most while Klipper is down (printer switched off by its plug)
            if (online && tick % 5 == 0) pollPower();
            bool ready;
            { Lock l; ready = !strcmp(S.klippy, "ready"); }
            if (ready && loadObjects()) {
                objectsLoaded = true;
                pollStatus();
                pollPower();
            }
        } else {
            pollStatus();
            if (consoleOn && tick % 2 == 0) pollConsole();
            if (tick % 10 == 0) pollPower();
            pollHost(tick);
        }

        tick++;
        uint32_t dt = millis() - t0;
        vTaskDelay(pdMS_TO_TICKS(dt < 1000 ? 1000 - dt : 50));
    }
}

// ---------------------------------------------------------------- commands

#define MAX_FILES 60  // file list cap (RAM: no PSRAM on this board)

static void doFiles() {
    JsonDocument filter;
    filter["result"][0]["path"] = true;
    filter["result"][0]["modified"] = true;
    filter["result"][0]["size"] = true;
    JsonDocument d;
    int code = httpGetJson("/server/files/list?root=gcodes", d, &filter, 8000);
    if (code != 200) return reportError(code, "", "File list");
    std::vector<MrFile> v;
    for (JsonObject o : d["result"].as<JsonArray>()) {
        MrFile f;
        f.path = (const char*)(o["path"] | "");
        f.modified = (uint32_t)(o["modified"] | 0.0);
        f.size = o["size"] | 0;
        if (f.path.length()) v.push_back(f);
    }
    std::sort(v.begin(), v.end(), [](const MrFile& a, const MrFile& b) { return a.modified > b.modified; });
    if (v.size() > MAX_FILES) v.resize(MAX_FILES);
    Lock l;
    files.swap(v);
    filesVer++;
}

static void doMeta(const char* file, bool storeMeta) {
    JsonDocument filter;
    JsonObject fr = filter["result"].to<JsonObject>();
    for (const char* k : {"estimated_time", "filament_total", "filament_weight_total", "object_height", "layer_count",
                          "first_layer_extr_temp", "first_layer_bed_temp", "slicer"})
        fr[k] = true;
    JsonDocument d;
    int code = httpGetJson("/server/files/metadata?filename=" + urlEnc(file, false), d, &filter, 6000);
    if (code != 200) {
        if (storeMeta) {
            Lock l;
            memset(&meta, 0, sizeof(meta));
            strlcpy(meta.file, file, sizeof(meta.file));
            metaVer++;
        }
        return;
    }
    JsonObject r = d["result"];
    if (storeMeta) {
        Lock l;
        memset(&meta, 0, sizeof(meta));
        strlcpy(meta.file, file, sizeof(meta.file));
        meta.valid = true;
        meta.estTime = r["estimated_time"] | 0.0f;
        meta.filamentMm = r["filament_total"] | 0.0f;
        meta.filamentG = r["filament_weight_total"] | 0.0f;
        meta.height = r["object_height"] | 0.0f;
        meta.layers = r["layer_count"] | 0;
        meta.extTemp = r["first_layer_extr_temp"] | 0;
        meta.bedTemp = r["first_layer_bed_temp"] | 0;
        strlcpy(meta.slicer, r["slicer"] | "", sizeof(meta.slicer));
        metaVer++;
    }
}

// After the printer is powered on, Klipper stays in shutdown/error until a firmware restart. Give the board time to
// boot, then restart it unless something else (Moonraker's restart_klipper_when_powered) already did.
static void powerOnRestart(const char* device) {
    bool on = false;
    {
        Lock l;
        for (int i = 0; i < S.nPower; i++)
            if (!strcmp(S.power[i].name, device)) on = S.power[i].on;
    }
    if (!on) return;
    vTaskDelay(pdMS_TO_TICKS(5000));
    bool down;
    {
        Lock l;
        down = S.online && (!strcmp(S.klippy, "shutdown") || !strcmp(S.klippy, "error"));
    }
    if (!down) return;
    String out;
    int code = http("POST", "/printer/firmware_restart", "", &out);
    if (code != 200) reportError(code, out, "Firmware restart");
}

static void cmdTask(void*) {
    Cmd c;
    for (;;) {
        if (xQueueReceive(cmdq, &c, portMAX_DELAY) != pdTRUE) continue;
        while (paused) vTaskDelay(pdMS_TO_TICKS(200));
        String body, out;
        int code;
        switch (c.t) {
            case C_GCODE: {
                JsonDocument d;
                d["script"] = c.a;
                serializeJson(d, body);
                // gcode/script returns when the command finishes; long moves may time out, which is fine
                code = http("POST", "/printer/gcode/script", body, &out, 60000);
                if (code != 200 && code != HTTPC_ERROR_READ_TIMEOUT) reportError(code, out, c.a);
                break;
            }
            case C_PRINT:
                code = http("POST", "/printer/print/start?filename=" + urlEnc(c.a, false), "", &out, 10000);
                if (code != 200) reportError(code, out, "Start print");
                break;
            case C_POWER:
                code = http("POST", "/machine/device_power/device?device=" + urlEnc(c.a, false) + "&action=toggle", "", &out);
                if (code != 200) {
                    reportError(code, out, "Power toggle");
                    break;
                }
                pollPower();
                if (cfg.powerRestart) powerOnRestart(c.a);
                break;
            case C_FILES: doFiles(); break;
            case C_META: doMeta(c.a, true); break;
        }
    }
}

// Urgent requests (pause / cancel / e-stop / restarts) bypass the queue so a long
// running gcode can't delay them.
static void urgentTask(void* arg) {
    char* path = (char*)arg;
    String out;
    int code = http("POST", path, "", &out, 10000);
    if (code != 200) reportError(code, out, path);
    free(path);
    vTaskDelete(nullptr);
}

// ---------------------------------------------------------------- public API

void mr_begin() {
    mtx = xSemaphoreCreateMutex();
    dnsMtx = xSemaphoreCreateMutex();
    cmdq = xQueueCreate(16, sizeof(Cmd));
    memset(&S, 0, sizeof(S));
    strcpy(S.klippy, "offline");
    strcpy(S.pstate, "standby");
    S.speedFactor = S.extrudeFactor = 1;
    xTaskCreatePinnedToCore(pollTask, "mr_poll", 12288, nullptr, 1, nullptr, 0);
    xTaskCreatePinnedToCore(cmdTask, "mr_cmd", 12288, nullptr, 1, nullptr, 0);
}

void mr_reconfigure() { reconf = true; }
void mr_pause(bool on) { paused = on; }

void mr_get(MrState* out) {
    Lock l;
    *out = S;
}

void mr_gcode(const char* script) { enqueue(C_GCODE, script); }

void mr_gcodef(const char* fmt, ...) {
    char buf[200];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    enqueue(C_GCODE, buf);
}

void mr_post(const char* path) {
    char* p = strdup(path);
    if (xTaskCreatePinnedToCore(urgentTask, "mr_urgent", 8192, p, 2, nullptr, 0) != pdPASS) free(p);
}

void mr_print_file(const char* file) { enqueue(C_PRINT, file); }
void mr_power_toggle(const char* device) { enqueue(C_POWER, device); }
void mr_request_files() { enqueue(C_FILES, ""); }
void mr_request_meta(const char* file) { enqueue(C_META, file); }

uint32_t mr_files(std::vector<MrFile>& out) {
    Lock l;
    out = files;
    return filesVer;
}

uint32_t mr_meta(MrMeta* out) {
    Lock l;
    *out = meta;
    return metaVer;
}


void mr_console_active(bool on) { consoleOn = on; }

uint32_t mr_console(String& out) {
    Lock l;
    out = consoleText;
    return consoleVer;
}
