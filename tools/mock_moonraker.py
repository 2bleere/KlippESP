#!/usr/bin/env python3
"""Mock Moonraker server for previewing the KlippESP screens without a printer.

Implements the subset of the Moonraker HTTP API the display uses, with simulated
temperatures and print progress. Open http://<this-machine>:7125/ for a control page
to switch scenarios, then point the display's printer address at this machine.

    python3 tools/mock_moonraker.py [--port 7125] [--speed 20]

Standard library only.
"""
import argparse
import json
import math
import random
import struct
import threading
import time
import zlib
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import parse_qs, unquote, urlparse

# ----------------------------------------------------------------------------- state

LOCK = threading.Lock()

FILES = [
    ("Voron_Cube_PLA_0.2mm.gcode", 1_840_000, 2 * 3600 + 14 * 60, 9_820.0, 29.3, 151, 30.2, 215, 60),
    ("benchy/3DBenchy_PETG_0.2mm.gcode", 3_200_000, 1 * 3600 + 47 * 60, 5_310.0, 16.2, 240, 48.0, 240, 80),
    ("Stealthburner_parts_ABS.gcode", 12_400_000, 7 * 3600 + 3 * 60, 41_700.0, 124.0, 312, 62.4, 250, 105),
    ("calibration/first_layer_square.gcode", 220_000, 9 * 60, 820.0, 2.4, 1, 0.2, 215, 60),
]

SCENARIOS = {
    "idle": "Ready, idle",
    "heating": "Heating up (preheat PLA)",
    "printing": "Printing (animated)",
    "paused": "Paused mid-print",
    "complete": "Print complete",
    "cancelled": "Print cancelled",
    "print_error": "Print error",
    "startup": "Klipper starting",
    "shutdown": "Klipper shutdown (thermal runaway)",
    "error": "Klipper config error",
    "offline": "Moonraker offline (drop connections)",
}

S = {}


def reset(scenario="idle"):
    f = FILES[0]
    S.update(
        scenario=scenario,
        klippy="ready",
        klippy_msg="Printer is ready",
        pstate="standby",
        file="",
        message="",
        progress=0.0,
        print_dur=0.0,
        total_dur=0.0,
        filament=0.0,
        layer=0,
        layers=0,
        est=f[2],
        ext=dict(temp=24.3, target=0.0, power=0.0),
        bed=dict(temp=23.8, target=0.0, power=0.0),
        chamber=dict(temp=26.1, target=0.0, power=0.0),
        pi=41.2,
        mcu=38.7,
        fan=0.0,
        nevermore=0.0,
        hotend_fan=0.0,
        speed=1.0,
        flow=1.0,
        zoff=0.0,
        pos=[175.0, 175.0, 30.0, 0.0],
        homed="",
        absolute=True,
        power={"printer": True, "lights": True, "nevermore": False},
        console=[],
    )
    log("// Mock Moonraker: scenario '%s'" % scenario)


def start_job(fidx=0, progress=0.0):
    f = FILES[fidx]
    S.update(file=f[0], pstate="printing", layers=f[5], est=f[2], homed="xyz")
    set_progress(progress)
    S["ext"]["target"], S["bed"]["target"] = f[7], f[8]
    S["ext"]["temp"], S["bed"]["temp"] = f[7] - 0.4, f[8] + 0.1
    S["fan"], S["hotend_fan"], S["nevermore"] = 0.8, 1.0, 0.6


def set_progress(p):
    f = next(x for x in FILES if x[0] == S["file"]) if S["file"] else FILES[0]
    p = max(0.0, min(1.0, p))
    S["progress"] = p
    S["print_dur"] = f[2] * p
    S["total_dur"] = S["print_dur"] + 95
    S["filament"] = f[3] * p
    S["layer"] = max(1, int(round(f[5] * p))) if p > 0 else 0
    S["pos"][2] = round(f[6] * p + 0.2, 2) if p > 0 else S["pos"][2]
    S["message"] = "Printing layer %d" % S["layer"] if S["pstate"] == "printing" else ""


