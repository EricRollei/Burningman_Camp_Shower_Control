# Smoke Tests

One pass through this list before the burn. Check items off as they pass.

## Reliability hardening (branch `worktree-rock-solid-firmware`)

- [ ] **The freeze recipe:** open the admin page on a phone, start a shower
      session with a wristband, and play a song, all at the same time. Let it
      run 10+ minutes. Page should stay responsive; controller should not
      need a power cycle.
- [ ] **Audio quality with buffering:** play a full song and listen for
      dropouts. Check the **Controller** card (or serial `[AUDIO] song
      finished ... underruns=N`) — a few underruns right at song start are
      fine; a steadily climbing count during playback is a fail.
- [ ] **Song switching:** twist the music knob between several channels
      mid-song, including back to quiet (position 0). Each switch should be
      clean — no garbled audio, no crash.
- [ ] **Admin page recovery:** while the page is open, power-cycle the
      controller. The page should show "Controller not responding —
      retrying…" and come back on its own once the AP is up (no manual
      reload needed).
- [ ] **Watchdog reboot:** hold the serial monitor open and confirm the
      station reboots itself if the loop ever wedges (hard to force
      naturally — acceptable to just confirm normal operation shows no
      spurious watchdog resets over a long session).
- [ ] **Remote reboot button:** Controller card → Reboot. Pump shuts off, an
      in-progress session is logged with reason `REBOOT`, station returns to
      idle in ~20 s.
- [ ] **Find speaker button:** with the speaker off, wait until the speaker
      card shows "search paused" (≈2 min after boot), turn the speaker on,
      tap **Find speaker** — it should connect within ~15 s.
- [ ] **BT backoff vs WiFi:** with the speaker unavailable, confirm the
      admin page stays snappy after the first 2 minutes (discovery held).
- [ ] **Health telemetry:** watch serial `[HEALTH]` lines over an hour of
      mixed use — `min_heap` should level off, not keep sinking.
- [ ] **Pulse totals snapshot:** run a shower, note the member's total
      gallons on the admin page, reboot, confirm the total survives and boot
      is quick (`/PULSETOT.CSV` exists on the SD card).
- [ ] **Audio upload still works:** upload a channel-1 PCM from the admin
      page while nothing is playing; "all channel tracks ready" should be
      correct immediately after (no reboot needed).
- [ ] **Door display:** confirm the OLED sign still tracks
      OPEN / IN_USE / UNAVAILABLE through a full session.
- [ ] **Touch START/STOP backup:** tap a wristband; a large green START circle
      appears. Tap the circle once: pump relay clicks on, circle turns red and
      reads STOP. Tap STOP: pump off, summary screen, session logged with
      reason `TOUCH`. Then repeat mixing inputs (touch START, button finish;
      button start, touch STOP). Confirm a tap outside the circle, a tap on the
      idle/summary screens, and a rapid double-tap on START each do nothing
      extra (water stays on after the double-tap). Confirm the touch does
      nothing while no session is open or during calibration.

## CampNet (branch `worktree-camp-network`)

- [ ] **Right images on the right boxes:** serial `status` on each Tough prints
      `[STATION] id=… role=…` matching its label; each door sign shows `S1` /
      `S2` in the corner matching the shower it is mounted on.
- [ ] **Door sign tracks its own shower only:** start a session on Shower 1 —
      door 1 flips to IN USE within a second, door 2 stays OPEN. Power off
      Shower 1's Tough — door 1 shows OFFLINE within 3 s.
- [ ] **Peers visible:** every Tough's screen header shows `READY · 3 NET`
      with all four powered; the admin Station card lists the other three.
- [ ] **Enroll anywhere:** enroll a wristband on the water-fill station, tap
      it on a shower within 30 s — session opens.
- [ ] **Camp-wide total on the summary screen:** finish a shower, then a
      water fill with the same wristband — the fill's summary shows the shower
      gallons under "Showers" and the sum under "YOUR TOTAL THIS BURN".
- [ ] **Limits sync:** change the RV fill limit on Shower 1's admin page; the
      RV station's idle footer updates within 30 s and an RV fill ends at the
      new limit.
- [ ] **Ledger survives reboot:** power-cycle a Tough with the others off;
      the admin Members card still shows the other stations' gallons
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
