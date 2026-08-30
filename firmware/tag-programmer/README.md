# Tough Tag Programmer

Standalone batch-enrollment firmware for an M5Stack Tough and RFID2 reader.
It reads each wristband's factory UID; it does not modify data on the tag.

The programmer intentionally contains no relay, pump, flow-meter, audio, or
CampNet control code. The RFID2 may be connected directly to the Tough's
external I2C port or through a PaHUB on any of its six Grove channels.

## Use

1. Insert a microSD card in the Tough and connect the RFID2 reader.
2. Join Wi-Fi `CampTagProgrammer` with password `dustybutthole`.
3. Open `http://192.168.4.1/`.
4. Enter a member name, click **Arm next scan**, and tap one wristband.
5. Repeat for each member. Scan a tag without arming to verify its assignment.

The browser shows every saved member and supports editing, disabling, and
deleting entries. **Download registry** and **Download version** retrieve the
two production-compatible files from the SD card:

- `/MEMBERS.CSV`
- `/MEMBERS.VER`

Copy both files to the root of each production controller's microSD card while
the controller is powered off. Alternatively, retain this card in a Tough,
flash that Tough with its correct production station image, and let its member
registry propagate to the other powered stations over CampNet.

The production schema is:

```csv
uid,name,allowance_gallons,enabled
04A1B2C3D4E5F6,Dusty River,0.000,1
```

`allowance_gallons=0` selects the production station's normal shower limit.

## Build and upload

```sh
pio run --project-dir firmware/tag-programmer
pio run --project-dir firmware/tag-programmer --target upload --upload-port /dev/cu.usbserial-XXXXXXXXXXX
pio device monitor --port /dev/cu.usbserial-XXXXXXXXXXX --baud 115200
```
