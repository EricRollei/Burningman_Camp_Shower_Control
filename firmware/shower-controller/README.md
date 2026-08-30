# Station Controller Firmware

Session firmware for the M5Stack Tough stations and the attached controller
hardware:

- M5Stack PaHUB v2.1 multiplexer on external I2C address `0x70`
- RFID2 reader on a PaHUB port at I2C address `0x28`
- M5Stack Unit 4Relay on a PaHUB port at I2C address `0x26`
- protected flow-meter pulse signal on GPIO26
- onboard microSD card for persistent CSV logging
- secured local Wi-Fi admin dashboard
- CampNet ESP-NOW link to every other station and door sign
- shower stations only: M5NanoC6 door sign with SH1107 OLED, 10 kOhm
  music-selector potentiometer on GPIO36 (ADC1), WS2811-style 12 V addressable
  LED strip on GPIO13, Bluetooth speaker

## Stations

One codebase builds one image per station. The PlatformIO environment sets
`STATION_ID` and `STATION_ROLE`; everything else (name, SSID, features,
limits) derives from those in `include/Config.h`.

| env | Station | Role | Admin SSID | Door sign |
|---|---|---|---|---|
| `shower1` (default) | Shower 1 | shower | `CampShower-1` | `door1` |
| `shower2` | Shower 2 | shower | `CampShower-2` | `door2` |
| `water_fill` | Water Fill | water fill | `CampWaterFill` | — |
| `rv_fill` | RV Fill | RV fill | `CampRVFill` | — |

Fill stations run the same session flow (wristband opens a session, the GPIO14
button starts the water and ends the fill) with the speaker, music knob, LED
strip and door sign compiled out. Never flash two Toughs with the same id.

## CampNet

All four Toughs and both door signs share one peer-to-peer ESP-NOW network on
Wi-Fi channel `CampNet::CHANNEL` (1). There is no router and no hub: every
station broadcasts, every station listens, and any station can be off or out
of range without affecting the others. Each Tough still runs its own admin
soft-AP (pinned to the same channel) so a phone can reach any station.

What travels over the link (`firmware/shared/CampNetProtocol.h`):

| Packet | From every Tough | Purpose |
|---|---|---|
| `STATUS` | every 2 s + on change | `OPEN` / `IN_USE` / `UNAVAILABLE` for the door signs |
| `USAGE` | every 10 s + right after a session ends | this station's per-wristband totals (from `/SESSIONS.CSV`) |
| `MEMBERS` | every 30 s + right after an edit | the whole member registry with a version number |
| `LIMITS` | every 30 s + right after an edit | per-role gallon and minute limits with a version number |

Every Tough keeps the latest `USAGE` snapshot from each other station in
`/NETUSAGE.CSV` (written at most every 15 s). A wristband's camp-wide total is
this station's own sessions plus those snapshots, so the summary screen after
a shower or a fill shows **this session** and **total this burn**. The admin
dashboard retains the per-role split between showers, water fill and RV fill.
Snapshots are idempotent, so a lost packet is healed by the next one and a
rebooted station has full totals again within about 10 s of hearing its peers.

Members and limits use version-numbered last-writer-wins: a local edit bumps
the version past anything heard on the air and broadcasts; a newer version
replaces the local copy and is saved to the SD card. Two stations that edit at
the same version (for example two fresh SD cards each with their own
enrollments) are merged by union so no wristband disappears. The member version
is embedded in the atomically replaced `/MEMBERS.CSV` header; legacy cards with
`/MEMBERS.VER` are still read until the next successful registry save. Limit
versions live in `SETTINGS.CSV` (`limits_version`).

The header of every screen shows `READY - n NET`, where `n` is the number of
other stations heard in the last 15 s. The admin page's home screen has a
tile per peer and its **Camp settings** page lists each one with its
last-seen time; `/api/status` exposes `peers`, `net` counters, `limits`,
`membersVersion` and `features`.

## Session behavior

The person showering or filling interacts with the RFID reader, the momentary
button on GPIO14, and the large touchscreen Start/Stop control. The physical
and digital buttons operate the same toggle lifecycle. Touching outside that
control, or touching any idle, message, calibration, or summary screen, never
operates a relay or changes a session.

1. At boot, all four relays are explicitly commanded OFF. After settings load,
   the configured accessory rail is restored if it is enabled.
2. An enabled, enrolled wristband opens a shower session with the configured
   pump relay off, turns on the configured phone-charger relay, and shows the
   member's name, burn total, and a large green **START WATER** button. On the
   person-facing Tough display, ordinary names are shortened to first name and
   last initial (`MICHAEL P.`), while numeric sister-camp registrations display
   as `MAD T 12`. Full names remain unchanged in the admin page and logs.
