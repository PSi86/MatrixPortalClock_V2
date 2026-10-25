# Flashing the clock

How to put a released firmware onto a board, with nothing to build. The images
are on the repository's [Releases](https://github.com/PSi86/MatrixPortalClock_V2/releases)
page. Building it yourself with PlatformIO is described in the
[README](README.md#setup).

## Which file

Each release has two images per board:

| Board | Panels | First install | Update |
|---|---|---|---|
| Adafruit MatrixPortal S3 | one 64x32 panel | `matrixportalclock-<version>-matrixportal-s3-64x32-full.bin` | `matrixportalclock-<version>-matrixportal-s3-64x32-update.bin` |
| Waveshare ESP32-S3-RGB-Matrix | six 64x64 panels as one 192x128 wall | `matrixportalclock-<version>-waveshare-wall-192x128-full.bin` | `matrixportalclock-<version>-waveshare-wall-192x128-update.bin` |

- **Full** (written at address `0x0`): bootloader, partition table and the
  clock. Use it the first time, and on a board that ran any other firmware
  before (Adafruit's demo or CircuitPython, Waveshare's demo, WLED, ...). It
  also clears the clock's settings: the stored WiFi, the time zone, the faces'
  settings and the Home Assistant link go back to their defaults. GIFs already
  on the board stay.
- **Update** (written at address `0x10000`): the clock alone. Settings, WiFi
  and GIFs stay. Only for a board that already runs this clock.

The wall image expects the six panels chained as described in the README's
[Wall clock](README.md#wall-clock) section: the board feeds the bottom right
panel, the chain runs left along the bottom row and back right along the top
row, whose panels hang upside down.

`SHA256SUMS.txt` holds the checksums of the images. To check a download:
`certutil -hashfile <file> SHA256` on Windows, `sha256sum -c SHA256SUMS.txt`
on Linux, `shasum -a 256 <file>` on macOS.

## What you need

- The board and a USB-C cable that carries data. Many charging cables do not:
  if the board does not show up on the PC at all, try another cable.
- Either a browser with Web Serial (Chrome or Edge on Windows, macOS or Linux;
  not Safari, not Firefox), or Python with
  [esptool](https://docs.espressif.com/projects/esptool/) 5 or newer
  (`pip install --upgrade esptool`).
- For the wall clock, a 5 V supply for the panels: the brightness is capped
  for one of 7.5 A (see [Wall clock](README.md#wall-clock)).

## The bootloader by hand

On a **first install** the board still runs other firmware, which may not let
the flashing tool reset it. Start the chip's own bootloader by hand before you
connect:

1. Plug the board into the PC.
2. Hold **BOOT**, press and release **RESET**, then release **BOOT**.
   (Or hold BOOT while you plug the cable in.)

The panel may freeze or go dark while the bootloader runs; that is harmless. On an **update** the clock lets the tool reset it by itself and none
of this is needed. Should connecting ever fail, start the bootloader by hand
and try again.

## Before an update of the wall clock

While the bootloader runs, nothing drives the panels, and the last row pair the
clock sent stays lit at full brightness for those few seconds. Switch the
panels dark first. The clock's web app must be on the home network (the wall
clock has it on from the start); use the clock's own password (preset to
`clock1234`) and its address, which the web app shows:

```
curl -X POST -u clock:clock1234 http://matrixclock-xxxxxx.local/api/black
```

The panels stay dark until the clock starts again after the update.

## Way 1: in the browser

1. Open Espressif's flasher, <https://espressif.github.io/esptool-js/>, in
   Chrome or Edge.
2. Plug the board in. On a first install, start the bootloader by hand (see
   above).
3. Under **Program**, leave **Baudrate** at 921600 and click **Connect**. The
   browser lists the serial ports: pick the board's. If you are not sure which
   one it is, unplug the board and see which entry disappears.
4. Once connected, the page shows a table with **Flash Address** and **File**.
   In its row:
   - set the address: `0x0` for the full image, `0x10000` for an update. The
     row starts with `0x1000`, which is wrong for these boards: change it, and
     keep the `0x`.
   - choose the file.

   Use one row only. Leave **Flash Mode**, **Flash Frequency** and
   **Flash Size** at `keep`.
5. Click **Program** and wait until it has finished writing.
6. If the clock does not start by itself, press **RESET** (or unplug the board
   and plug it in again).

Do **not** click **Erase Flash**: it would also wipe the GIFs and the
settings, and the clock does not need it.

## Way 2: on the command line

With the board plugged in (and, on a first install, in its bootloader), using
the board's port (`COM5` on Windows, `/dev/ttyACM0` on Linux,
`/dev/cu.usbmodem...` on macOS):

First install:
```
esptool --chip esp32s3 --port COM5 --before default-reset --after watchdog-reset write-flash 0x0 matrixportalclock-<version>-matrixportal-s3-64x32-full.bin
```

Update:
```
esptool --chip esp32s3 --port COM5 --before default-reset --after watchdog-reset write-flash 0x10000 matrixportalclock-<version>-matrixportal-s3-64x32-update.bin
```

For the wall clock, take the `waveshare-wall-192x128` files instead. esptool
resets the board into its bootloader before writing and starts the clock
again afterwards. On Windows, set `PYTHONIOENCODING=utf-8` first
(`$env:PYTHONIOENCODING='utf-8'` in PowerShell, `set PYTHONIOENCODING=utf-8`
in cmd): without it, esptool's progress output can break a transfer off with a
misleading serial error.

## The first start

The clock shows dashes until it has a time. Set it up in its web app through
its own hotspot:

1. Open the hotspot:
   - **MatrixPortal S3:** press DOWN to open the menu, go to **Hotspot** with
     UP or DOWN, and hold DOWN. Or hold UP while the clock starts (hold UP,
     press RESET, keep UP held until the hotspot screen shows).
   - **Wall clock:** the hotspot opens by itself while no home WiFi is stored
     (with a gesture sensor fitted: hold a hand over it while the clock
     starts, then take it away).
2. Join the WiFi `MatrixClock` with the password `clock1234`. The settings page
   opens by itself on most phones; otherwise open `http://4.3.2.1`.
3. Under **Home WiFi**, pick your network, enter its password and press
   **Save WiFi & connect**. The clock joins it and fetches the time.

Everything else - faces, brightness, time zone, the info band, Home Assistant -
is set on the same page; the [README](README.md) describes it all.

GIFs are not part of the images. The MatrixPortal S3 keeps them in a GIF pack in
its flash (see [GIF pack](README.md#gif-pack)); the wall clock plays them from a
microSD card as well (see
[GIFs on the wall clock's microSD card](README.md#gifs-on-the-wall-clocks-microsd-card)).

## If something goes wrong

- **The board does not show up on the PC:** the cable carries no data, or the
  board is not in the bootloader. Try another cable, then start the bootloader
  by hand.
- **The clock restarts while it joins the WiFi:** the power supply. A USB-C
  charger on a C-to-C cable made the MatrixPortal S3 reset at random moments;
  with an A-to-C cable it ran. The clock shows the cause of an
  unexpected restart for 2 s when it starts (`BROWN`, `PANIC`, ...).
- **Back to Adafruit's firmware** on the MatrixPortal S3: see
  [Flash layout](README.md#flash-layout-s3) in the README.