def apply_scenario(name):
    reset(name)
    if name == "heating":
        S["ext"]["target"], S["bed"]["target"], S["homed"] = 215.0, 60.0, "xyz"
        S["message"] = "Preheating..."
    elif name == "printing":
        start_job(0, 0.37)
    elif name == "paused":
        start_job(1, 0.52)
        S["pstate"], S["message"] = "paused", "Filament runout - paused"
        S["ext"]["target"] = 0
    elif name in ("complete", "cancelled", "print_error"):
        start_job(2, 1.0 if name == "complete" else 0.64)
        S["pstate"] = {"complete": "complete", "cancelled": "cancelled", "print_error": "error"}[name]
        S["message"] = "Move out of range: 352.000 355.200 12.400 [41.230]" if name == "print_error" else ""
        S["ext"]["target"] = S["bed"]["target"] = 0
        S["fan"] = 0
    elif name == "startup":
        S["klippy"], S["klippy_msg"] = "startup", "Printer is not ready\nKlipper is starting up..."
    elif name == "shutdown":
        S["klippy"] = "shutdown"
        S["klippy_msg"] = ('Heater extruder not heating at expected rate\nSee the "verify_heater" section in '
                           "docs/Config_Reference.md\nOnce the underlying issue is corrected, use the "
                           '"FIRMWARE_RESTART" command to reset the firmware, reload the config, and restart the host software.')
    elif name == "error":
        S["klippy"] = "error"
        S["klippy_msg"] = ("Option 'rotation_distance' in section 'extruder' must be specified\n"
                           "Once the underlying issue is corrected, use the \"RESTART\" command to reload the config.")


def log(msg, kind="response"):
    S.setdefault("console", []).append({"message": msg, "time": time.time(), "type": kind})
    del S["console"][:-80]


# ----------------------------------------------------------------------------- simulation

def approach(h, dt):
    tgt = h["target"]
    ambient = 24.0
    if tgt > 0:
        err = tgt - h["temp"]
        h["power"] = max(0.0, min(1.0, err / 15 + 0.35))
        h["temp"] += max(-3.0, min(4.0, err * 0.25)) * dt + random.uniform(-0.15, 0.15)
    else:
        h["power"] = 0.0
        h["temp"] += (ambient - h["temp"]) * 0.03 * dt
    h["temp"] = round(h["temp"], 2)


def sim_loop(speed):
    last = time.time()
    while True:
        time.sleep(0.5)
        now = time.time()
        dt = now - last
        last = now
        with LOCK:
            for h in ("ext", "bed", "chamber"):
                approach(S[h], dt)
            S["pi"] = round(41 + 2 * math.sin(now / 30) + random.uniform(-0.3, 0.3), 1)
            S["mcu"] = round(38 + 1.5 * math.sin(now / 40), 1)
            if S["pstate"] == "printing" and S["klippy"] == "ready":
                f = next(x for x in FILES if x[0] == S["file"])
                set_progress(S["progress"] + dt * speed / f[2])
                S["pos"][0] = 175 + 60 * math.sin(now * 1.7)
                S["pos"][1] = 175 + 60 * math.cos(now * 1.3)
                if S["progress"] >= 1.0:
                    S["pstate"], S["message"] = "complete", ""
                    S["ext"]["target"] = S["bed"]["target"] = 0
                    log("// Print finished")
            if S["pstate"] in ("printing", "paused"):
                S["total_dur"] += dt


# ----------------------------------------------------------------------------- thumbnail

def make_png(w=200, h=200):
    """A shaded isometric calibration cube on transparent background."""
    rows = []
    cx, cy, s = w / 2, h / 2 + 10, w * 0.32
    top = [(cx, cy - s), (cx + s * 0.87, cy - s / 2), (cx, cy), (cx - s * 0.87, cy - s / 2)]
    left = [(cx - s * 0.87, cy - s / 2), (cx, cy), (cx, cy + s), (cx - s * 0.87, cy + s / 2)]
    right = [(cx, cy), (cx + s * 0.87, cy - s / 2), (cx + s * 0.87, cy + s / 2), (cx, cy + s)]

    def inside(px, py, poly):
        c = False
        for i in range(len(poly)):
            (x1, y1), (x2, y2) = poly[i], poly[(i + 1) % len(poly)]
            if (y1 > py) != (y2 > py) and px < (x2 - x1) * (py - y1) / (y2 - y1) + x1:
                c = not c
        return c

    for y in range(h):
        row = bytearray(b"\x00")
        for x in range(w):
            if inside(x, y, top):
                px = (90, 200, 120, 255)
            elif inside(x, y, left):
                px = (40, 140, 75, 255)
            elif inside(x, y, right):
                px = (55, 170, 95, 255)
            else:
                px = (0, 0, 0, 0)
            if px[3] and (y % 8 == 0):  # layer lines
                px = tuple(int(c * 0.85) for c in px[:3]) + (255,)
            row += bytes(px)
        rows.append(bytes(row))
    raw = zlib.compress(b"".join(rows), 9)

    def chunk(t, d):
        c = struct.pack(">I", len(d)) + t + d
        return c + struct.pack(">I", zlib.crc32(t + d) & 0xFFFFFFFF)

    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0))
            + chunk(b"IDAT", raw) + chunk(b"IEND", b""))


