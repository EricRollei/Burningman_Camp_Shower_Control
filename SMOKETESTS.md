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

## (add in-flight work below)
