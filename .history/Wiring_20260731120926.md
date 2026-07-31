# Wiring & Electrical

12V DC system. One fully independent electrical system per shower.

See [drawings/PowerDiagram_v1.svg](drawings/PowerDiagram_v1.svg) and
[drawings/ControlDiagram_v1.svg](drawings/ControlDiagram_v1.svg).

------------------------------------------------------------------------

## Battery

- 12.8V 40Ah LiFePO₄ battery (one per shower)
- One Anderson SB50 quick disconnect for the entire shower system
- Battery pigtail only contains: Positive, Negative, Anderson connector

## Charging

### Solar
50W panel → Solar charge controller → Battery

### Generator
110V generator → 10A LiFePO₄ charger → Battery

Both chargers connect to the **battery-side node** (ahead of the main
breaker).

### Battery removal sequence
1. Turn off AC charger.
2. Disconnect solar panel (or open solar disconnect).
3. Turn OFF 30A breaker.
4. Unplug Anderson connector.

Reconnect in reverse order.

------------------------------------------------------------------------

## Main Protection

Battery → 30A manual-reset breaker → Marine fuse block with integrated
negative bus.

The breaker functions as both the main disconnect and the main circuit
protection.

------------------------------------------------------------------------

## Fuse Block

Each positive branch is individually fused. All negative returns
terminate on the fuse block's integrated negative bus.

| Fuse | Circuit |
|------|---------|
| 15A | Pump |
| 5A | USB-C converter |
| LED | LED strip |
| 3–5A | M5Stack controller |
| 10A | Utility lights |
| Spare | Future expansion |

> ❓ Fuse sizes to be finalized (see Remaining Decisions).

------------------------------------------------------------------------

## Pump Control

The SEAFLO pump is switched by an automotive relay to keep the high
current off the M5Stack relay module.

```
M5Stack 4Relay → Automotive relay coil → Automotive relay contacts → Pump
```

------------------------------------------------------------------------

## USB-C Charging

```
12V → Relay → 12V→USB-C buck converter → USB-C outlet
```

Enabled only during an authorized shower session.

------------------------------------------------------------------------

## LED Lighting

- ~24 ft around shower perimeter
- 12V addressable strip (TBD)
- Power from fused 12V supply; main power switched by relay
- Data from M5Stack GPIO
- ❓ Switching method: relay vs MOSFET

------------------------------------------------------------------------

## Controller Power

M5Stack Tough powered directly from 12V through the RS485 connector.
USB-C reserved for programming.

------------------------------------------------------------------------

## Relay Allocation

1. Pump relay
2. USB-C power
3. LED strip power
4. Utility lights / spare

------------------------------------------------------------------------

## GPIO

- Flow sensor pulse input
- Shower switch
- LED strip data
- Spare GPIO

See [SoftwareSpec.md](SoftwareSpec.md) for the pin assignment table.

------------------------------------------------------------------------

## Connectors

| Connector | Use |
|-----------|-----|
| Anderson SB50 | Battery |
| MC4 or Deutsch DT | Solar |
| Deutsch DT | Pump |
| Grove | I²C devices |
| VH3.96 | Relay terminals |
| Panel USB-C | Charging outlet |

------------------------------------------------------------------------

## Wire Sizes

| Circuit | Wire |
|---------|------|
| Battery / main feed | 12 AWG |
| Pump | 12 AWG |
| Lighting | 16 AWG |
| Controller power | 18 AWG |
| Signals | 22 AWG |

------------------------------------------------------------------------

## Power Budget

- 40Ah battery ≈ 512 Wh
- 50W solar ≈ 200–260 Wh/day
- 10A charger ≈ 5 hours for a depleted 40Ah battery
- Largest continuous load expected to be LED lighting
