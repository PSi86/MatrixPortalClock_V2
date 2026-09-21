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
| Arduino core | Arduino-ESP32 3.3.11 on ESP-IDF 5.5 (pioarduino platform) | Adafruit SAMD (`atmelsam` 8.3.0) |
| WiFi | on-chip | WiFiNINA co-processor over SPI |
| NTP | lwIP SNTP client | the NINA firmware's own SNTP |
| Settings stored in | NVS partition (0x9000) | flash block at 0x7E000 |
| User button | GPIO6 (`BUTTON_UP`) | D2 (`UP`) |
| Upload | esptool over native USB | UF2 bootloader (double reset) |

The S3 was added because the M4's WiFiNINA link kept causing WiFi trouble; on the S3
the radio sits on the main MCU, which also lets the AP config page preview the clock at
the full frame rate instead of 5 fps. The onboard LIS3DH accelerometer (I2C `0x19`) and the external
BH1750 light sensor are identical on both boards.

**Caution on the S3:** `D2` is the matrix clock line, not the UP button - the M4's
button pin cannot be carried over.

## Features

- **Two watchfaces**, selectable on the config page:
  - *Classic* — six animated digits `HH MM SS` (fly-in, direction configurable per digit)
  - *Tetris* — `HH:MM` built up from falling tetromino blocks (S3 only)
- **Automatic orientation** via the onboard accelerometer: the display rotates in
  90° steps to match how the panel is held (both portrait and both landscape
  positions)
- **Automatic brightness** via an external BH1750 light sensor: a configurable
  lux → brightness mapping dims the clock once per second to match the room
- NTP time synchronization with a daily resync
- **Automatic summer/winter time** for every timezone in the menu (rules from the
  IANA tz database), or a fixed summer or winter time
- **User button** (UP button; S3: GPIO6, M4: D2):
  - **1x short** -> cycle the daylight-saving mode: automatic -> summer time ->
    winter time -> automatic; the clock shifts by 1 h where needed and shows a 3 s
    `auto` / `summer` / `winter` banner (orange while summer time is in effect,
    ice-blue otherwise)
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
- **AP config page** (`http://4.3.2.1`): watchface, Tetris drop and turn pace, timezone, daylight saving (automatic / summer / winter), brightness, animation speed, colors, fly-in directions, NTP sync time
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

   **Console:** the normal build uses the board's TinyUSB console (239A:8125).
   The debug environment switches to the chip's USB-Serial-JTAG controller
   (`ARDUINO_USB_MODE=1`, 303A:1001), which is the port the ROM bootloader uses,
   so its output survives from reset into the running clock.

   **Power:** WiFi transmit bursts are the clock's highest current draw. At the
   core's default transmit power of 19.5 dBm the S3 reset while joining the
   WiFi on every power supply tried (5 V/2 A and a 65 W PD charger, several
   cables) while working on a PC USB port; at 11 dBm it runs through. The build
   therefore sets `WIFI_TX_POWER` to 11 dBm (board_hal.h). A charger that drops
   VBUS altogether cannot be fixed that way: with the PD charger on a C-to-C
   cable the board still resets at random moments, so use an A-to-C cable with
   it. Because such a reset can fall into TinyUF2's double-reset window, it may
   look as if the board only ever boots into `MATRXS3BOOT`.

   **Diagnosis without a console:** the clock shows the cause of an abnormal
   reset (`BROWN`, `PANIC`, `TWDT`, ...) for 2 s at boot, and remembers how far
   the previous start got: `DIED1` before the panel, `DIED2` after panel and
   sensors, `DIED3` while joining WiFi, `DIED4` in normal operation. The
   breadcrumb lives in flash, so it survives the detour through the bootloader.

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
change them in the web UI (at the full frame rate on the S3, 5 fps on the M4 - every
WiFiNINA socket write is an SPI round trip there). The timezone is chosen from a dropdown. "Save &
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

## Daylight saving

The daylight-saving setting has three modes: **automatic** (factory default),
**summer time** and **winter time**. Automatic uses the rule of the selected
timezone, taken from the IANA tz database (tzdata 2026.4) as a POSIX TZ string in
the `TZONES` table; zones without daylight saving never switch. The C library
decides from UTC whether summer time is in effect, once at every NTP sync and then
once a minute, and the clock shifts by one hour at the exact moment (EU: 01:00 UTC
on the last Sunday of March and of October). Deciding on UTC means the hour that
repeats in autumn does not switch back again.

