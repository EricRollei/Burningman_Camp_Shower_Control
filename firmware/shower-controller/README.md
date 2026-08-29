# Shower Controller Firmware

Shower-session firmware for the M5Stack Tough and the attached controller
hardware:

- M5Stack PaHUB v2.1 multiplexer on external I2C address `0x70`
- RFID2 reader on a PaHUB port at I2C address `0x28`
- M5Stack Unit 4Relay on a PaHUB port at I2C address `0x26`
- M5NanoC6 door-display controller with a locally connected SH1107 OLED
- protected flow-meter pulse signal on GPIO26
- 10 kOhm music-selector potentiometer on GPIO36 (ADC1)
- WS2811-style 12 V addressable LED strip on GPIO13
- onboard microSD card for persistent CSV logging
- secured local Wi-Fi admin dashboard

## Session behavior

The Tough touchscreen is not used; the person showering only interacts with
the RFID reader and the single momentary button on GPIO14.

1. At boot, all four relays are explicitly commanded OFF.
2. An enabled, enrolled wristband opens a shower session with relay 1 off and
   shows `PRESS BUTTON TO START`.
3. The first button press starts the water. The screen switches to
   `PRESS BUTTON TO FINISH` and shows live gallons against the member's limit.
4. The second button press ends the shower and turns the pump off. Button
   presses are ignored when no member is authenticated.
5. After a shower ends the screen shows the gallons used and elapsed time for
   10 seconds (`SUMMARY_DISPLAY_MS`), then returns to the ready screen.
6. If a different enrolled wristband is tapped while a session is open (someone
   forgot to log out), the open shower is ended and logged with reason
   `HANDOFF`, and the new member is logged in. Unknown or disabled wristbands
   never end someone else's shower; they only show `NOT AUTHORIZED`.
7. The shower also ends at its per-user gallon limit (10 gallons by default)
   or a 20-minute safety timeout.
8. Every exit path commands all relays OFF and appends a completed record to
   `/SESSIONS.CSV`; `/PULSES.CSV` remains the raw audit log.
9. Unknown and disabled wristbands are denied.

The NanoC6 and external OLED form the public availability sign. The Nano joins
the Tough's local Wi-Fi access point and requests status twice per second. While
available, the OLED uses an inverted bright background and rotates through
short messages such as `HEY STINKY`, `SHOWER TIME`, and `SCRUB A DUB`. During
an authorized shower session it displays `IN USE`, even when the pump is
paused. It fails safe to `OFFLINE` after three seconds without a valid reply.
The Tough's built-in display shows the operational UI to the person using the
controller; its touch layer is not used.

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

The Shower speaker card provides a 0-100% volume control. Applying a new value
changes the Bluetooth audio level immediately and saves it in `/SETTINGS.CSV`,
so the same level is restored after a restart or speaker reconnection.

Calibration starts relay 1 and counts raw pulses while water is dispensed into
a known-volume container. Enter the measured gallons and stop calibration to
save the new pulses-per-gallon ratio in `/SETTINGS.CSV`. A 10-minute safety
timeout stops calibration without changing the saved ratio.

The music-vibe calibration wizard captures ten physical knob positions in
order: position 0 is quiet and positions 1-9 are music channels. During
calibration, knob-driven audio is disabled. The firmware rejects points that
are out of order, too close together, or span less than 1000 ADC counts, and
only replaces the previous calibration after the complete set is saved to
`/SETTINGS.CSV`. At runtime, meaningful knob movement immediately replaces the
current song with synthesized radio static. The static remains continuous
while the knob moves; after the input settles for 100 ms, the nearest captured
position locks in with boundary hysteresis. Position 0 becomes quiet and
positions 1-9 start their assigned songs.

