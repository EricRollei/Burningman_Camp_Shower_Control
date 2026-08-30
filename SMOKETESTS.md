# Smoke Tests

One pass through this list before the burn. Check items off as they pass.

## Reliability hardening (branch `worktree-rock-solid-firmware`)

- [ ] **The freeze recipe:** open the admin page on a phone, start a shower
      session with a wristband, and play a song, all at the same time. Let it
      run 10+ minutes. Page should stay responsive; controller should not
      need a power cycle.
- [ ] **Audio quality with buffering:** play a full song and listen for
      dropouts. Check the station page's **Controller health** card (or
      serial `[AUDIO] song finished ... underruns=N`) — a few underruns right at song start are
      fine; a steadily climbing count during playback is a fail.
- [ ] **Speaker volume independence:** with source volume at 100%, confirm the
      test tone and a song play at full source level. Press the speaker's
      volume-down and volume-up buttons and confirm they change only the
      speaker's own output; the dashboard source-volume value must not change
      and serial must not report an automatic absolute-volume command.
- [ ] **Song switching:** twist the music knob between several channels
      mid-song, including back to quiet (position 0). Each switch should be
      clean — no garbled audio, no crash.
- [ ] **Admin page recovery:** while the page is open, power-cycle the
      controller. The header banner should turn red with "Controller not
      responding — retrying" and come back on its own once the AP is up (no
      manual reload needed).
- [ ] **Admin page walkthrough (phone, in sunlight):** open
      `http://192.168.4.1/` on a phone. Home shows the orange Enroll button
      and one tile per powered station with green health dots; nothing on
      the home screen needs scrolling. Tap each tile and confirm its page
      opens and `‹ Home` returns; the browser back button also returns.
      Start a shower: the header banner shows the member, station and live
      gallons on every page. Tap a member on the Members page, change the
      shower limit, Save — the row updates without a browser dialog. Tap
      **End session** once (button reads "Tap again to confirm"), wait 5 s
      (it reverts), then tap twice — session ends with reason `REMOTE` and a
      toast appears. Pull the SD card from one station: its home tile turns
      red with a red SD dot and the header banner says it needs attention.
- [ ] **Watchdog reboot:** hold the serial monitor open and confirm the
      station reboots itself if the loop ever wedges (hard to force
      naturally — acceptable to just confirm normal operation shows no
      spurious watchdog resets over a long session).
- [ ] **Remote reboot button:** station page → Controller health → Reboot
      controller (tap twice). Pump shuts off, an in-progress session is
      logged with reason `REBOOT`, remains off throughout boot, and the station
      returns to idle in ~20 s.
- [ ] **Find speaker button:** with the speaker off, wait until the station
      page's Speaker card shows "search paused" (≈2 min after boot), turn
      the speaker on, tap **Find speaker** — it should connect within ~15 s.
- [ ] **BT backoff vs WiFi:** with the speaker unavailable, confirm the
      admin page stays snappy after the first 2 minutes (discovery held).
- [ ] **Health telemetry:** watch serial `[HEALTH]` lines over an hour of
      mixed use — `min_heap` should level off, not keep sinking.
- [ ] **Pulse totals snapshot:** run a shower, note the member's total
      gallons on the admin page, reboot, confirm the total survives and boot
      is quick (`/PULSETOT.CSV` exists on the SD card).
- [ ] **I2C hot-replug recovery:** while idle, unplug the RFID2 Grove cable
      (then the PaHUB cable) for 10 s and reseat. Within ~10 s the serial log
      shows re-initialisation and a wristband tap works again without reboot.
- [ ] **Audio upload gated:** start a shower, then try a PCM upload from that
      station's page; it must be refused. Upload while idle must complete
      (multi-minute song) without the station rebooting.
- [ ] **Audio upload still works:** upload a channel-1 PCM from the local
      station's page (Speaker card → Upload) while nothing is playing;
      "channel 1 track ready" should be correct immediately after (no reboot
      needed). Another station's page shows the "upload is local only" note
      instead of the control.
- [ ] **Door display:** confirm the OLED sign still tracks
      OPEN / IN_USE / UNAVAILABLE through a full session.
- [ ] **Physical-button-only control:** tap an authorized wristband; the Big
      Top logged-in screen says `PRESS BUTTON TO START WATER` and the pump is
      off. Tap anywhere on every screen and confirm no relay or session action.
      Press GPIO14 once: pump on and the footer changes to `PRESS BUTTON WHEN
      DONE`. Press it again: pump off, summary screen, reason `BUTTON`. Confirm
      presses without an authorized session and during calibration do nothing.
- [ ] **Camper display names:** scan a normally named member and confirm the
      Tough shows first name plus last initial (for example `MICHAEL P.`),
      while the admin page retains the full name. Scan a numeric Mad T member
      and confirm the Tough shows `MAD T <number>`.
- [ ] **Big Top screen flow and readability:** in direct sun and at night,
      inspect idle, logged-in, dispensing, summary, denial, unavailable, and
      calibration screens. Confirm the red/cream sunburst, gold frame, bulbs,
      circus headlines, live gallons, elapsed time, and ticket summary are
      legible without clipped text or visible flicker. Repeat with a long
      member name and a member name containing a non-ASCII character.
