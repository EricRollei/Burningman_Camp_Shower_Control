# Shower Controller Firmware

Shower-session firmware for the M5Stack Tough and the attached controller
hardware:

- RFID2 reader on external I2C address `0x28`
- M5Stack Unit 4Relay on external I2C address `0x26`
- protected flow-meter pulse signal on GPIO26
- onboard microSD card for persistent CSV logging
- secured local Wi-Fi admin dashboard

## Session behavior

1. At boot, all four relays are explicitly commanded OFF.
2. An enabled, enrolled wristband starts a shower and energizes relay 1.
3. Flow is displayed in gallons using the saved station calibration.
4. The shower ends from the touchscreen, its per-user gallon limit (10 gallons
   by default), or a 20-minute safety timeout.
5. Every exit path commands all relays OFF and appends a completed record to
   `/SESSIONS.CSV`; `/PULSES.CSV` remains the raw audit log.
6. Unknown and disabled wristbands are denied.

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
| microSD CS | GPIO4 |
| microSD SCK/MISO/MOSI | GPIO18 / GPIO38 / GPIO23 |

GPIO26 uses its internal pull-up as a backup. Keep the already-tested external
flow-signal protection harness in place; do not connect a 5 V pulse directly.