The timezone setting stores only the base UTC offset, so every offset appears
once in the menu. Where zones with the same offset follow different rules, the
menu names the one it uses (e.g. "Athens, Helsinki", not Cairo).

Debug builds run a self-test 10 s after boot: 38 checks, one second before and at
each 2026 change of nine zones, printed as `DST self-test: 38/38 ok`. To watch a
real change, build with `-D DST_TEST_UTC=<utc seconds>` in addition to
`CLOCK_DEBUG`: every NTP sync then sets the clock to that instant, e.g.
`1792889940` = 2026-10-25 00:59 UTC, one minute before the EU autumn change.

## Watchfaces

The **watchface** setting on the config page switches between two clock faces. It
previews live, so the choice can be judged on the panel before saving.

**Classic** (default) shows `HH MM SS` with the six flying digits. This is the
face the clock has always had; nothing about it changed.

**Tetris** shows `HH:MM` built up from falling tetromino blocks. Only the digits
that actually change are rebuilt, so at a minute change the rest of the display
stays put. The colon blinks once per second (the face has no seconds digits —
there is no room for them at this block size). Both orientations use the same
block size:

| | layout |
|---|---|
| landscape 64x32 | one line, hours at x 2..27, minutes at x 36..61, rows 6..25, colon at x 30..33 |
| portrait 32x64 | hours in rows 8..27, minutes in rows 36..55, x 3..28, separator dots in the gap at rows 30..33 |

**No two builds look alike.** Each digit is a 6×10 cell glyph, and every glyph
has a cell count divisible by four, so it can be tiled exactly by tetrominoes —
in a great many ways. `scripts/gen_tetris_digits.py` searches 32 different
tilings per digit, orders each one so the pieces can be dropped in from above,
and colours it. The firmware picks a tiling at random whenever a digit changes
and maps the colour classes onto a freshly shuffled palette, so the same digit
is neither assembled nor coloured the same way twice.

**Touching pieces always contrast.** The palette is six hues exactly 60° apart
(red, yellow, green, cyan, blue, magenta — orange is left out because it sits
only 23° from red). The generator colours each tiling as a proper graph
colouring, so two pieces that touch never share a class, and because the hues
are evenly spaced any shuffle keeps them at least 60° apart. Verified on the
generated tables: of 4414 touching piece pairs, **zero** share a colour. It also
spreads the pieces evenly over all six classes, so every digit shows the whole
palette. This deliberately drops the classic Tetris convention of one fixed
colour per shape — that convention is exactly what made equal colours end up
side by side.

**Nothing lands in mid-air.** The pieces of a variant are ordered so that each
one both reaches its place from above and comes to rest on something: on the
bottom row of the digit, or on a piece already lying there. That includes the
first piece, which therefore always starts on the bottom row — every glyph has
cells there, so a build can always begin on the floor. A digit builds upwards
from its base: in a `7` the stroke grows first and the top bar attaches to it
afterwards.

The generator proves this rather than assuming it — it first searches for an
order where every piece lands on something, and only where the glyph makes that
impossible (the middle bar of a `2` reaches out over empty space, so nothing can
ever be under it) may a piece settle against one it touches sideways. A tiling
that needs more than that is thrown away. Over the shipped tables: no piece lands
in mid-air, 77 rest sideways, all 77 forced by the glyph and none avoidable.

**Pieces turn as they fall**, as separate flicks rather than a steady spin: each
piece draws its own turn times, spaced between half and one and a half times the
configured interval, and some pieces do not turn at all. Nothing turns over the
last two rows, so a piece always arrives in the orientation it keeps.

A quarter turn changes a piece's bounding box, so a turning piece can be wider
than where it lands. The library this replaced let those pixels simply run off
the panel — measured on its tables: 21 of its 229 fall states left the six-cell
digit box by up to 6 px, which on the rightmost digit meant drawing out to x=67
on a 64 px panel. Here a turning piece is pushed back inside its own digit, the
way a game kicks a piece off the wall, and it hangs from its landing edge so the
bottom travels smoothly however it is turned. Verified over all 452 560 draw
states — every piece at every height at every turn count: none leaves the box.

