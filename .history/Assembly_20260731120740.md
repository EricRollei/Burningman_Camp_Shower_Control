# Assembly — Build Guide

Step-by-step build for one shower station. Repeat for the second
station. Read [Wiring.md](Wiring.md) and [Plumbing.md](Plumbing.md)
before starting.

> ⚠️ Work with the battery disconnected (Anderson unplugged, 30A breaker
> OFF) until powered testing begins.

------------------------------------------------------------------------

## 1. Enclosure & Layout

- Mount controller, PaHub, relay module, fuse block, and breaker.
- Plan cable routing; keep power and signal wiring separated.
- See [drawings/EnclosureLayout_v1.svg](drawings/EnclosureLayout_v1.svg).

## 2. Power Distribution

1. Mount 30A breaker and marine fuse block.
2. Run battery feed: Anderson → breaker → fuse block.
3. Land all negatives on the fuse block negative bus.
4. Do **not** connect the battery yet.

## 3. Charging Inputs

- Wire solar controller and AC charger to the battery-side node
  (ahead of the breaker).

## 4. Pump Circuit

- Wire 4Relay → automotive relay coil.
- Wire automotive relay contacts → pump (15A fused).

## 5. Control Electronics

- Power M5Stack Tough from 12V via RS485 connector (3–5A fused).
- Connect PaHub; attach RFID2, 4Relay, keypad via Grove.

## 6. Accessory Loads

- USB-C: 12V → relay → buck converter → panel outlet (5A fused).
- LED strip: fused 12V + relay; data from GPIO.
- Utility lights (10A fused).

## 7. Plumbing

- Assemble tank → filter → pump → accumulator → heater/bypass →
  mixing valve → flow sensor → shower valve → head.
- Install flow sensor **after** the mixing valve.

## 8. First Power-Up

1. Confirm all connections and fuse sizes.
2. Insert SD card.
3. Plug Anderson, close 30A breaker.
4. Verify controller boots and RFID reads a tag.
5. Proceed to [TestProcedure.md](TestProcedure.md).

------------------------------------------------------------------------

> _TODO: add photos and torque/fitting specs as the build progresses._
