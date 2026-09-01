# Camp Firmware Uploader

This terminal app builds and installs the correct PlatformIO environment for
each camp controller. Toughs can be updated over their local Wi-Fi after a
one-time USB bootstrap; USB flashing remains available for all six devices.
It always builds the source in the current checkout, so the displayed Git
branch and commit identify the firmware being installed.

## Start it

From the repository root:

```sh
python3 firmware/uploader/firmware_uploader.py
```

To install the short `tawdry` command on macOS, run this once from the
repository root:

```sh
ln -sf "$PWD/firmware/uploader/tawdry" "$HOME/.local/bin/tawdry"
```

After that, you can start the app from any directory with:

```sh
tawdry
```

The command forwards arguments, so `tawdry --dry-run` is also supported. The
link points at this checkout, which means it always launches the uploader code
you are currently working on.

For a USB flash, the app will:

1. Ask which labeled station is being flashed.
2. Detect connected USB serial devices with PlatformIO.
3. Show the station profile, expected hardware, source commit and USB port.
4. Require confirmation before building or uploading.
5. Stream the PlatformIO log and show a progress bar while esptool writes the
   firmware.

After an upload, connect the next device and press Enter to return to the
profile menu. The uploader never edits Wi-Fi credentials, CampNet secrets,
station limits or SD-card data. Those remain controlled by the firmware and
admin page.

For a Tough Wi-Fi update, the app will:

1. Ask for one of the four labeled Tough profiles.
2. Build that profile before the laptop joins the controller's no-internet
   Wi-Fi network.
3. Prompt you to join the controller Wi-Fi and query `192.168.4.1`.
4. Refuse the update unless the connected station id and role match, the
   controller is idle, and the image fits the inactive OTA slot.
5. Ask for confirmation, command the station into all-relays-off maintenance
   mode, stream the image with progress, and verify its SHA-256.
6. Wait for reboot and the five-second boot health check. Success is reported
   only after the new image is marked valid; a returned prior version is
   reported as an automatic rollback.

All Toughs currently advertise the same Wi-Fi name and address. Stay close to
the intended controller while updating one at a time. Tawdry checks identity
before upload and after reboot, and stops if macOS roams to another station.
The Wi-Fi password is the OTA access gate; the OTA endpoints are not exposed
through CampNet or the internet.

The first OTA deployment still needs USB: choose **Flash controller over USB**
and install the OTA-capable image on each Tough once. Door displays do not host
the controller access point and remain USB-only.

To check the selected operation, profile and generated command without
building, connecting or touching a device, use:

```sh
python3 firmware/uploader/firmware_uploader.py --dry-run
```

## Requirements and troubleshooting

- macOS, Python 3.10 or newer, and the `pio` command on `PATH`
- one M5Stack Tough or M5NanoC6 connected over USB (multiple devices can be
  connected, but the port must then be selected explicitly)
- the PlatformIO platforms and dependencies used by the two firmware projects

The station selection is authoritative. USB serial metadata cannot determine
which physical station label a board should receive, so check the confirmation
screen before every upload. A failed or interrupted upload is reported as a
failure and can be retried; reconnect the board if its USB port disappears.

PlatformIO may need to download a different Arduino framework when switching
between Tough and NanoC6 builds. Let one upload finish before starting another.

`--ota-host HOST[:PORT]` overrides `192.168.4.1` for development or a local
test server. OTA uses only Python's standard library; it adds no Python package
dependency.

## Tests

```sh
cd firmware/uploader
python3 -m unittest -v
```
