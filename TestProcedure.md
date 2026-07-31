# Test Procedure — Bench Checklist

Run before field deployment. Perform for **each** station and record
results.

Station: `____`   Date: `____`   Tester: `____`

------------------------------------------------------------------------

## 1. Power & Protection

- [ ] Battery voltage reads ~12.8V+ at fuse block.
- [ ] 30A breaker trips/resets correctly.
- [ ] Each branch fuse correct value and seated.
- [ ] Solar charger brings in current in sunlight.
- [ ] AC charger charges from generator.

## 2. Controller

- [ ] M5Stack Tough boots from 12V (RS485 power).
- [ ] Display shows Idle screen.
- [ ] SD card detected; test log file writable.
- [ ] RTC time correct (if fitted).

## 3. RFID / NFC

- [ ] Known tag → authorized flow.
- [ ] Unknown tag → denied.
- [ ] NTAG213 / 215 / 216 read test recorded.

## 4. Pump Control

- [ ] Pump relay energizes only during authorized session.
- [ ] Automotive relay clicks; pump runs.
- [ ] Pump stops on session end.

## 5. Flow Measurement

- [ ] Pulses counted while water flows.
- [ ] Calibration constant applied (see [Calibration.md](Calibration.md)).
- [ ] Measured volume within target error vs known volume.

## 6. Accessories

- [ ] USB-C outputs power only during session.
- [ ] LED strip powers and shows selected color/effect.
- [ ] Utility lights switch correctly.

## 7. Logging

- [ ] CSV record written on session end.
- [ ] Fields present: timestamp, user, UID, gallons, elapsed, flow time,
      avg flow.

## 8. Full Session Dry-Run

- [ ] Idle → scan → welcome → color select → pump → flow → summary →
      log → idle.

------------------------------------------------------------------------

## Notes / Issues

```
(record anomalies, measured values, and follow-ups here)
```
