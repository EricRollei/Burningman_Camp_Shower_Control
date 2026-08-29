# Software Specification

Firmware for the M5Stack Tough controller. One image per station (two
showers, water fill, RV fill) from a single codebase. Stations keep their own
logs and sync members, limits and per-person usage totals live over a
router-free ESP-NOW network; see `firmware/shower-controller/README.md`.

------------------------------------------------------------------------

## Platform

- **Controller:** M5Stack Tough (ESP32)
- **I²C hub:** M5Stack PaHub
- **Framework:** ❓ Arduino / ESP-IDF / PlatformIO (TBD)

------------------------------------------------------------------------

## Modules

| Module | Responsibility |
|--------|----------------|
| Boot | Hardware init, self-test |
| Configuration | Settings, water allowance policy |
| RFID | Read UID from M5Stack RFID2 |
| User database | Map UID → user, allowance |
| Pump control | Drive pump relay during authorized session |
| Flow measurement | Count flow-sensor pulses → volume |
| Lighting | LED strip effects / color select |
| USB controller | Session-gated USB-C power |
| Display / UI | Screens & workflow (see [UI.md](UI.md)) |
| Logging | Append CSV records to SD card |
| Diagnostics | Fault reporting, bench test hooks |

------------------------------------------------------------------------

## I²C Devices (via PaHub)

- RFID2 reader
- M5Stack 4Relay
- Keypad / buttons
- Future I²C modules

> ❓ Decide whether to inject separate 5V into the PaHub.

------------------------------------------------------------------------

## Relay Allocation

| Relay | Load |
|:---:|------|
| 1 | Pump (via automotive relay) |
| 2 | USB-C power |
| 3 | LED strip power |
| 4 | Utility lights / spare |

------------------------------------------------------------------------

## Pin Assignment

> _TODO: finalize and lock GPIO map after bench test._

| Signal | Pin | Direction | Notes |
|--------|-----|-----------|-------|
| Flow sensor pulse | TBD | Input (interrupt) | Measure output voltage first |
| Shower switch | TBD | Input | |
| LED strip data | TBD | Output | |
| Spare | TBD | — | |

------------------------------------------------------------------------

## User Workflow

1. Idle
2. Scan tag
3. Display welcome
4. Enable shower
5. User selects LED color / effect
6. Pump enabled
7. Flow measured
8. Session logged
9. Return to idle

------------------------------------------------------------------------

## Flow Measurement

```
Flow sensor pulse output → GPIO interrupt → Pulse count → Calibration → Gallons/Liters
```

Calibration constant is per-station (see [Calibration.md](Calibration.md)).

------------------------------------------------------------------------

## Data Logging

CSV record fields:

- Timestamp
- User
- UID
- Gallons
- Elapsed time
- Water flow time
- Average flow

> _TODO: define exact CSV header, units, and filename convention._

------------------------------------------------------------------------

## RFID / NFC

- Reader: M5Stack RFID2
- Candidate tags: NTAG213 / NTAG215 / NTAG216
- If compatible, NFC tags become the standard.
- ❓ Define RFID enrollment workflow.

------------------------------------------------------------------------

## Open Software Decisions

- Water allowance policy (per-session? per-day? per-user?).
- Final UI.
- RFID enrollment workflow.
