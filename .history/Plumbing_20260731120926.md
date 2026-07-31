# Plumbing

Water path from the camp storage tank to the shower head.

See [drawings/EnclosureLayout_v1.svg](drawings/EnclosureLayout_v1.svg).

------------------------------------------------------------------------

## Flow Path

```text
500 gal tank
    ↓
Filter
    ↓
SEAFLO 42 Pump (12V, 3 GPM, 55 PSI)
    ↓
Accumulator
    ↓
 ├── Cold bypass
 └── Water heater
        ↓
Thermostatic mixing valve
        ↓
Flow sensor
        ↓
Shower valve
        ↓
Shower head
```

------------------------------------------------------------------------

## Notes

- The flow sensor is installed **after** the mixing valve so it measures
  the actual delivered water.
- The accumulator smooths pump cycling and reduces pressure pulsing.
- Cold bypass allows blending / a cold-only path.
- Each shower is plumbed and calibrated independently
  (see [Calibration.md](Calibration.md)).

------------------------------------------------------------------------

## Open Items

- ❓ Water heater selection (12V-compatible).
- ❓ Fittings / hose sizes and material list.
- ❓ Winterization / draining procedure for teardown.
