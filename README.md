# MatrixPortalClock V2

LED matrix clock with animated digits for the **Adafruit MatrixPortal S3** (ESP32-S3)
and the **Adafruit MatrixPortal M4** (SAMD51 + WiFiNINA).
Fetches the time from the local network via NTP and offers a WiFi configuration mode.

## Hardware targets

One code base, two boards. `src/board_hal.h` holds everything that differs between
them - matrix and button pins, the WiFi/NTP calls, the settings storage and the
reset instruction - so the sketch itself is board independent.

| | MatrixPortal S3 | MatrixPortal M4 |
|---|---|---|
| PlatformIO env | `adafruit_matrixportal_s3` (default) | `adafruit_matrix_portal_m4` |
| MCU | ESP32-S3, 8 MB flash | SAMD51, 512 KB flash |
| WiFi | on-chip | WiFiNINA co-processor over SPI |
| NTP | lwIP SNTP client | the NINA firmware's own SNTP |
| Settings stored in | NVS partition (0x9000) | flash block at 0x7E000 |
| User button | GPIO6 (`BUTTON_UP`) | D2 (`UP`) |
| Upload | esptool over native USB | UF2 bootloader (double reset) |

The S3 was added because the M4's WiFiNINA link kept causing WiFi trouble; on the S3
the radio sits on the main MCU, which also lets the AP config page preview the clock at
30 fps instead of 5. The onboard LIS3DH accelerometer (I2C `0x19`) and the external
BH1750 light sensor are identical on both boards.

**Caution on the S3:** `D2` is the matrix clock line, not the UP button - the M4's
button pin cannot be carried over.

## Features

- Animated digits (fly-in, direction configurable per digit)
- **Automatic orientation** via the onboard accelerometer: the display rotates in
  90° steps to match how the panel is held (both portrait and both landscape
  positions)
- **Automatic brightness** via an external BH1750 light sensor: a configurable
  lux → brightness mapping dims the clock once per second to match the room
- NTP time synchronization with a daily resync
- **User button** (UP button; S3: GPIO6, M4: D2):
  - **1x short** -> toggle daylight saving / standard time (+/- 1 h); shows a 3 s
    `summer` (orange) / `winter` (ice-blue) banner
  - **2x short** -> toggle auto-brightness (light sensor) on/off
  - **3x short** -> open the WiFi access point for settings (3x again closes it)
  - **hold long** -> adjust brightness (cyclic, perceptually linear); releasing
    saves it. With auto-brightness on, this trims the brightness *relative* to the
    measured ambient level (neutral in the middle) instead of setting an absolute level
  - **Feedback:** the red board LED (D13) lights while the button is held. Once a
    click sequence has triggered its function, it blinks once per click (1x, 2x or
    3x), starting after a short dark pause (~0.65 s after the last release). A sequence that triggers nothing (1x/2x while the config AP is open) gets
    no blink. A 2x2 square in the bottom-right matrix corner mirrors the LED
    (compile-time switch `BUTTON_FEEDBACK_ON_MATRIX`)
- **AP config page** (`http://4.3.2.1`): timezone, daylight saving, brightness, animation speed, colors, fly-in directions, NTP sync time
- All settings are stored outside the program image, so they survive a restart
  **and a firmware re-upload** (S3: the NVS partition, which a normal upload does
  not touch - `pio run -t erase` does; M4: a fixed flash block at 0x7E000)
- Recovery: hold the user button during boot -> the AP opens even without the home WiFi

## Setup

1. Create the WiFi credentials:
   ```
   copy src\arduino_secrets.h.example src\arduino_secrets.h
   ```
   and enter SSID/password in `src/arduino_secrets.h`.

2. Build / upload. The default environment is the MatrixPortal S3:
   ```
   pio run                 # compile
   pio run -t upload       # flash
   pio device monitor      # serial output (115200 baud)
   ```
   **Uploading to the S3 needs the board in a bootloader first.** The automatic
   1200-baud reset through the running clock's USB port does not work here: the
   board switches its USB over to the ROM bootloader, but Windows does not notice
   the disconnect and keeps a dead serial port until the next reset. Use one of:
   - **UF2 (simplest):** double-tap **RESET** (the second tap while the NeoPixel
     is purple), then copy `.pio/build/adafruit_matrixportal_s3/firmware.uf2`
     (written by every build) onto the `MATRXS3BOOT` drive. The board restarts
     into the new firmware by itself.
   - **ROM bootloader:** hold **BOOT**, tap **RESET**, release **BOOT**, then
     `pio run -t upload --upload-port <port>`. Afterwards esptool cannot restart
     the board over USB - press **RESET** to start the clock.

   **Power:** WiFi transmit bursts draw about 0.3-0.5 A. On a weak USB port or
   cable the supply dips far enough to reset the board (reset reason *brownout*
   or *power-on* in the serial log) right when WiFi starts. Because that reset
   falls into TinyUF2's double-reset window, it can look as if the board only
   ever boots into `MATRXS3BOOT`. Use a good cable and port, or a power supply.

   For the MatrixPortal M4, select its environment and put the board into the
   bootloader via **double reset** first:
   ```
   pio run -e adafruit_matrix_portal_m4 -t upload
   ```

