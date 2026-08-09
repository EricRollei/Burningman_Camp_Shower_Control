# Shower Controller Firmware

Hardware-validation MVP for the M5Stack Tough. It verifies the three devices
currently attached to the prototype:

- RFID2 reader on external I2C address `0x28`
- M5Stack Unit 4Relay on external I2C address `0x26`
- protected flow-meter pulse signal on GPIO26
- onboard microSD card for persistent CSV logging
- local Wi-Fi setup page for UID enrollment

## MVP behavior

1. At boot, all four relays are explicitly commanded OFF.
2. Scan a tag to make its UID active.
3. Each falling edge from the flow sensor is attributed to that active UID.
4. Pulse deltas and lifetime per-tag totals are appended to `/PULSES.CSV`
   every two seconds while pulses arrive.
5. Tap **END TAG** to stop attribution.
6. Tap a relay button to toggle that channel. Leave real loads disconnected
   during the first bench test.

## Phone enrollment prototype

At boot the Tough creates this local setup network:

| Setting | Value |
|---|---|
| Wi-Fi name | `CampShower-Setup` |
| Wi-Fi password | `camp-shower-setup` |
| Setup page | `http://192.168.4.1/` |

The network is local and does not provide internet access. To enroll a tag:

1. Join `CampShower-Setup` from a phone.
2. Open `http://192.168.4.1/` in the phone browser.
3. Enter a member name and tap **Enroll next tag**.
4. Tap the tag against the Tough's RFID2 reader.

The association is written to `/MEMBERS.CSV` on the microSD card. The setup
page lists registered tags and their lifetime pulse totals, and supports rename
and delete operations. Deleting a registration does not delete its historical
usage from `/PULSES.CSV`.

The first prototype uses the tag's factory UID as its identity; it does not
write user data onto the tag. The JSON endpoints under `/api/` intentionally
keep UID, member registry, and usage totals separate so the same model can be
shared with the other shower and fill-station controllers later.

The CSV is append-only and is replayed at boot to restore each tag's total:

```csv
uptime_ms,uid,delta_pulses,tag_total_pulses,event
1250,04A1B2C3D4E5F6,0,0,SELECT
4260,04A1B2C3D4E5F6,37,37,PULSES
```

`uptime_ms` is time since the current boot. A wall-clock timestamp can be
added later once the RTC policy is decided.

## Build and upload

```sh
cd firmware/shower-controller
pio run
pio run --target upload
pio device monitor
```

The configured USB port and verified upload speed come from the existing
Tough RFID prototype. Serial commands are available as a backup to touch:

- `r1`, `r2`, `r3`, `r4` — toggle one relay
- `off` — force all relays off
- `end` — end the active tag
- `status` — print current hardware and counter state

## Current prototype pin lock

| Function | Tough pin |
|---|---:|
| External I2C SDA | GPIO32 |
| External I2C SCL | GPIO33 |
| Flow pulse input | GPIO26 |
| microSD CS | GPIO4 |
| microSD SCK/MISO/MOSI | GPIO18 / GPIO38 / GPIO23 |

GPIO26 uses its internal pull-up as a backup. Keep the already-tested external
flow-signal protection harness in place; do not connect a 5 V pulse directly.
