# Burning Man Shower Controller Project

Two independent, solar-powered shower stations for Burning Man with
RFID/NFC access control and per-person water logging.

## Project Goals

- Authenticate users via RFID/NFC tags.
- Measure and log water usage per person.
- Enable the pump only for authorized users.
- Store logs on an SD card.
- Control decorative and utility lighting.
- Provide USB-C charging only during an active shower session.
- Operate primarily from solar with generator-assisted charging.
- Support quick battery swaps using a single Anderson connector.
- Survive the harsh playa environment.

The two showers are completely independent. Their CSV logs can be merged
after the event.

------------------------------------------------------------------------

## System at a Glance

- **Water:** 500 gal tank → filter → SEAFLO 12V pump → accumulator →
  heater/cold bypass → mixing valve → flow sensor → shower head.
- **Power:** 12.8V 40Ah LiFePO₄ per station, 50W solar +
  generator-assisted AC charging, 30A breaker + fused distribution.
- **Control:** M5Stack Tough + PaHub (I²C), RFID2 reader, 4Relay,
  flow-sensor pulse counting, SD-card CSV logging.

------------------------------------------------------------------------

## Documentation Index

| Document | Purpose |
|----------|---------|
| [BOM.md](BOM.md) | Bill of Materials |
| [Wiring.md](Wiring.md) | Electrical wiring & power |
| [Plumbing.md](Plumbing.md) | Plumbing layout |
| [SoftwareSpec.md](SoftwareSpec.md) | Firmware architecture |
| [UI.md](UI.md) | Screen layouts & workflow |
| [Calibration.md](Calibration.md) | Flow meter calibration |
| [Assembly.md](Assembly.md) | Step-by-step build guide |
| [TestProcedure.md](TestProcedure.md) | Bench testing checklist |
| [drawings/](drawings/) | Schematics & layout diagrams |

------------------------------------------------------------------------

## Remaining Decisions

### Hardware
- Select LED strip.
- Decide LED switching (relay vs MOSFET).
- Add battery monitor.

### Electrical
- Finalize fuse sizes.
- Measure flow sensor output voltage.
- Determine whether to inject separate 5V into the PaHub.

### Software
- Water allowance policy.
- Final UI.
- RFID enrollment workflow.

------------------------------------------------------------------------

## Next Phase

Produce a complete engineering package:

1. Electrical schematic.
2. Mechanical layout.
3. Pin assignment document.
4. Software specification.
5. Bill of Materials (BOM).

This README is the project index; detailed content lives in the linked
documents above.
