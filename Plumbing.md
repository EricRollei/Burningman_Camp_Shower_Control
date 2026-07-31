# Plumbing

Water path from the camp storage tank to the shower head.

See [drawings/EnclosureLayout_v1.svg](drawings/EnclosureLayout_v1.svg).

------------------------------------------------------------------------

## Flow Path

```text
500 gal tank
    ↓
Inline strainer (Shurflo 255-213)
    ↓
SEAFLO 42 Pump (12V, 3 GPM, 55 PSI)
    ↓
Accumulator
    ↓
 ├── Cold bypass
 └── Water heater (CAMPLUX propane, tankless)
        ↓
Thermostatic mixing valve
        ↓
Flow sensor (SENSTREE G1/2" Hall)
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
- ⚠️ The water heater is a **propane** tankless unit (CAMPLUX, 41k BTU),
  not electric. It needs a propane supply + regulator and its own
  ignition batteries (typically 2× D-cell) — it is **not** a 12V system
  load.
- The pre-pump strainer (Shurflo 255-213) protects the pump; an optional
  finer filter can be added.
- Each shower is plumbed and calibrated independently
  (see [Calibration.md](Calibration.md)).

------------------------------------------------------------------------

## Open Items

- Propane supply, regulator, and hose sizing for the heater.
- ❓ Fittings / hose sizes and full material list (see BOM.md).
- ❓ Winterization / draining procedure for teardown.
