#include "settings.h"
#include <Preferences.h>
#include <string.h>

Settings cfg;

static void getStr(Preferences& p, const char* key, char* dst, size_t n, const char* def) {
    String v = p.getString(key, def);
    strlcpy(dst, v.c_str(), n);
}

void settings_load() {
    Preferences p;
    p.begin("klcd", true);  // NVS namespace kept from the original name so saved settings survive
    getStr(p, "ssid", cfg.ssid, sizeof(cfg.ssid), "");
    getStr(p, "pass", cfg.pass, sizeof(cfg.pass), "");
    getStr(p, "host", cfg.host, sizeof(cfg.host), "");
    getStr(p, "apikey", cfg.apikey, sizeof(cfg.apikey), "");
    cfg.port = p.getUShort("port", 7125);
    cfg.sleepMin = p.getUShort("sleep", 0);
    cfg.rotation = p.getUChar("rot", 0) & 2;
    cfg.brightness = p.getUChar("bri", 200);
    p.end();
}

void settings_save() {
    Preferences p;
    p.begin("klcd", false);
    p.putString("ssid", cfg.ssid);
    p.putString("pass", cfg.pass);
    p.putString("host", cfg.host);
    p.putString("apikey", cfg.apikey);
    p.putUShort("port", cfg.port);
    p.putUShort("sleep", cfg.sleepMin);
    p.putUChar("rot", cfg.rotation);
    p.putUChar("bri", cfg.brightness);
    p.end();
}