THUMB = make_png()
START = time.time() - 3 * 86400 - 4 * 3600

# ----------------------------------------------------------------------------- API

OBJECTS = [
    "webhooks", "print_stats", "virtual_sdcard", "display_status", "toolhead", "gcode_move",
    "idle_timeout", "extruder", "heater_bed", "heater_generic chamber", "temperature_sensor raspberry_pi",
    "temperature_sensor mcu", "fan", "heater_fan hotend_fan", "controller_fan electronics",
    "fan_generic nevermore", "probe", "quad_gantry_level", "bed_mesh",
    "gcode_macro PRINT_START", "gcode_macro PRINT_END", "gcode_macro LOAD_FILAMENT",
    "gcode_macro UNLOAD_FILAMENT", "gcode_macro PARK", "gcode_macro CLEAN_NOZZLE",
    "gcode_macro BED_MESH_ADAPTIVE", "gcode_macro LIGHTS_ON", "gcode_macro LIGHTS_OFF", "gcode_macro _HELPER",
]


def status():
    h = lambda d: {"temperature": d["temp"], "target": d["target"], "power": d["power"]}
    return {
        "webhooks": {"state": S["klippy"], "state_message": S["klippy_msg"]},
        "print_stats": {"state": S["pstate"], "filename": S["file"], "print_duration": S["print_dur"],
                        "total_duration": S["total_dur"], "filament_used": S["filament"], "message": S["message"],
                        "info": {"current_layer": S["layer"] or None, "total_layer": S["layers"] or None}},
        "virtual_sdcard": {"progress": S["progress"], "is_active": S["pstate"] == "printing"},
        "display_status": {"progress": S["progress"], "message": S["message"] or None},
        "toolhead": {"position": S["pos"], "homed_axes": S["homed"]},
        "gcode_move": {"speed_factor": S["speed"], "extrude_factor": S["flow"], "absolute_coordinates": S["absolute"],
                       "homing_origin": [0, 0, S["zoff"], 0], "gcode_position": S["pos"]},
        "idle_timeout": {"state": "Printing" if S["pstate"] == "printing" else "Idle"},
        "extruder": dict(h(S["ext"]), can_extrude=S["ext"]["temp"] >= 170),
        "heater_bed": h(S["bed"]),
        "heater_generic chamber": h(S["chamber"]),
        "temperature_sensor raspberry_pi": {"temperature": S["pi"]},
        "temperature_sensor mcu": {"temperature": S["mcu"]},
        "fan": {"speed": S["fan"]},
        "heater_fan hotend_fan": {"speed": 1.0 if S["ext"]["temp"] > 50 else 0.0},
        "controller_fan electronics": {"speed": 0.4 if S["homed"] else 0.0},
        "fan_generic nevermore": {"speed": S["nevermore"]},
    }


def metadata(name):
    f = next((x for x in FILES if x[0] == name), None)
    if not f:
        return None
    return {
        "filename": f[0], "size": f[1], "modified": time.time() - 3600 * (FILES.index(f) + 1) * 7,
        "slicer": "OrcaSlicer", "slicer_version": "2.2.0", "estimated_time": f[2], "filament_total": f[3],
        "filament_weight_total": f[4], "layer_count": f[5], "object_height": f[6],
        "first_layer_extr_temp": f[7], "first_layer_bed_temp": f[8],
        "thumbnails": [
            {"width": 32, "height": 32, "size": 900, "relative_path": ".thumbs/%s-32x32.png" % f[0].split("/")[-1][:-6]},
            {"width": 200, "height": 200, "size": len(THUMB), "relative_path": ".thumbs/%s-200x200.png" % f[0].split("/")[-1][:-6]},
        ],
    }