3. NTP on the S3 uses `pool.ntp.org` / `time.nist.gov`. If the clock has no internet
   access, point it at a server on the LAN (e.g. a Fritz!Box) by adding to the
   `[env:adafruit_matrixportal_s3]` section of `platformio.ini`:
   ```
   build_flags = -D NTP_SERVER_1='"192.168.2.1"'
   ```

## AP configuration

Press the user button 3x short -> the board opens the access point (another
3x short closes it again and returns to the normal clock):

| | |
|---|---|
| SSID | `MatrixClock` |
| Password | `clock1234` |
| Config page | `http://4.3.2.1` |

A built-in **captive portal** (DNS hijack + portal page) makes the config page pop
up automatically on most phones right after connecting to the AP. If it does not,
open the config page address manually.

Both boards deliberately use a *public* address (4.3.2.1, same as WLED) for the
AP. Current Android versions skip their captive-portal check when its host name
resolves to a private address such as 192.168.4.1 and then report "connected
without internet" instead of "sign in to network". The address only exists
inside the clock's own isolated access point.

The matrix shows SSID, password and IP **immediately** when the AP opens (before
the radio has finished coming up, so there is no frozen display), in landscape
orientation, until a client connects; after that it switches to the live clock
preview so that **brightness, colors and animation speed preview live** while you
change them in the web UI (at 30 fps on the S3, 5 fps on the M4 - every WiFiNINA
socket write is an SPI round trip there). The timezone is chosen from a dropdown. "Save &
Restart" stores everything to flash and reboots.

The color palette offers **full colors only** for both the digit and the fly-in
color. The fly-in (trail) is automatically dimmed relative to the master
brightness — floored so it never quantises to black — so the fly-in animation
stays visible at any brightness instead of disappearing.

## Orientation

The onboard **LIS3DH** accelerometer (present on both boards, I2C `0x19`) detects
gravity and rotates the display in 90° increments so the clock is always upright:

- **Portrait** (32 wide × 64 tall): the hours/minutes/seconds digits are stacked
  vertically (the original layout).
- **Landscape** (64 wide × 32 tall): hours and minutes are shown large
  side-by-side with the seconds small underneath. The per-digit fly-in
  directions are swapped (`from right ↔ from top`, `from left ↔ from bottom`) so
  digits enter across the short edge instead of sweeping the full width.

The rotation switches with a short debounce and ignores near-45° tilts to avoid
flicker. If the accelerometer is not found, the clock stays in portrait.

Every screen aligns the same way: the **boot status text**, the **DST banner**,
and the **AP clock preview** all follow the accelerometer. The **AP info screen**
(SSID/PW/IP) is always landscape because the text is too wide for portrait, but
it auto-flips (rotation 0/2) so it is never upside down.

If the panel rotates the "wrong" way for your build, adjust the single
`ORIENT_MAP` table in `updateOrientation()` — the serial console prints the raw
axes and the chosen rotation to make calibration easy.

## Auto-brightness (BH1750 light sensor)

An external **BH1750** ambient light sensor (I²C, default address `0x23`) can
drive the master brightness automatically. Wire it to the board's I²C pins
(SDA/SCL, 3V3, GND — e.g. via the STEMMA QT connector); it shares the bus with
the accelerometer.

The sensor is read **once per second** and feeds a **10 s moving average**; the
averaged lux sets a brightness target that the display **fades to smoothly every
frame** instead of stepping once per second. The averaged lux is mapped to a
brightness through a configurable linear curve, set on the AP config page:

| Field | Meaning |
|---|---|
| Dark lux | at/below this lux → **Min brightness** |
| Bright lux | at/above this lux → **Max brightness** |
| Min / Max brightness | brightness endpoints (0–255) |

Between the two lux values the brightness is interpolated linearly and clamped.
Toggle the feature with the **Auto brightness** checkbox or a **double click** of
the user button. When it is off (or the sensor is missing) the manual brightness
slider / long-press fade apply as before. The serial console logs the measured
lux and resulting brightness.

In auto mode the **brightness value (0–255) becomes a relative trim** around the
sensor-derived brightness rather than an absolute level: **128 = neutral** (use
the sensor value as-is), lower = darker, higher = brighter (up to ~2×, clamped).
So the long-press fade (or the slider) lets you quickly nudge the clock brighter
or darker without touching the lux mapping. The configured **Min brightness is a
hard floor** that the manual trim can never undercut. The detailed lux range /
mapping fields stay available for finer control.

## Libraries

Resolved automatically by PlatformIO from `platformio.ini`.

Both boards: Adafruit Protomatter, Adafruit GFX, Adafruit BusIO, Adafruit LIS3DH,
Adafruit Unified Sensor, BH1750FVI_RT (Rob Tillaart). All versions are pinned.

Timekeeping lives in `src/clock_time.h` and only uses the C library's `<time.h>`.
It replaced the Time library (TimeLib), which is unmaintained since 2021 and does
not build against picolibc, the default C library from ESP-IDF 6 on.

MatrixPortal M4 only: WiFiNINA, FlashStorage_SAMD. On the S3 the WiFi stack and the
NVS settings storage come from the ESP32 Arduino core, so no extra library is needed.
