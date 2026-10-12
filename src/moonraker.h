#pragma once
// Moonraker HTTP client. Runs in background FreeRTOS tasks; the UI reads snapshots
// with mr_get() and sends requests that are queued (never blocking the UI thread).
#include <Arduino.h>
#include <vector>

#define MR_MAX_EXTRA 8
#define MR_MAX_FANS 8
#define MR_MAX_MACROS 48
#define MR_MAX_POWER 8

struct MrTemp {
    char obj[48];    // klipper object name, e.g. "heater_generic chamber"
    char label[32];  // display name, e.g. "chamber"
    float temp, target, power;
    float maxTemp;   // configured max_temp (0 = unknown)
    bool settable;   // heater (true) or plain sensor (false)
};

enum MrFanKind : uint8_t { FAN_PART, FAN_GENERIC, FAN_READONLY };

struct MrFan {
    char obj[48];
    char label[32];
    float speed;  // 0..1
    MrFanKind kind;
};

struct MrPower {
    char name[32];
    bool on;
};

// Host computer (Raspberry Pi etc.) statistics from /machine/proc_stats and system_info
struct MrHost {
    bool valid;
    float cpu;                     // total CPU usage %
    float cpuTemp;                 // deg C, NAN if not available
    uint32_t memTotal, memUsed;    // kB
    uint64_t diskTotal, diskUsed;  // bytes (gcodes file system)
    uint32_t uptime;               // seconds
    uint32_t throttled;            // Pi throttled bits (0 = fine)
    char flags[64];                // e.g. "Under-Voltage Detected"
    char model[48];                // e.g. "Raspberry Pi 4 Model B Rev 1.4"
    char os[48];
    int cores;
};

struct MrState {
    bool wifi;          // WiFi associated
    bool online;        // Moonraker reachable
    bool ready;         // klippy ready
    char klippy[16];    // ready / startup / shutdown / error / disconnected
    char klippyMsg[200];

    // print job
    char pstate[16];    // standby / printing / paused / complete / cancelled / error
    char file[128];
    char message[96];
    float progress;     // 0..1
    float printDur, totalDur, filamentUsed;
    int layer, layers;

    // temperatures
    MrTemp ext, bed;
    bool hasBed, canExtrude;
    MrTemp extra[MR_MAX_EXTRA];
    int nExtra;

    MrFan fans[MR_MAX_FANS];
    int nFans;

    int nMacros;        // names via mr_macro(): kept out of this struct, which is copied to the UI every tick

    MrPower power[MR_MAX_POWER];
    int nPower;

    // motion
    float pos[4];
    char homed[8];
    bool absolute;
    float speedFactor, extrudeFactor, zOffset;
    char idle[16];

    bool hasProbe, hasQgl, hasZTilt, hasBedMesh;

    MrHost host;

    // last error reported by Moonraker / Klipper (errSeq increments on each new one)
    uint32_t errSeq;
    char err[200];
};

struct MrFile {
    String path;
    uint32_t modified;
    uint32_t size;
};

struct MrMeta {
    char file[128];
    bool valid;
    float estTime, filamentMm, filamentG, height;
    int layers;
    int extTemp, bedTemp;
    char slicer[32];
};



void mr_begin();
void mr_stack_free(uint32_t* poll, uint32_t* cmd);  // diagnostics
bool mr_macro(int i, char* out, size_t n);  // i-th macro name (sorted, no _hidden ones)
bool mr_has_macro(const char* name);        // case-insensitive
void mr_reconfigure();  // after host/port/apikey changed

void mr_get(MrState* out);

// actions (asynchronous)
void mr_gcode(const char* script);
void mr_gcodef(const char* fmt, ...);
void mr_post(const char* path);  // urgent POST, e.g. "/printer/print/pause"
void mr_print_file(const char* file);
void mr_power_toggle(const char* device);

// data requests (asynchronous)
void mr_request_files();
// File list lives in the Moonraker module only; the UI reads the entries it shows (no per-tick list copies).
uint32_t mr_files_version(int* count);  // changes whenever the list is replaced
bool mr_file(int i, MrFile* out);       // newest first
void mr_request_meta(const char* file);
uint32_t mr_meta(MrMeta* out);

void mr_pause(bool on);  // stop all network activity (used during firmware updates)

void mr_console_active(bool on);
uint32_t mr_console(String& out);
