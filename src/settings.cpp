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
    getStr(p, "ntp", cfg.ntp, sizeof(cfg.ntp), "pool.ntp.org");
    getStr(p, "tz", cfg.tz, sizeof(cfg.tz), "UTC0");
    cfg.clock24 = p.getBool("c24", true);
    cfg.port = p.getUShort("port", 7125);
    cfg.sleepMin = p.getUShort("sleep", 0);
    cfg.rotation = p.getUChar("rot", 0) & 3;
    cfg.uiMode = p.getUChar("ui", 0) % 3;
    p.end();
}

void settings_save() {
    Preferences p;
    p.begin("klcd", false);
    p.putString("ssid", cfg.ssid);
    p.putString("pass", cfg.pass);
    p.putString("host", cfg.host);
    p.putString("apikey", cfg.apikey);
    p.putString("ntp", cfg.ntp);
    p.putString("tz", cfg.tz);
    p.putBool("c24", cfg.clock24);
    p.putUShort("port", cfg.port);
    p.putUShort("sleep", cfg.sleepMin);
    p.putUChar("rot", cfg.rotation);
    p.putUChar("ui", cfg.uiMode);
    p.end();
}