3. The first physical button press or a tap on green **START WATER** starts the
   water. The screen shows live gallons, elapsed time, and a large red **STOP
   WATER** button.
4. The next physical button press or a tap on red **STOP WATER** ends the
   shower and turns the pump off. Both controls are ignored when no member is
   authenticated or during calibration.
5. After a shower ends the screen shows the gallons used and elapsed time for
   10 seconds (`SUMMARY_DISPLAY_MS`), then returns to the ready screen.
6. If a different enrolled wristband is tapped while a session is open (someone
   forgot to log out), the open shower is ended and logged with reason
   `HANDOFF`, and the new member is logged in. Unknown or disabled wristbands
   never end someone else's shower; they only show `NOT AUTHORIZED`.
7. The shower also ends at its per-user gallon limit (10 gallons by default)
   or a 20-minute safety timeout.
8. Every exit path commands the pump and phone-charger relays OFF and appends a
   completed record to `/SESSIONS.CSV`; the accessory rail remains at its
   configured state and `/PULSES.CSV` remains the raw audit log.
9. Unknown and disabled wristbands are denied.

The NanoC6 and external OLED form the public availability sign of each shower.
The Nano listens on CampNet for `STATUS` broadcasts from its own shower's
station id (every two seconds). While available, the OLED uses an inverted
bright background and rotates through short messages such as `HEY STINKY`,
`SHOWER TIME`, and `SCRUB A DUB`. During an authorized shower session it
displays `IN USE`, even when the pump is paused. It fails safe to `OFFLINE`
after three seconds without a valid broadcast.

Session limits come from the **Station limits** card on the admin page (per
kind of station: gallons and minutes) and sync to every controller. Defaults
are shower 10 gal / 20 min, water fill 10 gal / 60 min, RV fill 100 gal /
60 min; the firmware clamps to 0.5–500 gal and 1–180 min. A member's own
allowance (`allowance_gallons` in `/MEMBERS.CSV`) is a shower-only override;
`0` means "use the station limit", and fills always use the station limit.
The Tough's built-in display shows the operational UI to the person using the
controller. Its Big Top theme uses a static red-and-cream sunburst, gold frame
and marquee bulbs, an embedded circus headline font, and a ticket-style
summary. The screen deliberately omits allowance bars and limit text, but the
configured gallon and time limits remain fully enforced.

Both downstream I2C devices are discovered by address at boot, so their
PaHUB ports may be rearranged without rebuilding the firmware. Missing devices
are retried every five seconds while the shower is idle, which also supports
reconnecting a loose Grove cable without rebooting.

## Admin dashboard

Every Tough runs a soft-AP with the same name and password, so a phone
auto-joins whichever station is nearest and the same address works everywhere:

| Setting | Value |
|---|---|
| Wi-Fi name | `CampShower` (every station) |
| Wi-Fi password | `dustybutthole` |
| Admin page | `http://192.168.4.1/` — no login; the Wi-Fi password is the gate |

The network is local and does not provide internet access. To enroll a tag
(from any station — the registry syncs to the others within seconds):

1. Join `CampShower` from a phone.
2. Open `http://192.168.4.1/` in the phone browser.
3. Tap the orange **Enroll a wristband** button, enter a member name, pick
   the station whose reader you are standing at under **Enroll on**, and tap
   **Enroll**.
4. Tap the tag against that Tough's RFID2 reader.

The header eyebrow (`CAMP SHOWER · ON SHOWER 1`) says which station you are
physically connected to; that only matters for audio upload.
`Config::ADMIN_PAGE_PASSWORD` (`include/Config.h`) re-enables HTTP Basic
login (`admin` / initial `change-me-shower`, salted SHA-256 on the SD card,
synced between stations) if a page password is ever wanted again; the
**Camp settings** page then grows an **Admin password** card.

The association is written to `/MEMBERS.CSV` on the microSD card. The
**Members** page lists registered tags with their camp-wide usage and any
custom shower limit; tapping a member opens an inline editor (name, shower
limit, can-start toggle, delete). Every change propagates to the other
stations over CampNet. Up to 100 members may be registered. Deleting a
registration does not delete historical usage.

### One page for every station

Opening any station's page is enough: the page shows the whole camp. It is
laid out like a small phone app (`drawings/admin-mockups.html`, option C):

- A sticky teal **header** with a live banner that always shows the most
  important thing happening anywhere on CampNet, in priority order: a
  wristband enrollment waiting for a tap, sessions in progress (member,
  station, gallons of limit), a station with a hub/relay/RFID/SD fault, or
  "All stations open". If the controller stops answering it turns red and
  says "Controller not responding — retrying" until polling succeeds again.
