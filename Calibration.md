# Flow Meter Calibration

Each shower is calibrated **independently**. The calibration constant
converts flow-sensor pulses into delivered volume.

------------------------------------------------------------------------

## Principle

```
Pulses ÷ Pulses-per-unit (K) = Volume
```

`K` (pulses per gallon or per liter) is unique to each sensor + plumbing
installation because backpressure and mounting affect pulse output.

------------------------------------------------------------------------

## Procedure

1. Place the shower in a calibration/test mode that counts raw pulses.
2. Dispense into a **known-volume** container (e.g., a calibrated bucket
   or graduated jug).
3. Record raw pulse count for the dispensed volume.
4. Compute `K = pulses / volume`.
5. Repeat 3–5 times and average.
6. Store `K` in per-station configuration.
7. Verify by dispensing a different known volume and checking error.

------------------------------------------------------------------------

## Data Log

| Run | Known volume | Pulses | K (pulses/unit) |
|:---:|:---:|:---:|:---:|
| 1 | | | |
| 2 | | | |
| 3 | | | |
| **Avg** | | | |

Target accuracy: ❓ define acceptable error (e.g., ±3%).

------------------------------------------------------------------------

## Notes

- Re-check calibration after any plumbing change.
- Record the flow-sensor part and mounting orientation.
- Measure the sensor's pulse output voltage before wiring to GPIO
  (see [Wiring.md](Wiring.md)).
