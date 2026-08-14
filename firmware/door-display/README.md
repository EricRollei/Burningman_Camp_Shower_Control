# NanoC6 Door Display

This firmware drives the external M5Stack SH1107 Unit OLED from an M5NanoC6.
Keeping the NanoC6 and OLED together avoids running I2C over the six-foot cable.

## Wiring

Connect the OLED directly to the NanoC6 Grove I2C port with a short cable:

- Red: 5V
- Black: GND
- Yellow: SDA (GPIO 2)
- White: SCL (GPIO 1)

The NanoC6 joins the Tough's `CampShower-Setup` access point and requests the
current shower state over local UDP. No internet connection or external router
is required. The Tough reports `IN USE` for the entire authorized session,
including while its pump is paused. The Nano displays `OFFLINE` if it goes more
than three seconds without a valid response.

The NanoC6 onboard button (GPIO 9) requests an immediate status refresh; it does
not override the state supplied by the Tough.

## Build and upload

```sh
cd firmware/door-display
pio run --target upload
pio device monitor
```

The Tough-side UDP responder listens on port `4210`; the Nano uses local port
`4211`.
