# MatrixPortalClock V2

LED matrix clock with animated digits for the **Adafruit MatrixPortal S3** (ESP32-S3),
and as a wall clock on the **Waveshare ESP32-S3-RGB-Matrix** board with six 64x64
panels. The **Adafruit MatrixPortal M4** was supported until 2026-10-21: the last
commit that builds for it is **ae18faf** (see [MatrixPortal M4](#matrixportal-m4)).
Fetches the time from the local network via NTP and offers a WiFi configuration mode.

## Hardware targets

One code base, two boards, both ESP32-S3. `src/board_hal.h` holds everything that
differs between them - matrix and button pins, the panel driver's setup, the board's
own I2C bus - so the sketch itself is board independent.

| | MatrixPortal S3 | Wall clock |
|---|---|---|
| PlatformIO env | `adafruit_matrixportal_s3` (default) | `waveshare_wallclock` |
| Board | Adafruit MatrixPortal S3 | Waveshare ESP32-S3-RGB-Matrix |
| MCU | ESP32-S3, 8 MB flash | ESP32-S3 (WROOM-2-N32R16V), 32 MB flash, 16 MB PSRAM |
| Panel | one 64x32 | six 64x64, 192x128 (three across, two down) |
| Arduino core | Arduino-ESP32 3.3.11 on ESP-IDF 5.5 (pioarduino platform) | as the S3 |
| WiFi | on-chip | on-chip |
| NTP | lwIP SNTP client | as the S3 |
| Settings stored in | NVS partition (0x9000) | NVS partition (0x9000) |
| Buttons | UP GPIO6 (`BUTTON_UP`), DOWN GPIO7 (`BUTTON_DOWN`) | none (the web app, the gesture sensor) |
| Upload | esptool over the chip's USB-Serial-JTAG, no buttons | as the S3 |

**Caution on the MatrixPortal S3:** `D2` is the matrix clock line, not a button
(on the M4 it was UP).

### Wall clock

The Waveshare ESP32-S3-RGB-Matrix drives six 64x64 panels in one chain, three
across and two down: the board feeds the bottom right panel, the chain runs left
along the bottom row and back right along the top row, whose panels hang upside
down. The clock draws its 64x32 picture as everywhere else and shows it three
times the size in the top 192x96; the 192x32 below is the info band (see
[The info band on the wall clock](#the-info-band-on-the-wall-clock)). GIFs play
in the wall's own 192x128 LEDs.

- **Panel driver:** ESP32-HUB75-MatrixPanel-DMA 3.0.15 as a copy of its own in
  `lib/hub75-dma-wall` (what it adds is listed in its README): one 8-bit buffer
  of 192 KB in PSRAM - in internal RAM it left too little for the WiFi - at
  16 MHz on the panel bus (160 Hz); the clock writes only what changed and
  writes those rows back from the CPU's cache to the PSRAM. The output's DMA
  channel runs first among the GDMA channels, and a watch starts the output
  again should it stop.
- **Power:** the brightness is capped at 230 for a 7.5 A supply. Measured on its
  5 V side: the wall dark 555 mA, one panel white 564-567 mA more at brightness
  160 and 451 mA at 128, so the whole wall white would draw about 6 A at 255 and
  5.4 A at 230.
- **Flash:** two app slots of 3 MB, 26,496 KB for the GIF pack from 0x610000
  (`partitions_wallclock.csv`). GIFs above the first 16 MB of the flash are read
  into PSRAM instead of mapped, since the core maps only 16 MB.
- **Knock:** from the board's QMI8658 motion sensor, by its Wake on Motion: the
  chip compares every sample (128 Hz) with a threshold and toggles its INT1
  (GPIO13) for each one over it, and an interrupt counts the edges - the chip
  decides, as the S3's LIS3DH does. The knock sensitivity 1-10 sets the
  threshold from 255 mg down to 30 mg (6: 80 mg); at rest the sensor reads 4-13
  mg, knocks on a panel 180 mg to 3 g (measured 2026-10-19). A knock never
  counts as the input that has to open the menu on this clock without
  buttons, so it does not stop the web app from saving. The chip's tap engine
  answers its commands but reports nothing on this board.
- **Inputs:** no buttons, and no gesture sensor yet; the clock is set up in
  the web app, which is on the home network from the start (on clocks without
  buttons). The hotspot opens
  by itself when no home WiFi is stored, and stays open; and when a stored one
  has not joined for 2 minutes, for 5 minutes, after which the clock tries the
  WiFi again. The I2C sensors (DS3231, BH1750, PAJ7620) go on IO45/IO46 (SDA,
  SCL) and are not tried on this board yet.
- **Real-time clock:** the board's own PCF85063, with a coin cell in its socket
  J4 if wanted, and a DS3231 on the GPIO header before it (see
  [The wall clock's own real-time clock](#the-wall-clocks-own-real-time-clock-pcf85063)).
- **Room temperature and humidity:** the board's Sensirion SHTC3 (0x70 on its
  own bus), read every 5 s and filtered over 30 s; shown in the web app under
  the panel's preview, in the info band, and sent to Home Assistant. It sits on
  the board beside the ESP32-S3 and its regulators, which warm it: on
  2026-10-22 it read 34.5 °C and 27 % in a room of about 24 °C and 50 %.
  **Board warmth** (System section, under the sensors; 0 to 20.0 °C, 10.5 on
  the wall since then) is taken off its temperature, and the humidity is worked
  out for the room's temperature through the saturation vapour pressure
  (Magnus) - the air at the sensor holds as much water as the room's, only
  warmer: 34.5 °C / 27 % became 24.0 °C / 49 %. The conversion also scales the
  sensor's own error, about x1.8 from 34 to 24 °C, so the humidity is a guide
  within a few percent. Whether the warmth stays the same with the panels'
  brightness and the time of day is to be seen in Home Assistant.
- **Panels dark before an upload:** `POST /api/black` (the web app's
  interface, in every build) switches the panels dark until the next start -
  before every upload, since the ROM bootloader leaves the last row pair lit at
  full duty. The debug build's console has it as `black`.
- **Console** (debug build only, see [Console](#console); USB, 115200 baud,
  DTR and RTS low): `black`, `test <panel> <brightness>` and
  `test off` a panel in white to measure the current; `bus <divider>` the panel
  bus' clock; `lan on|off` the web app on the home network; for the panel
  output and the fetches `fetch`, `prio <0-5>`, `gdma`, `crypto [ext]`,
  `tlsmem int|ext`, `status test` for the band's clock status, `rtc` for
  what the real-time clocks hold, `imu` for the knock's sensor, threshold
  and the edges and knocks it gave, and `sd ...` for the microSD card.
- **microSD card:** the GIFs come from it while one with GIFs is in (see
  [GIFs on the wall clock's microSD card](#gifs-on-the-wall-clocks-microsd-card)).
- **Wiring rules** from the bench: the panels' supply minus to the board's GND
  first, the supply on before the ribbon cable goes in.

### MatrixPortal M4

The **Adafruit MatrixPortal M4** (SAMD51, WiFi through a WiFiNINA co-processor
over SPI) was a supported board until 2026-10-21. Its WiFi worked completely
differently from the ESP32-S3's and had caused much of the WiFi trouble, so it was
dropped before the web server and WiFi work that followed.

- **Last commit with M4 support: `ae18faf`** (2026-10-21 16:51, the merge of
  PR #60, full hash `ae18fafc511a73d1493db4fd62185ed6d0807ab9`). It builds for
  the M4 (135,712 bytes of 466,944) and was tested on the M4 on 2026-10-21,
  without a panel: a build of it with the console switched on
  (`-D CLOCK_CONSOLE=1`) ran for 10 minutes without a restart, its loop
  answering a console command every minute; it found its LIS3DH, joined the
  WiFi through the NINA and took the time over NTP. Every commit after it
  builds for the two ESP32-S3 boards only.
- **The M4 clock runs `ae18faf`'s release build** since that test: it answered
  pings during its first sync and took a second upload over USB. Before, it ran
  `2c1194f` (2026-10-10, the merge of PR #43).
- What the M4 build had at `ae18faf`: the Classic face only, the old config
  page written line by line instead of the web app, no GIFs, no info band data,
  no Home Assistant link; buttons UP D2 and DOWN D3; the panel driven by
  Adafruit Protomatter; WiFi and NTP through nina-fw 3.3.0 with WiFiNINA 2.0.1,
  whose AP is always at `192.168.4.1`; the settings in one 8 KB flash block per
  blob from 0x76000 to 0x7E000 (FlashStorage_SAMD).
- To build or flash it again, check out that commit and use its environment.
  The upload resets the board into its bootloader by itself, also from the
  release build, which has no console; a double reset is the fallback:
  ```
  git checkout ae18faf
  pio run -e adafruit_matrix_portal_m4 -t upload
  ```
  Its packages (`atmelsam` 8.3.0 and the ARM toolchain) are still in the
  project's core dir, `~/.platformio-pioarduino`.

## Features

- **Four watchfaces**, selectable on the config page:
  - *Classic* — six animated digits `HH MM SS` (fly-in, direction configurable per digit)
  - *Tetris* — `HH:MM` built up from falling tetromino blocks, and knocking the
    clock throws them apart and rebuilds them
  - *Muncher* — `HH:MM` in dots, which a muncher eats and lays anew at a change
    of the time, two ghosts behind it
  - *Aliens* — `HH:MM` in blocks: at a change of the time a cannon shoots the
    old digit away and the new one marches in, all four as a wave at the full hour
- **Automatic orientation** via the onboard accelerometer: the display rotates in
  90° steps to match how the panel is held (both portrait and both landscape
  positions)
- **Automatic brightness** via an external BH1750 light sensor: a configurable
  lux → brightness mapping fades the clock to the room's light within seconds
- NTP time synchronization with a daily resync. The WiFi is switched on one
  minute before the sync time and off again after the sync, unless the info band
  data still needs it. A sync that has not worked out after a minute, on a clock
  that already has a time, gives up the radio and tries again 10 minutes later
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
  sync gives up (after a minute), also after a restart, until a sync works.
  It is red as well while a sensor found at an earlier start is missing (see
  below), until the sensor is back or the web app forgets it
- **Sensors looked for at every start**, each up to 3 times, 50 ms apart: the
  LIS3DH (orientation, knock; MatrixPortals), BH1750 (light), DS3231
  (real-time clock), PAJ7620U2 (gestures), and on the wall clock its board's
  PCF85063 (real-time clock), QMI8658 (knock) and SHTC3 (room temperature and
  humidity). The clock remembers which it
  had; one of those missing at a start makes the panel say `SENSOR` for 2 s
  and turns the status pixel red. The web app's System section lists every
  sensor - found, found on a later try, missing, or not fitted - with its
  address and what answers on the I2C bus, and **Forget** for a sensor the
  clock no longer has. (On 2026-10-21 the MatrixPortal S3 came up once without
  its LIS3DH, stuck in portrait until the next start; the cause is not known.)
- **Automatic summer/winter time** for every timezone in the menu (rules from the
  IANA tz database), or a fixed summer or winter time
- **On-screen menu** with the UP and DOWN buttons: brightness, auto brightness,
  timezone, daylight saving, the config hotspot and, with a real-time clock, the
  time and date, set on the clock itself without WiFi (see
  [Menu and buttons](#menu-and-buttons))
- **AP config page** (`http://4.3.2.1`): watchface, Tetris drop and turn pace, knock sensitivity and effect, timezone, daylight saving (automatic / summer / winter), brightness, fly-in time, colors, fly-in directions, NTP sync time, GIFs, the info band's place and sources and the **home WiFi**; on a page of its own the input profile and what
  buttons, knock and gestures do on the face (see [Face shortcuts](#face-shortcuts))
- **Web app**: the settings page as an app, through the hotspot and, with
  continuous network access, on the home network behind a password (see
  [Web app and the home network](#web-app-and-the-home-network))
- All settings, the home WiFi included, are stored outside the program image, so
  they survive a restart **and a firmware re-upload** (the NVS partition, which
  a normal upload does not touch - `pio run -t erase` does)
- Recovery: the config hotspot is in the menu from the first second, also
  without the home WiFi; holding UP during boot opens it as well
- **GIFs**: now and then a GIF takes the panel for a few seconds, and holding
  UP on the face plays one at once (see [GIF playback](#gif-playback))
- **Gesture sensor (optional):** with a PAJ7620U2 on the I2C bus, swipes and
  push work the menu next to the buttons, or instead of them (see
  [Gesture sensor](#gesture-sensor-paj7620u2))
- **Info band data**: the weather, the air quality and the DWD
  weather warnings for a place, fetched over HTTPS, and what Home Assistant
  sends (see [Info band data](#info-band-data))
- **Info band** (wall clock): the 192x32 below the face shows the items one
  after the other with their icons, and what the clock works out itself - the
  date, sunrise and sunset, the moon, the clock change, its own status (see
  [The info band on the wall clock](#the-info-band-on-the-wall-clock))
- **Alerts, messages and info**: a weather warning from `moderate` on, or
  an item from Home Assistant marked as an alert, comes up once as a banner
  with its icon, large and in colour; a message from Home Assistant runs
  through as text; the weather and the other information come as info cards,
  once when new and in an info round on the clock's time (every 15 minutes at
  :00, :15, :30 and :45). A mark in a corner of the face stays while there are
  alerts or messages, and everything can be shown again (see [Alerts, messages and info on the panel](#alerts-messages-and-info-on-the-panel))

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
   **Uploading to the S3** needs no button: the clock's console is the chip's
   own USB-Serial-JTAG (USB 303A:1001), so esptool resets the running clock
   into its ROM bootloader by itself and starts it again with a watchdog reset
   when it is done (`pio run -t upload --upload-port <port>`). While the ROM
   bootloader runs nothing drives the panel, and the row pair it was sent last
   stays lit at full duty for those few seconds. Should the board ever not
   answer, the ROM bootloader is there by hand: hold **BOOT**, tap **RESET**,
   release **BOOT**, upload. An upload leaves the settings and the GIF pack
   alone. This needs the clock's own flash layout (see
   [Flash layout](#flash-layout-s3)); a board still on Adafruit's TinyUF2 layout
   is moved over once, as described there.

   **Console:** only the debug builds have one (`adafruit_matrixportal_s3_debug`,
   `waveshare_wallclock_debug`, and the `..._verbose` ones) and the test builds;
   the release builds write nothing and take no commands (since 2026-10-21), not
   even the core's own errors (`CORE_DEBUG_LEVEL=0`). Only the chip's ROM prints
   its few lines at every start. `pio device monitor` (115200
   baud) on the same port. Open it with DTR and RTS low, as `platformio.ini` sets
   for the monitor (`monitor_dtr = 0`, `monitor_rts = 0`): a tool that opens the
   port with both raised - pyserial's default - restarts the clock.

   <a id="console"></a>**Log levels** of the debug build (`CLOCK_LOG`, 1 unless
   built otherwise; the console's `log 1` or `log 2` changes it until the next
   start):

   | Level | What the console shows |
   |---|---|
   | 1 | the start, the sensors, errors and changes of state: the WiFi, the time, Home Assistant, fetches, GIFs, inputs; of the core's own lines only its errors |
   | 2 | and the running figures: the light every second, the frames and the face's passes every second or two (`frames`, `show:`, `face N:`), the RAM every 30 s |
   | 3 | and the core's WiFi and HTTP lines, which the core builds in or leaves out as it is compiled: the environments `adafruit_matrixportal_s3_verbose` and `waveshare_wallclock_verbose` |

   Its commands on every board: `log 1|2`, `fetch` (every source of the info
   band due now), `lan on|off` (the web app on the home network); the wall
   clock's own are in [Wall clock](#wall-clock). A release build with
   `-DCLOCK_CONSOLE=1` has the console as well.

   **Power:** WiFi transmit bursts are the clock's highest current draw. At the
   core's default transmit power of 19.5 dBm the S3 reset while joining the
   WiFi on every power supply tried (5 V/2 A and a 65 W PD charger, several
   cables) while working on a PC USB port; at 11 dBm it runs through. The build
   therefore sets `WIFI_TX_POWER` to 11 dBm (board_hal.h). A charger that drops
   VBUS altogether cannot be fixed that way: with the PD charger on a C-to-C
   cable the board still resets at random moments, so use an A-to-C cable with
   it.

   **Diagnosis without a console:** the clock shows the cause of an abnormal
   reset (`BROWN`, `PANIC`, `TWDT`, ...) for 2 s at boot, and remembers how far
   the previous start got: `DIED1` before the panel, `DIED2` after panel and
   sensors, `DIED3` after the WiFi join started (the clock already runs then),
   `DIED4` after it joined. A start that runs for 15 s clears it. The
   breadcrumb lives in flash, so it survives the detour through the bootloader.
   `SENSOR` for 2 s: a sensor found at an earlier start is missing (web app,
   System).

   For the **wall clock**, switch the panels dark first (`POST /api/black`, see
   [Wall clock](#wall-clock)), then upload as on the S3; esptool resets the
   board by itself:
   ```
   pio run -e waveshare_wallclock -t upload --upload-port <port>
   ```
   `waveshare_wallclock_debug` is the same with the console (the self-tests
   at start-up; the frame and memory statistics at log level 2). The GIF pack goes on as on
   the S3, from the GIF gallery or with `-t uploadgifs` (192x128 GIFs at most).

3. NTP on the S3 uses `pool.ntp.org` / `time.nist.gov`. If the clock has no internet
   access, point it at a server on the LAN (e.g. a Fritz!Box) by adding to the
   `[env:adafruit_matrixportal_s3]` section of `platformio.ini`:
   ```
   build_flags = -D NTP_SERVER_1='"192.168.2.1"'
   ```

## Menu and buttons

Two buttons operate the MatrixPortal S3: **UP** (GPIO6) and **DOWN** (GPIO7). They keep their printed meaning in every orientation.

| On the clock face | |
|---|---|
| UP or DOWN short, DOWN held | open the menu |
| UP held | play random GIF |
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
between the list and an editor, deeper to the left.

Saving a new zone or daylight-saving mode shows a 3 s **banner** with the
resulting time and its offset from UTC, orange while summer time is in effect
and ice-blue otherwise. A button pressed while a banner is up only closes it.

**Feedback:** the red board LED (D13) and a 2x2 square in the bottom-right
matrix corner light while a button is held (the square is the compile-time
switch `BUTTON_FEEDBACK_ON_MATRIX`).

**Classic clicks.** The profile *Classic clicks* (config page, **Inputs** page)
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

### Face shortcuts

What every input does on the face is set on the config page's **Inputs** page
(`/inputs`, linked from the settings): UP and DOWN pressed, pressed twice or
three times and held, the knock (with the accelerometer) and each gesture (with
a gesture sensor), each to one of: nothing, open the menu, play random GIF, play
last GIF again (both S3), trigger watchface animation, toggle auto brightness
on / off, toggle daylight saving: auto, summer, winter, toggle hotspot on / off,
fade brightness while held (held buttons only), show gesture hints, show
notifications (see [Alerts, messages and info](#alerts-messages-and-info-on-the-panel)).
On the S3 each input also has a tick, **closes**: a ticked input closes an alert
or a message on the panel and goes on to the next one. The watchface animation
is the face's own: the Tetris digits break up in the
break-up effect, the Muncher and the Aliens play the scene their setting picks, and the classic digits all fly in at the next second as if
every one of them changed - not when they would not have landed 3 s before the
minute's own fly-in during second :59, so a knock from :55 on does nothing
there. A new one is ignored until the running one has ended and 1 s has passed, on every face, so a knock that reports itself
more than once (2 to 7 times, seen) plays it once, and a fast run of knocks
plays the next one 1 s after the last has ended; the Tetris face keeps its
own rules besides (see the break-up effect). The page lists only
the inputs found. The profile chosen there keeps the menu, the hotspot screen
and the start; choosing it fills the face with its own rows to begin with, and
nothing set means the profile's rows. Saving is refused unless an input found
opens the menu, and should stored shortcuts leave none that does (a gesture
sensor unplugged, say), the clock falls back to the profile's rows. Giving a
button's twice or three times something to do makes its single press wait 0.4 s
for a second one. The hint a hand coming near shows follows the shortcuts.

The shortcuts are stored as codes that a later firmware never renumbers
(`FACE_EVENTS` and `FACE_ACTIONS` in `src/input_map.h`, checked at build time),
in a blob of their own (NVS key `keys`).

## Who takes the panel

On the S3 everything that wants the panel follows one set of rules (concept doc,
tab "Panel priorities"; decided by Peter on 2026-10-12). They came from a count:
of 106 GIFs on the evening of 2026-10-11, 22 started within 2 s after the new time
stood (an even spread gives 5) and 9 at :08 of the minute after an info round,
because whatever waited took the panel the moment it was free.

- **Nine levels**, highest first: the hotspot; the person at the clock (menu,
  editor, banners, the gesture test, what an input asks for); alerts; the time's
  change; messages; information (the info round, new cards); the faces' extras;
  GIFs of their own accord; the plain face.
- **The time's change is protected** from what the clock plans itself, on
  every face, the hour's included: that ends 3 s before it starts (a GIF's whole
  loops counted), does not start while it runs, and waits as well while an
  animation the user asked for runs. Alerts come at once, also over a change.
- **What the user asks for is granted** unless there is a clear reason against
  it (Peter, 2026-10-22): a GIF asked for plays during the face's change or a
  scene and into the next change - the user may miss the change, or see the
  rest of it after; the Tetris and classic animations may meet the change, which
  takes them over as a knock takes over a knock's build. The clear reasons: the
  Muncher's and the Aliens' scenes must have ended when their change has to
  start (it would cut them off, or come late), and do not start while it runs;
  an animation already running is not started again (the Tetris digits coming
  apart, the classic digits flying); and after one ends the next waits 1 s,
  since a knock reports itself 2 to 7 times. Until then a knock was refused in
  up to the minute's last 15 s or so on the Tetris face, from about :55 on the
  classic one, and a GIF asked for whenever it would not end 3 s before the
  change. Measured on the S3 (`tools.local/wishes_check.py`, debug builds of
  main and of the change): a Tetris knock 5 s before the minute and a GIF 2 s
  before it now play, a classic knock taken in second :58 too; the Muncher's
  and the Aliens' scenes still play 40 s before and are refused 2 s before.
- **Messages and information at a fixed place in the minute:** 3 s after the
  change and before second :30; what comes later waits for the next minute, so
  they show at times one can count on. A message waits for a GIF's end.
- **Animations** - GIFs and the faces' extras of their own accord - take the
  rest of the minute, and the whole minute when no information waits. They
  need the plain face at rest for 10 s first, and a GIF a minute after info
  cards and before an info round.
- **Nothing optional waits:** a GIF or an extra that has no room when it is due
  is dropped (`GIF: due, the panel not free for it - dropped`, `Extra: due, the
  panel not free for it - dropped`), and the next random time is drawn; an
  extra is measured by its own length, which only the face knows, and one that
  would run into the face's next change is dropped too (`Extra: due, the face
  has no room for it - dropped`); a random time that falls into the slot around a change
  is drawn anew straight away - the slot each face's own change, as long as it
  really is, the Aliens' hour wave and the Muncher's hour show included. A GIF or a face scene asked for by an input
  (button, gesture, knock, the web app) plays at once or not at all, by the
  rules above.
- **Now and then** (the face section's card **Now and then**, its slider **How often**,
  shared by the faces that have such scenes):
  how often the Aliens' saucer, an alien shooting back and a waking block, and
  the Muncher's fruit come - never, or about every 60, 30, 15, 10, 5 (the
  default) or 2 minutes, at a random time from half to one and a half times
  that.
- **Quiet** (the GIF section): **quiet hours** from one hour to another, and a
  **dark room** below a light level in lux (the light sensor's filtered light) -
  no GIFs and no extras of their own accord then. Both are off by default; what
  an input asks for still plays.

The settings for extras and quiet are a blob of their own (NVS key `deco`).

In the web app every face's settings come in the same groups (Peter,
2026-10-20): **Change** - how its time changes: the Tetris break-up effect and
paces, the classic fly-in time and sides, the Muncher's and the Aliens' style
and the Muncher's pace -, **Look** - colours, pellets -, **Watchface
animation** - what the input action of that name plays, only on the Muncher and
the Aliens, which have a choice; on the classic face all six digits fly in, on
the Tetris face all four break up - and **Now and then** - how often, and the
Aliens' waking blocks. The keys and the stored values are those of before.

## GIF playback

On the MatrixPortal S3 a GIF takes the panel in place of the face now and then,
at a random time between a minimum and a maximum (2 and 30 minutes by default),
for a set time (7 s by default); all three and switching it off are on the config
page under **GIFs**. Holding UP on the face plays one at once. A GIF plays in whole
loops, as many as fit into the set time and at least one, so it is never cut off in
the middle: with 8 s a GIF of 3.5 s plays twice, one of 12 s once. The length of a
loop is worked out from the file when the GIF starts, from its frame delays (a
frame given less than 20 ms, or no delay at all, shows for 100 ms, as in browsers),
and the frames follow the file's own timeline, so a frame drawn a few ms late does
not stretch the loop. A GIF whose single loop runs longer than a minute is ended
after a minute. A GIF of its own accord comes only over the plain face, and when
it is due it plays at once or not at all - it never waits (see [Who takes the
panel](#who-takes-the-panel)). Any button press ends it early and does nothing
else; the hold that started it does not end it.

A GIF plays at its own size, centred, unscaled, and only where it fits the panel
as it is held: 64x32 GIFs in landscape, 32x64 upright, 32x32 either way. Turning
the panel ends a GIF. It is drawn at 8 bits per colour straight into the panel
driver, through a gamma table (GIF colours are made for screens, the light of an
LED is linear in its on-time), and the panel's brightness dims it like the face.

A GIF needs more light than the face to be made out, the more so the lighter the
room: a lamp lights the switched-off LEDs too, so dark parts of a picture no
longer look dark. So while a GIF plays the panel runs at the higher of two values
from the config page, **at least** a brightness of its own (54) and **at least
times the clock's** (2.0). They were found on the MatrixPortal S3 with the GIFs
hardest to make out, a log fire's dark logs and glow and a city drawn in many
like colours: 54 in a dark room, twice the clock's with a lamp on (108 against
the clock's 52). Below about 48 the panel's colours also step too coarsely for
such pictures. When the clock has switched the panel dark (brightness 0), a GIF
stays dark too.

All the GIFs are in the **GIF pack**, in the board's `ffat` partition (4544 KB,
see [Flash layout](#flash-layout-s3)); none is built into the firmware, so the
firmware stays small and the room goes to GIFs. Without a pack the clock plays no
GIFs, and the repository brings none: the pack takes those of the folders listed
in `gif_pack.local` in the project folder (one per line, ignored by git) or in
`GIF_PACK_DIRS`, smallest first while there is room. A folder's `exclude.txt` (or
its parent's) names files to leave out, one per line; a line matches the end of a
file's path. Many well-known GIFs belong to their creators: keep them out of the
repository, and do not publish a GIF pack built with them. The firmware holds no
GIFs, so it can be passed on.

Which GIFs fit depends on how the clock stands, `custom_gif_orientation` in
`platformio.ini`: `landscape` (the default) takes GIFs up to 64x32, `portrait`
up to 32x64, `both` those that fit one way or the other, for a clock that gets
turned. A clock that always stands the same way then carries no GIFs it could
never play.

### GIFs on the wall clock's microSD card

The wall clock plays its GIFs from a microSD card in the Waveshare board's slot
while one with GIFs is in, and from its pack otherwise.

- **The card:** FAT32 (cards up to 32 GB come so; exFAT is not read), the GIFs as
  single files in the folder `gifs`. Up to 192x128 and 4 MB each - beyond the
  pack's 600 KB - and up to 8000 of them; a GIF whose one loop takes more than
  25 s is left out, as is one that cannot be read or does not open.
- **Filling it:** with a card reader on the PC, from the GIF gallery ("write onto
  the card") or the GIF library's `card.py`. It
  copies the GIFs chosen for the clock, reads each one back, deletes only what it
  put there itself, and writes the index `gifs/.index` (size, width, height, loop,
  frames, SHA-1, name per GIF). It writes only to a card that carries the mark
  `gifs/.matrixclock`. GIFs put into `gifs` by hand play as well.
- **The clock never writes to the card**, so a power cut cannot damage it. It
  mounts the card at start-up in the background (about 0.4 s for 966 GIFs: the
  index, and one pass over the folder for the files' sizes), at 40 MHz in 1-bit
  mode.
- **The next GIF is read ahead** into PSRAM while no GIF plays (1.8 MB in about
  0.7 s), so a GIF due or asked for starts at once - or not at all, when it is not
  ready yet. The last one shown stays there for "GIF again". Each read is checked
  against the index's SHA-1.
- **Card pulled:** the clock notices at the next reads that fail and plays the
  pack. Put back, it is read again by **Read the card again** (web app, GIFs;
  `POST /api/card`) or at the next start.
- **Console:** `sd` (the card and its GIFs), `sd speed` (every GIF read once and
  checked), `sd remount`, `sd freq <kHz>`, `sd stress` (the largest GIFs read
  ahead in turn).

Measured on the wall clock on 2026-10-20 with a 32 GB SDHC card and 966 GIFs
(143 MB): 1.74 MB/s at 40 MHz and 1.13 MB/s at 20 MHz, no read error, every
SHA-1 right, and the panel output never stopped while the card was read. The
card's DMA reads into a buffer in internal RAM, and the CPU copies from there
into PSRAM: DMA into PSRAM is what stopped the output during TLS on 2026-10-18.

### GIF pack

The commands are for PowerShell on Windows (not Git Bash: the S3 platform's
tools refuse to run there). Replace `<project folder>` with the folder of this
repository and `<GIF folder>` with a folder of your GIFs outside it. If `pio` is
on your PATH, `pio` does instead of the full path. A firmware upload leaves the
pack alone; `pio run -t erase` wipes it.

**Over the home network** (with continuous network access on, see
[Web app and the home network](#web-app-and-the-home-network)), no cable:

    $env:GIF_PACK_HOST = 'matrixclock-xxxxxx.local'; $env:GIF_PACK_PASSWORD = '<the clock's password>'
    & "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe" run -d '<project folder>' -e adafruit_matrixportal_s3 -t sendgifs

It builds the pack as below, asks the clock for its MAC (`GIF_PACK_MAC`, if
set, has to match) and its room, and sends the pack (`POST /api/pack`). The
clock checks the pack's header before it erases anything, so a file that is no
pack leaves the old one; it writes the pack as it arrives, its header last, so
an upload that breaks off leaves no pack rather than a damaged one. Then it
checks every GIF's CRC and plays the new ones at once, without a restart. A full
pack of 3776 KB took 15.6 s; the panel shows how far it is. The web app's GIFs
card sends a pack file the same way (built by `-t gifpack` or the GIF library),
with a bar for the upload.

**Over USB**, for a clock that is not on the network. Placeholders as above,
plus `<board MAC>`:

1. Name the folder for the pack: list it in `gif_pack.local` or name it with
   `GIF_PACK_DIRS`. Set `GIF_PACK_MAC` too if more than one ESP32-S3 is ever
   plugged in: then only the board with that MAC is written. esptool prints a
   board's MAC when it connects (`MAC: 7c:4f:...`), so the first run without it
   shows it.

       $env:GIF_PACK_DIRS = '<GIF folder>'
       $env:GIF_PACK_MAC = '<board MAC>'

2. Build the pack and write it:

       & "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe" run -d '<project folder>' -e adafruit_matrixportal_s3 -t uploadgifs

   Without `--upload-port COMx` it takes the one ESP32-S3 USB-Serial-JTAG port
   there is (USB 303A:1001: the clock's console and its ROM bootloader alike).
   Before it writes, it reads the board's MAC and partition table and stops
   without writing when the MAC is not `GIF_PACK_MAC` or the board has no ffat
   partition where this build expects one - another ESP32-S3 board, or one still
   on TinyUF2's layout, is never written over. `-t gifpack` only builds
   `.pio\build\adafruit_matrixportal_s3\gifpack.bin`.

3. The clock then starts by itself: the upload ends with a watchdog reset (a
   reset through RTS would leave an ESP32-S3 in its ROM bootloader). Its
   console and config page say how many GIFs the pack holds. If the target
   stopped without writing, nothing changed on the board.

`scripts/gif_pack.py` takes the GIFs that fit the panel the way the clock
stands, leaves out second copies, and puts them in smallest first while there
is room. Each GIF is kept as it is or deflated, whichever is smaller (deflating
saves about a fifth on typical pixel-art GIFs). The clock reads the pack's index
at start-up, checks a GIF's CRC each time it is about to play, and inflates a
deflated one into PSRAM with the inflate in the chip's ROM before it plays.

## Info band data

The wall clock shows these items in its info band below the face (see
[The info band on the wall clock](#the-info-band-on-the-wall-clock)); the
MatrixPortal S3, which has no room for a band, shows them over its face (see
[Alerts, messages and info on the panel](#alerts-messages-and-info-on-the-panel)).
The web app's section **Info** lists what the clock knows, and the console what
was fetched.

The clock fetches only a few key-free sources that are likely to last, and
everything else is to come through Home Assistant:

| Source | What it gives | How often |
|---|---|---|
| [Open-Meteo](https://open-meteo.com) | temperature, weather (with how heavy rain or snow is, from the weather code), the day's high and low, chance of rain, UV index from 3; when rain starts or stops within the next 4 hours | every 30 min |
| [Open-Meteo air quality](https://open-meteo.com/en/docs/air-quality-api) | the European Air Quality Index for the place, worldwide (the CAMS forecasts): good, fair, moderate, poor, very poor, extremely poor, with its value; the icon a leaf in the band's colour | every hour |
| [Bright Sky](https://brightsky.dev) | the DWD's weather warnings for the place, the most severe first (up to four): `minor` as information, from `moderate` on as an alert; each with its level and the icon of its event (wind, thunder, rain, snow, frost or black ice, heat, fog; the warning triangle for any other) | every 10 min |

Weather data by Open-Meteo.com (CC BY 4.0); air quality: CAMS ENSEMBLE data
provider, through Open-Meteo.com; the warnings are the Deutscher Wetterdienst's,
through Bright Sky, under the DWD's terms of use. None needs a key.

The place is typed on the config page as latitude and longitude in degrees, from
a map; nothing is fetched before. Changing it drops what was fetched for the
old place and fetches at once. Each source can be switched off there, and a
status line per source says when it was last fetched, or why it failed.

- **WiFi:** a source that is due wakes the radio, and the radio goes off again
  once neither the info band nor the NTP sync needs it. Sources due within 3
  minutes come along while the radio is on anyway, so they share a wake-up.
- **No stale values:** an item runs out after three intervals without a new
  fetch (weather 90 min, warnings 30 min), a warning at its end at the latest.
  A failed fetch is tried again after 1, 2, 4 ... minutes, at most after the
  source's interval; its items stay until they run out. An item goes as soon
  as its time has come: the clock keeps the earliest end of all items and
  compares it with the time on every pass, and once a minute it looks at every
  item as well.
- **Beside the loop:** a fetch (name lookup, TLS handshake, the answer) blocks
  for half a second to a few seconds, so it runs in a task of its own on the
  other core, and the clock keeps drawing. The answers are read as a stream
  through a JSON filter, so only the fields used take RAM.
- **HTTPS:** the servers are checked against the certificate bundle of ESP-IDF
  that the core's libraries carry. TLS and that bundle cost about 200 KB of
  flash (the S3 image went from 83 % to 93.5 % of its 2 MB), and a TLS
  connection takes its buffers from internal RAM, next to the panel driver's;
  the console reports the internal RAM free before each fetch and the lowest
  since the start.

The code is in `src/band_data.h` (the items, the two sources, the fetch task)
and at the end of the sketch (when the sources are due, the items, the config
page section).

## Home Assistant link

Everything the clock does not fetch itself comes from Home Assistant, over MQTT
through the broker Home Assistant runs (the Mosquitto add-on). The client is the
one ESP-IDF brings along (esp-mqtt, in the core's libraries): it runs in a task
of its own, keeps the connection alive and connects again by itself after a
break, so the clock keeps drawing.

Setting it up:

1. In Home Assistant, install the **Mosquitto broker** add-on, add a Home
   Assistant user for the clock, and set up the MQTT integration if Home
   Assistant does not offer it by itself.
2. On the clock's page, section **Home Assistant**:
   the broker's address (`homeassistant.local` or its IP), port 1883, that user
   and its password, the switch on, **Save Home Assistant link**. The link needs
   a stored home WiFi and **continuous network access** (Network section);
   the card says what is missing.
3. The clock shows up in Home Assistant as the device *Matrix Clock xxxxxx*, by
   MQTT discovery, with an entity **Message** (notify), with a BH1750 a sensor
   **Light** (lux), and on the wall clock the sensors **Temperature** (°C) and
   **Humidity** (%) from its board's SHTC3.
4. **Values to Home Assistant** in the same section: a sensor's value goes out
   when it changed - the light by 10 % (at least 1 lx), the temperature by
   0.2 °C, the humidity by 1 % - but at most every *At most every* seconds
   (60 unless set, 5 to 255), and unchanged still every *At least every*
   minutes (15 unless set, 1 to 255), so Home Assistant sees the sensor
   alive. Inside the clock the sensors are read as often as they need (the
   light five times a second, the room every 5 s). Saving them does not start
   the link anew.

`xxxxxx` is the end of the clock's host name, the last three bytes of its MAC;
the card shows its topics.

| Topic | Direction | Payload |
|---|---|---|
| `mpclock/<id>/item/<name>` | Home Assistant to the clock, **retained** | JSON `text`, `icon`, `level` (`info` or `alert`), `until` (Unix time, required), and optionally `severity`, `intensity`, `chance`, `short`; an empty payload takes the item away |
| `mpclock/<id>/message` | Home Assistant to the clock, best not retained | JSON `text`, `title`, `level`, `icon`, `seconds` (3600 unless given, 5 to 3600), and optionally `severity`, `intensity`, `chance` |
| `mpclock/<id>/status` | the clock to Home Assistant, retained | `online`, or `offline` (also as the last will) |
| `mpclock/<id>/light` | the clock to Home Assistant | lux (the filtered light the brightness follows), as set under step 4 |
| `mpclock/<id>/temperature`, `mpclock/<id>/humidity` | the clock to Home Assistant (wall clock) | °C with one decimal, % whole, as set under step 4 |
| `homeassistant/notify/<id>/message/config`, `homeassistant/sensor/<id>/light/config`, `.../temperature/config`, `.../humidity/config` | the clock to Home Assistant, retained | MQTT discovery; a sensor not found at start-up takes its config back |

`<name>` is 1 to 23 of `a-z A-Z 0-9 _ -`. The icons: `clear-day`, `clear-night`,
`partly-day`, `partly-night`, `cloudy`, `fog`, `drizzle`, `rain`, `snow`,
`thunder`, `warning`, `wind`, `frost`, `heat`, `calendar`, `air`, `indoor`. The optional fields change how
the icon looks:

- `severity`: a warning's level, as the DWD names them - `minor`, `moderate`,
  `severe` or `extreme`. The warning triangle and an alert's frame take its
  colour: yellow, orange, red, violet.
- `intensity`: how heavy drizzle, rain or snow is - `light`, `moderate` or
  `heavy`. The icon shows one, two or three of its drops, dots or flakes; an
  item that gives no intensity shows two.
- `chance`: how likely rain or snow is, 0 to 100 (%). The drops or flakes are
  grey at 0 and take their colour as it rises; without it, they have their
  colour.

A value that is none of these counts as not given. `short` is the item's info
card on the 64x32 clock: two lines with `|` between them, up to six
characters each (five upright), such as `"31°|feels"`; a first line of three
characters or fewer is drawn at double size. The card shows it, when the item
is new, in the round and on **show notifications**; the band and the web app
show `text`. Without `short`, the card runs `text` beside the icon. In `text`,
` | ` (a bar with a space either side) sets its parts apart: the band breaks its
two lines there and leaves the bar out at the break (see
[The info band on the wall clock](#the-info-band-on-the-wall-clock)). An item for
the band, from an automation:

```yaml
action: mqtt.publish
data:
  topic: mpclock/06c16c/item/waste
  retain: true
  payload: >-
    {"text": "Paper bin | tomorrow morning", "icon": "warning",
     "until": {{ (now() + timedelta(hours=12)).timestamp() | int }}}
```

### Example: public holidays

The clock has no holiday rules of its own: they differ from country to
country and from region to region. Home Assistant's
[Holiday](https://www.home-assistant.io/integrations/holiday/) integration knows
them, offline, and an automation hands today's or tomorrow's to the clock as an
item - in the band on the wall clock, as a card on the 64x32 clock.

1. In Home Assistant, **Settings → Devices & services → Add integration →
   Holiday**: the country, and the state or region where the clock hangs (for
   Germany the federal state; some holidays exist only there). It makes a
   calendar entity; note its entity ID (the integration's entity list shows it).
2. On the clock: the Home Assistant link as above, nothing else. Its card shows
   the clock's topics; `06c16c` below stands for the end of your clock's name.
3. In Home Assistant, a new automation (**Settings → Automations & scenes →
   Create automation → Edit in YAML**), with your calendar's entity ID and your
   clock's topic in its first lines:

```yaml
alias: "Matrix Clock: public holiday"
description: "Today's or tomorrow's public holiday as an item on the clock"
triggers:
  - trigger: time
    at: "00:01:00"
  - trigger: homeassistant
    event: start
variables:
  holidays: calendar.germany           # your Holiday calendar's entity ID
  topic: mpclock/06c16c/item/holiday   # your clock's id in place of 06c16c
actions:
  - action: calendar.get_events
    target:
      entity_id: "{{ holidays }}"
    data:
      start_date_time: "{{ today_at('00:00') }}"
      duration:
        hours: 48
    response_variable: found
  - variables:
      events: "{{ found[holidays].events }}"
      name_today: >-
        {{ events | selectattr('start', 'match', now().date() | string)
                  | map(attribute='summary') | first | default('') }}
      name_tomorrow: >-
        {{ events | selectattr('start', 'match', (now() + timedelta(days=1)).date() | string)
                  | map(attribute='summary') | first | default('') }}
  - if:
      - condition: template
        value_template: "{{ name_today != '' or name_tomorrow != '' }}"
    then:
      - action: mqtt.publish
        data:
          topic: "{{ topic }}"
          retain: true
          payload: >-
            {{ {"text": ("Public holiday today | " ~ name_today) if name_today
                        else ("Public holiday tomorrow | " ~ name_tomorrow),
                "icon": "calendar",
                "until": (today_at('00:00') + timedelta(days=1)).timestamp() | int} | to_json }}
    else:
      - action: mqtt.publish
        data:
          topic: "{{ topic }}"
          retain: true
          payload: ""
```

It runs a minute after midnight and when Home Assistant starts. A holiday today
comes as "Public holiday today | …", else one tomorrow as "Public holiday
tomorrow | …", until midnight; on the band the text breaks at the bar. A
day before none, the empty payload takes the item away. The holiday's name is in
the language Home Assistant gives it; the words before it can be your own
(umlauts are fine, see the font below).

### Example: civil protection warnings (NINA)

Germany's warnings of the Federal Office of Civil Protection (Bundesamt für
Bevölkerungsschutz und Katastrophenhilfe) come through Home Assistant's
[NINA](https://www.home-assistant.io/integrations/nina/) integration.

1. In Home Assistant, **Settings → Devices & services → Add integration →
   NINA**: the city or district, and how many warnings to fetch at most. It
   checks every 5 minutes and makes one binary sensor per warning slot, on
   while a warning fills it.
2. On the clock: the Home Assistant link, nothing else.
3. The automation, with your slots' entity IDs in its trigger and in `slots`,
   and your clock's id:

```yaml
alias: "Matrix Clock: NINA warnings"
description: "The NINA warnings for the place as items on the clock"
mode: queued
triggers:
  - trigger: state
    entity_id:                               # your NINA warning slots
      - binary_sensor.stuttgart_warning_1
      - binary_sensor.stuttgart_warning_2
      - binary_sensor.stuttgart_warning_3
  - trigger: homeassistant
    event: start
variables:
  clock: mpclock/06c16c                      # your clock's id in place of 06c16c
  slots:                                     # the same slots as above
    - binary_sensor.stuttgart_warning_1
    - binary_sensor.stuttgart_warning_2
    - binary_sensor.stuttgart_warning_3
actions:
  - repeat:
      for_each: "{{ slots }}"
      sequence:
        - action: nina.get_details             # null for a slot without a warning
          target:
            entity_id: "{{ repeat.item }}"
          response_variable: nina
        - variables:
            topic: "{{ clock }}/item/nina{{ repeat.index }}"
            w: "{{ (nina or {}).get(repeat.item) }}"
        - if:
            - condition: template
              value_template: >-
                {{ w is mapping and not (w.id | default('')).startswith('dwd.') }}
          then:
            - action: mqtt.publish
              data:
                topic: "{{ topic }}"
                retain: true
                payload: >-
                  {{ {"text": w.headline,
                      "icon": "warning",
                      "level": "alert" if (w.severity | lower) in ['moderate', 'severe', 'extreme'] else "info",
                      "severity": w.severity | lower,
                      "until": as_timestamp(w.expires, 0) | int if w.expires
                               else (now() + timedelta(hours=6)).timestamp() | int} | to_json }}
          else:
            - action: mqtt.publish
              data:
                topic: "{{ topic }}"
                retain: true
                payload: ""
```

Each slot becomes an item `nina1`, `nina2` …: the warning's headline (in German,
as NINA writes it), the warning triangle in the colour of its level - from
`moderate` on as an alert, which comes up as a banner - until the warning
expires, 6 hours when it gives no end. A slot that empties takes its item away.
NINA passes on the DWD's weather warnings too, whose IDs start with `dwd.` (as in
the integration's example); the clock fetches those itself, so the automation
leaves them out. Drop the `startswith` part to have them twice.

### Example: breaking news (tagesschau)

The tagesschau app's homepage marks a breaking news item (`breakingNews`). Home
Assistant reads it with a [RESTful sensor](https://www.home-assistant.io/integrations/rest/),
in `configuration.yaml`:

```yaml
rest:
  - resource: https://www.tagesschau.de/api2u/homepage/
    scan_interval: 300     # every 5 minutes; the API allows at most 60 calls an hour
    timeout: 30            # the homepage is about 600 KB
    sensor:
      - name: "tagesschau breaking news"
        unique_id: tagesschau_breaking_news
        value_template: >-
          {{ value_json.news | selectattr('breakingNews') | map(attribute='title')
             | first | default('none') | truncate(250) }}
```

The sensor holds the headline of the breaking news item, `none` while there is
none. An automation hands it to the clock as an alert:

```yaml
alias: "Matrix Clock: breaking news"
triggers:
  - trigger: state
    entity_id: sensor.tagesschau_breaking_news
variables:
  topic: mpclock/06c16c/item/news            # your clock's id in place of 06c16c
actions:
  - if:
      - condition: template
        value_template: "{{ trigger.to_state.state not in ['none', 'unknown', 'unavailable', ''] }}"
    then:
      - action: mqtt.publish
        data:
          topic: "{{ topic }}"
          retain: true
          payload: >-
            {{ {"text": "Breaking news | " ~ trigger.to_state.state,
                "icon": "warning", "level": "alert",
                "until": (now() + timedelta(hours=2)).timestamp() | int} | to_json }}
    else:
      - action: mqtt.publish
        data:
          topic: "{{ topic }}"
          retain: true
          payload: ""
```

A new breaking news item comes up as an alert - its banner over the face, then
first in the band - for two hours, or until the homepage no longer marks it. The
tagesschau's content is for private, non-commercial use only; its API's terms
allow no more than 60 calls an hour.

A message goes to the **Message** entity (`notify.send_message`), or as JSON to
the message topic. It is an item for its seconds, an hour unless it says; the
clock keeps up to four, and a fifth takes the oldest one's place (an urgent
one's only when all four are urgent). On the 64x32 clock its text runs through,
and with `"level": "alert"` its icon comes first (see
[Alerts, messages and info on the panel](#alerts-messages-and-info-on-the-panel)). The
Message entity sends the text alone, so its messages are information.

- **Retained items:** the broker keeps them, so a clock that starts or comes
  back gets the current set at once, and nothing is polled. On every connect
  the clock drops what Home Assistant sent before and takes them as they come,
  so an item taken away meanwhile does not linger. `until` is required, so no
  item outlives Home Assistant.
- **Online and offline:** the clock says goodbye (`offline`) when the link or the
  continuous access is switched off or the hotspot opens; a clock that just
  vanishes is reported offline by the broker about 90 s later (keep-alive 60 s).
  When Home Assistant has restarted, the clock announces itself again.
- **A message is shown once:** one the broker keeps (sent retained) is shown
  when it comes, but not again when the broker hands it out at a later connect;
  an empty payload on the message topic, which takes a kept message off the
  broker, is ignored.
- **Status:** the card says whether the link is connecting, since when it is
  connected and how many items it brought, or what went wrong (user name or
  password refused, nothing answering at the address, address not found).
- **Flash:** the client costs 41.7 KB (it brings the TLS and WebSocket
  transports the core's build has switched on, which the link does not use),
  the whole link about 57 KB. With the alerts and messages on the panel
  (2026-10-10) the S3's image has 1,441,648 bytes, 393,360 less than its app
  slot.

The code is in `src/ha_link.h` (the client, its task and the topics) and at the
end of the sketch (when it runs, what arrives, the card's part).

## Alerts, messages and info on the panel

The 64x32 clock has no room for the info band, so its items come over the face
as banners: the alerts - the DWD warnings from `moderate` on, and the items Home
Assistant sends with `"level": "alert"` - as their icon, the messages from Home
Assistant as running text, and all the other information as **info cards**.

- **An alert:** its icon alone, 24x24 in its colours, in the middle of the
  panel, for 10 s; an item without an icon shows the warning sign. Round the
  panel's edge runs a frame in the colour of the alert's level - the DWD's
  yellow, orange, red or violet, and red for an alert that gives no level. The
  web app's Info section switches the frames off (**Frame round an alert**);
  the icon then stands alone.
- **An info card:** the item's icon on the left and its short form in two lines
  beside it (upright: below it), for 4 s - the weather's temperature at double
  size with the day's high and low, rain soon "from" or "until" and the time, a
  DWD warning at level 1 "until" and the time in a yellow frame, a Home
  Assistant item its `short` field. An item without a short form has its text
  running beside the icon once, or across the panel without an icon.
- **When a card comes:** once when its item is new - an item that was not there
  before, or the weather when its icon changes (cloudy to rain); a new
  temperature does not count - and in the **info round**: every card once more,
  one after the other, in the band's order (DWD warnings, rain soon, the
  weather, then Home Assistant's items as they came). The round goes by the
  clock's time, every 15 minutes at :00, :15, :30 and :45 (the web app: never,
  or every 5, 10, 15, 20, 30 or 60 minutes, always on the minutes the interval
  divides), 3 s after the face has shown the new time - the classic face's
  fly-in, the Tetris blocks, the muncher's walk and hour show - and stood at
  rest. One that finds no free face within a minute is left out. A GIF of its
  own accord gives way: none starts when it would still play at the round's
  minute or end less than a minute before it, and none within a minute after
  info cards. New cards come at the minute's place for information - 3 s after
  the time's change and before :30, else in the next minute - and wait for a
  GIF's end ([Who takes the panel](#who-takes-the-panel)). Cards never run into
  the next change. A ticked input (see below) ends the cards on show.
- **A message:** its text (`title | text`) runs through the middle of the panel
  in the 5x7 font, white, from the right edge out at the left, twice, at about
  five characters a second; an urgent one (`"level": "alert"`) shows its icon
  for 3 s first and runs in red. The font has ä ö ü ß, é è à ç ñ and the other
  letters of code page 437, and °; other characters show as `?`. A message
  keeps 96 bytes, so two runs of the longest take about 40 s.
- **When:** over the face. While the menu, an editor or another banner is up,
  they wait until the face is back. An alert comes at once - also over a
  running change of the time - and ends a GIF on show; a message comes at the
  minute's place for it, 3 s after the change and before :30 (else in the next
  minute), and lets a GIF play to its end. Several new ones come one after the other, alerts first,
  each kind in the order it came; nothing else cuts them short - not a new
  alert or message, a GIF that is due, turning the clock, the web app - only
  their own end, a ticked input (the Inputs page: **closes**, every button
  press and gesture to begin with, not the knock) or a restart. A ticked input
  goes on to the next one and does nothing else.
- **Show notifications** (a face shortcut, UP pressed twice in the Default
  profile, or **Show notifications now** in the web app's Info section) shows
  everything there is: the alerts first with their icon and then their text in
  red, then the messages, each in the order it came, every text running through
  once - whoever asked is looking from the start - and then the info cards,
  each a card alone: the text is for what is urgent. With nothing
  there, the panel says "nothing to show". In the Default profile UP pressed
  twice makes UP's single press wait 0.4 s; a clock with shortcuts of its own
  keeps them.
- **The mark:** a 2x2 square in the top right corner of the face while there
  are any: red while an alert holds, white for messages alone. In the hotspot's
  preview that corner has the hotspot's blue mark.
- **Once:** an alert comes up when it is new, or when an item that was
  information turns into an alert. The same item with a new text does not come
  up again. One its source takes away (an empty payload) or that runs out is
  over: sent again, it comes up again. One dropped for a technical reason stays
  known for 15 minutes, so a reconnect to Home Assistant or a fetch that missed
  it does not bring it back. A DWD warning with another headline counts as
  another warning; every message is one of its own. Several new alerts with the
  same icon share one banner, which shows the most severe of them.

The icons are drawn as pixels in `src/item_icons.h`, part of the firmware like
the rest of the band, so they need nothing loaded onto the clock. Each has up
to three parts, and each part a role whose colour the clock picks when it draws
it: the sun yellow with orange rays, the moon pale yellow, a cloud white (grey
over rain, snow and thunder), rain blue, snow white, the bolt yellow, wind light
blue, a thermometer white with blue (frost) or red (heat), the warning triangle
in its level's colour with a white mark, the calendar white with a red head.
Drizzle, rain and snow have a light and a heavy picture beside the usual one
(see `intensity` above).

## The info band on the wall clock

The wall clock (192x128) shows its 64x32 face three times the size in the top
192x96 and uses the 192x32 below as the info band: one item after the other,
its icon on the left and its text in two lines of the 5x7 font beside it, up to
26 characters each.

- **Order:** the alerts first, then the messages, each in the order they came,
  then the information as on the 64x32 clock's cards (DWD warnings, rain soon,
  the weather, the air quality, Home Assistant's items), then the clock's own,
  worked out on the clock once a minute without the internet:
  - **the clock's status**, only while its status pixel is red: a new home WiFi
    that does not join ("Home WiFi does not join" / its name), or a time sync
    that keeps failing ("Time sync is failing" / "last good 18 Oct 03:00"),
    else a sensor found before that is missing now ("Sensor missing" /
    "QMI8658"), beside the warning sign in yellow;
  - **the clock change**, from 24 hours before until 12 hours after it, when
    daylight saving is set to auto: "Clocks go back 1 h" / "tonight at 3:00"
    (the time on the clock before it), then "Clocks went back 1 h" / "last
    night";
  - **the date**: "Sunday 18 October 2026" / "week 42" (the ISO week), with the
    calendar icon;
  - **sunrise and sunset** at the place set for the weather, today's and after
    sunset tomorrow's, "Midnight sun" or "Polar night" when there is none -
    after NOAA's solar calculator, within a minute of published tables;
  - **the moon**: its phase and how much of it is lit, and the next full or new
    moon, whichever comes first ("First quarter | 53% lit" / "Full moon Mon 26 Oct"),
    from its longitude, within about half an hour (`src/sky.h`);
  - **the room**: "Indoor 22.4°C" / "humidity 48%" from the board's SHTC3,
    with a thermometer in a house, once it gave a reading.
- **Turns:** each stands 6 s, one that runs through as long as that takes.
- **Lines** (one rule for every text in the band, Peter's of 2026-10-18): a
  text's parts are set apart by ` | ` - the clock's own, those it puts together
  from a source's fields (the weather "10°C overcast | 10/8°C | rain 100%", the
  warnings, the air quality "Air quality fair | index 32") and Home Assistant's
  (a message's title and text, and what an item's `text` sets apart so). A text
  that fits one line stands in one, bars and all. Else it breaks at the first
  bar after which the rest fits the second line, and that bar is left out
  ("10°C overcast" / "10/8°C | rain 100%"); else between words, a bar at the
  break left out too. One that fits neither runs through once beside the icon,
  at five characters a second. On the panel a bar shows as a middle dot - the
  5x7 font draws `|` broken in the middle, like a colon - here and in the
  64x32 clock's running texts; the texts themselves, the web app and Home
  Assistant keep the bar, which every keyboard has.
- **Alerts:** in their level's colour, framed when **Frame round an alert** is
  on. A new alert takes the band at once and still comes up once as its banner
  over the face, as on the 64x32 clock.
- **Messages:** a new one takes the band at once, for twice its time (its text
  runs through twice); its text no longer runs over the face. An urgent one's
  icon comes over the face first.
- **No cards, no info round:** the band shows the information all the time; the
  web app has no info round for this clock. **Show notifications** brings the
  alerts and messages over the face as on the 64x32 clock and starts the band
  again at its first item.
- **Covered and dark:** a GIF covers the band as it covers the face; it comes
  back after the GIF. The band is dark while the hotspot is open and until the
  clock has its time.

The band is drawn in `bandUpdate()` at the end of the sketch into a picture of
its own and written onto the wall by `PanelCanvas::showBand()` in
`src/board_hal.h`, which writes only what changed.

## Web app and the home network

The S3 serves its settings as a web app: one page (`webui/index.html`, gzipped
into the firmware at build time by `scripts/embed_webui.py`) that talks to the
clock over a JSON interface.

| | Through the hotspot | On the home network |
|---|---|---|
| Switched on by | the menu's **Hotspot**, an input set to *toggle hotspot on / off*, or UP held at boot | the page's card **Continuous network access**, switch *Stay connected to the home network* |
| Address | `http://4.3.2.1` (opens as a captive portal) | `http://matrixclock-xxxxxx.local` (the last three bytes of the board's MAC) or its IP, both on the page and the console |
| Password | the hotspot's WiFi password | the clock's own: preset to the hotspot's (`clock1234`), changed on the page (8–63 characters) |
| Networks in range | listed, scanned when the hotspot opens | not listed: a scan would take the radio off the home network |

**On the home network** the clock stays connected to the stored WiFi while the
switch is on; otherwise its radio is on only for the time sync and the info
band's fetches. The station's power saving is off meanwhile, so a request is
answered in about 40 ms instead of 1.2 s. The page itself loads without a
password and then asks for it; every request to `/api/` carries it (HTTP Basic,
any user name). Five wrong passwords in a row lock the check for 30 s (HTTP
429). A data source that needs a standing connection is marked on the page (the
Home Assistant link does), and switching one on without a stored WiFi or
without continuous access says that it needs both.

**How the clock answers** (since 2026-10-21): beside its loop, a piece per
pass, so an open page no longer makes the running text or the flying digits
jump. Up to four connections are served at a time. A request is read as far
as it has arrived; once it is in, its answer is built whole in RAM and handed
to the network without waiting, at most 2,920 bytes a pass; the connection is
closed a pass later. A connection that has not sent its request within 2 s,
or whose form or answer moves no byte for 5 s, is dropped (browsers open
connections in case they need them). Measured on the MatrixPortal S3 (debug
build, 2026-10-21) with the page's requests - the panel every 200 ms, the
status every 2 s - as the longest time between two passes of the loop in each
2 s: 5.5 ms in the median and 9.3 ms at most, against 4.6 and 5.0 ms without
the page. Before, when every request held the loop until it was answered, it
was 32.5 and 42 ms, with 113 gaps over 20 ms in a minute. On the wall clock
(its GIFs and extras held back) 5.3 and 9.4 ms, against 3.3 and 9.1 ms without
the page and 39.6 and 60.5 ms before, with 161 gaps over 20 ms. Only a GIF
pack's upload still holds the loop while it is written, with its progress on
the panel.

**The interface** — the page uses nothing else, so tools can too. Writes are
form-encoded POSTs, answers are JSON:

| Request | What it does |
|---|---|
| `GET /api/state` | settings, the lists the page offers, hardware found (with `hw.sensors`: every sensor the board supports, found on which try, missing), WiFi, system |
| `GET /api/status` | brightness, light, time and its source, rotation, screen, the info band's sources and items, the Home Assistant link (the page asks every 2 s) |
| `GET /api/panel` | the panel picture, 64x32 RGB565, 4096 bytes (every 200 ms for the page's preview) |
| `POST /api/live` | tries settings live, in RAM only; when a preview was asked for (`pv=1`, or a new watchface animation), `{"preview": "played"}` or `"refused"` - it plays now or not at all |
| `POST /api/save` | stores everything the page shows |
| `POST /api/discard` | puts the stored settings back, and the face shows them as a preview (the same answer) |
| `POST /api/black` | the wall clock: its panels dark until the next start, before an upload |
| `POST /api/time` | sets the time, from the phone or typed |
| `POST /api/wifi`, `/api/forget` | stores or deletes the home WiFi |
| `POST /api/sensor-forget` | `id=<sensor>` (`accel`, `light`, `rtc`, `gesture`, `boardRtc`, `motion`, as `hw.sensors` in `/api/state` lists them): a sensor this clock no longer has is no longer reported missing |
| `POST /api/gif` | plays a GIF now (on the face) |
| `POST /api/card` | the wall clock: reads its microSD card again, as soon as no GIF plays |
| `POST /api/pack` | a GIF pack as the body (`application/octet-stream`), written into the ffat partition (see [GIF pack](#gif-pack)) |
| `POST /api/lan` | continuous network access on or off, a new password |
| `POST /api/ha` | the Home Assistant link: on or off, broker, port, user, password (empty keeps the stored one) |
| `POST /api/restart` | restarts the clock |

### Settings: one table per blob

Each blob of settings the web app edits has a table beside its struct (since
2026-10-22; the watchfaces' since the face refactor): per setting the web app's
key, where it sits in the blob, its kind, its range, whether the page tries it
live, and what a stored value outside its range becomes when it is loaded. One
piece of code (`src/settings_table.h`) takes the settings from `/api/save` and
`/api/live`, checks them when they are loaded and reports them in `/api/state`.
The keys, the stored blobs and the values in `settings` are the ones they were.

`/api/state` also tells the page how to send them back, under `lists.form`:
per key `[kind, lo, hi, live]`, the range as the clock keeps it.

| kind | in `settings` | sent back as |
|---|---|---|
| `range`, `choice`, `flag` | the number | the number; outside its range it is moved into it |
| `checkbox` | `true` / `false` | `on` when ticked; on a save an absent one is off, live only one that is sent counts (`on` or `off`) |
| `tenths` | tenths (`gifbx`: 20 = twice) | the number with a decimal, `2.0` or `2,0` |
| `tens` | ms | ms, kept in tens (the classic fly-in) |
| `colour` | `#rrggbb` | `#rrggbb` |
| `special` | as the setting needs | its own form: `dirs` as `dir0` .. `dir5`, `keys` as `k0` .., `closes` as `cset` with `c0` .., and `tz`, `lat`, `lon`, `iround` as they are |

The page sends exactly the keys listed, so a setting the clock does not offer as
it is - the days between time syncs without a real-time clock, the gesture
sensor's mount without the sensor, the info round on the wall clock - is not
sent. `lists.iround` holds the info round's choices in minutes. A new setting
needs its field, its default, a row in its table and its control on the page.

## AP configuration

Open the menu, select **Hotspot** and hold DOWN -> the board opens the access
point; holding UP closes it again and returns to the normal clock:

| | |
|---|---|
| SSID | `MatrixClock` |
| Password | `clock1234` |
| Config page | `http://4.3.2.1` |

A built-in **captive portal** (DNS hijack + portal page) makes the config page pop
up automatically on most phones right after connecting to the AP. If it does not,
open the config page address manually.

The clock deliberately uses a *public* address (4.3.2.1, same as WLED) for the AP.
Current Android versions skip their captive-portal check when its host name
resolves to a private address such as 192.168.4.1 and then report "connected
without internet" instead of "sign in to network". The address only exists
inside the clock's own isolated access point.

The matrix shows SSID, password and IP **immediately** when the AP opens (before
the radio has finished coming up, so there is no frozen display), in landscape
orientation, until a client connects; after that it switches to the live clock
preview so that **brightness, colors and the fly-in time preview live** while you
change them in the web UI, at the full frame rate. In the preview a small blue
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

The MatrixPortal S3's onboard **LIS3DH** accelerometer (I2C `0x19`) detects
gravity and rotates the display in 90° increments so the clock is always upright:

- **Portrait** (32 wide × 64 tall): the hours/minutes/seconds digits are stacked
  vertically (the original layout).
- **Landscape** (64 wide × 32 tall): hours and minutes are shown large
  side-by-side with the seconds small underneath.

The per-digit fly-in directions mean the same in both: a digit set to come
from the top comes from the top edge as the clock is held. Each flight is long
enough that the incoming digit starts wholly off the panel and the outgoing one
leaves it wholly. All flights take the same time, the **fly-in time** (100–850
ms, default 400). So whatever flies at a tick lands together, the shorter
flights moving slower, every second looks the same whether one digit flies or
six, and every digit has landed before the next second. Digits that come from the same side all fly the
longest distance any of them needs, so they move as one block and keep their
spacing: a pair set to one side flies in as it stands, and with every digit set
to one side the whole face slides. On the S3 the speed is kept in real time by the
loop rate the face really had in its last second, so a web page asking for the
panel picture does not slow the digits down. (Until 2026-10-08 landscape
swapped the directions — from right ↔ from top, from left ↔ from bottom — but a
save set them back until the next screen change, a sideways flight was only
32 px long, half the landscape width, and later that day, briefly, each flight
took as long as its own distance, so hours, minutes and seconds landed one
after the other.)

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

The sensor is read **five times a second**. The median of the last 13 readings
(2.6 s) is filtered in log units with 1 s, so a hand in front of the sensor for
up to about a second - a gesture - changes nothing, and a lamp switched on or off
shows half its change after about 2 s and nearly all of it after 2 to 4 s (until
2026-10-21 a 10 s moving average: 5 to 6 s and 8 to 10 s). The brightness then
**fades** to the target at an even pace in log units - at most a factor of 2.7 a
second, slowing over its last part - and only once the target is 4 % away, so a
light that only wavers moves nothing. The filtered lux is mapped to a brightness
through a configurable linear curve, set on the AP config page:

| Field | Meaning |
|---|---|
| Dark lux | at/below this lux → **Min brightness** |
| Bright lux | at/above this lux → **Max brightness** |
| Min / Max brightness | brightness endpoints (0–255); on the S3, 1 to 13 all give its darkest, about 5 % (see [Panel driver](#panel-driver)), and 0 switches the panel dark |

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

The **watchface** setting on the config page switches between the clock faces:
Classic, Tetris, Muncher and Aliens, on both clocks. It previews live, so the
choice can be judged on the panel before saving.

**Classic** (default) shows `HH MM SS` with the six flying digits. This is the
face the clock has always had. Since 2026-10-20 its flights are placed by the
time since their second began, and it draws only when a digit has moved.

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
the main settings are untouched by them. The *fly-in time* applies to the
classic watchface only.

### The digits break up

When the time changes a digit, the old one breaks up before the new one is
built, and an input set to *trigger watchface animation* (by default the knock,
see [Face shortcuts](#face-shortcuts)) throws all four away and builds them up again
from the current time — with a fresh tiling and fresh colours. The **break-up
effect** is one of three, selectable and previewing live: **Collapse** (the
stack gives way and drops), **Scatter** (the pieces fly off and tumble) and
**Clear rows** (rows flash and vanish from the bottom, everything above dropping
into the gap), plus *Random each time*. The effect is drawn once per event, so
all the digits involved always come apart the same way.

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

*Knock sensitivity* (in the web app under Inputs, since the knock can do other
things than this) runs from 0 (off) to 10. The scale was measured on the
device, not guessed: over about 70 s of standing still the largest deviation in
any one second was 780 raw counts, which is the sensor's own noise, while every
deliberate interaction produced 1900 or more and a firm knock 12700. That is
0.094 g and 0.375 g — one g is 16000 counts, because the library leaves the part
in high-resolution mode — which the threshold register takes in steps of 16 mg,
so level 10 is 6 steps and level 1 is 24.

Nothing is triggered while another screen owns the panel (the menu, an editor,
a banner), in the config AP, for
the first three seconds after start-up, for 1.2 s after a rotation, for 1 s
after an effect has run (the rest every face keeps after its animation), while the digits are already coming apart, **while a button is
held, and for 3 s after a button was last pressed or let go** — the switches sit
next to the sensor, so a button press is a knock as far as it is concerned, and
so is everything around it: the release, steadying the clock, letting go of a
held button. A knock the sensor latched while the config AP was up is
discarded when the face comes back. Digits still being built can be knocked
apart again, the minute change's too; only the time just before a change is
kept free: a knock whose break-up and rebuild - worked out from the drop pace and the
digits' tilings: up to 10 s at the default 70 ms, 33 s at the slowest - would not end 3 s
before the next minute starts does nothing, so the change always starts from a
clean picture. The reasons the sensor fires without a real knock sit together
in `knockIsReal()`, the reasons the face cannot be knocked apart right now in
`TetrisFace::request()` (`src/face_tetris.h`).

At a minute rollover only the digits that actually changed break up, and they
rebuild while the others stand still; a digit still being built is simply
swapped. Whatever has come apart leaves the space empty for 300 ms before it
builds again. (Until 2026-10-08 an option chose between this and a plain swap,
where the old digit vanished at once; the break-up now always counts.) The colon is never touched by any of this — it blinks on the second
throughout, being the one thing on this face that shows time actually passing.

**Trying it out on the config page.** Changing the drop or turn pace or the
effect takes the digits apart and builds them again straight away, so the
setting can be judged on the panel; choosing the face drops its digits anew.
That matters here because the clock deliberately ignores the sensor while the AP
is up, so a knock is not available to test with. It happens on the settled value
only — a slider let go of, or a point on it tapped — and now or not at all, as
the knock: while the digits are coming apart, for 1 s after, or when it would run
into the minute's change, the page says that no preview is possible now, and the
setting is taken anyway (until 2026-10-20 the preview waited). **Discard
changes** puts the stored settings back and shows them the same way on every
face: everything on the page previews live but sits in RAM until saved, and this
is the way back from that.

The **digit and trail colors do not apply** to this face. The master brightness
(and therefore the light sensor) does: the palette is rescaled before every frame.

The face only repaints while blocks are falling or when something visible changes
(colon, brightness, button indicator), so an idle Tetris clock costs almost no CPU.

### Muncher

**Muncher** shows `HH:MM` as dots on a seven-segment grid, 5 × 9 dots 2 px
apart, so a digit is 9 × 17 px, in the colour of a game's pellets. The colon is
two power pellets that blink with the seconds. Below the digits runs a corridor
between two blue walls, with an opening under each digit's lower right corner.
Between changes nothing moves.

| | layout |
|---|---|
| landscape 64x32 | digits at x 3, 15, 40 and 52, rows 2..18; colon at x 30..33; corridor walls in rows 21 and 31 |
| portrait 32x64 | hours in rows 4..20, minutes in rows 27..43, x 3..11 and 20..28; colon in rows 22..25; one shaft up between the columns; corridor walls in rows 47 and 57 |

**A change of the time.** The muncher comes in along the corridor from the
left, two ghosts behind it, goes up into each digit that changes and walks its
segments: the dots that go are eaten, the ones that come appear behind it. While
it is up in a digit the ghosts wait below. Then it leaves to the right, and
the corridor's pellets are back. The way through a digit is the shortest walk
over the segments it has to pass, from the corner where the muncher comes in and
back to it (a breadth-first search over corner and segments passed, in
`src/muncher.h`).

**The time stands at :00.** A change is planned backwards from the next minute:
it starts as early as its walk needs, so the last digit is complete as the new
minute begins. The console says when it was, against the clock's own second:
`Muncher: the new time stands at :00.000`. It is stepped by the time since it started, so a screen that was on top
for a while, or a loop held up by a request, does not put it behind; a time that
moved without a change (set by hand, corrected by a sync) is walked right at once.
No GIF of its own accord starts while the muncher walks, nor when its whole
loops would not end 3 s before the next change starts ([Who takes the
panel](#who-takes-the-panel)): in a first test a GIF came up during a change and
the new time showed only at :04.7, when it had ended. One asked for by an input
(*play random GIF*, *play last GIF again*, the web app's button) plays at once,
also during a scene or the change and into it (Peter, 2026-10-22): the face
walks on behind it by the time since its start. Until then it was dropped there
(on 2026-10-11 a GIF on *play last GIF again*, most likely from a knock, had
covered the change to midnight; from then to 2026-10-12 one was kept for the
scene's end). The same goes for the Aliens face.

**Change**, one of three: *Mixed* (the default) eats the whole old digit and
walks the new one when one digit changes, and walks only the segments that differ
when several do; *Whole digit* and *Only the difference* do one of the two
always. The walk takes 25 px/s by default (*Pace*, 20 to 80 ms per pixel); the
longest change of a single digit, from the left edge to the digit complete,
is 199 steps, 8 s at that pace (the debug build's self-test, below).

**Pellets**: one LED each (the default), or more to be seen better from afar -
two *side by side*, which makes the level strokes solid lines and leaves the
upright ones dotted, two *one above the other*, the other way round, or *Solid*,
a block of 2 × 2 for every pellet, which makes every stroke a solid line 2 px
thick: the digits drawn for reading first, the game kept as a token. The
corridor's pellets take the same shape. The other LEDs go right of and below the
pellet's own, so a digit grows by a pixel; the muncher's way stays the same.
Stored as `mpel` (0 to 3; rev 2 of the blob, in a byte that was spare). A block
of 2 × 2 with gaps, every other dot of the grid, was tried and left out: the two
digits of the hours and of the minutes stood too close and read badly.

**To a full hour** four ghosts follow the muncher instead of two (cyan and orange
join red and pink), and after the change comes the **hour show**: the corridor's
walls flash white four times, as when a level of the game is cleared, then the
intermission - a ghost chases the muncher out to the left, and a giant muncher
(13 × 13 px, over the corridor) chases the ghost, turned blue, back out to the
right. About 9 s.

**Bonus fruit**: now and then, at the pace of the extras the faces share (the
**How often** slider under **Now and then**, about every 5 min by default; none in quiet hours or a dark
room), a fruit appears in the corridor at its drawn time, outside the slot around
the change - a cherry, a strawberry, an orange or an apple, 7 × 7 px - and lies
still until the next change, whose muncher eats it on its way along (`Muncher: a
bonus fruit (now and then)`, then `Muncher: a bonus fruit eaten` on the console).
Until 2026-10-20 it came only at the end of a minute change.

**Watchface animation** (`mkn`), what the input action *trigger watchface animation*
plays - a knock, unless the Inputs page says otherwise: *Power pellet*,
*Last digit again* (the minutes' ones eaten and laid again), *Ghosts win*, or
*Random each time* (one of the three). All of them start as a change of the time
does, the muncher in from the left with both ghosts behind it, so none gives
itself away before the muncher turns. When the ghosts win, it turns back at the
middle and runs into the ghost behind it: everything stands still for half a
second, the ghosts vanish, the muncher folds up (its mouth opening wider until
nothing is left, then a little burst), and after a pause a new one walks through.
In the power pellet scene it turns up to one of the colon's power pellets,
and the ghosts close in on the opening, the first passing under it to the far
side, as if the muncher were trapped. Turned blue, they stand frozen until it is
back in the corridor, then shuffle away, blinking white as it closes in (but not
while the other one is caught: white is the caught one's colour), and it
catches them one after the other near the middle (in landscape at x 34 and 24,
upright at x 8 and 23): each
flashes white for half a second while everything else stands still, as in the
game, and is gone. Before, both fled to the left, and the muncher caught one at
the very edge of the panel and, upright, none; then a ghost coming in from the
right gave the scene away from its start; and a caught ghost ran off as a pair of
eyes, which on the panel read as a lone dot flying through the corridor. A knock is not taken while a
scene runs, for 1 s after one, or when its scene could not end 3 s before the
next change starts. However the ghosts stand, a scene ends at the latest 150
steps after the muncher's walk, so no scene can hold up a change of the time.

The settings are in a blob of their own (NVS key `muncher`), as the Tetris ones
are; `mst`, `mkn` (0 power pellet, 1 last digit again, 2 random, 3 ghosts win:
the newest value went to the end, so a stored *random* stays random) and `mpace`
in the web app's interface. Changing the pace shows a change of the minutes' ones
(eaten and laid again); choosing another watchface animation shows its scene; a
new change style shows at the next change of the time. A preview plays at once
when it can end 3 s before the next change starts and no scene runs, and
otherwise not at all - the page says so; until 2026-10-20 it waited for room.
The time on show is never wrong for it: the first previews showed the last
minute's digits and walked them to this minute's, which at midnight set the clock
back to 23:59 for a moment.
The debug build walks every change of every digit, both ways
and on both layouts, and every other scene - the power pellet (both ghosts
caught, away from the panel's edges), the ghosts winning, both with the same
start as a change, step by step, a change with four ghosts, the hour show and a
fruit eaten - ten seconds after start-up, says on the console whether each came
out right, and where the knock's scene caught its ghosts.

### Aliens

**Aliens** (after Space Invaders, with a plain name and pixel art of its own)
shows `HH:MM` in blocks of 3 × 3 px with a gap, on a grid of 3 × 5 cells of
4 × 4 px a digit - 12 × 20 px, the Tetris size - with a green cannon at the
bottom. Upright the hours stand above the minutes and the cannon below them.
Between changes the blocks stand still. The trial build of 2026-10-11 also had
digits made of small aliens, of wide aliens and of solid cells; on the panel the
digits of aliens could not be read as a time, so the blocks stayed (concept doc
"Aliens: draft").

**The minute change** is planned backwards from the minute, as the Muncher's, so
the new time stands at :00: the blocks the change takes wake as aliens and march
on the spot for 1.5 s - legs in two frames and a pixel to the right and to the
left, 400 ms a step - so whoever looks sees them turn; an alien sways only where
its row has room that way (an empty cell or the digit's edge before a block that
stays), so one between two staying blocks stands still. Then the cannon drives
under them and shoots them one by one, column by column from the bottom up, each
hit an orange burst; the new formation marches down from above the panel in steps
of a cell, lands, marches on the spot for 1.5 s as aliens and turns into blocks at
:00; the cannon drives home. Each transformation has its moment, apart from what
moves around it (Peter, 2026-10-11). **Change** (`ast`): *Whole digit* wakes and
shoots every block of a changing digit and brings its whole new formation, *Only
the difference* only the blocks the new digit does not have - and a block that
stays but stands above a new one in its column: its way down is shot free, so
every new alien is seen coming down instead of behind a block (Peter, 2026-10-14);
such a block comes back with the new formation. *Mixed* (the default) takes the
whole digit when one changes and the difference when several do. Upright, the
minutes' new formation still comes down behind the hours above them.

**The full hour** goes as a wave, whatever the Change setting: every block of all
four digits wakes; the cannon shoots the wave row by row from the bottom (upright
the minutes' rows first), sweeping to and fro at about the arcade's pace. It keeps
a beat of its own, a shot every 250 ms: it drives on to the next alien as soon as
it has fired and fires on the next beat at which it stands there and its last shot
has hit, so a long way costs it a beat. Once two rows below the wave are empty the
wave steps down a row on the next beat of its march, a row a beat, while the cannon
fires on - so it comes nearer as it thins out, but never further than a row above
where the digits' lowest row stood; a shot on its way meets its alien where it
stands, and a burst goes down with the wave (Peter, 2026-10-14: it had stepped down
at once, before a shot; 2026-10-16: the cannon waited for every step down). Then
the new time marches in. That takes about 17 s, upright as well.

**Now and then**, at the pace of the extras the faces share (the **How often** slider,
about every 5 min by default) and at a random time outside the slot around the
change, a scene leaves the time as it is: a red **saucer** crosses the lane above the digits and
the cannon shoots it through a gap between two digits it can reach in time (upright
the saucer flies slower, the panel being narrow and the shot's way long); or an
**alien shoots back** - the lowest block of a column wakes, the cannon drives under
it and, once it is there, the alien drops its bomb, which hits it; the wreck flickers, a new cannon blinks
in at home and the alien turns back into its block; or, with **Waking blocks**
(`anap`, on by default), a single block wakes as an alien for 3 s and turns back.
Upright only the minutes shoot back, since the hours stand above them. One that has
no room when it is due, measured by its own length - the wave's minute, info cards
waiting, a GIF just before - is dropped, not kept. **Watchface animation** (`akn`): what the input action "trigger watchface
animation" (a knock, unless the Inputs page says otherwise) plays - *Last digit again* (the minutes' ones shot and brought back),
*Saucer*, *Aliens shoot back*, or *Random each time* (the default); a new value
chosen in the web app plays it once, at once or not at all (the page says when
not). None of them starts when it could not end 3 s before the next change; GIFs
and info cards do not start into a scene.

**Colours** (`acol`): white, as the arcade's overlay, or each row in its own colour
(magenta, cyan, green). The settings blob of the trial build (revision 1, with a
look and an idle setting) is read once: its change and colours are kept; a blob of
revision 2 gets the knock's default.

Each scene is a plan of actions on a timeline (`src/aliens.h`): what the face shows
at any moment follows from the plan and the time alone, so it is drawn on whatever
pass the loop has and is never behind. The settings are in a blob of their own (NVS
key `aliens`). The debug build checks, on both layouts, every change of every digit
both ways - no woken alien sways into a block that stays, no new alien comes down
behind a block, a whole digit's shots fly through nothing, the new formation stands
a moment as aliens and then as the digit, the cannon home, within the minute - the
change of all four at midnight, the wave at every full hour (shots through nothing
meeting their aliens where they stand, the cannon on its beat, one or two beats
apart, a step down only with two rows empty below, and at least one), the saucer
from either side to each gap, and every alien that can shoot back: the shot meets
the saucer, the bomb meets the cannon, the time stays.

### How the faces are built

Each face is a class of its own in a header - `src/face_classic.h`,
`face_tetris.h`, `face_muncher.h`, `face_aliens.h` - behind one interface,
`Watchface` in `src/face.h` (since 2026-10-20; before, the sketch asked at about
30 places which face was on, and the Muncher and the Aliens were near copies of
each other). The core keeps the time, the screens and the rules for who takes
the panel. It hands a face the moment (`FaceNow`: the time, the ticks, the panel
as it is turned), how to colour (`FacePaint`) and the canvas, and asks it:

| | what the face does or says |
|---|---|
| `enter()` | it comes onto the panel: chosen, shown again after a screen on top, the panel turned |
| `update()` | one pass: it steps what moves, by the time; whether its picture changed - only then is it drawn |
| `draw()` | its picture, without the marks the core puts over it (status pixel, hotspot mark, notices, an input's feedback) |
| `busy()` | whether its change of the time or a scene runs: nothing optional starts then |
| `changeSpan()` | how long before a minute its change starts and how long after it goes on, the full hour's included: the rules keep that free |
| `offers()`, `request()` | an animation asked for - the input action "trigger watchface animation", now and then, the web app's preview - played now or refused |

The Muncher and the Aliens share `face_planned.h`: they plan their change
backwards from the minute, work its start out anew on every pass from the
second's beginning, start it at its moment even when that has passed, and log
when the new time stood (`... the new time stands at :00.0xx`). Each face also
keeps a table of its own settings (`SettingParam` in `src/settings_table.h`: the
web app's key, where it sits in the face's blob, its kind and range), through
which one piece of code loads, checks, takes from the web app and reports them
all. Since 2026-10-22 the clock's own settings go through such tables too
(`CLOCK_PARAMS`, `UI_PARAMS`, `GIF_PARAMS`, `DECOR_PARAMS`, `FEED_PARAMS` in
`src/main.cpp`), and `/api/state` tells the web app, under `lists.form`, how to
send each one back (see [Settings: one table per blob](#settings-one-table-per-blob)).

The same rules for every face: an animation asked for plays at once or not at
all, and not when it would not end 3 s before the face's next change; after one,
the next waits 1 s; now and then, the core first looks at quiet hours, a dark
room, the face at rest and information waiting, and the face then at the length
of its extra; a preview in the web app plays now or the page says it cannot.

A **test build** (the release env with `-DFACE_FRAME_LOG`) logs every frame a
face draws as a checksum (`F hh:mm:ss.mmm crc face ms`) and has `POST /api/test`,
which runs the panel on a clock of its own - 5 ms a pass, from a given time -
so two builds show the same frames when nothing has changed. That is how the
move of the faces into their headers was checked to leave the panel as it was;
the scripts for it are not part of the repository.

Measured on the wall clock before the faces moved behind the interface and
after (debug builds, each face 150 s on the live clock, 2026-10-21; a pass is
one run of the main loop, a frame one the face drew):

| Face | passes/s | time a pass takes, average | frames/s |
|---|---|---|---|
| Classic | 145 -> 656 | 5711 -> 514 us | 145 -> 54.6 |
| Tetris | 992 -> 994 | 14 -> 18 us | 3.3 -> 2.4 |
| Muncher | 992 -> 993 | 16 -> 14 us | 4.4 -> 4.1 |
| Aliens | 991 -> 991 | 24 -> 25 us | 6.0 -> 5.9 |

The classic face drew on every pass and so held the loop for over 80 % of its
time; it now draws when a digit has moved. The other faces drew only on a change
before as well, and stay as they were (their frames depend on how many digits
changed in the window). The internal RAM free while running was about 200 bytes
lower (142.6 instead of 142.9 KB at its least).

## Animation timing

The **fly-in time** is how long every flight of the classic face takes, in ms
(100–850, default 400); each digit covers its own way in it, so all land
together. Until 2026-10-08 the setting was the time per pixel (4–60 ms, default
12), rounded to whole loop iterations per pixel; with flights of up to 62 px and
every flight held under a second, it had only three steps. A stored value of the
old kind is converted once, times 32 (the length a flight had then in landscape):
12 ms per pixel becomes 380 ms.

The Tetris watchface has its own pair of settings instead (see above); the
fly-in time does not apply to it.

On the S3 the loop runs in step with the panel refresh (about 200 Hz):
`show()` waits out one refresh period after the previous frame. A flight is
placed by the time since its second began, its pixels spread evenly over the
fly-in time, and the face is drawn on a pass where a digit has moved - a loop
held up for a moment (a web request) does not slow a flight, the next pass puts
it where it ought to be. Until 2026-10-20 the fly-in time was turned into the
passes the loop had had in the second before, and the face was drawn on every
pass, about 200 times a second.

### Panel driver

The clock drives the panel with ESP32-HUB75-MatrixPanel-DMA. It draws into a
16-bit canvas, and `show()` hands the pixels that changed to the DMA driver, which
keeps its own frame buffer at 8 bits per colour. Adafruit Protomatter, which the
S3 used before, dims by scaling the colours and loses levels at low brightness (six
bit planes did not help: it draws into RGB565 and only green gains a level).

The S3 dims through the colour values at the full OE time, not through the OE
time as the driver would. The panel's LED drivers need a minimum on-time per
colour, longest for blue, so a short OE time dims blue first, then green.
Measured with a BH1750 in front of the panel: at OE brightness 64 blue was down
to a fifth of red and white turned yellow, at 48 and below only red was left.
With the values turned down instead, red, green and blue stayed together down to
about 5 % of full (blue at 0.84 of red there, 0.95 at 13 %), which is as dark as
the S3 goes: brightness 1 to 13 all give that. The same measurement gave the
light of each of the driver's bit planes per colour (`PANEL_BIT_LIGHT`); they are
not weighted in powers of two (bit 7 gives 46 % instead of 50 %). For every
colour value and brightness, `show()` hands the driver the value whose measured
light comes nearest, from a table per colour that is rebuilt when the brightness
changes; GIFs go through the same tables. Blue steps the coarsest at the dark end
(its weak low bits leave nothing between 8.5 % and 12.6 % of full), so the light
blue really gives sets the share all three colours aim at: white stays white
(red and green within 7 % of blue), and at the dark end the brightness follows
blue's steps, up to 21 % off the one asked for. Measured through this path,
blue stayed within 0.94 to 1.07 of red at every brightness and green within 1.00
to 1.13 (the widest at the darkest, where red gives the sensor only about 1.6 lx),
where the OE time had left blue at 0 to 0.62 of red. A brightness step whose
tables differ costs a full frame for each of the driver's two buffers, 3.6 ms
each; one with the same tables (often at the dark end: brightness 36 to 40 give
the same) costs nothing. Blue's steps are also why a fade at the dark end shows
jumps: between brightness 30 and 70 there are about 8 steps of light, +47 % from
26 to 27 and +29 % from 40 to 41.

On the wall clock a new brightness goes through the picture a few rows a pass
(`retintStep()`): four of the canvas's rows or twelve of the band's, from what
the wall shows. Written all at once it held the loop 42 ms (the canvas 28.4 ms
writing its LEDs and 1.4 ms writing the cache back to PSRAM, the band 10.8 and
1.2 ms; measured 2026-10-22). Now a step takes 11 passes of at most 5.7 ms, and
in a fast fade of single steps the longest gap between two passes went from 48 ms
to 10 ms, with none over 20 ms instead of 51 in 12 s. Meanwhile a row is at most
one step of light behind the others.

Two more settings matter on the MatrixPortal S3 (`PanelCanvas` in
`src/board_hal.h`):

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
QT port) recognises nine gestures by itself, six of which the clock uses.
The clock looks for it at start-up; without one everything works as before.
Its gestures do the same in both input profiles; on the face these are the
shortcuts to begin with, which the Inputs page can change (see
[Face shortcuts](#face-shortcuts)):

| Gesture | On the clock face | In the menu and its editors |
|---|---|---|
| swipe up / down | open the menu | previous / next item, or change the value |
| swipe right, push | open the menu | open the item; in an editor: save |
| swipe left | - | back, without saving |
| a hand comes near and stays | a hint: `swipe` / `menu` | - |
| a hand held over the sensor for 3 s during boot, then taken away when the panel says `LET GO` | open the config hotspot | - |

On the hotspot screen a swipe to the left closes the hotspot again. Pull (a
hand moving away) does nothing unless the Inputs page gives it something. The
wave is not used: in the tests of 2026-10-10 the sensor reported a wave for 3
of 5 waves, and late, in the first run, and for none of 10 in the second
(with either of the settings below); a wave came as left and right, or as a
circle, instead. Nor are the circles: in the gesture test they came as circles
(9 of 9, with the settings below), but in the brightness and time editors the
direction came wrong now and then, and a hand is hard to keep at the distance
and the size of circle the sensor wants. The sensor still reports them; the
clock leaves them out, and the Inputs page does not offer them.

- **Directions follow the panel.** The direction the sensor reports, plus the
  turn it is mounted at, minus the rotation the panel is drawn in, gives the
  direction on the panel: a swipe up is up as the viewer sees it, however the
  clock is held. The mount turn is set once on the config page (**Inputs** page);
  if a swipe up acts as another direction, try the next setting.
- **Reading:** the clock reads the sensor's two result registers itself, in
  one read (as the datasheet's burst read, and the Zephyr and M5Stack drivers
  do); the library only sets it up. With more than one bit set, a circle (left
  out) counts before push and pull, and those before a swipe. Two of the
  sensor's values are then set as PixArt's datasheet v1.5 starts it (0x91 =
  0x0C, so a hand may be out of view for 12 frames before a trace ends, and
  0xCF = 0x62): with them circles came as circles in the tests (9 of 9), with
  the library's values often as swipes (6 of 14). So a circling hand is left
  out instead of moving through the menu.
- **A swipe acts at once.** What comes after a gesture is held against it:
  any gesture within 300 ms, the opposite swipe within 450 ms (the hand going
  back; in the tests it came 250 to 305 ms after the swipe), and a swipe
  within 1 s of a circle (part of the circling). Every gesture counts as
  handling the clock, so it holds off the knock for 3 s, as a button press
  does.
- **The hint** is a banner that only explains: the swipe that follows closes it
  and goes on to the face, instead of being used up as on other banners.
- **INT line or not.** STEMMA QT carries no interrupt line. Without one the
  clock asks the sensor for a gesture every 50 ms, and on the face also every
  100 ms whether a hand is near (for the hint). With the sensor's INT line
  wired to a free pin and that pin given at build time
  (`-D GESTURE_INT_PIN=A0` in `build_flags`), the sensor is read only when the
  line is low, and nothing is polled; the line reports gestures only, so there
  is no hint then. The 3 s hold at boot is asked either way, once at start-up.
- **Wiring:** four wires, through the STEMMA QT port or to its pins: 3.3 V,
  GND, SDA, SCL. Not 5 V: the module's pull-ups go to its supply and would put
  5 V on the ESP32-S3's 3.3 V pins (the board has its own pull-ups to 3.3 V).
  INT stays free unless it is wanted (above).
- **On the console** every gesture taken shows as `Gesture: Swipe up, 1840 ms
  after the last (sensor up, turned 0, panel 0)`, one left out with its reason
  (`- left out, the hand going back, 300 ms after the last`), and a hand that
  came near as `Gesture sensor: a hand near`. The time counts from the last
  gesture taken; when the sensor set more than one bit, the register follows
  (`0x43=41`), and the gesture shown is the one the priority above chose. The
  addresses that answered on the I2C bus at start-up are on the console and in
  `/api/state` (`hw.i2c`).
- **Gesture test** (the web app's Inputs section): the panel shows every
  gesture instead of doing it - big, the direction the clock takes, grey when
  it is left out; on top the two result registers in hex and the sensor's own
  direction; below the count (G), the sensor's wave count (W) and what is in
  view (N brightness/size). The web app lists the last eight with their
  registers, the console logs every read with a bit set, with the debug
  registers 0xB6 and 0xB7, and a switch there puts the library's values back
  for a comparison, until the next restart. Any button ends the test, and so
  do 3 minutes without a gesture. `GET /api/gestures`, `POST /api/gesturetest`
  (`on=1|0`, `tune=1|0`).

Tested on 2026-10-10 with a PAJ7620U2 module (Berrybase CJ-PAJ7620) on the
MatrixPortal S3, mount turn 0: swipes, push and pull came reliably; circles
and the wave not well enough to be used (above). Still to be tried: how reliably
it tells a hand that stays from one passing by (the hint and the boot hold),
and whether the panel's own light or a cover in front of the sensor makes it
see an object that is not there.

## Real-time clock (DS3231)

A DS3231 on the I2C bus (address 0x68; on the MatrixPortal through the STEMMA QT
port, next to the BH1750) keeps the time without power, from its coin cell, to
±2 ppm (about a minute a year). The clock looks for it at start-up; without one
everything works as before.

| | Without RTC | With DS3231 |
|---|---|---|
| Power-up | `--:--` until the first NTP sync or a time set from a phone | the time from the RTC at once |
| NTP sync | daily at the sync time, and at every power-up | at the sync time on the day the interval runs out (7 days by default; daily, 30 days, 3, 6 or 12 months, or never); at power-up only when the interval has run out, the RTC had no time, or no sync is known yet |
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
- When the last good sync was is stored too (NVS key `ntp`), written at every sync while a DS3231 is there, so a
  restart neither syncs by itself nor starts the interval anew. A stored sync
  later than the RTC's time (an RTC set back) is not taken, and the clock
  syncs at start-up.
- **Wiring:** four wires are enough: 3.3 V, GND, SDA, SCL (not 5 V, as with the
  gesture sensor). The other pins of the modules stay free: SQW/INT (alarm or
  square wave out), 32K (a 32 kHz clock out), RST (reset) and BAT (a second way
  to the coin cell). The clock reads the time over I2C at start-up and once a
  minute and uses no alarm. Some modules carry an EEPROM (an AT24C32 at 0x57),
  which the clock does not use. The ZS-042 module charges its coin cell through
  a resistor and a diode, made for a rechargeable LIR2032: with a CR2032 in it,
  never power it from 5 V.

Tested on the MatrixPortal S3 with Adafruit's DS3231 breakout (3013) and a
CR1220 on 2026-10-10.

### The wall clock's own real-time clock (PCF85063)

The Waveshare board carries a PCF85063 (0x51 on the board's own I2C bus, GPIO47
and GPIO48 behind a level shifter). The clock uses it besides a DS3231 on the
GPIO header:

- **Which time counts:** a DS3231 with a time comes first, then the PCF85063.
  Every new time (NTP, a phone, typed in) is written to both.
- **NTP stays daily** with the PCF85063 alone, as without a real-time clock: its
  crystal has no temperature compensation, so the sync interval and "never" are
  offered only with a DS3231 (`hw.rtcSync` in `/api/state`). What the PCF85063
  brings is the time at once after a restart, a flash and, with a coin cell, a
  power cut.
- **Coin cell:** the board's socket J4 (SH1.0, two pins, pin 1 plus) feeds the
  chip through a Schottky diode, and the charging path is not fitted (Waveshare's
  schematic), so a primary cell such as a CR2032 or CR1220 with an SH1.0 lead may
  go in. Without one the chip keeps its time over a restart, not over a power
  cut; it then reports that its oscillator stopped, and the clock waits for NTP.
- **A time of this clock's only:** the clock marks the chip's free RAM byte
  whenever it sets the time and trusts no time without that mark - Waveshare's
  demo firmware writes a fixed date at every start. A chip without a time of
  the clock's is reset (as the datasheet gives), set to 12.5 pF and its CLKOUT
  switched off.
- **Drift:** each NTP sync logs how far the PCF85063 was off and how long since
  it was set, in ppm. Within 2 s it is left running rather than set again, so
  its drift adds up over the days. When it was last set is stored (NVS key
  `pcf`). Its Offset register stays 0 until that drift is known.
- The clock follows only a DS3231 once a minute, not the PCF85063: its crystal
  is no better than the ESP32's.
- The console's `rtc` shows what each chip holds against the clock.

## Libraries

Resolved automatically by PlatformIO from `platformio.ini`.

Both clocks: Adafruit GFX, Adafruit BusIO, Adafruit LIS3DH, Adafruit Unified
Sensor, BH1750FVI_RT (Rob Tillaart), RTClib (Adafruit, for the DS3231), RevEng
PAJ7620 (Aaron S. Crandall, for the gesture sensor). All versions are pinned.

Panel driver: ESP32-HUB75-MatrixPanel-DMA (mrfaptastic) on the MatrixPortal S3,
built with `NO_CIE1931` so colours and brightness stay linear as before; on the
wall clock a copy of it in `lib/hub75-dma-wall`.

Timekeeping lives in `src/clock_time.h` and only uses the C library's `<time.h>`.
It replaced the Time library (TimeLib), which is unmaintained since 2021 and does
not build against picolibc, the default C library from ESP-IDF 6 on. The clock
keeps UTC, exactly as NTP delivers it; the timezone and daylight saving are added
only where the time is shown, so changing either never touches the clock.

The WiFi stack, the NVS settings storage, HTTPClient and the TLS client come from
the ESP32 Arduino core; ArduinoJson (Benoit Blanchon) reads the info band's sources.

The Tetris watchface needs no library. Its block tables are generated by
`scripts/gen_tetris_digits.py` into `src/tetris_digits.h` (a generated file — do
not edit it by hand, re-run the script). The script has a fixed seed, so a re-run
reproduces the header byte for byte, and it verifies every table it emits before
writing: exact coverage of the glyph, no overlaps, every piece droppable from
above in the stored order, and a proper colouring within the palette. It refuses
to write anything if a check fails.

## Flash layout (S3)

The MatrixPortal S3 comes with Adafruit's TinyUF2 layout: two app slots of 2 MB,
of which the clock used one, TinyUF2 (the `MATRXS3BOOT` drive) and 3776 KB of
FAT that the clock used for its GIF pack. Its app slot became too small (the
firmware with built-in GIFs and the Home Assistant link no longer fitted), so
since 2026-10-09 the clock has a layout of its own, `partitions_clock_s3.csv`:

| Partition | Offset | Size | What for |
|---|---|---|---|
| `nvs` | 0x9000 | 20 KB | the settings - where TinyUF2's table had them, so they survive the change |
| `otadata` | 0xE000 | 8 KB | which app slot boots |
| `app0` | 0x10000 | 1792 KB | the firmware |
| `app1` | 0x1D0000 | 1792 KB | for updates over WiFi (to come) |
| `ffat` | 0x390000 | 4544 KB | the GIF pack, all the clock's GIFs |

No GIFs are built into the firmware, and there is no TinyUF2: uploads go over
the USB-Serial-JTAG without a button (see [Setup](#setup)). The board
definition `boards/adafruit_matrixportal_esp32s3_clock.json` belongs to it:
Adafruit's own would also write TinyUF2 at 0x410000, now inside the GIF pack,
and touches the port at 1200 baud and waits for a new one before it uploads. `scripts/check_app_size.py` stops
the build when `firmware.bin` does not fit its app slot (PlatformIO's "Flash:"
line counts the sections only and reads about 26 KB low; once an image 6.7 KB
too large was cut short by TinyUF2 and the board did not start).

**Moving a board over** from the TinyUF2 layout, once:

1. Keep its whole flash first, through the ROM bootloader (hold BOOT, tap
   RESET, release BOOT): `esptool --chip esp32s3 --port <port> --no-stub
   read-flash 0 0x800000 flash-full.bin`, then `verify-flash 0x0 flash-full.bin`.
   Writing that file back at 0x0 gives the board exactly its old state again.
   (On Windows, set `PYTHONIOENCODING=utf-8` first: esptool's progress bar
   otherwise breaks a read off with a misleading "Serial data stream stopped".
   The stub's fast read broke off at the same 64 KB every time; the ROM's own
   read, `--no-stub`, took 6 minutes for 8 MB and read it all.)
2. Still in the ROM bootloader: `pio run -e adafruit_matrixportal_s3 -t upload
   --upload-port <port>`. It writes the bootloader, the partition table,
   `otadata` and the firmware, and leaves `nvs` alone; the clock starts by
   itself with its settings.
3. Send the GIF pack anew (see [GIF pack](#gif-pack)): the old one sat where
   the new table has no FAT.

**Back to Adafruit's state:** Adafruit's [Factory Reset](https://learn.adafruit.com/adafruit-matrixportal-s3/factory-reset)
page has TinyUF2's combined image (erase the flash, write it at 0x0) and the
shipped demo as a UF2; CircuitPython for the board is on
[circuitpython.org](https://circuitpython.org/board/adafruit_matrixportal_s3/).

## Toolchain

PlatformIO's own `espressif32` platform still ships Arduino-ESP32 2.0.17, built on
ESP-IDF 4.4, which has been end-of-life (no bug or security fixes) since July 2024.
The S3 therefore builds with the **pioarduino** platform, which packages Espressif's
unmodified Arduino-ESP32 releases for PlatformIO (the same core the Arduino IDE
installs). Two things to know on the build machine:

- This project keeps its PlatformIO platforms and packages in its own core dir,
  `~/.platformio-pioarduino` (`core_dir` in `platformio.ini`), about 6 GB, mostly
  pioarduino's prebuilt ESP-IDF libraries for every ESP32 chip and the toolchain.
  pioarduino deletes every other Arduino-ESP32 framework version in the packages
  dir it runs in and sets up its own Python env there; in the shared
  `~/.platformio` that would break the other ESP32 projects on the machine.
  The regular PlatformIO IDE extension in VS Code handles the separate dir by
  itself.
- Command-line builds must run from PowerShell or cmd. ESP-IDF's tool installer
  refuses to start under Git Bash (it checks for the `MSYSTEM` variable). The
  build and upload buttons in VS Code are not affected.
