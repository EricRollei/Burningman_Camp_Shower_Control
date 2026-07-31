# Bill of Materials (BOM)

Per-shower bill of materials. The camp builds **two independent
stations**, so most quantities are **×2**.

> Status legend: ✅ selected · �searching · ❓undecided

------------------------------------------------------------------------

## Power & Charging

| Item | Spec / Part | Qty (per shower) | Status | Notes |
|------|-------------|:---:|:---:|-------|
| Battery | 12.8V 40Ah LiFePO₄ | 1 | ✅ | ~512 Wh |
| Solar panel | 50 W 12V | 1 | ✅ | |
| Solar charge controller | 12V MPPT/PWM | 1 | ❓ | Size to panel |
| AC charger | 10A LiFePO₄ charger | 1 | ✅ | Generator-fed |
| Battery monitor | Coulomb / shunt meter | 1 | ❓ | See Remaining Decisions |

## Protection & Distribution

| Item | Spec / Part | Qty | Status | Notes |
|------|-------------|:---:|:---:|-------|
| Main breaker | 30A manual-reset | 1 | ✅ | Main disconnect |
| Fuse block | Marine, w/ negative bus | 1 | ✅ | Individually fused branches |
| Fuses | 15A / 10A / 5A / 3–5A assortment | — | ✅ | See Wiring.md |

## Pump & Plumbing

| Item | Spec / Part | Qty | Status | Notes |
|------|-------------|:---:|:---:|-------|
| Pump | SEAFLO 42-series, 12V, 3 GPM, 55 PSI | 1 | ✅ | |
| Accumulator tank | 12V system accumulator | 1 | ✅ | |
| Inline filter | Pre-pump | 1 | ✅ | |
| Water heater | 12V-compatible | 1 | ❓ | |
| Thermostatic mixing valve | | 1 | ✅ | |
| Flow sensor | Pulse output | 1 | ✅ | Calibrate per station |
| Shower valve + head | | 1 | ✅ | |
| Tank | 500 gal (shared/camp) | 1 | ✅ | Shared resource |

## Control Electronics

| Item | Spec / Part | Qty | Status | Notes |
|------|-------------|:---:|:---:|-------|
| Controller | M5Stack Tough | 1 | ✅ | 12V via RS485 conn. |
| I²C hub | M5Stack PaHub | 1 | ✅ | |
| RFID/NFC reader | M5Stack RFID2 | 1 | ✅ | |
| Relay unit | M5Stack 4Relay | 1 | ✅ | |
| Automotive relay | For pump switching | 1 | ✅ | Coil driven by 4Relay |
| SD card | For CSV logs | 1 | ✅ | |
| RTC | Real-time clock | 1 | ❓ | For log timestamps |

## Charging & Accessories

| Item | Spec / Part | Qty | Status | Notes |
|------|-------------|:---:|:---:|-------|
| USB-C buck converter | 12V → USB-C PD | 1 | ✅ | Session-gated |
| USB-C panel outlet | | 1 | ✅ | |
| LED strip | 12V addressable, ~24 ft | 1 | � | Strip TBD |

## Connectors

| Connector | Use | Status |
|-----------|-----|:---:|
| Anderson SB50 | Battery quick-disconnect | ✅ |
| MC4 / Deutsch DT | Solar | ❓ |
| Deutsch DT | Pump | ✅ |
| Grove | I²C devices | ✅ |
| VH3.96 | Relay terminals | ✅ |

## Wire

| Circuit | Gauge |
|---------|-------|
| Battery / main feed | 12 AWG |
| Pump | 12 AWG |
| Lighting | 16 AWG |
| Controller power | 18 AWG |
| Signals | 22 AWG |

## RFID / NFC Tags

| Tag | Purpose | Status |
|-----|---------|:---:|
| NTAG213 | Candidate | � test |
| NTAG215 | Candidate | � test |
| NTAG216 | Candidate | � test |

------------------------------------------------------------------------

_TODO: add vendor links, part numbers, and per-item cost columns._
