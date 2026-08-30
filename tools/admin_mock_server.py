#!/usr/bin/env python3
"""Serve the Tough admin page with fake multi-station data for UI work.

The page is read straight out of ``AdminServer.cpp`` (the PROGMEM raw string
is the single source of truth), so what you see here is exactly what the
controller serves. The JSON shapes mirror ``/api/overview`` and the command
endpoints; state changes (enrol, end session, edit member, limits) are kept in
memory so the page can be exercised end to end without hardware.

    python3 tools/admin_mock_server.py            # http://127.0.0.1:8765/
    python3 tools/admin_mock_server.py --port 9000
    python3 tools/admin_mock_server.py --local 2  # pretend to be Shower 2

Add ``?scenario=`` to the page URL to start in a particular state:
``idle`` (default), ``shower`` (someone showering), ``enroll`` (waiting for a
wristband), ``fault`` (SD down on Shower 2, Water Fill offline).
"""

import argparse
import json
import random
import re
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import parse_qs, urlparse

ROOT = Path(__file__).resolve().parent.parent
CPP = ROOT / "firmware" / "shower-controller" / "src" / "AdminServer.cpp"

ROLE_NAMES = ["Shower", "Water Fill", "RV Fill"]
CHANNELS = ["Quiet", "Purple Rain", "Africa", "Whose Bed Have Your Boots Been Under",
            "It's My House", "Dancing Queen", "What a Feeling", "Footloose",
            "Maniac", "Jesus Built My Hotrod"]
REASONS = ["BUTTON", "BUTTON", "BUTTON", "LIMIT", "TIMEOUT", "HANDOFF", "REBOOT", "REMOTE"]


def page_html():
    text = CPP.read_text()
    match = re.search(r'R"HTML\((.*?)\)HTML"', text, re.S)
    if not match:
        raise SystemExit(f"could not find the admin page raw string in {CPP}")
    return match.group(1)