- A **home screen** that never scrolls on a phone: the orange **Enroll a
  wristband** button, then tiles for **Members** (count, disabled count),
  **Water** (camp-wide gallons and sessions), **one tile per station** heard
  on CampNet (OPEN / IN USE / ENROLLING / CALIBRATING / RELAY TEST /
  UNAVAILABLE / OFFLINE, health dots for hub, relay, RFID, SD and speaker,
  the active member or the last session), and **Camp settings** (the three
  station limits). Every tile is also the button that opens its page.
- **Members**: enroll card (name, **Enroll on** picker, cancel while
  waiting), searchable member list with the inline editor.
- **Water use**: total / sessions / average tiles, a per-member table with
  the shower, water-fill and RV split, and every station's recent sessions.
- **Station** (one per Tough): live session with **End session**, recent
  sessions with colour-coded end reasons (`LIMIT`/`HANDOFF` amber,
  `TIMEOUT`/`REBOOT`/errors red), flow calibration, on shower stations the
  speaker (volume slider, test tone, play, stop, find speaker, channel-1
  upload) and music-knob calibration wizard, the **Relay & power** card
  (see below), then controller health (pills for hub / relay / RFID / SD /
  speaker, heap, Wi-Fi clients, underruns, commanded charger and accessory
  state) with **Reboot controller**. Cards grey out while the station is
  offline.
- **Camp settings**: station limits (gallons and minutes per station kind,
  synced everywhere), CampNet peers with last-seen times and counters, and
  the password card when enabled.

Destructive buttons (End session, Reboot, Delete, Save assignments,
accessory power, relay tests) never use browser dialogs: the first tap arms
the button ("Tap again to confirm") for four seconds and the second tap
acts. Results show as a toast at the bottom of the screen. Pages are
addressed by URL hash (`#members`, `#water`, `#station/2`, `#camp`), so the
browser back button and a reload keep your place. The palette is the light
cream/teal/orange "daylight" theme for readability in the sun; it switches
to a dark variant automatically when the phone is in dark mode.

To work on the page without hardware, `python3 tools/admin_mock_server.py`
serves the exact HTML embedded in `src/AdminServer.cpp` with fake
four-station data at `http://127.0.0.1:8765/` (`--scenario shower|enroll|fault`
starts it in a particular state, `--local N` pretends to be another station).

- Every station broadcasts a `TELEMETRY` packet (state, health, calibration,
  speaker, session) and a `RECENT` packet (last eight sessions) over CampNet;
  the page reads them from `/api/overview` (`stations`) or `/api/stations`.
- Buttons post to `/api/command` with the target `station`. For the station
  you are connected to the action runs immediately. For any other station it
  is sent as an authenticated `COMMAND` packet; the page shows "Sending to
  <station>…", polls `/api/command?nonce=` for the `ACK` and toasts its
  message (or "No answer from <station>" after about 4 s). Actions that go
  over the air: enroll / cancel enrollment (the wristband must be tapped on
  the chosen station's reader, so the Enroll card has an **Enroll on**
  picker), flow calibration start/stop, music knob calibration, test tone,
  play, stop, volume, find speaker, relay assignment, accessory power,
  five-second relay tests, end session, and reboot. Audio commands on a fill
  station answer "not supported".
- Remote commands are HMAC-signed with `CampNet::SECRET` and de-duplicated by
  nonce, so a stray or replayed packet is dropped. Over the air an admin can
  start flow calibration (which runs that station's pump, still bounded by
  the 10-minute calibration timeout) and end a session (pump off, logged with
  reason `REMOTE`). Nothing over the air can open a shower session or turn the
  pump on for one; that still takes a wristband and the physical button.
- Audio upload is local only: stand at the station whose channel 1 track you
  want to replace so the phone is on its AP (the header eyebrow names it).
  Other stations' pages show a note instead of the upload control.
- There is no page login by default (the shared Wi-Fi password is the gate).
  If `ADMIN_PAGE_PASSWORD` is enabled, changing the password on any station
  syncs the salted hash to every station (`AUTH` packet).
- The legacy per-action routes (`/api/enroll`, `/api/calibration/*`,
  `/api/audio/*`, `/api/reboot`, ...) still work and act on the local station.

## Reliability

- Song PCM is staged through a PSRAM ring buffer by the main loop, so the
  Bluetooth task never touches the SD card (the SD/SPI stack is not safe
  across tasks; concurrent access was the leading cause of hard freezes).
- The main loop is subscribed to the ESP32 task watchdog: if it ever wedges
  for 20 seconds the station reboots itself instead of staying dead.
- Bluetooth speaker discovery scans continuously for only the first two
  minutes, then retries one round every five minutes so inquiry scanning does
  not starve the Wi-Fi access point. The dashboard's **Find speaker** button
  forces a fresh scan window.
