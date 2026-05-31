# MatrixPortalClock V2

LED matrix clock with animated digits for the **Adafruit MatrixPortal M4** (SAMD51 + WiFiNINA).
Fetches the time from the local network via NTP and offers a WiFi configuration mode.

## Features

- Animated digits (fly-in, direction configurable per digit)
- **Automatic orientation** via the onboard accelerometer: the display rotates in
  90° steps to match how the panel is held (both portrait and both landscape
  positions)
- NTP time synchronization with a daily resync
- **User button** (UP button, D2):
  - **1x short** -> toggle daylight saving / standard time (+/- 1 h)
  - **3x short** -> open the WiFi access point for settings
  - **hold long** -> fade brightness cyclically (perceptually linear); releasing saves it
- **AP config page** (`http://192.168.4.1`): timezone, daylight saving, brightness, animation speed, colors, fly-in directions, NTP sync time
- All settings are stored in flash (survive a restart)
- Recovery: hold the user button during boot -> the AP opens even without the home WiFi

## Setup

1. Create the WiFi credentials:
   ```
   copy src\arduino_secrets.h.example src\arduino_secrets.h
   ```
   and enter SSID/password in `src/arduino_secrets.h`.

2. Build / upload (first put the board into the bootloader via **double reset**):
   ```
   pio run                 # compile
   pio run -t upload       # flash
   pio device monitor      # serial output (115200 baud)
   ```

## AP configuration

Press the user button 3x short -> the board opens the access point:

| | |
|---|---|
| SSID | `MatrixClock` |
| Password | `clock1234` |
| Config page | `http://192.168.4.1` |

A built-in **captive portal** (DNS hijack + portal page) makes the config page pop
up automatically on most phones right after connecting to the AP. If it does not,
open `http://192.168.4.1` manually.

The matrix shows SSID, password and IP in landscape orientation until a client
connects; after that it switches back to the normal clock so that **brightness,
colors and animation speed preview live** while you change them in the web UI.
The timezone is chosen from a dropdown. "Save & Restart" stores everything to
flash and reboots.

## Orientation

The onboard **LIS3DH** accelerometer detects gravity and rotates the display in
90° increments so the clock is always upright:

- **Portrait** (32 wide × 64 tall): the hours/minutes/seconds digits are stacked
  vertically (the original layout).
- **Landscape** (64 wide × 32 tall): hours and minutes are shown large
  side-by-side with the seconds small underneath. The per-digit fly-in
  directions are swapped (`from right ↔ from top`, `from left ↔ from bottom`) so
  digits enter across the short edge instead of sweeping the full width.

The rotation switches with a short debounce and ignores near-45° tilts to avoid
flicker. If the accelerometer is not found, the clock stays in portrait.

If the panel rotates the "wrong" way for your build, adjust the single
`ORIENT_MAP` table in `updateOrientation()` — the serial console prints the raw
axes and the chosen rotation to make calibration easy.

## Libraries

Resolved automatically by PlatformIO from `platformio.ini`:
Adafruit Protomatter, Adafruit GFX, WiFiNINA, Time (TimeLib), FlashStorage_SAMD,
Adafruit LIS3DH, Adafruit Unified Sensor.
