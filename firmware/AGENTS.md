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
top-level `README.md` contains the system wiring and plumbing design.

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
  and an explicit pump-button action are required.
- Unknown or disabled tags must not open a session.
- Preserve the session gallon limit and the 20-minute safety timeout unless a
  requirements change explicitly calls for different behavior.
- Calibration is the one intentional flow-calibration path that energizes the
  pump. It must retain a bounded timeout and a reliable stop path.
- Never connect or document 5 V or 12 V as safe for an ESP32 GPIO. Preserve the
  protected GPIO26 flow input, the GPIO36 3.3 V potentiometer wiring, common
  grounds, and separately powered LED-strip guidance.
- The door display must fail to `OFFLINE` after loss of valid controller status;
  its local button must not override the Tough's occupancy state.

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
  lighting, network service, and safety timeouts must continue to run.
- Preserve append-only audit/history semantics unless a migration plan is part
  of the change. Treat SD-card write failures as operational failures, not as
  successful sessions.
- Do not edit generated `.pio/` content or vendor libraries. Change
  `platformio.ini` when a dependency must change and retain/update
  `THIRD_PARTY_NOTICES.md` when copied or adapted third-party material changes.

## Change Review Checklist

Before handing off a firmware change:

1. Build each affected PlatformIO project.
2. Recheck relay-off and timeout behavior for controller lifecycle changes.
3. Check both ends of shared Tough/NanoC6 protocol changes.
4. Update documentation for observable behavior or hardware changes.
5. Identify any verification that still requires connected hardware: relay
   state, RFID, flow pulses, SD persistence, display output, Bluetooth audio,
   LED timing, upload, or serial logs.