def run_gcode(script):
    import re
    log(script, "command")
    for line in script.upper().splitlines():
        p = dict(re.findall(r"(\w+)=([\w.\-]+)", line))
        words = line.split()
        cmd = words[0] if words else ""
        num = lambda k, d=0.0: float(next((w[1:] for w in words[1:] if w.startswith(k)), d))
        if cmd == "SET_HEATER_TEMPERATURE":
            key = {"EXTRUDER": "ext", "HEATER_BED": "bed", "CHAMBER": "chamber"}.get(p.get("HEATER", ""))
            if key:
                S[key]["target"] = float(p.get("TARGET", 0))
        elif cmd in ("M104", "M109"):
            S["ext"]["target"] = num("S")
        elif cmd in ("M140", "M190"):
            S["bed"]["target"] = num("S")
        elif cmd == "TURN_OFF_HEATERS":
            for k in ("ext", "bed", "chamber"):
                S[k]["target"] = 0
        elif cmd == "M106":
            S["fan"] = num("S", 255) / 255
        elif cmd == "M107":
            S["fan"] = 0
        elif cmd == "SET_FAN_SPEED" and p.get("FAN") == "NEVERMORE":
            S["nevermore"] = float(p.get("SPEED", 0))
        elif cmd == "M220":
            S["speed"] = num("S", 100) / 100
        elif cmd == "M221":
            S["flow"] = num("S", 100) / 100
        elif cmd == "SET_GCODE_OFFSET":
            if "Z_ADJUST" in p:
                S["zoff"] = round(S["zoff"] + float(p["Z_ADJUST"]), 3)
            elif "Z" in p:
                S["zoff"] = float(p["Z"])
        elif cmd == "G28":
            S["homed"] = "xyz"
            S["pos"] = [175.0, 175.0, 10.0, 0.0]
        elif cmd == "M84":
            S["homed"] = ""
        elif cmd == "G1" and S["homed"]:
            for i, ax in enumerate("XYZ"):
                if any(w.startswith(ax) for w in words[1:]):
                    S["pos"][i] = round(S["pos"][i] + num(ax), 2)
        elif cmd == "G1" and not S["homed"] and any(w[0] in "XYZ" for w in words[1:]):
            raise ValueError("Must home axis first: 0.000 0.000 0.000 [0.000]")
        elif cmd == "M112":
            S["klippy"], S["klippy_msg"] = "shutdown", "Shutdown due to M112 command"
    log("ok")


CONTROL_PAGE = """<!doctype html><meta charset=utf-8><meta name=viewport content='width=device-width,initial-scale=1'>
<title>Mock Moonraker</title><style>body{font-family:sans-serif;background:#0d1117;color:#e6edf3;max-width:640px;margin:24px auto;padding:0 16px}
button{background:#263143;color:#e6edf3;border:0;border-radius:6px;padding:10px 14px;margin:4px;font-size:15px;cursor:pointer}
button.on{background:#2f81f7}input{width:100%%}small{color:#8b949e}pre{background:#1c2330;padding:10px;border-radius:6px;font-size:13px}</style>
<h2>Mock Moonraker</h2><p><small>Point the display's printer address at this machine, port %(port)d.</small></p>
<h3>Scenario</h3><div id=sc>%(buttons)s</div>
<h3>Print progress <small id=pv></small></h3><input type=range min=0 max=100 id=pr oninput="post('progress='+this.value)">
<h3>State</h3><pre id=st></pre>
<script>
function post(q){fetch('/mock/set?'+q,{method:'POST'}).then(r=>r.json()).then(show)}
function show(s){document.getElementById('st').textContent=JSON.stringify(s,null,1);
 document.querySelectorAll('#sc button').forEach(b=>b.className=b.dataset.s==s.scenario?'on':'');
 document.getElementById('pv').textContent=Math.round(s.progress*100)+'%%';
 if(document.activeElement.id!='pr')document.getElementById('pr').value=s.progress*100}
setInterval(()=>fetch('/mock/state').then(r=>r.json()).then(show),1000);fetch('/mock/state').then(r=>r.json()).then(show)
</script>"""