class State:
    def __init__(self, local_id, scenario):
        self.lock = threading.Lock()
        self.local_id = local_id
        self.boot = time.time()
        self.limits = {"shower": {"gal": 10.0, "min": 20}, "water": {"gal": 10.0, "min": 60},
                       "rv": {"gal": 100.0, "min": 60}, "version": 7}
        self.members_version = 12
        names = ["Colton", "Dusty River", "Sparkle", "Marisol", "Big Tex", "Juniper", "Rook",
                 "Fern", "Ziggy", "Mo", "Lark", "Pepper", "Ocean", "Hank"]
        self.members = []
        for i, n in enumerate(names):
            shower = round(random.uniform(0, 45), 2)
            water = round(random.uniform(0, 12), 2)
            rv = round(random.uniform(0, 30), 2) if i % 4 == 0 else 0.0
            self.members.append({
                "uid": "04%02X%02X%02X%02X%02X%02X" % tuple(random.randrange(256) for _ in range(6)),
                "name": n, "allowance": 12.0 if i == 4 else 0.0, "enabled": i not in (2, 7),
                "gallons": shower, "sessions": int(shower / 6), "networkGallons": shower + water + rv,
                "networkSessions": int(shower / 6) + int(water / 3) + int(rv / 15),
                "showerGallons": shower, "waterGallons": water, "rvGallons": rv,
                "pulses": int(shower * 450)})
        self.stations = {}
        for sid, role in ((1, 0), (2, 0), (3, 1), (4, 2)):
            self.stations[sid] = {
                "online": True, "last_seen": time.time(), "role": role,
                "session": None, "session_start": 0.0, "pump": False,
                "door": 0, "enroll": None, "cal": False, "cal_pulses": 0,
                "cal_msg": "Ready to calibrate", "ppg": 450.0,
                "speaker": "connected" if role == 0 else "absent",
                "playback": "idle", "audio_file": True, "volume": 60,
                "knob_raw": 1873, "knob_channel": 2, "knob_cal": True, "knob_cal_active": False,
                "knob_next": 0, "positions": [120, 560, 990, 1420, 1860, 2300, 2740, 3180, 3620, 4030],
                "hub": True, "relay": True, "rfid": True, "sd": True, "underruns": 0,
                "message": "Ready", "recent": []}
            for _ in range(random.randint(3, 8)):
                m = random.choice(self.members)
                g = round(random.uniform(0.8, 12.0), 3)
                self.stations[sid]["recent"].append({
                    "name": m["name"], "uid": m["uid"], "gallons": g,
                    "durationS": int(g * 60 + random.randint(-20, 40)),
                    "reason": random.choice(REASONS)})
        if scenario == "shower":
            self.start_session(1, "Colton")
        elif scenario == "enroll":
            self.stations[2]["enroll"] = "Rook"
        elif scenario == "fault":
            self.stations[2]["sd"] = False
            self.stations[3]["online"] = False
            self.stations[3]["last_seen"] = time.time() - 400
            self.stations[1]["speaker"] = "searching"

    def start_session(self, sid, name):
        s = self.stations[sid]
        s.update(session={"name": name, "gallons": 0.0,
                          "limit": self.limits[["shower", "water", "rv"][s["role"]]]["gal"]},
                 session_start=time.time(), pump=True, door=1, message=f"{name} logged in")

    def tick(self):
        for s in self.stations.values():
            if s["session"] and s["pump"]:
                s["session"]["gallons"] += 0.05
                if s["session"]["gallons"] >= s["session"]["limit"]:
                    self.end_session(s, "LIMIT")
            if s["cal"]:
                s["cal_pulses"] += 12
                s["cal_msg"] = "Dispensing"
            if s["role"] == 0 and s["speaker"] == "connected":
                s["knob_raw"] = 1873 + random.randint(-6, 6)

    def end_session(self, s, reason):
        if not s["session"]:
            return
        s["recent"].insert(0, {"name": s["session"]["name"], "uid": "", "gallons": round(s["session"]["gallons"], 3),
                               "durationS": int(time.time() - s["session_start"]), "reason": reason})
        del s["recent"][8:]
        s.update(session=None, pump=False, door=0, message=f"Session ended ({reason})")

    def telemetry(self, sid):
        s = self.stations[sid]
        se = s["session"]
        music = s["role"] == 0
        return {
            "calibrationActive": s["cal"], "calibrationPulses": s["cal_pulses"],
            "calibrationMessage": s["cal_msg"], "pulsesPerGallon": s["ppg"],
            "speaker": s["speaker"] if music else "n/a", "audioPlayback": s["playback"],
            "audioFile": s["audio_file"], "speakerVolume": s["volume"],
            "musicKnobRaw": s["knob_raw"], "musicChannel": s["knob_channel"],
            "musicChannelName": CHANNELS[s["knob_channel"]], "musicKnobCalibrated": s["knob_cal"],
            "musicCalibrationActive": s["knob_cal_active"], "musicCalibrationNext": s["knob_next"],
            "musicPositions": s["positions"] if s["knob_cal"] else [None] * 10,
            "enrollmentPending": bool(s["enroll"]), "pendingName": s["enroll"] or "",
            "message": s["message"],
            "features": {"music": music, "leds": music, "doorSign": music},
            "health": {"uptimeS": int(time.time() - self.boot) + 11520 * sid, "freeHeap": 118000 - sid * 3000,
                       "minFreeHeap": 96000, "wifiClients": 1 if sid == self.local_id else 0,
                       "hub": s["hub"], "relay": s["relay"], "rfid": s["rfid"], "sd": s["sd"],
                       "audioUnderruns": s["underruns"]},
            "session": {"active": bool(se), "name": se["name"] if se else "",
                        "gallons": round(se["gallons"], 3) if se else 0.0,
                        "limit": se["limit"] if se else 0.0, "pumpOn": s["pump"],
                        "doorState": s["door"] if s["hub"] and s["relay"] and s["sd"] else 2}}

    def overview(self):
        with self.lock:
            self.tick()
            local = self.stations[self.local_id]
            stations = []
            peers = []
            for sid, s in self.stations.items():
                is_local = sid == self.local_id
                stations.append({
                    "id": sid, "name": ["", "Shower 1", "Shower 2", "Water Fill", "RV Fill"][sid],
                    "role": s["role"], "roleName": ROLE_NAMES[s["role"]], "local": is_local,
                    "online": is_local or s["online"],
                    "lastSeenS": 0 if is_local else int(time.time() - s["last_seen"]),
                    "telemetry": self.telemetry(sid), "recent": s["recent"]})
                if not is_local:
                    peers.append({"id": sid, "name": stations[-1]["name"], "role": ROLE_NAMES[s["role"]],
                                  "online": s["online"], "lastSeenS": stations[-1]["lastSeenS"],
                                  "state": ["OPEN", "IN_USE", "UNAVAILABLE"][s["door"]],
                                  "membersVersion": self.members_version, "limitsVersion": self.limits["version"]})
            status = {
                "station": stations[self.local_id - 1]["name"], "ip": "192.168.4.1",
                "stationId": self.local_id, "role": local["role"], "roleName": ROLE_NAMES[local["role"]],
                "ssid": "CampShower", "pagePassword": False,
                "features": {"music": local["role"] == 0, "leds": local["role"] == 0, "doorSign": local["role"] == 0},
                "limits": self.limits, "membersVersion": self.members_version,
                "net": {"ready": True, "channel": 1, "rx": 4021 + int(time.time() - self.boot), "rxDropped": 0,
                        "tx": 3877 + int(time.time() - self.boot), "txFail": 2},
                "peers": peers, "enrollmentPending": bool(local["enroll"]), "pendingName": local["enroll"] or "",
                "lastUid": "", "message": local["message"]}
            return {"status": status, "health": self.telemetry(self.local_id)["health"],
                    "members": self.members, "sessions": local["recent"], "stations": stations}

    def run(self, sid, action, args):
        s = self.stations[sid]
        music = s["role"] == 0
        if action in ("tone", "play", "stop", "volume", "findSpeaker", "musicCalStart",
                      "musicCalCapture", "musicCalCancel") and not music:
            return 501, "Not available on a fill station"
        if action == "enroll":
            name = args.get("name", "").strip()
            if not (1 <= len(name) <= 32):
                return 400, "Name must be 1-32 characters"
            s["enroll"] = name
            s["message"] = "Waiting for wristband"
            # A fake tap arrives after a few seconds.
            threading.Timer(6.0, self.fake_tap, args=(sid,)).start()
            return 200, "Tap wristband on reader"
        if action == "cancel":
            s["enroll"] = None
            s["message"] = "Enrollment cancelled"
            return 200, s["message"]
        if action == "calStart":
            s.update(cal=True, cal_pulses=0, cal_msg="Starting…")
            return 200, "Calibration requested"
        if action == "calStop":
            try:
                gal = float(args.get("gallons", "0"))
            except ValueError:
                gal = 0.0
            if gal <= 0:
                return 400, "Enter a known volume"
            if s["cal"]:
                s["ppg"] = round(s["cal_pulses"] / gal, 2)
            s.update(cal=False, cal_msg=f"Saved {s['ppg']} pulses/gal")
            return 200, "Stop requested"
        if action == "musicCalStart":
            s.update(knob_cal_active=True, knob_next=0)
            return 200, "Music knob calibration requested"
        if action == "musicCalCapture":
            if not s["knob_cal_active"]:
                return 409, "Start music knob calibration first"
            s["knob_next"] += 1
            if s["knob_next"] >= 10:
                s.update(knob_cal_active=False, knob_next=0, knob_cal=True)
            return 200, "Position capture requested"
        if action == "musicCalCancel":
            s.update(knob_cal_active=False, knob_next=0)
            return 200, "Music knob calibration cancelled"
        if action == "tone":
            if s["speaker"] != "connected":
                return 409, "Speaker not connected"
            s["playback"] = "test tone"
            return 200, "Test tone started"
        if action == "play":
            if s["speaker"] != "connected" or not s["audio_file"]:
                return 409, "Connect speaker and upload audio first"
            s["playback"] = "playing /CH1.PCM"
            return 200, "Song started"
        if action == "stop":
            s["playback"] = "idle"
            return 200, "Audio stopped"
        if action == "volume":
            v = args.get("volume", "")
            if not v.isdigit() or not 0 <= int(v) <= 100:
                return 400, "Volume must be 0-100"
            s["volume"] = int(v)
            return 200, f"Speaker volume set to {v}%"
        if action == "findSpeaker":
            s["speaker"] = "searching"
            threading.Timer(4.0, lambda: s.update(speaker="connected")).start()
            return 200, "Searching for speaker"
        if action == "reboot":
            self.end_session(s, "REBOOT")
            s["online"] = False
            threading.Timer(5.0, lambda: s.update(online=True, last_seen=time.time())).start()
            return 200, "Rebooting"
        if action == "endSession":
            if not s["session"]:
                return 409, "No active session"
            self.end_session(s, "REMOTE")
            return 200, "Ending session"
        return 400, "Unknown action"

    def fake_tap(self, sid):
        with self.lock:
            s = self.stations[sid]
            if not s["enroll"]:
                return
            self.members_version += 1
            self.members.append({
                "uid": "04%02X%02X%02X%02X%02X%02X" % tuple(random.randrange(256) for _ in range(6)),
                "name": s["enroll"], "allowance": 0.0, "enabled": True, "gallons": 0.0, "sessions": 0,
                "networkGallons": 0.0, "networkSessions": 0, "showerGallons": 0.0,
                "waterGallons": 0.0, "rvGallons": 0.0, "pulses": 0})
            s["message"] = f"{s['enroll']} enrolled"
            s["enroll"] = None