| Position | Track | SD path |
|---:|---|---|
| 0 | Quiet | — |
| 1 | Purple Rain — Prince | `/CH1.PCM` |
| 2 | Africa — Toto | `/CH2.PCM` |
| 3 | Whose Bed Have Your Boots Been Under — Shania Twain | `/CH3.PCM` |
| 4 | It's My House — Diana Ross | `/CH4.PCM` |
| 5 | Dancing Queen — ABBA | `/CH5.PCM` |
| 6 | What a Feeling — Irene Cara | `/CH6.PCM` |
| 7 | Footloose — Kenny Loggins | `/CH7.PCM` |
| 8 | Maniac — Michael Sembello | `/CH8.PCM` |
| 9 | Jesus Built My Hotrod — Ministry | `/CH9.PCM` |

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
Tough RFID prototype. Serial commands are available as a backup to the button:

- `off` — force all relays off
- `end` — end the active shower
- `status` — print current hardware and counter state

## Current prototype pin lock

| Function | Tough pin |
|---|---:|
| External I2C SDA | GPIO32 |
| External I2C SCL | GPIO33 |
| Flow pulse input | GPIO26 |
| Music selector wiper | GPIO36; potentiometer ends to 3V3 and GND |
| Shower start/finish button | GPIO14 to GND (internal pull-up) |
| Addressable LED data | GPIO13 (Port C white wire) |
| microSD CS | GPIO4 |
| microSD SCK/MISO/MOSI | GPIO18 / GPIO38 / GPIO23 |

GPIO26 uses its internal pull-up as a backup. Keep the already-tested external
flow-signal protection harness in place; do not connect a 5 V pulse directly.

GPIO36 is the white signal on Port B, beside the flow input on yellow/GPIO26.
Power the 10 kOhm potentiometer from the Tough EXT board's 3V3 and GND header
pins. Port B's red wire is 5 V and must not be connected to the potentiometer,
because that could put 5 V onto the ESP32 ADC. Wire the end terminal reached at
fully counter-clockwise to GND, the opposite end to 3V3, and the center/wiper
terminal to GPIO36. The first test channel starts `/MEXICO.PCM` above roughly
25% and stops below roughly 18% until a knob calibration has been saved. After
calibration, the ten captured notch positions define quiet and channels 1-9.
Serial output reports the raw ADC value and selected channel for diagnostics.

The addressable strip runs a continuous moving rainbow. The installed strip is
a 12 V WS2811-style three-wire strip in GRB order. Adjust `LED_STRIP_COUNT` in
`include/Config.h` to its number of addressable groups (typically one group per
three physical LEDs), not its raw LED-package count. Power the strip directly
from its fused 12 V branch, connect its negative return to the same fuse-block
negative bus as Tough, and connect Port C's white/GPIO13 wire to the strip data
input. Never feed 12 V into a Tough GPIO. A 220-470 ohm series resistor at the
strip data input is recommended. For a long data lead, use a 74HCT-family
3.3-to-5 V level shifter close to Tough; inject 12 V power along the long strip
as required by measured current and voltage drop.

Every music channel has a synchronized, song-specific cue show matched to the
exact installed PCM edit. The PCM byte position is the clock, so restarting a
song or changing channels cannot create permanent wall-clock drift. Shows use
distinct palettes and motifs: Purple Rain's violet weather, Africa's sunset and
rain, Shania's pink-and-gold boot stomps, Diana Ross and ABBA disco treatments,
Flashdance neon, Footloose kick chases, Maniac's electric pursuit, and Ministry's
industrial sparks and firestorm. The default moving rainbow appears while no
song is playing or during radio tuning.

Purple Rain additionally uses a 460-beat timestamp map extracted from the
installed PCM, plus live bass/mid/treble envelopes calculated from the exact
samples sent to Bluetooth. This prevents cumulative tempo drift and avoids the
latency and playa-noise problems of microphone beat detection. Its purple-only
frequency waves, eight-beat breathing, spectrum bands, expanding rhythm
ripples, alternating chasers, and layered rainstorms adapt concepts from the
MIT-licensed M5Stack Audio Visualizer; see `THIRD_PARTY_NOTICES.md`.

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
