# UI — Screens & Workflow

Screen layouts and interaction flow for the M5Stack Tough display.

------------------------------------------------------------------------

## State Flow

```mermaid
stateDiagram-v2
    [*] --> Idle
    Idle --> LoggedIn: Tag scanned (authorized)
    Idle --> Denied: Tag scanned (unknown)
    Denied --> Idle: Timeout
    LoggedIn --> Showering: Physical or green Start button
    Showering --> Summary: Physical or red Stop button / limit / 20 min timeout
    LoggedIn --> LoggedIn: Different authorized tag (handoff)
    Showering --> LoggedIn: Different authorized tag (handoff, logs old shower)
    Summary --> Idle: 10 s timeout
```

------------------------------------------------------------------------

## Screens

### Idle
- Big Top red-and-cream sunburst, gold frame, and static marquee bulbs
- Role name plus `READY/SERVICE - n NET`
- "TAP YOUR WRISTBAND" with role-specific open wording

### Logged in
- "HOWDY" with a size-to-fit member name
- Camp-wide gallons used this burn
- Large green "START WATER" touchscreen button
- Physical button backup reminder

### Denied
- "NOT AUTHORIZED" and the denial reason
- Return to idle after timeout

### Dispensing (active session)
- Member name and elapsed `MM:SS`
- Live water used in gallons to two decimal places
- Role-specific "used this shower/fill" wording
- Large red "STOP WATER" touchscreen button
- Physical button backup reminder

### Summary (10 seconds, then Idle)
- Ticket with gallons used, elapsed time, and camp-wide burn total
- Role-specific thanks message
- Logging failures retain the usage summary and direct the member to an admin

------------------------------------------------------------------------

## Display behavior

The implemented theme is Option A, **Big Top**, from
`drawings/ui-mockups.html`. All screens use gallons only. Allowance and limit
indicators are intentionally omitted from the display, but firmware enforcement
is unchanged. The frame is redrawn only on state transitions; active gallons
refresh at most four times per second and elapsed time once per second. The
touchscreen Start/Stop control and physical GPIO14 button operate the same
toggle lifecycle; touches outside the large control do nothing.
