# Firmware Agent Guide

This file applies to everything under `firmware/`.

## Projects

- `shower-controller/` is the M5Stack Tough station firmware. One codebase
  builds four station images (`shower1`, `shower2`, `water_fill`, `rv_fill`)
  selected by `STATION_ID` / `STATION_ROLE` build flags. It owns RFID
  authorization, relay and pump control, flow accounting, SD-card persistence,
  the admin access point, the CampNet ESP-NOW link, and (shower role only)
  Bluetooth audio and lighting.
- `door-display/` is a separate M5NanoC6 PlatformIO project (`door1`, `door2`).
  It drives the SH1107 OLED and treats its shower's CampNet `STATUS`
  broadcast as authoritative.
- `shared/` holds the wire protocol both projects compile against
  (`CampNetProtocol.h`, `CampNetEspNow.h`); it is on both include paths via
  `build_flags = -I../shared`.
- `audio-reactive-led/` is a standalone Arduino sketch for an AtomS3 with an
  Atomic Echo Base. It is a prototype, not part of either PlatformIO build.

Read the README in the project being changed before editing. The repository's
top-level `README.md` contains the system wiring and plumbing design. The
top-level `SMOKETESTS.md` is the canonical connected-hardware reliability
checklist for the firmware.

## Build and Validation

Run commands from the repository root:

```sh
pio run --project-dir firmware/shower-controller -e shower1 -e shower2 -e water_fill -e rv_fill
pio run --project-dir firmware/door-display -e door1 -e door2
```

There is currently no automated test suite. At minimum, build every PlatformIO
environment touched by a change (a role-gated change must build for a shower
and a fill env). If anything under `firmware/shared/` or CampNet behavior
changes, build both projects. Report when a build cannot run because
PlatformIO, cached dependencies, or the target toolchain is unavailable.

Build the two projects one after the other, never concurrently: the Tough
(espressif32 6.9 / core 2.0.17) and the NanoC6 (pioarduino / core 3.3.9) both
install into `~/.platformio/packages/framework-arduinoespressif32`, so
switching projects re-downloads the framework and a parallel build sees a
half-written package. If the first build after a switch fails with a Python
`TypeError` or a missing framework header, simply run it again.

Use `SMOKETESTS.md` for hardware validation. Run the checks relevant to a
change when suitable hardware is connected, and run the complete checklist
before a field release or pre-burn handoff. Do not mark a check complete based
only on code review or a successful build. Add or revise a smoke test whenever
user-visible or reliability-sensitive behavior changes; keep the detailed
procedure in `SMOKETESTS.md` rather than duplicating it here.

Do not upload firmware or open a serial monitor unless the user explicitly
asks for hardware interaction and the target port has been confirmed. The
`upload_port`, `monitor_port`, and the local `tool-esptoolpy` path in the
PlatformIO files are workstation-specific.

The AtomS3 `.ino` has no checked-in CLI build configuration. Validate changes
to it with the Arduino IDE setup documented at the top of the sketch, or state
clearly that only source review was performed.

## Safety-Critical Invariants

Treat pump and relay behavior as safety-critical.

- Boot, session exit, timeout, storage failure, and relay communication failure
  must leave all relays off.
- The pump must never start merely from an RFID scan. A valid active session
  and a first explicit start action are required: either the GPIO14 button or
  a tap on the on-screen START circle. The next action ends the session and
  turns the pump off; it does not merely pause the water. Both inputs go
  through the same `toggleShowerWater()` path; do not add a second one.
- The Tough touchscreen carries only the backup START/STOP circle on the
  in-progress screen; keep every other screen display-only. Touch must use
  the press edge plus `TOUCH_DEBOUNCE_MS`, never level or repeat events.
  Preserve the post-session gallons and elapsed-time summary before returning
  to the ready screen.
- Unknown or disabled tags must not open a session or terminate another
  member's session. A different authorized tag ends the prior session with
  reason `HANDOFF` before starting the new member's session.
- Preserve the session gallon limit and the 20-minute safety timeout unless a
  requirements change explicitly calls for different behavior.
- Calibration is the one intentional flow-calibration path that energizes the
  pump. It must retain a bounded timeout and a reliable stop path.
- Never connect or document 5 V or 12 V as safe for an ESP32 GPIO. Preserve the
  protected GPIO26 flow input, the GPIO36 3.3 V potentiometer wiring, common
  grounds, and separately powered LED-strip guidance.
- The door display must fail to `OFFLINE` after loss of valid controller status;
  its local button must not override the Tough's occupancy state. It must only
  act on `STATUS` packets whose `stationId` equals its `DOOR_STATION_ID`.
- A dashboard reboot request (local or remote) must stop the pump and record an
  active session with reason `REBOOT` before restarting. Preserve the watchdog
  as a recovery path for a wedged main loop.
- Unauthenticated CampNet traffic (STATUS/USAGE/MEMBERS/LIMITS/TELEMETRY/
  RECENT) only updates data: ledger, registry, limits, telemetry tables. Only
  HMAC-signed COMMAND packets may act, and the only relay-adjacent actions are
  the same ones the local admin page has (calibration start/stop, end
  session, reboot). A remote command must never open a session or turn the
  pump on for one. Keep `CampNet::SECRET` and the Wi-Fi password private.