class Handler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def log_message(self, fmt, *args):
        if ARGS.verbose:
            super().log_message(fmt, *args)

    def send(self, code, body, ctype="application/json"):
        if isinstance(body, (dict, list)):
            body = json.dumps(body).encode()
        elif isinstance(body, str):
            body = body.encode()
        self.send_response(code)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Access-Control-Allow-Origin", "*")
        self.end_headers()
        self.wfile.write(body)

    def err(self, code, msg):
        self.send(code, {"error": {"code": code, "message": msg}})

    def offline(self, path):
        if S["scenario"] == "offline" and not path.startswith("/mock") and path != "/":
            self.close_connection = True
            self.connection.close()
            return True
        return False

    def snapshot(self):
        return {k: v for k, v in S.items() if k != "console"}

    def do_GET(self):
        u = urlparse(self.path)
        q = parse_qs(u.query, keep_blank_values=True)
        path = u.path
        with LOCK:
            if self.offline(path):
                return
            if path == "/":
                b = "".join("<button data-s=%s onclick=\"post('scenario=%s')\">%s</button>" % (k, k, v)
                            for k, v in SCENARIOS.items())
                return self.send(200, CONTROL_PAGE % {"buttons": b, "port": ARGS.port}, "text/html")
            if path == "/mock/state":
                return self.send(200, self.snapshot())
            if path == "/printer/info":
                return self.send(200, {"result": {"state": S["klippy"], "state_message": S["klippy_msg"],
                                                  "hostname": "mockprinter", "software_version": "v0.12.0-mock"}})
            if path == "/server/info":
                return self.send(200, {"result": {"klippy_connected": True, "klippy_state": S["klippy"]}})
            if path == "/printer/objects/list":
                if S["klippy"] != "ready":
                    return self.err(503, "Klippy Host not connected")
                return self.send(200, {"result": {"objects": OBJECTS}})
            if path == "/printer/objects/query":
                if S["klippy"] != "ready":
                    return self.err(503, "Klippy Host not connected")
                st = status()
                wanted = [unquote(k) for k in q.keys()]
                return self.send(200, {"result": {"eventtime": time.monotonic(),
                                                  "status": {k: st[k] for k in wanted if k in st}}})
            if path == "/server/files/list":
                return self.send(200, {"result": [{"path": f[0], "modified": metadata(f[0])["modified"], "size": f[1],
                                                   "permissions": "rw"} for f in FILES]})
            if path == "/server/files/metadata":
                m = metadata(q.get("filename", [""])[0])
                return self.send(200, {"result": m}) if m else self.err(404, "Metadata not available")
            if path.startswith("/server/files/gcodes/") and path.endswith(".png"):
                return self.send(200, THUMB, "image/png")
            if path == "/server/gcode_store":
                n = int(q.get("count", ["50"])[0])
                return self.send(200, {"result": {"gcode_store": S["console"][-n:]}})
            if path == "/machine/proc_stats":
                t = time.time()
                cpu = 8 + 6 * math.sin(t / 7) + (25 if S["pstate"] == "printing" else 0) + random.uniform(0, 4)
                return self.send(200, {"result": {
                    "moonraker_stats": [], "cpu_temp": round(S["pi"] + 6, 1),
                    "system_cpu_usage": {"cpu": round(cpu, 2), "cpu0": round(cpu * 1.2, 2)},
                    "system_memory": {"total": 3884356, "available": 2650000 - int(120000 * math.sin(t / 20)), "used": 1200000},
                    "system_uptime": t - START, "websocket_connections": 2,
                    "throttled_state": {"bits": 0x50000 if ARGS.undervoltage else 0,
                                        "flags": ["Previously Under-Voltage", "Previously Throttled"] if ARGS.undervoltage else []},
                    "network": {"wlan0": {"rx_bytes": 1, "tx_bytes": 1, "bandwidth": 1500.0}}}})
            if path == "/machine/system_info":
                return self.send(200, {"result": {"system_info": {
                    "cpu_info": {"cpu_count": 4, "bits": "64bit", "processor": "aarch64", "cpu_desc": "",
                                 "hardware_desc": "BCM2835", "model": "Raspberry Pi 4 Model B Rev 1.4",
                                 "total_memory": 3884356, "memory_units": "kB"},
                    "distribution": {"name": "Debian GNU/Linux 12 (bookworm)", "id": "debian"}}}})
            if path == "/server/files/directory":
                return self.send(200, {"result": {"dirs": [], "files": [], "root_info": {"name": "gcodes", "permissions": "rw"},
                                                  "disk_usage": {"total": 29_700_000_000, "used": 11_420_000_000,
                                                                 "free": 18_280_000_000}}})
            if path == "/machine/device_power/devices":
                return self.send(200, {"result": {"devices": [
                    {"device": k, "status": "on" if v else "off", "locked_while_printing": False, "type": "gpio"}
                    for k, v in S["power"].items()]}})
        self.err(404, "Not found: " + path)

    def do_POST(self):
        u = urlparse(self.path)
        q = parse_qs(u.query, keep_blank_values=True)
        path = u.path
        n = int(self.headers.get("Content-Length") or 0)
        body = self.rfile.read(n) if n else b""
        with LOCK:
            if self.offline(path):
                return
            if path == "/mock/set":
                if "scenario" in q:
                    apply_scenario(q["scenario"][0])
                if "progress" in q and S["file"]:
                    set_progress(float(q["progress"][0]) / 100)
                return self.send(200, self.snapshot())
            ok = {"result": "ok"}
            if path == "/printer/gcode/script":
                if S["klippy"] != "ready":
                    return self.err(503, "Klippy Host not connected")
                try:
                    run_gcode(json.loads(body or b"{}").get("script", ""))
                except ValueError as e:
                    log("!! " + str(e), "response")
                    return self.err(400, str(e))
                return self.send(200, ok)
            if path == "/printer/print/start":
                fn = q.get("filename", [""])[0]
                idx = next((i for i, f in enumerate(FILES) if f[0] == fn), None)
                if idx is None:
                    return self.err(400, "File not found: " + fn)
                reset("printing")
                start_job(idx, 0.0)
                S["ext"]["temp"], S["bed"]["temp"] = 160.0, 45.0
                log("// Starting print " + fn)
                return self.send(200, ok)
            if path == "/printer/print/pause" and S["pstate"] == "printing":
                S["pstate"], S["message"], S["scenario"] = "paused", "Paused", "paused"
                return self.send(200, ok)
            if path == "/printer/print/resume" and S["pstate"] == "paused":
                S["pstate"], S["scenario"] = "printing", "printing"
                S["ext"]["target"] = 215
                return self.send(200, ok)
            if path == "/printer/print/cancel" and S["pstate"] in ("printing", "paused"):
                S["pstate"], S["scenario"], S["message"] = "cancelled", "cancelled", ""
                S["ext"]["target"] = S["bed"]["target"] = 0
                return self.send(200, ok)
            if path == "/printer/emergency_stop":
                S["klippy"], S["klippy_msg"], S["scenario"] = "shutdown", "Shutdown due to webhooks request", "shutdown"
                return self.send(200, ok)
            if path in ("/printer/restart", "/printer/firmware_restart"):
                reset("idle")
                return self.send(200, ok)
            if path in ("/server/restart", "/machine/reboot", "/machine/shutdown"):
                return self.send(200, ok)
            if path == "/machine/device_power/device":
                d = q.get("device", [""])[0]
                if d in S["power"]:
                    S["power"][d] = not S["power"][d]
                    return self.send(200, {"result": {d: "on" if S["power"][d] else "off"}})
                return self.err(404, "Device not found")
        self.err(400, "Invalid request: " + path)