**Two settings of its own**, both on the config page and both previewing live:
*Tetris drop* is the milliseconds a block takes per row (20–250, default 70), and
*Tetris turn* the average milliseconds between quarter turns (80–1500, default
260). They are deliberately separate — one sets how fast the digit builds, the
other how busy it looks doing it — and they are kept in their own flash blob, so
the main settings are untouched by them. The *animation speed* setting applies to
the classic watchface only.

The **digit and trail colors do not apply** to this face. The master brightness
(and therefore the light sensor) does: the palette is rescaled before every frame.

The face only repaints while blocks are falling or when something visible changes
(colon, brightness, button indicator), so an idle Tetris clock costs almost no CPU.

The face is switched on for the S3 only (`WATCHFACE_TETRIS` in `board_hal.h`).
Nothing in it is board specific any more, but it has not been tested on the M4,
whose config page therefore has no watchface selector.

## Animation timing

The digits fly in one pixel per step; the **animation speed** setting is the time
per pixel in ms (default 12).

The Tetris watchface has its own pair of settings instead (see above); the
animation speed does not apply to it.

On the S3 the loop runs in step with the panel refresh (about 165 Hz with the
current 5 bit planes): `show()` waits for the refresh that takes the new frame, and
a digit moves one pixel every whole number of refreshes. The speed setting is
therefore rounded to steps of about 6 ms (12 ms = 2 refreshes per pixel), and every
pixel step stays on the panel equally long. Drawing a frame takes about 0.5 ms, so
the S3 has plenty of headroom. With a fixed millisecond loop, as before, the loop
drifted against the refresh and single steps stayed on screen for 1, 2 or 3
refreshes, which showed as a slight judder. The M4 keeps the fixed loop time.

Debug builds (`CLOCK_DEBUG`, e.g. the `adafruit_matrixportal_s3_debug` env) print
the frame rate, the measured panel refresh rate and the draw and `show()` times once
per second.

## Libraries

Resolved automatically by PlatformIO from `platformio.ini`.

Both boards: Adafruit Protomatter, Adafruit GFX, Adafruit BusIO, Adafruit LIS3DH,
Adafruit Unified Sensor, BH1750FVI_RT (Rob Tillaart). All versions are pinned.

Timekeeping lives in `src/clock_time.h` and only uses the C library's `<time.h>`.
It replaced the Time library (TimeLib), which is unmaintained since 2021 and does
not build against picolibc, the default C library from ESP-IDF 6 on.

MatrixPortal M4 only: WiFiNINA, FlashStorage_SAMD. On the S3 the WiFi stack and the
NVS settings storage come from the ESP32 Arduino core, so no extra library is needed.

The Tetris watchface needs no library. Its block tables are generated by
`scripts/gen_tetris_digits.py` into `src/tetris_digits.h` (a generated file — do
not edit it by hand, re-run the script). The script has a fixed seed, so a re-run
reproduces the header byte for byte, and it verifies every table it emits before
writing: exact coverage of the glyph, no overlaps, every piece droppable from
above in the stored order, and a proper colouring within the palette. It refuses
to write anything if a check fails.

## Toolchain (S3)

PlatformIO's own `espressif32` platform still ships Arduino-ESP32 2.0.17, built on
ESP-IDF 4.4, which has been end-of-life (no bug or security fixes) since July 2024.
The S3 therefore builds with the **pioarduino** platform, which packages Espressif's
unmodified Arduino-ESP32 releases for PlatformIO (the same core the Arduino IDE
installs). Two things to know on the build machine:

- This project keeps its PlatformIO platforms and packages in its own core dir,
  `~/.platformio-pioarduino` (`core_dir` in `platformio.ini`), about 7 GB for
  both boards (pioarduino alone is about 6 GB, mostly prebuilt ESP-IDF libraries
  for every ESP32 chip and the toolchain).
  pioarduino deletes every other Arduino-ESP32 framework version in the packages
  dir it runs in and sets up its own Python env there; in the shared
  `~/.platformio` that would break the other ESP32 projects on the machine.
  The regular PlatformIO IDE extension in VS Code handles the separate dir by
  itself.
- Command-line builds must run from PowerShell or cmd. ESP-IDF's tool installer
  refuses to start under Git Bash (it checks for the `MSYSTEM` variable). The
  build and upload buttons in VS Code are not affected.
