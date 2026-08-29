# Firmware Agent Guide

This file applies to everything under `firmware/`.

## Projects

- `shower-controller/` is the primary M5Stack Tough firmware. It owns RFID
  authorization, relay and pump control, flow accounting, SD-card persistence,
  the admin access point, Bluetooth audio, lighting, and the UDP status server.
- `door-display/` is a separate M5NanoC6 PlatformIO project. It drives the
  SH1107 OLED and treats the Tough's UDP response as authoritative.
- `audio-reactive-led/` is a standalone Arduino sketch for an AtomS3 with an
  Atomic Echo Base. It is a prototype, not part of either PlatformIO build.

Read the README in the project being changed before editing. The repository's
top-level `README.md` contains the system wiring and plumbing design. The
top-level `SMOKETESTS.md` is the canonical connected-hardware reliability
checklist for the firmware.

## Build and Validation

Run commands from the repository root:

```sh
pio run --project-dir firmware/shower-controller
pio run --project-dir firmware/door-display
```

There is currently no automated test suite. At minimum, build every PlatformIO
project touched by a change. If shared Wi-Fi or UDP behavior changes, build
both projects. Report when a build cannot run because PlatformIO, cached
dependencies, or the target toolchain is unavailable.

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
  and a first explicit pump-button press are required. The next press ends the
  session and turns the pump off; it does not merely pause the water.
- Keep the Tough touchscreen display-only. Preserve the post-session gallons
  and elapsed-time summary before returning to the ready screen.
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
  its local button must not override the Tough's occupancy state.
- A dashboard reboot request must stop the pump and record an active session
  with reason `REBOOT` before restarting. Preserve the watchdog as a recovery
  path for a wedged main loop.

When changing lifecycle logic, trace every return/error path that can follow a
relay-on operation and ensure it reaches a stop/all-off action.

## Shared Contracts

The Tough and NanoC6 duplicate several wire-level constants because they are
separate firmware images. Keep both sides synchronized when changing:

- Wi-Fi SSID and password.
- UDP ports (`4210` on the Tough and `4211` on the NanoC6).
- Request text `SHOWER_DISPLAY_V1 STATUS?`.
- Response prefix `SHOWER_STATUS_V1 ` and states `OPEN`, `IN_USE`, and
  `UNAVAILABLE`.

Update the relevant firmware README whenever pins, wiring, credentials,
protocols, build steps, user-visible behavior, CSV schemas, or SD-card paths
change. Do not commit real deployment credentials; configuration currently
contains prototype defaults that must be replaced before field use.

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
3. Check both ends of shared Tough/NanoC6 protocol changes.
4. Update documentation for observable behavior or hardware changes.
5. Update and run the relevant checks in `SMOKETESTS.md`, or state which checks
   remain pending because connected hardware or a long-duration soak is needed.
6. Identify any verification that still requires connected hardware: relay
   state, RFID, flow pulses, SD persistence, display output, Bluetooth audio,
   LED timing, upload, or serial logs.
