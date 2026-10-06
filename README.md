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
| Buttons | UP GPIO6 (`BUTTON_UP`), DOWN GPIO7 (`BUTTON_DOWN`) | UP D2, DOWN D3 |
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
  - *Tetris* — `HH:MM` built up from falling tetromino blocks, and knocking the
    clock throws them apart and rebuilds them (S3 only)
- **Automatic orientation** via the onboard accelerometer: the display rotates in
  90° steps to match how the panel is held (both portrait and both landscape
  positions)
- **Automatic brightness** via an external BH1750 light sensor: a configurable
  lux → brightness mapping dims the clock once per second to match the room
- NTP time synchronization with a daily resync. The WiFi is switched on one
  minute before the sync time and off again after the sync. A sync that has not
  worked out after a minute, on a clock that already has a time, switches the
  radio off and tries again 10 minutes later
- **Time from a phone** on the config page, or typed in, for a clock without
  WiFi
- **Real-time clock (optional):** with a DS3231 on the I2C bus the clock has its
  time at power-up, keeps it without WiFi, and syncs with NTP only every 7 days
  by default (up to 12 months, or never); see [Real-time clock](#real-time-clock-ds3231)
- **No waiting for the WiFi at start-up:** the clock is on the panel and
  operable at once, joins the home WiFi in the background (starting the join
  again every 10 s until it has joined) and shows dashes instead of digits until
  it has a time
- **Status pixel**: off in normal operation; a single red pixel in the bottom-left
  corner means a due NTP sync has not succeeded for an hour. That hour is for a
  known-good WiFi config, one a sync has worked over before. A new one - stored
  on the config page, or copied from the build - has not proven itself yet, so
  the pixel turns red at once when it fails to join (within about 10 s) or the
  sync gives up (after a minute), also after a restart, until a sync works
- **Automatic summer/winter time** for every timezone in the menu (rules from the
  IANA tz database), or a fixed summer or winter time
- **On-screen menu** with the UP and DOWN buttons: brightness, auto brightness,
  timezone, daylight saving, the config hotspot and, with a real-time clock, the
  time and date, set on the clock itself without WiFi (see
  [Menu and buttons](#menu-and-buttons))
- **AP config page** (`http://4.3.2.1`, M4: `http://192.168.4.1`): watchface, Tetris drop and turn pace, knock sensitivity and effect, timezone, daylight saving (automatic / summer / winter), brightness, animation speed, colors, fly-in directions, NTP sync time, the button profile, GIFs (S3) and the **home WiFi**
- All settings, the home WiFi included, are stored outside the program image, so
  they survive a restart **and a firmware re-upload** (S3: the NVS partition, which
  a normal upload does not touch - `pio run -t erase` does; M4: one 8 KB flash
  block each at the top of the flash, from 0x7A000 up)
- Recovery: the config hotspot is in the menu from the first second, also
  without the home WiFi; holding UP during boot opens it as well
- **GIFs** (S3): now and then a GIF takes the panel for a few seconds, and holding
  UP on the face plays one at once (see [GIF playback](#gif-playback))
- **Gesture sensor (optional):** with a PAJ7620U2 on the I2C bus, swipes, push,
  circles and a wave work the menu next to the buttons, or instead of them (see
  [Gesture sensor](#gesture-sensor-paj7620u2))

## Setup

1. Optionally, compile in the home WiFi:
   ```
   copy src\arduino_secrets.h.example src\arduino_secrets.h
   ```
   and enter SSID/password in `src/arduino_secrets.h`. A clock that has no WiFi
   stored copies these into flash on its first start, once; from then on the
   WiFi is changed or forgotten on the config page, and editing the file no
   longer changes it. Without the file, set the WiFi on the config page.

2. Build / upload. The default environment is the MatrixPortal S3:
   ```
   pio run                 # compile
   pio run -t upload       # flash
   pio device monitor      # serial output (115200 baud)
   ```
   **Uploading to the S3 needs the board in a bootloader first.** The running
   clock ignores the automatic reset into the bootloader over its USB port (1200
   baud, or DTR and RTS toggled in esptool's order): it never worked here -
   Windows kept a dead serial port - and a serial tool that happened to toggle
   the lines that way sent the clock into its bootloader, where nothing drives
   the panel and one row pair stays lit at full duty. Use one of:
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
   sensors, `DIED3` after the WiFi join started (the clock already runs then),
   `DIED4` after it joined. A start that runs for 15 s clears it. The
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

## Menu and buttons

Two buttons operate the clock: **UP** (S3: GPIO6, M4: D2) and **DOWN** (S3:
GPIO7, M4: D3). They keep their printed meaning in every orientation.

| On the clock face | |
|---|---|
| UP or DOWN short, DOWN held | open the menu |
| UP held | play a GIF (S3) |
| UP held during boot | open the config hotspot, even without the home WiFi |

| In the menu and its editors | |
|---|---|
| UP / DOWN short | previous / next item, or change the value |
| DOWN held (0.6 s) | open the item; in an editor: save |
| UP held (0.6 s) | back, without saving; held on, one more level every 0.6 s, out to the face |

The menu holds five items, shown with their current value: **Bright**, **Auto**
(only with a light sensor), **Zone**, **DST** and **Hotspot**, and a sixth,
**Time**, on a clock with a real-time clock.

- **Bright** shows the brightness large with a scale under it and changes the
  panel live. Manual: 16 steps on a perceptual scale. With auto brightness the
  value is a trim around the light sensor, -8 to +8 (half to twice the sensor's
  brightness), and the editor says `auto`.
- **Auto** switches auto brightness on and off straight in the list.
- **Zone** picks one of the 26 timezones of the config page. Each row shows its
  offset, the time it would show now and its place names, which scroll when they
  are too long. UP goes west, DOWN east.
- **DST** chooses Auto, Summer or Winter, with the time each choice would show.
- **Hotspot** opens the config hotspot; holding UP closes it again.
- **Time** (with a real-time clock) sets the local time and date in five fields
  in turn: hour, minute, year, month, day. UP and DOWN change the field on show
  (UP is later), DOWN held goes to the next field and saves on the last, UP held
  goes back a field and leaves without saving on the first. The seconds start
  at zero, and a banner shows the new time.

From the fourth of several quick presses on (less than 0.4 s apart), each press
moves five steps, so the 16 brightness steps and the 26 zones are quick to cross.
A value is written to flash only when it is saved and differs from the stored
one; leaving an editor any other way puts the stored value back. The menu closes
by itself after 20 s without input. On the S3 the picture slides sideways
between the list and an editor, deeper to the left (`MENU_SLIDE` in
`board_hal.h`); the M4 changes it at once.

Saving a new zone or daylight-saving mode shows a 3 s **banner** with the
resulting time and its offset from UTC, orange while summer time is in effect
and ice-blue otherwise. A button pressed while a banner is up only closes it.

**Feedback:** the red board LED (D13) and a 2x2 square in the bottom-right
matrix corner light while a button is held (the square is the compile-time
switch `BUTTON_FEEDBACK_ON_MATRIX`).

**Classic clicks.** The profile *Classic clicks* (config page, **Inputs**)
brings back the button codes the clock had before the menu: on the face UP 1x
cycles daylight saving with a banner, 2x toggles auto brightness with a banner,
3x opens or closes the hotspot, and holding UP fades the brightness, saved on
release. Each click sequence is confirmed by one blink of the LED per click.
Holding DOWN opens the menu, which then works as above.

Under the hood every input becomes a named event (short, 2x, 3x, hold, hold
repeat, boot, knock, and the gestures), and a profile table in `src/input_map.h`
says what each event does on the current screen. The build checks every
profile: no event may mean two things on one screen, the menu must be possible
to open, move through and leave, and a boot event must open a hotspot that can
be closed again on the clock. These rules hold for each source on its own: the
button table of a profile maps only buttons (and the knock) and keeps them, so a
clock without a gesture sensor can be operated with its buttons; the gesture
table maps only gestures and keeps them as well, so a clock with nothing but
the sensor can be operated too. A table that breaks one of these does not
compile.

## GIF playback

On the MatrixPortal S3 a GIF takes the panel in place of the face now and then,
at a random time between a minimum and a maximum (2 and 30 minutes by default),
for a set time (7 s by default); all three and switching it off are on the config
page under **GIFs**. Holding UP on the face plays one at once. A GIF comes up only
over the plain face; while the menu, a banner or the hotspot is up it waits. Any
button press ends it early and does nothing else; the hold that started it does
not end it.

A GIF plays at its own size, centred, unscaled, and only where it fits the panel
as it is held: 64x32 GIFs in landscape, 32x64 upright, 32x32 either way. Turning
the panel ends a GIF. It is drawn at 8 bits per colour straight into the panel
driver, through a gamma table (GIF colours are made for screens, the light of an
LED is linear in its on-time), and the panel's brightness dims it like the face.

The GIFs are built into the firmware by `scripts/embed_gifs.py`: every GIF in
[`gifs/`](gifs/README.md) that fits the panel (four examples with their licences
and authors), and those of the folders listed in `gif_dirs.local`, smallest first,
up to `custom_gif_budget_kb` (600 KB). That file is ignored by git, so private
GIFs stay out of the repository; a firmware built with them must not be
published. The build says how many it took and which it left out.

Which GIFs fit depends on how the clock stands, `custom_gif_orientation` in
`platformio.ini`: `landscape` (the default) takes GIFs up to 64x32, `portrait`
up to 32x64, `both` those that fit one way or the other, for a clock that gets
turned. A clock that always stands the same way then carries no GIFs it could
never play.

### GIF pack

More GIFs than fit into the firmware go into the GIF pack, in the board's
`ffat` partition (3776 KB), which the clock uses for nothing else. List the
folders for it in `gif_pack.local` in the project folder, one per line (ignored
by git; an `exclude.txt` works as for `gif_dirs.local`), then:

    pio run -e adafruit_matrixportal_s3 -t gifpack                         # builds gifpack.bin
    pio run -e adafruit_matrixportal_s3 -t uploadgifs --upload-port COMx   # builds and writes it

Writing goes through the ROM bootloader: hold BOOT, tap RESET, release BOOT,
run the upload with that port, then press RESET. A UF2 copy cannot write the
pack (TinyUF2 writes the app partition only), so the firmware's UF2 uploads
leave it alone; `pio run -t erase` wipes it.

`scripts/gif_pack.py` takes the GIFs that fit the panel the way the clock
stands, leaves out those the firmware has built in and second copies, and puts
them in smallest first while there is room. Each GIF is kept as it is or
deflated, whichever is smaller (deflating saves about a fifth on typical
pixel-art GIFs). The clock reads the pack's index at start-up, checks a GIF's
CRC each time it is about to play, and inflates a deflated one into PSRAM with
the inflate in the chip's ROM before it plays; the pack's GIFs then come up
like the built-in ones. The console and the config page say how many it holds.
Private GIFs in the pack stay in the pack: the firmware does not contain them,
so a firmware built without `gif_dirs.local` can be passed on while the pack
is not.

## AP configuration

Open the menu, select **Hotspot** and hold DOWN -> the board opens the access
point; holding UP closes it again and returns to the normal clock:

| | |
|---|---|
| SSID | `MatrixClock` |
| Password | `clock1234` |
| Config page | `http://4.3.2.1` (M4: `http://192.168.4.1`) |

A built-in **captive portal** (DNS hijack + portal page) makes the config page pop
up automatically on most phones right after connecting to the AP. If it does not,
open the config page address manually.

The S3 deliberately uses a *public* address (4.3.2.1, same as WLED) for the AP.
Current Android versions skip their captive-portal check when its host name
resolves to a private address such as 192.168.4.1 and then report "connected
without internet" instead of "sign in to network". The address only exists
inside the clock's own isolated access point.

**M4: open `http://192.168.4.1`.** The M4's WiFi co-processor runs Adafruit's
nina-fw 3.3.0, which starts its AP with `WiFi.AP.create()` and offers no command
to set the AP's address (its `setIPconfig` only configures the station). So the
M4's AP is always 192.168.4.1, the web UI answers there, and Android shows no
sign-in prompt for it. The firmware still asks for 4.3.2.1, and the info screen
and the DNS answers show that address, which is wrong on this board (left as it
is for now; new clocks are built on the S3).

The matrix shows SSID, password and IP **immediately** when the AP opens (before
the radio has finished coming up, so there is no frozen display), in landscape
orientation, until a client connects; after that it switches to the live clock
preview so that **brightness, colors and animation speed preview live** while you
change them in the web UI (at the full frame rate on the S3, 5 fps on the M4 - every
WiFiNINA socket write is an SPI round trip there). In the preview a small blue
square blinks in the top right corner: the hotspot is still up, and only holding
UP (close it) works. Once no client has been connected for 5 s, the info screen
comes back. The timezone is chosen from a dropdown. "Save"
stores everything to flash, applies it at once and closes the hotspot, back to
the clock face; the clock keeps its time (it used to restart, which lost a time
set from a phone).

Everything on the page previews live but is only written to flash by "Save".
**Discard changes** puts the stored settings back, so trying something out costs
nothing.

**Time** shows the clock's date and time, its offset from UTC and where the time
came from (NTP, a phone, typed in, the real-time clock), or that it has none, and
whether a real-time clock was found. **Set from this phone** takes the phone's
own clock in one tap; date and time fields (local time in the clock's zone) are
the fallback. Without a real-time clock such a time lasts until the next power
loss, and the next NTP sync replaces it; with one, the real-time clock keeps it.
On a clock with a real-time clock the page also offers how often to sync with
NTP, next to the sync time.

**Inputs** chooses the button profile (*Default* or *Classic clicks*), saved with
the rest of the page.

**Home WiFi** has a form of its own, sent by POST so the password is never part
of a URL. It shows the stored network name; the stored password is never sent
to the page, and a password field left empty keeps it. When the hotspot opens,
the clock scans the networks in range (about 1.6 s, the info screen is already
up) and the page offers the strongest 12 to pick from; a hidden network is
typed into the name field. A line at the top of the page reports how the last
join of the home WiFi went - when, and at what signal, or why it failed - since
the radio is off between syncs. "Save WiFi & connect"
stores the network, closes the hotspot and syncs over it at once. **Forget WiFi**
(after a confirmation) deletes network name and password; the clock keeps its
time until the next power loss (with a real-time clock for good), and the
credentials compiled in from `arduino_secrets.h` are not copied in again.

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

Every screen aligns the same way: the **boot status text**, the **menu**, the
**banners** and the **AP clock preview** all follow the accelerometer; turning
the clock with the menu open keeps the focus and an open edit. The **AP info screen**
(SSID/PW/IP) is always landscape because the text is too wide for portrait, but
it auto-flips (rotation 0/2) so it is never upside down.

If the panel rotates the "wrong" way for your build, adjust the single
`ORIENT_MAP` table in `sensorRotation()` — the serial console prints every
rotation the clock switches to, which makes calibration easy.

The firmware keeps two rotations apart: the **device rotation** (how the panel
is held, from the accelerometer after the debounce) and the **screen rotation**
(what the panel is drawn in right now). They only differ while the AP info
screen is up, so leaving it always returns to how the panel is held — also when
the panel lies flat or has no accelerometer, where the clock used to come back
in the info screen's landscape rotation.

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
Toggle the feature with the **Auto brightness** checkbox or the menu item
**Auto**. When it is off (or the sensor is missing) the manual brightness
(slider, menu item **Bright**) applies as before. The serial console logs the measured
lux and resulting brightness.

In auto mode the **brightness value (0–255) becomes a relative trim** around the
sensor-derived brightness rather than an absolute level: **128 = neutral** (use
the sensor value as-is), lower = darker, higher = brighter (up to ~2×, clamped).
So the menu item **Bright** (or the slider) lets you quickly nudge the clock
brighter or darker without touching the lux mapping. The configured **Min brightness is a
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
once in the list. Where zones with the same offset follow different rules, the
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

**Every piece lands on something** — the bottom row of the digit, or a piece
already lying there. That includes the first piece, which therefore always starts
on the bottom row: every glyph has cells there, so a build can always begin on the
floor. A digit builds upwards from its base, so in a `7` the stroke grows first
and the top bar attaches to it afterwards.

Only tilings that manage this are shipped. Nothing rests on a neighbour and
nothing hangs in mid-air — verified on the emitted tables: of 3038 pieces, 662
land on the floor and 2376 on another piece, and none on nothing. The price is
paid in variety rather than in physics: a digit that cannot offer 32 such builds
ships fewer of them. The `2` has 22, the `5` 23 and the `7` 25, because their
middle bars reach out over empty space and most of their tilings therefore need a
piece that only leans. Every other digit has the full 32.

**Pieces are walked into place.** A piece appears centred over its digit, rounded
to the left as the Tetris guideline has it, and walks across to the column it
lands in - and it turns on the way, as separate flicks rather than a steady spin,
with some pieces not turning at all.

Both obey the game's rule that a piece may not pass through the stack: the walk
and the turning are confined to the rows the piece can fall while still clear of
everything already lying there, because below that only its own column is free.
A high stack therefore leaves little room for either, and a piece that cannot be
walked all the way comes in closer to its column instead — which is what the game
does to you as well. When each of these happens is drawn separately: when the
turning starts, when the walk starts, and how briskly it walks. So a piece with a
long way to fall can still be sorting itself out well down the panel, and the
last column change happens four or more rows down for 73% of them.

The guideline's other move — shifting a piece sideways as it touches down, which
is what the lock delay is for — is deliberately not used. It only works if the
piece could come down in the wrong column first, and with tilings where
everything is reached by a straight drop, that column is usually blocked: a
replay of every frame showed such pieces sinking through the stack. It was worth
one extra cell of reach in about one case in 250.

All of this is checked by replaying 7.6 million drawn frames — both layouts,
several speed settings, every turn count, every walking pace, every start row and
every intermediate orientation. Nothing passes through the stack or the walls,
and every piece lands exactly on the square its tiling gives it.

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

### Knock it and the digits come apart

Give the clock a knock and the digits are thrown away, then built up again from
the current time — with a fresh tiling and fresh colours, so the knock is worth
something. Three effects, selectable and previewing live: **Collapse** (the stack
gives way and drops), **Scatter** (the pieces fly off and tumble) and **Clear
rows** (rows flash and vanish from the bottom, everything above dropping into the
gap), plus *Random each time*. The effect is drawn once per event, so all the
digits involved always come apart the same way.

**The sensor raises the knock itself.** Its own interrupt generator watches for
it and pulls INT1, which is wired to GPIO15 — not documented on Adafruit's pinout
page, but the board schematic has the LIS3DH's INT1 on the same net as the
ESP32-S3's IO15. The sketch only reads that pin. Sampling the sensor instead cost
four I2C transactions 50 times a second and still left a 20 ms gap between reads
that a short tap could slip through.

The sensor compares against a fixed threshold, which would fire on gravity as
soon as the clock is tilted, so its high-pass filter is enabled **for the
interrupt generator only** and not for the output registers — the orientation
detection still needs to see gravity. The data rate went from 10 Hz to 100 Hz:
at 10 Hz a knock lasts about as long as one sample.

*Shake sensitivity* runs from 0 (off) to 10. The scale was measured on the
device, not guessed: over about 70 s of standing still the largest deviation in
any one second was 780 raw counts, which is the sensor's own noise, while every
deliberate interaction produced 1900 or more and a firm knock 12700. That is
0.094 g and 0.375 g — one g is 16000 counts, because the library leaves the part
in high-resolution mode — which the threshold register takes in steps of 16 mg,
so level 10 is 6 steps and level 1 is 24.

Nothing is triggered while another screen owns the panel (the menu, an editor,
a banner), in the config AP, for
the first three seconds after start-up, for 1.2 s after a rotation or after an
effect has run, while an animation is already running, **while a button is
held, and for 3 s after a button was last pressed or let go** — the switches sit
next to the sensor, so a button press is a knock as far as it is concerned, and
so is everything around it: the release, steadying the clock, letting go of a
held button. A knock the sensor latched while the config AP was up is
discarded when the face comes back. The reasons the sensor fires without a real
knock sit together in `knockIsReal()`, the reasons the face cannot be knocked
apart right now in `knockEffectReady()`.

With **Also use it when the time changes** ticked, the same effect replaces the
plain swap when a digit changes: at a minute rollover only the digits that
actually changed come apart, and they rebuild while the others stand still.
Whatever has come apart leaves the space empty for 300 ms before it builds
again. The colon is never touched by any of this — it blinks on the second
throughout, being the one thing on this face that shows time actually passing.

**Trying it out on the config page.** Changing the watchface, the drop or turn
pace, the sensitivity, the effect or the time-change option takes the digits
apart and builds them again straight away, so the setting can be judged on the
panel. That matters here because the clock deliberately ignores the sensor while
the AP is up, so a knock is not available to test with. It happens on the
settled value only — a slider let go of, or a point on it tapped. **Discard
changes** puts the stored settings back: everything on the page previews live but
sits in RAM until saved, and this is the way back from that.

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

On the S3 the loop runs in step with the panel refresh (about 200 Hz):
`show()` waits out one refresh period after the previous frame, and a digit moves
one pixel every whole number of refreshes. The speed setting is therefore rounded
to steps of about 5 ms (12 ms = 2 refreshes per pixel), and every pixel step stays
on the panel equally long. Drawing a frame and handing it over takes well under a
refresh period, so the S3 has plenty of headroom. With a fixed millisecond loop,
as before, the loop drifted against the refresh and single steps stayed on screen
for 1, 2 or 3 refreshes, which showed as a slight judder. The M4 keeps the fixed
loop time.

### Panel driver

The S3 drives the panel with ESP32-HUB75-MatrixPanel-DMA, the M4 with Adafruit
Protomatter (5 bit planes). The clock draws into a 16-bit canvas on both; on the
S3, `show()` hands the pixels that changed to the DMA driver, which keeps its own
frame buffer at 8 bits per colour and dims the whole panel through the OE time.
So dimming costs no colour levels there, while Protomatter dims by scaling the
colours and loses levels at low brightness (six bit planes did not help: it draws
into RGB565 and only green gains a level). Two settings matter on the
MatrixPortal S3 (`PanelCanvas` in `src/board_hal.h`):

- `clkphase = false`: with the driver's default the picture sits one column to the
  left and the right column shows stale data.
- GPIO drive strength 0 on all panel pins: the driver clocks the bus without a
  pause, and at the strongest drive its edges disturbed the WiFi so badly that 20
  of 30 TCP connections to the router failed and the hotspot's captive portal
  hardly came up. At drive strength 0 none failed; the level shifters between the
  S3 and the panel are all these pins drive.

Debug builds (`CLOCK_DEBUG`, e.g. the `adafruit_matrixportal_s3_debug` env) print
the frame rate, the measured panel refresh rate and the draw and `show()` times once
per second.

## Gesture sensor (PAJ7620U2)

A PAJ7620U2 on the I2C bus (address 0x73; on the MatrixPortal through the STEMMA
QT port) recognises nine gestures by itself. The clock looks for it at
start-up; without one everything works as before. Its gestures do the same in
both input profiles:

| Gesture | On the clock face | In the menu and its editors |
|---|---|---|
| swipe up / down | open the menu | previous / next item, or change the value |
| swipe right, push | open the menu | open the item; in an editor: save |
| swipe left | - | back, without saving |
| wave | the knock effect | out to the face |
| circle clockwise / counter-clockwise | play a GIF | previous / next; in an editor five steps (clockwise is up) |
| a hand comes near and stays | a hint: `swipe` / `menu` | - |
| a hand held over the sensor for 3 s during boot | open the config hotspot | - |

On the hotspot screen a swipe to the left closes the hotspot again. Pull (a
hand moving away) does nothing yet.

- **Directions follow the panel.** The direction the sensor reports, plus the
  turn it is mounted at, minus the rotation the panel is drawn in, gives the
  direction on the panel: a swipe up is up as the viewer sees it, however the
  clock is held. The mount turn is set once on the config page (**Inputs**);
  if a swipe up acts as another direction, try the next setting.
- **One gesture at a time:** for 300 ms after a gesture the sensor's next one
  is ignored, so the hand pulled back is not read as the opposite swipe. The
  driver library's own pauses after a gesture (200 ms of blocking) are
  switched off. Every gesture counts as handling the clock, so it holds off the
  knock effect for 3 s, as a button press does.
- **The hint** is a banner that only explains: the swipe that follows closes it
  and goes on to the face, instead of being used up as on other banners.
- **INT line or not.** STEMMA QT carries no interrupt line. Without one the
  clock asks the sensor for a gesture every 50 ms, and on the face also every
  100 ms whether a hand is near (for the hint). With the sensor's INT line
  wired to a free pin and that pin given at build time
  (`-D GESTURE_INT_PIN=A0` in `build_flags`), the sensor is read only when the
  line is low, and nothing is polled; the line reports gestures only, so there
  is no hint then. The 3 s hold at boot is asked either way, once at start-up.

Not yet tested on hardware: no gesture sensor has been connected so far. Still
to be tried with the part: how reliably it tells a hand that stays from one
passing by (the hint and the boot hold), whether the panel's own light or a
cover in front of the sensor makes it see an object that is not there, and
which mount turn its modules need.

## Real-time clock (DS3231)

A DS3231 on the I2C bus (address 0x68; on the MatrixPortal through the STEMMA QT
port, next to the BH1750) keeps the time without power, from its coin cell, to
±2 ppm (about a minute a year). The clock looks for it at start-up; without one
everything works as before.

| | Without RTC | With DS3231 |
|---|---|---|
| Power-up | `--:--` until the first NTP sync or a time set from a phone | the time from the RTC at once |
| NTP sync | daily at the sync time, and at every power-up | at the sync time on the day the interval runs out (7 days by default; daily, 30 days, 3, 6 or 12 months, or never), and at every power-up unless it is never |
| Sync never | not offered | the radio stays off unless the hotspot is opened, and the status pixel stays off |
| Setting the time | phone or date and time fields on the config page | the same, plus the menu item **Time** |

- The RTC holds UTC, like the clock; the zone and daylight saving stay on top,
  so changing them never touches it.
- Every time the clock is given (from NTP, a phone, the config page or the
  menu) is written to the RTC.
- The clock itself counts on `millis()`, whose crystal drifts far more than the
  DS3231. Once a minute, half way through it, the clock reads the RTC and takes
  its time when they are two seconds or more apart (one second apart can be
  the moment of reading alone).
- An RTC whose oscillator had stopped (empty coin cell, or never set) reports
  it, and its time is not used: the clock shows dashes until NTP or the user
  gives it a time, and the config page says so.
- The sync interval is counted in the clock's calendar days since the last good
  sync, so the sync time of the day it runs out counts, whatever time of day
  that sync was made. Stored with the UI settings (`syncDays`).

Not yet tested on hardware: no DS3231 has been connected so far.

## Libraries

Resolved automatically by PlatformIO from `platformio.ini`.

Both boards: Adafruit GFX, Adafruit BusIO, Adafruit LIS3DH, Adafruit Unified
Sensor, BH1750FVI_RT (Rob Tillaart), RTClib (Adafruit, for the DS3231), RevEng
PAJ7620 (Aaron S. Crandall, for the gesture sensor). All versions are pinned.

Panel driver: ESP32-HUB75-MatrixPanel-DMA (mrfaptastic) on the MatrixPortal S3,
built with `NO_CIE1931` so colours and brightness stay linear as before; Adafruit
Protomatter on the MatrixPortal M4.

Timekeeping lives in `src/clock_time.h` and only uses the C library's `<time.h>`.
It replaced the Time library (TimeLib), which is unmaintained since 2021 and does
not build against picolibc, the default C library from ESP-IDF 6 on. The clock
keeps UTC, exactly as NTP delivers it; the timezone and daylight saving are added
only where the time is shown, so changing either never touches the clock.

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