- The dashboard polls a single, chunk-streamed `/api/overview` endpoint with a
  timeout and an in-flight guard, so the controller does not retain duplicate
  camp-wide JSON buffers and a slow controller degrades to "retrying" instead
  of a frozen page. The browser supplies its member-registry version on each
  poll, and the controller omits the roster when that cached copy is current;
  member edits still refresh the roster on the next poll. `/api/health` exposes
  uptime, heap, Wi-Fi client count, and
  hardware status; the same data is printed to serial every 30 seconds as
  `[HEALTH]` lines. Each station page's **Controller health** card shows
  verified controller communications as OK/DOWN pills, lists downstream
  loads (charger, accessory rail) as commanded-only with no electrical
  feedback, and includes a remote reboot button.
- If the Wi-Fi access point fails to start, it is retried every 30 seconds at
  runtime.
- `/PULSETOT.CSV` snapshots per-tag totals at each session end so boot replays
  only the tail of `/PULSES.CSV` rather than the whole week's log.

The Shower speaker card provides a 0-100% source-level control, which defaults
to full-scale PCM. Applying a new value changes the Bluetooth audio level
immediately and saves it in `/SETTINGS.CSV`, so the same level is restored
after a restart or speaker reconnection. The speaker's physical volume buttons
remain local to the speaker; the controller does not subscribe or react to
their AVRCP volume notifications. On first boot after this update, the old 43%
default is migrated to 100%; any other saved source-level choice is preserved.

Calibration starts the configured pump relay and counts raw pulses while water
is dispensed into a known-volume container. Enter the measured gallons and
stop calibration to save the new pulses-per-gallon ratio in `/SETTINGS.CSV`.
A 10-minute safety timeout stops calibration without changing the saved ratio.

### Relay and power configuration

Every station has local mappings for the pump, phone charger, and accessory
rail. Pump defaults to relay 1; charger and accessory default to **Not
assigned**, so installing new firmware cannot energize an unknown auxiliary
load. Assigned roles must use different channels. The mappings and the
accessory enabled/disabled choice are saved in `/SETTINGS.CSV` and can be
edited from that station's page on any online admin page.

The phone charger turns on as soon as an authorized wristband successfully
opens a session, before the pump starts, and turns off on every session exit.
The accessory rail supplies the LED power converter, speaker, and outer display
and stays at its configured state through normal session transitions. Boot,
reboot, relay recovery, remapping, and raw tests first command all four relays
off, so the accessory rail may briefly power-cycle during those operations.

The **Relay & power** card shows the last successfully commanded state of all
four channels and offers a persistent accessory toggle. An idle station can
also test any physical channel for five seconds; the firmware stops the test
even if the browser disconnects and then restores the accessory policy. These
states do not prove that relay contacts, converters, or loads are electrically
working because the current hardware provides no load-feedback signal.

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

The firmware uses the tag's factory UID as its identity; it does not write
user data onto the tag. The JSON endpoints under `/api/` keep UID, member
registry, and usage totals separate; `/api/members` reports both this
station's `gallons` and the camp-wide `networkGallons` with a per-role split.

The CSV is append-only and is replayed at boot to restore each tag's total:

```csv
uptime_ms,uid,delta_pulses,tag_total_pulses,event
1250,04A1B2C3D4E5F6,0,0,SELECT
4260,04A1B2C3D4E5F6,37,37,PULSES
```

`uptime_ms` is time since the current boot. A wall-clock timestamp can be
added later once the RTC policy is decided.

## Build and upload

For routine flashing, the repository's terminal uploader provides labeled
profiles, USB-port selection, confirmation, and upload progress:

```sh
python3 firmware/uploader/firmware_uploader.py
```

See [`firmware/uploader/README.md`](../uploader/README.md) for the full workflow.
The direct PlatformIO commands remain available for development:

```sh
cd firmware/shower-controller
pio run -e shower1            # or shower2, water_fill, rv_fill
pio run -e shower1 --target upload
pio device monitor
```

Build and flash each Tough with its own environment; the station id is baked
into the image. The serial `status` command prints a `[STATION]` line with the
id, role, SSID, channel, peer count and sync versions for confirmation.

The configured USB port and verified upload speed come from the existing
Tough RFID prototype. Serial commands are available as a backup to the button:

- `off` — force pump and phone-charger relays off; preserve accessory policy
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
a 2 m, 12 V WS2811-style three-wire strip in GRB order. The configured estimate
is 60 physical LEDs/m: 120 physical LEDs arranged as 40 addressable groups.
Adjust `LED_STRIP_COUNT` in `include/Config.h` if the physical strip has a
different density; count addressable groups (typically one group per three
physical LEDs), not its raw LED-package count. Power the strip directly from
its fused 12 V branch, connect its negative return to the same fuse-block
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