- [ ] **Role-specific fill screens:** flash `water_fill` and `rv_fill`; confirm
      the headers, idle prompts, open labels, used-this-fill copy, and thanks
      messages refer to jugs and RV filling as appropriate. Complete one fill
      on each and confirm the same physical-button-only lifecycle and summary.

## CampNet (branch `worktree-camp-network`)

- [ ] **Right images on the right boxes:** flash each labeled device with
      `python3 firmware/uploader/firmware_uploader.py`, checking its profile and
      USB port on the confirmation screen. Serial `status` on each Tough then
      prints `[STATION] id=… role=…` matching its label; each door sign shows
      `S1` / `S2` in the corner matching the shower it is mounted on.
- [ ] **Door sign tracks its own shower only:** start a session on Shower 1 —
      door 1 flips to IN USE within a second, door 2 stays OPEN. Power off
      Shower 1's Tough — door 1 shows OFFLINE within 3 s.
- [ ] **Peers visible:** every Tough's screen header shows `READY - 3 NET`
      with all four powered; the admin home screen shows a tile for each of
      the other three and Camp settings lists them as online.
- [ ] **Enroll anywhere:** enroll a wristband on the water-fill station, tap
      it on a shower within 30 s — session opens.
- [ ] **Registry over 64 members:** with at least 65 test registrations,
      confirm the admin page lists them all and the 65th wristband opens a
      session on a different station after CampNet sync. Production capacity
      is 100 members.
- [ ] **Camp-wide total on the summary screen:** finish a shower, then a
      water fill with the same wristband — the fill's ticket shows its session
      gallons and a total that includes the earlier shower.
- [ ] **Limits sync:** change the RV fill limit on Shower 1's Camp settings page; the
      RV station adopts it within 30 s and an RV fill ends at the new limit,
      even though limit text is intentionally absent from the member display.
- [ ] **Ledger survives reboot:** power-cycle a Tough with the others off;
      the admin Water use page still shows the other stations' gallons
      (`/NETUSAGE.CSV` on the SD card).
- [ ] **Coexistence:** play a song on a shower while an admin page is open;
      `[HEALTH]` `net_rxdrop` stays at 0 and door signs stay OPEN/IN USE.
- [ ] **Heap headroom:** with a song streaming and the admin page open on a
      shower Tough, `[HEALTH] min_heap` stays above ~30 KB for 10 minutes
      (bench with nothing connected: ~39 KB free, 35 KB min).
- [ ] **One page, every station:** sign in to Shower 1's page, open the RV
      Fill tab — its health, session and calibration cards populate within
      3 s and the tab shows online. Tap **Test tone** on the Shower 2 tab; the
      speaker at Shower 2 beeps and the page reports the ACK message.
- [ ] **Remote enroll:** from Shower 1's page choose "Enroll on: Water Fill",
      enter a name, tap the wristband on the Water Fill reader — it enrolls
      and appears in Members on every page.
- [ ] **Remote end session:** start a session at Shower 2, press End session
      from Shower 1's page — pump off within a second, logged as `REMOTE`.
- [ ] **One network:** every Tough advertises `CampShower`; a phone that
      joined at Shower 1 opens `http://192.168.4.1/` at the RV station without
      re-entering anything and the page loads with no login prompt.
- [ ] **Bad command rejected:** (optional, needs a spare ESP32) a COMMAND
      packet with a wrong secret is ignored and `[HEALTH]` shows no action.

## (add in-flight work below)

## Admin relay and power configuration

- [ ] **Upgrade defaults are inert:** boot with an existing `SETTINGS.CSV` that
      has no relay keys. Pump remains mapped to relay 1; charger and accessory
      show **Not assigned**, and no auxiliary relay energizes.
- [ ] **Mapping validation and persistence:** assign pump, charger, and
      accessory to three different channels. Duplicate assignments are
      rejected. Reboot and confirm the mappings and accessory enabled state
      survive.
- [ ] **Accessory policy:** with the accessory load connected, toggle it off
      and on from the local station's page and from another station's page
      (tap twice to confirm). Confirm the
      choice persists across reboot and the commanded/unmonitored label does
      not claim load feedback.
- [ ] **Authorized charger lifecycle:** scan an authorized wristband and
      confirm the charger relay turns on before START is pressed while the pump
      remains off. Confirm charger and pump are off after BUTTON, LIMIT,
      TIMEOUT, HANDOFF, REMOTE, REBOOT, SD_ERROR, and RELAY_ERROR exits, while
      the enabled accessory rail remains on.
- [ ] **Five-second raw tests:** while idle, test each channel from the
      Relay & power card (tap twice) and confirm only
      the selected channel energizes, automatically stops within five seconds,
      and restores accessory power. Repeat using immediate Stop and with the
      browser disconnected mid-test. Confirm tests and remapping are rejected
      during a session or flow calibration.
- [ ] **Relay recovery:** disconnect and reconnect the 4Relay module while
      idle. Confirm health changes to DOWN, recovery starts from all-off, and
      the configured accessory state is restored. Verify the UI reports relay
      state as commanded and downstream pump/charger/LED/display loads as
      unmonitored.