if __name__ == "__main__":
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--port", type=int, default=7125)
    ap.add_argument("--speed", type=float, default=20.0, help="print simulation speed multiplier (default 20x)")
    ap.add_argument("--scenario", default="printing", choices=SCENARIOS.keys())
    ap.add_argument("-v", "--verbose", action="store_true", help="log every request")
    ap.add_argument("--undervoltage", action="store_true", help="report Pi under-voltage/throttling flags")
    ARGS = ap.parse_args()
    with LOCK:
        apply_scenario(ARGS.scenario)
    threading.Thread(target=sim_loop, args=(ARGS.speed,), daemon=True).start()
    srv = ThreadingHTTPServer(("0.0.0.0", ARGS.port), Handler)
    ips = set()
    try:
        import socket
        s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        s.connect(("10.255.255.255", 1))  # no packet is sent; just picks the LAN interface
        ips.add(s.getsockname()[0])
        s.close()
    except OSError:
        pass
    print("Mock Moonraker listening on all interfaces, port %d" % ARGS.port)
    for ip in sorted(ips) or ["<this-machine-ip>"]:
        print("  control page:      http://%s:%d/" % (ip, ARGS.port))
        print("  display settings:  printer IP %s, port %d" % (ip, ARGS.port))
    print("If the display can't connect, allow the port in the firewall (e.g. 'sudo ufw allow %d/tcp')." % ARGS.port, flush=True)
    srv.serve_forever()
