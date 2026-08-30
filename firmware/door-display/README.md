# NanoC6 Door Display

This firmware drives the external M5Stack SH1107 Unit OLED from an M5NanoC6.
Keeping the NanoC6 and OLED together avoids running I2C over the six-foot cable.

## Wiring

Connect the OLED directly to the NanoC6 Grove I2C port with a short cable:

- Red: 5V
- Black: GND
- Yellow: SDA (GPIO 2)
- White: SCL (GPIO 1)

## How it follows its shower

The NanoC6 does not join any Wi-Fi network. It parks its radio on the shared
CampNet channel (`CampNet::CHANNEL` in `firmware/shared/CampNetProtocol.h`)
and listens for the ESP-NOW `STATUS` broadcast that every Tough sends twice a
second. It only acts on packets whose station id matches its own
`DOOR_STATION_ID`, so a sign for Shower 1 ignores Shower 2 and the fill
stations entirely.

The Tough reports `IN USE` for the entire authorized session, including while
its pump is paused. The Nano displays `OFFLINE` if it goes more than three
seconds without a valid broadcast from its shower, and `LISTENING FOR SHOWER n`
until the first one arrives. Every screen carries a small `S1` / `S2` badge in
the top-right corner so a sign flashed for the wrong shower is obvious.

The NanoC6 onboard button (GPIO 9) redraws the current screen and prints
packet counters to serial; it does not override the state supplied by the Tough.

## Build and upload

Each sign gets its own image with the shower id baked in:

For routine flashing, run the repository's labeled terminal uploader from the
repository root. It selects the USB port, confirms the door profile, and shows
upload progress:

```sh
python3 firmware/uploader/firmware_uploader.py
```

See [`firmware/uploader/README.md`](../uploader/README.md) for details. The
direct PlatformIO commands remain available for development:

```sh
cd firmware/door-display
pio run -e door1 --target upload    # sign on Shower 1
pio run -e door2 --target upload    # sign on Shower 2
pio device monitor
```

`door1` matches the Tough built with `-e shower1`, `door2` with `-e shower2`.
