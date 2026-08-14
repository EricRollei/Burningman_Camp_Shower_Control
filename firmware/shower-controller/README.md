# Shower Controller Firmware

Shower-session firmware for the M5Stack Tough and the attached controller
hardware:

- M5Stack PaHUB v2.1 multiplexer on external I2C address `0x70`
- RFID2 reader on a PaHUB port at I2C address `0x28`
- M5Stack Unit 4Relay on a PaHUB port at I2C address `0x26`
- M5NanoC6 door-display controller with a locally connected SH1107 OLED
- protected flow-meter pulse signal on GPIO26
- onboard microSD card for persistent CSV logging
- secured local Wi-Fi admin dashboard

## Session behavior

1. At boot, all four relays are explicitly commanded OFF.
2. An enabled, enrolled wristband opens a shower session with relay 1 off.
3. The momentary button on GPIO14 toggles the pump during that session; button
   presses are ignored when no member is authenticated.
4. Flow is displayed in gallons using the saved station calibration.
5. The shower ends from the touchscreen, its per-user gallon limit (10 gallons
   by default), or a 20-minute safety timeout.
6. Every exit path commands all relays OFF and appends a completed record to
   `/SESSIONS.CSV`; `/PULSES.CSV` remains the raw audit log.
7. Unknown and disabled wristbands are denied.

The NanoC6 and external OLED form the public availability sign. The Nano joins
the Tough's local Wi-Fi access point and requests status twice per second. While
available, the OLED uses an inverted bright background and rotates through
short messages such as `HEY STINKY`, `SHOWER TIME`, and `SCRUB A DUB`. During
an authorized shower session it displays `IN USE`, even when the pump is
paused. It fails safe to `OFFLINE` after three seconds without a valid reply.
The Tough's built-in touchscreen keeps the operational UI shown to the person
using the controller.

Both downstream I2C devices are discovered by address at boot, so their
PaHUB ports may be rearranged without rebuilding the firmware. Missing devices
are retried every five seconds while the shower is idle, which also supports
reconnecting a loose Grove cable without rebooting.

## Admin dashboard

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

The initial dashboard login is `admin` / `change-me-shower`. Change it from
the dashboard before field use. The password is stored on the SD card as a
salted SHA-256 hash. HTTP Basic authentication protects the controller's local
admin page.

The association is written to `/MEMBERS.CSV` on the microSD card. The setup
page lists registered tags, completed usage and per-shower limits. It supports
enrollment, editing, disabling, and deletion. Deleting a registration does not
delete historical usage.

Calibration starts relay 1 and counts raw pulses while water is dispensed into
a known-volume container. Enter the measured gallons and stop calibration to
save the new pulses-per-gallon ratio in `/SETTINGS.CSV`. A 10-minute safety
timeout stops calibration without changing the saved ratio.

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

- `off` — force all relays off
- `end` — end the active shower
- `status` — print current hardware and counter state

## Current prototype pin lock

| Function | Tough pin |
|---|---:|
| External I2C SDA | GPIO32 |
| External I2C SCL | GPIO33 |
| Flow pulse input | GPIO26 |
| Pump toggle button | GPIO14 to GND (internal pull-up) |
| microSD CS | GPIO4 |
| microSD SCK/MISO/MOSI | GPIO18 / GPIO38 / GPIO23 |

GPIO26 uses its internal pull-up as a backup. Keep the already-tested external
flow-signal protection harness in place; do not connect a 5 V pulse directly.

The PaHUB channel numbers are intentionally not pinned in configuration; the
Tough firmware locates relay `0x26` and RFID2 `0x28` at runtime. The SH1107 is
connected directly to the NanoC6 on SDA GPIO2 and SCL GPIO1.

### Bench-power note

The Tough explicitly enables its external 5 V rail before probing the PaHUB.
Its AXP192 power-management protection may still withhold that rail when the
controller is powered only from USB and has an empty battery, preventing the
Mac's USB port from being overloaded by the PaHUB, relay coils, and RFID2. For
a complete bench test, power the Tough from 6–24 V through PWR+485
or power the I2C peripherals from a suitable independent 5 V supply with a
shared ground. Do not back-feed the computer's USB 5 V rail.