- Keep large CPU-only tables out of static/BSS memory: internal DRAM on the
  Tough is within a few tens of KB of exhaustion with Bluetooth + Wi-Fi up.
  Allocate them with `psramArray<>()` (`include/PsramAlloc.h`) in `begin()`,
  and keep `[HEALTH] min_heap` above ~30 KB on a shower build with the speaker
  connected and the admin page open.
- Role limits are bounded (`MIN/MAX_LIMIT_GALLONS`, `MIN/MAX_LIMIT_MINUTES` in
  `Config.h`); reject out-of-range values on both the admin and network paths
  so a bad packet cannot lengthen the safety timeout.

When changing lifecycle logic, trace every return/error path that can follow a
relay-on operation and ensure it reaches a stop/all-off action.

## Shared Contracts

Every wire-level constant lives once in `firmware/shared/CampNetProtocol.h`
and is compiled into both the Tough and the NanoC6:

- `CampNet::CHANNEL` (the Wi-Fi channel every soft-AP and door sign is pinned
  to), `MAGIC`, `PROTOCOL_VERSION`, `MAX_STATIONS`.
- Packet types `STATUS`, `USAGE`, `MEMBERS`, `LIMITS` and their packed structs.
  Every packet has a `static_assert` keeping it at or below 250 bytes; do not
  remove it (the classic ESP32 only receives ESP-NOW v1-sized frames).
- Roles (`ROLE_SHOWER`, `ROLE_WATER_FILL`, `ROLE_RV_FILL`) and door states.

Bump `PROTOCOL_VERSION` when a struct layout changes; receivers drop frames
with an unknown version. `CampNetEspNow.h` holds the callback-signature shims
for Arduino core 2.x (Tough) versus 3.x (NanoC6); the transport code uses the
raw `esp_now_*` C API on both sides.

Station identity is compile-time only: `STATION_ID` and `STATION_ROLE` on the
Tough, `DOOR_STATION_ID` on the NanoC6. Names, SSIDs and feature gating derive
from them in `shower-controller/include/Config.h`. Never flash two devices with
the same station id.

Update the relevant firmware README whenever pins, wiring, credentials,
protocols, build steps, user-visible behavior, CSV schemas, or SD-card paths
change. The shared Wi-Fi name/password in `Config.h` are the deployed camp
credentials by the owner's decision (the admin page has no login of its own;
`ADMIN_PAGE_PASSWORD` re-enables HTTP Basic auth). `CampNet::SECRET` in the
shared header should still be changed from its prototype default.

## Code Conventions

- Follow the existing Arduino C++ style: two-space indentation, opening braces
  on the same line, `constexpr` for fixed configuration, and early returns for
  guard conditions.
- Put shared Tough configuration and pin assignments in
  `shower-controller/include/Config.h`; do not scatter duplicate magic values.
- Keep hardware-facing responsibilities in their existing classes under
  `include/` and `src/`. Reserve `main.cpp` for orchestration and lifecycle
  behavior where practical.
- Use rollover-safe elapsed-time comparisons such as
  `millis() - previous >= interval`; do not compare absolute future timestamps.
- Keep interrupt handlers minimal. Flow-pulse accounting shared with an ISR
  must remain safe for concurrent access.
- Avoid blocking delays in controller loops; RFID polling, UI, logging, audio,
  lighting, network service, watchdog resets, and safety timeouts must continue
  to run. Keep normal loop work comfortably below the 20-second watchdog.
- Keep SD/SPI operations out of Bluetooth callbacks and other concurrent tasks.
  Song files are read by the main loop into a PSRAM ring buffer; the Bluetooth
  task consumes only that buffer. Preserve this ownership boundary.
- Avoid repeated SD-card probes in frequently polled admin endpoints. Track or
  refresh cached state when files change, and preserve the dashboard's bounded,
  single-flight `/api/overview` polling and recovery behavior.
- Treat Bluetooth discovery as shared-radio load. Preserve the initial search
  window, held/backoff state, periodic retry, and explicit **Find speaker** path
  so an absent speaker cannot starve the Wi-Fi admin access point.
- Preserve append-only audit/history semantics unless a migration plan is part
  of the change. `/PULSETOT.CSV` is a derived per-tag snapshot used to bound
  boot-time replay of `/PULSES.CSV`; keep snapshot replacement recoverable and
  compatible with replaying the append-only log tail. Treat SD-card write
  failures as operational failures, not as successful sessions.
- Do not edit generated `.pio/` content or vendor libraries. Change
  `platformio.ini` when a dependency must change and retain/update
  `THIRD_PARTY_NOTICES.md` when copied or adapted third-party material changes.

## Change Review Checklist

Before handing off a firmware change:

1. Build each affected PlatformIO project.
2. Recheck relay-off and timeout behavior for controller lifecycle changes.
3. Check both ends of shared Tough/NanoC6 protocol changes, and that a
   protocol change bumped `PROTOCOL_VERSION`.
4. Update documentation for observable behavior or hardware changes.
5. Update and run the relevant checks in `SMOKETESTS.md`, or state which checks
   remain pending because connected hardware or a long-duration soak is needed.
6. Identify any verification that still requires connected hardware: relay
   state, RFID, flow pulses, SD persistence, display output, Bluetooth audio,
   LED timing, upload, or serial logs.
