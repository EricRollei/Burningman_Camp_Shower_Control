# Camp Firmware Uploader

This terminal app builds and uploads the correct PlatformIO environment for
each of the camp's six controllers. It always builds the source in the current
checkout before uploading, so the displayed Git branch and commit identify the
firmware being installed.

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

The app will:

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

To check the selected profile and generated command without building or
touching the device, use:

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

## Tests

```sh
cd firmware/uploader
python3 -m unittest -v
```