class Handler(BaseHTTPRequestHandler):
    state: State = None
    page: str = ""
    pending = {}

    def log_message(self, fmt, *args):  # quieter
        if "/api/overview" not in (args[0] if args else ""):
            super().log_message(fmt, *args)

    def send_json(self, code, obj):
        body = json.dumps(obj).encode()
        self.send_response(code)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def msg(self, code, message):
        self.send_json(code, {"ok": code == 200, "message": message})

    def form(self):
        length = int(self.headers.get("Content-Length", "0"))
        raw = self.rfile.read(length).decode() if length else ""
        if self.headers.get("Content-Type", "").startswith("multipart/"):
            return {"_multipart": raw}
        return {k: v[0] for k, v in parse_qs(raw, keep_blank_values=True).items()}

    def do_GET(self):
        url = urlparse(self.path)
        if url.path == "/":
            body = self.page.encode()
            self.send_response(200)
            self.send_header("Content-Type", "text/html; charset=utf-8")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)
        elif url.path == "/api/overview":
            self.send_json(200, self.state.overview())
        elif url.path == "/api/command":
            nonce = parse_qs(url.query).get("nonce", ["0"])[0]
            p = self.pending.get(nonce)
            if not p:
                self.send_json(200, {"state": "unknown", "ok": False, "status": 1, "message": ""})
            elif time.time() < p["ready"]:
                self.send_json(200, {"state": "pending", "ok": False, "status": 0, "message": ""})
            else:
                self.send_json(200, {"state": "done", "ok": p["code"] == 200,
                                     "status": 0 if p["code"] == 200 else 3 if p["code"] == 501 else 1,
                                     "message": p["message"]})
        else:
            self.msg(404, "Not found")

    def do_POST(self):
        url = urlparse(self.path)
        args = self.form()
        st = self.state
        with st.lock:
            if url.path == "/api/command":
                try:
                    sid = int(args.get("station", "0"))
                except ValueError:
                    sid = 0
                if sid not in st.stations:
                    return self.msg(400, "Unknown station")
                if sid == st.local_id:
                    code, message = st.run(sid, args.get("action", ""), args)
                    return self.msg(code, message)
                if not st.stations[sid]["online"]:
                    nonce = str(random.randrange(1, 1 << 31))
                    self.pending[nonce] = {"ready": time.time() + 30, "code": 0, "message": ""}
                    return self.send_json(202, {"ok": True, "pending": True, "nonce": int(nonce)})
                code, message = st.run(sid, args.get("action", ""), args)
                nonce = str(random.randrange(1, 1 << 31))
                self.pending[nonce] = {"ready": time.time() + 0.9, "code": code, "message": message}
                return self.send_json(202, {"ok": True, "pending": True, "nonce": int(nonce)})
            if url.path == "/api/member":
                m = next((x for x in st.members if x["uid"] == args.get("uid")), None)
                name = args.get("name", "").strip()
                if not m or not (1 <= len(name) <= 32):
                    return self.msg(400, "Member update failed")
                try:
                    m["allowance"] = float(args.get("allowance", "0") or 0)
                except ValueError:
                    return self.msg(400, "Member update failed")
                m["name"] = name
                m["enabled"] = args.get("enabled") == "1"
                st.members_version += 1
                return self.msg(200, "Member updated")
            if url.path == "/api/delete":
                before = len(st.members)
                st.members = [x for x in st.members if x["uid"] != args.get("uid")]
                if len(st.members) == before:
                    return self.msg(404, "Registration not found")
                st.members_version += 1
                return self.msg(200, "Registration deleted")
            if url.path == "/api/limits":
                try:
                    new = {k: {"gal": float(args[k + "Gal"]), "min": int(args[k + "Min"])}
                           for k in ("shower", "water", "rv")}
                except (KeyError, ValueError):
                    return self.msg(400, "Fill in every limit")
                for v in new.values():
                    if not (0.5 <= v["gal"] <= 500 and 1 <= v["min"] <= 180):
                        return self.msg(400, "Gallons must be 0.5-500 and minutes 1-180")
                st.limits.update(new)
                st.limits["version"] += 1
                return self.msg(200, "Station limits saved")
            if url.path == "/api/password":
                if not (8 <= len(args.get("password", "")) <= 64):
                    return self.msg(400, "Password must be 8-64 characters")
                return self.msg(200, "Password changed")
            if url.path == "/api/audio/upload":
                size = len(args.get("_multipart", ""))
                return self.msg(200, f"Uploaded {size} bytes")
        self.msg(404, "Not found")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--port", type=int, default=8765)
    ap.add_argument("--local", type=int, default=1, help="station id this mock pretends to be (1-4)")
    ap.add_argument("--scenario", default="idle", choices=["idle", "shower", "enroll", "fault"])
    opts = ap.parse_args()
    Handler.state = State(opts.local, opts.scenario)
    Handler.page = page_html()
    server = ThreadingHTTPServer(("127.0.0.1", opts.port), Handler)
    print(f"admin page mock: http://127.0.0.1:{opts.port}/  (local station {opts.local}, scenario {opts.scenario})")
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass


if __name__ == "__main__":
    main()
