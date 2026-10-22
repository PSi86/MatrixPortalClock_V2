/* ----------------------------------------------------------------------
Matrix Clock with Animation
Todo Concept:
- Animate all Numbers individually (max 6 active Items)
- Check last digit of HH, MM, SS if it is "9" and generate a trigger
- use this trigger to animate the digit change:
  - boolean array holding animation status for all 6 displayed digits

- if secondTrigger: trigger secondNow[1] animation

- if secondNow+1 quotient 10 = 0: trigger secondNow[0] animation
- if secondNow=59: trigger minuteNow[1] animation

- if minuteNow+1 quotient 10 = 0: trigger minuteNow[0] animation
- if minuteNow=59: trigger hourNow[1] animation

- if hourNow+1 quotient 10 = 0: trigger hourNow[0] animation


------------------------------------------------------------------------- */

#include <Arduino.h>        // the Arduino core (the .ino conversion added it until 2026-10-22)
#include "board_hal.h"      // board detection: pins, WiFi/NTP, settings storage, reset
#include "sensors.h"        // the sensors each board supports, tried at the start and remembered
#include "clock_time.h"     // software clock: seconds since 1970 (UTC), C library only
#include "time_zones.h"     // the zones the clock offers and their daylight saving
#include "clock_state.h"    // what this file shares with the modules below
#include "settings.h"       // the clock's stored settings
#include "wifi_station.h"   // the home WiFi and the station joined to it
#include "time_sync.h"      // where the time comes from, and the zone it is shown in
#include "brightness.h"     // the panel's brightness, the light sensor, the room climate
#include "band_items.h"     // the info band's items, from every source
#include "feeds.h"          // when the info band's own sources are fetched
#include "notices.h"        // the items on the 64x32 clock: notices and info cards
#include "info_band.h"      // the info band on the wall clock
#include "panel_rules.h"    // who takes the panel, and when
#include "home_assistant.h" // the link to Home Assistant
#include "gif_player.h"     // GIFs on the panel, the GIF pack, the wall clock's card
#include "lan_access.h"     // the web app on the home network
#include "input_map.h"      // input events, functions and the profiles mapping them
#include "input.h"          // the buttons, the gesture sensor, the face shortcuts
#include "menu.h"           // the on-screen menu, its editors, the banners
#include "hotspot.h"        // the clock's own access point and its captive portal
#include "console_cmds.h"   // the console's commands, the wall clock's panel output watch
#include "web_server.h"     // the web server beside the loop, and its answers
#include "web_api.h"        // the web app and its JSON interface
#include "face.h"           // what the watchfaces share
#include "face_classic.h"   // the classic watchface, on every board

#include <Fonts/Picopixel.h>
#include <Fonts/TomThumb.h>    // 3x5, for the menu

#include "face_tetris.h"       // the Tetris watchface
#include "knock.h"             // the knock on the clock, from its motion sensor
#include "face_muncher.h"     // the Muncher watchface
#include "face_aliens.h"      // the Aliens watchface

#include <math.h>              // powf() for perceptual brightness fade

#include <Wire.h>              // I2C for the onboard accelerometer + light sensor
#include <Adafruit_Sensor.h>  // sensor base class
#include <Adafruit_LIS3DH.h>  // the MatrixPortal S3's onboard LIS3DH accelerometer
#include <BH1750FVI.h>         // external BH1750 ambient light sensor (auto-brightness)
#include <RTClib.h>            // DS3231 real-time clock (optional)
#include <RevEng_PAJ7620.h>    // PAJ7620U2 gesture sensor (optional)

#include <AnimatedGIF.h>       // GIF decoder
#include <new>                 // new (std::nothrow) for the decoder
#include <esp_partition.h>     // the GIF pack in the ffat partition
#include <esp_heap_caps.h>     // PSRAM for the pack's index and an inflated GIF
#include <esp_rom_crc.h>       // CRC-32 in ROM, to check the pack
#include <miniz.h>             // inflate in ROM (tinfl), for the pack's deflated GIFs

#include "web_request.h"       // a web connection: its request and its answer
#include <ESPmDNS.h>           // the clock's name on the home network
#include <mbedtls/sha256.h>    // the home network password, kept as a salted hash
#include <mbedtls/base64.h>    // and sent as HTTP Basic authorization

#if BOARD_WAVESHARE_WALL
#include <mbedtls/sha256.h>    // the console's "crypto" test of the panel output beside the crypto DMA
#include <mbedtls/aes.h>
#include <soc/gdma_struct.h>   // and its "gdma": the DMA channels' peripherals and priorities
#include <esp_timer.h>
#include "rtc_pcf85063.h"      // the board's own real-time clock
#include "imu_qmi8658.h"       // its motion sensor, for the knock
#include "shtc3.h"             // its temperature and humidity sensor
#include "gif_card.h"          // its microSD card, the GIFs' source while one is in
#endif

#include "band_data.h"         // the info band's items and the sources the clock fetches
#include "item_icons.h"        // their icons, for the band and the alert banners
#if BAND_ON_PANEL
#include "sky.h"               // sunrise, sunset and the moon's phase for the band
#endif

#include "ha_link.h"           // the link to Home Assistant over MQTT

/* ----------------------------------------------------------------------
The RGB matrix must be wired to VERY SPECIFIC pins, different for each
microcontroller board. board_hal.h picks the set that belongs to the board
being built for (MatrixPortal S3 or the wall clock's Waveshare board).
------------------------------------------------------------------------- */

PanelCanvas matrix(64, 32);   // the canvas the clock draws into; show() hands it to the DMA driver

// The clock's stored settings: settings.h.




// (GIFs on the panel, the GIF pack, the wall clock's card: gif_player.h.)

// (Who takes the panel, and when, and the faces' extras: panel_rules.h.)

// Info band data ------------------------------------------------------------------
// (The place and the sources that are on: settings.h. The sources and the
// fetch task: band_data.h; when they are fetched: feeds.h; the items, from
// every source: band_items.h.)

// (The items on the 64x32 clock, notices and info cards: notices.h.)

// (The wall clock's info band: info_band.h.)


// (The Home Assistant link: home_assistant.h.)

// (The home WiFi and the station joined to it: wifi_station.h.)



// (The inputs - buttons, gestures, face shortcuts - and their feedback: input.h.)


// Screens ------------------------------------------------------------------
// Exactly one screen owns the panel at a time. Screens can lie on top of each
// other - the menu on the face, an editor on the menu, a banner on either - and
// closing one uncovers the one beneath. The stack changes only through
// setScreen(), openScreen() and closeScreen().
// (The screens: enum Screen, clock_state.h.)
Screen        screen = SCREEN_BOOT;    // the screen on top, the one on the panel
const uint8_t SCREEN_DEPTH = 3;        // room for the screens beneath it
Screen        screenUnder[SCREEN_DEPTH];
uint8_t       screenUnderCount = 0;
// Screens that close by themselves: after this long without input, or, for a
// banner, after this long at all.
const unsigned long MENU_IDLE_MS    = 20000;   // menu and editors, back to the face
const unsigned long BANNER_MS       = 3000;

// (The menu, its editors, its slides and the banners: menu.h.)

// True while the panel may not show what the current screen would draw: after
// every screen change, a watchface change, a rotation, a brightness change, an
// input the screen shows, every new minute and whenever the press indicator
// flips. The screens that only repaint on a change (the Tetris face, the menu,
// the banner, the hotspot info) repaint when it is set; every full repaint
// clears it.
bool          panelDirty = true;
// Knock gates: one reason and one timestamp each, read only by knockIsReal().
// Declared here rather than with the knock code because setup() and the
// orientation code set them, and those are compiled on every board.
const unsigned long KNOCK_START_QUIET_MS = 3000;  // after setup(): plugging the clock in
const unsigned long KNOCK_AFTER_TURN_MS      = 1200;  // after a turn
unsigned long setupDoneAt  = 0;   // millis() at the end of setup()
unsigned long rotatedAt    = 0;   // millis() when the device rotation last changed
// (The boot breadcrumb's stages: clock_state.h.)
uint8_t prevBootStage = BOOT_STAGE_CLEAR;
bool bootStageCleared = false;        // breadcrumb cleared after the first healthy seconds

// (The hotspot - the clock's own access point - and its captive portal: hotspot.h.)

// (The web server beside the loop: web_server.h.)

// The classic watchface (face_classic.h), on every board, with its settings
// inside the clock's.
ClassicFace classicFace(settings.classic);

// Orientation handling -----------------------------------------------------
// The MatrixPortal S3 has an onboard LIS3DH accelerometer. We read gravity and
// rotate the display in 90 deg steps so the clock always shows the right way up.
Adafruit_LIS3DH lis = Adafruit_LIS3DH();
bool    accelOK = false;          // true once the LIS3DH was found on I2C
Knock   knock;                    // the knock (knock.h): the LIS3DH's, or the wall clock's QMI8658's (knockOK: clock_state.h)
// Two rotations, kept apart on purpose:
// - deviceRotation is how the panel is held: the accelerometer's reading after
//   the debounce, or the default when there is no sensor. Only the orientation
//   code below changes it, never a screen.
// - screenRotation is what the panel is drawn in right now. Most screens draw in
//   the device rotation; the hotspot info screen is landscape however the panel
//   is held, so the two differ while it is up.
const uint8_t ROTATION_NONE = 0xFF;   // sensorRotation(): the sensor cannot tell
uint8_t deviceRotation = BOARD_DEFAULT_ROTATION;   // MatrixPortal: 3 = portrait; wall clock: 0
uint8_t screenRotation = BOARD_DEFAULT_ROTATION;
unsigned long orientLast = 0;     // last accelerometer poll (throttle)
uint8_t orientCandidate = BOARD_DEFAULT_ROTATION;   // debounce: rotation the sensor currently favours
uint8_t orientStable = 0;         // consecutive polls the candidate has held
const unsigned long ORIENT_POLL_MS = 250; // ~4 Hz orientation polling
const uint8_t ORIENT_DEBOUNCE = 3;        // polls a new orientation must persist

// (The light sensor, the brightness it sets and the room climate: brightness.h.)

//Time Related Variables
uint16_t panelHz = 0;            // measured panel refresh rate, 0 until the first measurement
unsigned long panelRateLast = 0; // start of the running refresh-rate measurement
uint8_t hourNow, minuteNow, secondNow;
bool secondTrigger, minuteTrigger, hourTrigger;
unsigned long millisNow, deltaT;
unsigned long secondStartedAt = 0;   // millis() when the second on show began (timekeeper())
time_t localTime;   // what the panel shows: the clock's UTC plus zone and daylight saving
#if defined(CLOCK_DEBUG) || defined(FACE_FRAME_LOG)
long testShift = 0;   // debug and test builds: seconds the panel's time is moved for a test (POST /api/test)
#endif
#if defined(FACE_FRAME_LOG)
// Test builds: the test's own clock (POST /api/test virtual=1). Every pass
// of the loop is TEST_STEP_MS, whatever it really took; the panel's time runs
// from the scene's start, and what the scene asks for happens at its moment
// (plan=). So the same code shows the same frames on every run, which real
// time - a pass early or late, a request landing anywhere in a second - did
// not give (2026-10-20: Tetris differed from the first frame).
const unsigned long TEST_STEP_MS = 5;
bool          testVirtualOn = false;
bool          testVirtualStart = false;                // asked for: the next pass begins the scene
uint8_t       testVirtualReset = 0;                    // ... with the face shown afresh
uint32_t      testVirtualSeed = 1;
unsigned long testVirtualMs = 0, testVirtualMs0 = 0;   // the test's millis(), and where its scene began
time_t        testVirtualLocal = 0;                    // the panel's time at testVirtualMs0
bool          testQuiet = false;                       // no notices and no info cards over the face
String        testPendingPlan;                         // the next scene's steps, taken when it begins
bool          testFrameAnew = false;                   // log the next frame even if it looks like the last
struct TestStep { unsigned long atMs; char what; long arg; };   // ms after the scene's start
TestStep      testPlan[8];
uint8_t       testPlanCount = 0, testPlanNext = 0;
#endif

// (The NTP sync and the real-time clocks: time_sync.h.)

// Credentials a build may bring along (src/arduino_secrets.h, optional): only
// ever copied into flash on the first start of a clock with nothing stored.
#if __has_include("arduino_secrets.h")
  #include "arduino_secrets.h"
#endif



/* ----------------------------------------------------------------------
   The functions of this file, declared ahead of the code so that each
   can be called before its definition. Until 2026-10-22 the sketch was an
   .ino, whose conversion wrote these declarations itself; the five it
   missed stood here and there by hand. They follow the order of the
   functions, under their sections. A part that moves into a module of
   its own takes its declarations along to its header.
   ---------------------------------------------------------------------- */
// setup() and loop()
void setup(void);
void loop(void);
// Screens
bool screenShowsFace(Screen s);
Screen screenBeneath();
void leaveScreen(Screen s);
void showScreen(Screen next);
void setScreen(Screen next);
void openScreen(Screen next);
void closeScreen();
bool inputIdleFor(unsigned long ms);
void updateScreenTimeouts();
// Orientation
void applyOrientation(uint8_t rot);
void setScreenRotation(uint8_t rot);
bool screenIsLandscape();
uint8_t sensorRotation();
void adoptSensorRotation();
void updateOrientation();
void followDeviceRotation();
// Text in the middle of the panel
void drawCenteredText(const char *msg, uint16_t color);
void drawCenteredLines(const char *first, const char *second, uint16_t color);
void bootStatus(const char *msg);
// The panel refresh rate, the debug build's statistics, the marks on the face
void updatePanelRate();
void debugMemoryLog();
void apLoopStats(uint32_t webUs, uint32_t panelUs);
void frameStatsAdd(uint32_t drawUs, uint32_t showUs);
void facePassStats(uint32_t us);
void loopGapStats();
void webStatsPass(uint32_t us);
void webStatsDone(WebConn &w, bool dropped);
void webStatsPrint();
void faceFrameDrawn();
void drawStatusPixel();
void drawHotspotMark();
void drawNoticeMarker();
// The knock: knock.h senses it
bool knockIsReal();
void updateKnock();
void knockFollowSetting();
// Muncher watchface
void muncherSelfTest();
// Aliens watchface
void aliensSelfTest();
void testFaceReset();
void testPlanRun();
void testPlanParse(const String &plan);
// The faces' settings
void loadFaceSettings();
bool saveFaceSettings();
FaceOutcome applyFaceParams(const String &q, bool live, bool previews);
FaceOutcome previewOutcome(FaceOutcome a, FaceOutcome b);
FaceNow faceMoment();
void faceUpdate();
Watchface &faceFor(uint8_t id);
void faceShow();
uint8_t activeWatchface();
void faceCatchUp();
unsigned long secondBeganAt();
void timekeeper(void);
// Settings persistence
void loadSettings();
void applySettings();
uint16_t scaledColorB(uint8_t r, uint8_t g, uint8_t b, uint8_t bright);
uint16_t scaledColor(uint8_t r, uint8_t g, uint8_t b);
uint16_t scaledColorVisible(uint8_t r, uint8_t g, uint8_t b);
void normalizeColorFull(uint8_t &r, uint8_t &g, uint8_t &b);


// SETUP

void setup(void) {
  boardSerialBegin(115200);
#if !CLOCK_CONSOLE
  esp_log_level_set("*", ESP_LOG_NONE);   // and nothing of ESP-IDF's own from here on (console.h)
#endif

  // Boot breadcrumb: what the previous run reached before it ended (see
  // board_hal.h). Read it first, then mark this run as "started".
  prevBootStage = boardBootStageRead();
  boardBootStageWrite(BOOT_STAGE_START);

#if BOARD_BUTTON_COUNT
  pinMode(UP_BUTTON_PIN, INPUT_PULLUP);
  pinMode(DOWN_BUTTON_PIN, INPUT_PULLUP);
#endif
#if BOARD_HAS_FEEDBACK_LED
  pinMode(FEEDBACK_LED_PIN, OUTPUT);
  digitalWrite(FEEDBACK_LED_PIN, LOW);
#endif
  loadSettings(); // pulls brightness/colors/animation/tz from flash (or writes defaults)
  loadUiSettings();
  loadGifSettings();
  loadDecorSettings();
  gifPackLoad();
#if BOARD_WAVESHARE_WALL
  gifCardBegin();         // its task reads the card; until then the pack plays
#endif
  gifGammaInit();
  gifScheduleNext();
  loadWifiCreds();
  loadFeedSettings();
  loadLanSettings();
  loadHaSettings();

  // Onboard LIS3DH accelerometer -> automatic screen rotation. If it is not
  // found the clock simply stays in the default portrait orientation.
  Wire.begin();
#if BOARD_HAS_ACCEL
  // onboard, at a non-standard address
  sensorTries[SENSOR_ACCEL] = sensorBegin(SENSOR_ACCEL, []() { return lis.begin(ACCEL_I2C_ADDR); });
  accelOK = sensorTries[SENSOR_ACCEL] != 0;
#endif
  if (accelOK) {
    lis.setRange(LIS3DH_RANGE_2_G);
    // 100 Hz, not 10: a knock lasts about as long as one 10 Hz sample, so at
    // the old rate it was a coin toss whether the sensor even showed it.
    lis.setDataRate(LIS3DH_DATARATE_100_HZ);
    Console.println("LIS3DH found");
#if !BOARD_WAVESHARE_WALL
    knock.begin(Wire, ACCEL_I2C_ADDR, ACCEL_INT_PIN);   // let the sensor itself watch for a knock
#endif
  } else {
#if BOARD_HAS_ACCEL
    Console.println("LIS3DH not found - orientation locked to portrait");
#else
    Console.println("No LIS3DH on this board - the clock stays in landscape");
#endif
  }

  lightSensorBegin();     // the BH1750 for the auto-brightness, on the same bus

#if BOARD_WAVESHARE_WALL
  // The board's own bus, for its PCF85063 (and its motion sensor).
  if (!boardBusBegin()) { Console.println("Board I2C bus did not start"); }
#endif
  // DS3231 real-time clock on the same bus: the time at once, if it has one,
  // and with it when the last NTP sync was.
  rtcBegin();
  loadSyncRecord();
  // PAJ7620U2 gesture sensor, also on the bus.
  gestureBegin();
  i2cScan();              // after the sensors: a PAJ7620U2 asleep may miss the first call
#if BOARD_WAVESHARE_WALL
  boardBusScan();
  // the board's QMI8658; before loadFaceKeys(), which asks what was found
  sensorTries[SENSOR_MOTION] = sensorBegin(SENSOR_MOTION, []() { return knock.imuBegin(Wire1); });
  knock.begin(Wire1, sensorTries[SENSOR_MOTION] != 0);
  climateBegin();         // and its SHTC3, the room's temperature and humidity
#endif
  sensorsCheck();         // which of those found before are missing now
  loadFaceKeys();         // needs to know which sensors were found
  loadNoticeKeys();       // which of those events close a notice on the panel

  // Initialize matrix...
  matrix.setRotation(BOARD_DEFAULT_ROTATION);
  matrix.setTextWrap(false);      // Allow text off edge
  bool matrixOk = matrix.begin();
  Console.println(matrixOk ? "Panel driver started" : "Panel driver FAILED");
#if defined(CLOCK_DEBUG)
  debugMemoryLog();
#endif
  matrix.setBrightness(panelBrightness());     // the canvas colours are shares of it
  matrix.setRotation(BOARD_DEFAULT_ROTATION);
  if(!matrixOk) {
    // DO NOT CONTINUE if matrix setup encountered an error. The delay keeps the
    // ESP32's task watchdog fed so the board reports the error instead of
    // rebooting in a loop.
    for(;;) { delay(1000); }
  }

  matrix.setTextWrap(false);           // Allow text off edge

  // Why did the previous run end? Shown on the panel for 2 s when it was not a
  // normal power-up or reset button, so a crash is visible without a console
  // (e.g. while the clock runs on a power supply).
  Console.print("Reset reason: "); Console.println(boardResetReasonText());
  if (!boardResetWasNormal()) {
    bootStatus(boardResetReasonText());
    delay(2000);
  }
  // The previous run did not get far enough to clear its breadcrumb.
  if (prevBootStage != BOOT_STAGE_CLEAR) {
    Console.print("Previous boot died in stage "); Console.println(prevBootStage);
    char msg[8];
    snprintf(msg, sizeof(msg), "DIED%u", (unsigned)prevBootStage);
    bootStatus(msg);
    delay(2000);
  }
  // A sensor found at an earlier start is missing (sensorsCheck()): the
  // status pixel stays red for it, the web app says which.
  if (sensorsMissing) {
    bootStatus("SENSOR");
    delay(2000);
  }
  boardBootStageWrite(BOOT_STAGE_PANEL);

  bootStatus("CLOCK");

  // Recovery / manual entry: UP held during boot opens the config AP even when
  // the home WiFi is unavailable (the profile maps EV_UP_AT_BOOT to it). A
  // button held through the start has done its job with that: held on, it
  // would otherwise count as a hold once the loop runs - and holding UP on
  // the hotspot screen closes it again.
#if BOARD_BUTTON_COUNT
  buttonsSpentAtBoot();
  if (digitalRead(UP_BUTTON_PIN) == LOW) {
    handleInput(EV_UP_AT_BOOT, 1);
  }
#endif
  // The same for a hand held over the gesture sensor - the only way on a clock
  // without buttons.
  if (!apActive && gestureHeldAtBoot()) {
    handleInput(EV_APPROACH_AT_BOOT, 1);
  }
#if BOARD_BUTTON_COUNT == 0
  // No buttons and no gesture sensor, and no home WiFi to join: the hotspot is
  // the only way to set the clock up, so it opens and stays open.
  if (!apActive && !gestureOK && !wifiStored()) {
    startAPMode();
    Console.println("No input fitted and no home WiFi stored: the hotspot stays open");
  }
#endif

  // Start joining the home WiFi, without waiting for it: the clock comes up at
  // once and the join and the first sync run in the loop (SYNC_JOINING). The
  // config AP, if it was opened above, has the radio until it closes. With the
  // NTP sync switched off (a clock with a real-time clock only) nothing is due,
  // nor while the time came from the RTC and the interval since the last sync
  // has not run out: then the sync waits for the sync time it falls due at.
  if (!ntpSyncOn()) {
    setSyncState(SYNC_IDLE);
    Console.println("NTP sync off - the clock runs on its real-time clock");
  } else if (ntpSyncedAt != 0 && !syncDueAt(clockNow())) {
    setSyncState(SYNC_IDLE);
    long offset = tzTotalOffset();
    long ago = (long)((clockNow() + offset) / 86400) - (long)((ntpSyncedAt + offset) / 86400);
    Console.printf("NTP sync not due - the last one was %ld day(s) ago, one every %u days\n",
                  ago, (unsigned)uiSettings.syncDays);
  } else if (!apActive && wifiStored()) {
    netRadioInit();
    netPrintRadioInfo();
    boardBootStageWrite(BOOT_STAGE_WIFI);   // the radio's first transmit burst comes now
    stationUpdate();                        // the sync wants the station: the join starts
  } else if (!apActive) {
    Console.println("No home WiFi stored - set one on the config page");
  }
  // The boot screens are done; the config AP, if it was opened above, keeps its own.
  if (screen == SCREEN_BOOT) { setScreen(SCREEN_FACE); }
  // Set last, from the real clock. Knocks are ignored for a moment from here
  // on: that is the handling that comes with plugging the clock in.
  setupDoneAt = millis();
}

// MAIN
void loop(void) {
#if defined(CLOCK_DEBUG)
  loopGapStats();   // how long since the last pass: the jolts a waiting pass makes
#endif
  timekeeper(); // Updates Time variables and gives Triggers for second, minute and hour updates
#if CLOCK_CONSOLE
  consoleCommands();   // the console's commands, in builds with a console (console.h)
#endif
#if defined(FACE_FRAME_LOG)
  testPlanRun();   // a test scene's steps that are due on the test's clock
#endif
#if BOARD_WAVESHARE_WALL
  wallOutputWatch();
  if (matrix.testing()) { delay(5); return; }   // a test picture stands: the clock draws nothing
#endif
#if defined(CLOCK_DEBUG)
  // Ten seconds in, so the console has come back after a restart.
  static bool muncherTested = false;
  if (!muncherTested && millisNow > 10000) { muncherTested = true; muncherSelfTest(); }
#endif
#if defined(CLOCK_DEBUG)
  static bool aliensTested = false;
  if (!aliensTested && millisNow > 10000) { aliensTested = true; aliensSelfTest(); }
#endif
#if defined(CLOCK_DEBUG)
  static bool warningIconsTested = false;
  if (!warningIconsTested && millisNow > 10000) {
    warningIconsTested = true;
    warningIconSelfTest();
    feedRetrySelfTest();
#if BAND_ON_PANEL
    bandSelfTest();
#endif
  }
#endif
  if (minuteTrigger) { panelDirty = true; }   // whatever shows the time has to follow it
  if (secondTrigger && secondNow == 30) { rtcFollow(); }   // half way through every minute
  updatePanelRate();
  // Ran fine for 15 s: clear the breadcrumb, so only a real early death leaves one.
  if (!bootStageCleared && millisNow > 15000) { bootStageCleared = true; boardBootStageWrite(BOOT_STAGE_CLEAR); }
#if defined(CLOCK_DEBUG)
  static bool dstTested = false;
  if (!dstTested && millisNow > 10000) { dstTested = true; dstSelfTest(); }
  debugMemoryLog();
#endif
  updateInput();  // buttons -> events -> whatever the profile maps them to on this screen

  if (apActive) {      // config AP running
#if defined(CLOCK_DEBUG)
    uint32_t apT0 = micros();
#endif
#if BOARD_BUTTON_COUNT == 0
    apAutoClose();     // the hotspot that opened by itself, once its time is up
    if (!apActive) { return; }
#endif
    handleAP();        // captive-portal DNS + web UI + live settings updates
#if defined(CLOCK_DEBUG)
    uint32_t apT1 = micros();
#endif
    updateOrientation();// the info screen and the preview follow the panel as well
    updateBrightness();// same brightness logic during AP (info screen + clock preview)
#if BOARD_WAVESHARE_WALL
    climateUpdate();   // the room's temperature and humidity, for the web app
#endif
    updateApDisplay(); // AP-info screen until a client connects, then a live clock preview
#if BAND_ON_PANEL
    bandHide();        // no band beside the hotspot's screens
#endif
#if defined(CLOCK_DEBUG)
    apLoopStats(apT1 - apT0, micros() - apT1);
#endif
    return;
  }

  stationUpdate();      // the home WiFi while the sync, the info band or the web app needs it
#if BOARD_BUTTON_COUNT == 0
  apFallback();         // no input to open the hotspot with: it opens when the WiFi fails
  if (apActive) { return; }
#endif
  lanUpdate();          // the web app on the home network
  timeSync_WifiLib();
  feedsUpdate();        // the info band's sources, fetched beside the loop
  haUpdate();           // the link to Home Assistant, which runs beside the loop
  if (minuteTrigger) { updateAutoDst(); } // follow the timezone's summer/winter time changes
  updateOrientation();  // rotate the display to match how the panel is held
  updateKnock();        // a knock does what the Inputs page maps it to
  updateBrightness();   // resolve the brightness for every screen (manual or auto)
#if BOARD_WAVESHARE_WALL
  climateUpdate();      // the room's temperature and humidity
#endif
  updateScreenTimeouts();
  panelWatch();         // when the face's change ends, for who takes the panel next
  noticesUpdate();      // a new alert or message comes up, before a GIF that is due
#if BOARD_WAVESHARE_WALL
  gifCardUpdate();      // the card's task: its results, and the next GIF read ahead
#endif
  gifUpdate();          // a GIF of its own accord when one is due
  extrasUpdate();       // the face's extra when one is due
  switch (screen) {
#if defined(CLOCK_DEBUG)
    case SCREEN_FACE: { uint32_t passStart = micros(); faceUpdate(); facePassStats(micros() - passStart); } break;
#else
    case SCREEN_FACE:   faceUpdate(); break;
#endif
    case SCREEN_GIF:    drawGif(); break;
    case SCREEN_MENU:   if (panelDirty) { drawMenu(); }   break;   // static: redrawn on a change only
    case SCREEN_BANNER:   // a running text moves
      if (panelDirty || bannerKind == BANNER_RUN || (bannerKind == BANNER_CARD && cardRuns)) { drawBanner(); }
      break;
    case SCREEN_EDITOR: if (panelDirty || editorMoving()) { drawEditor(); } break;
    case SCREEN_GESTURE_TEST: gestureTestUpdate(); if (panelDirty) { drawGestureTest(); } break;
    default:            break;   // the boot screens belong to setup(), the hotspot ones to updateApDisplay()
  }
#if BAND_ON_PANEL
  bandUpdate();         // the wall's info band below the face
#endif
}

/* ======================================================================
   Screens: the stack and the one way to change it
   ====================================================================== */

// Whether a screen shows the clock face.
bool screenShowsFace(Screen s) {
  return s == SCREEN_FACE || s == SCREEN_HOTSPOT_PREVIEW;
}

// The screen beneath the one on top; the face when there is none.
Screen screenBeneath() {
  return screenUnderCount ? screenUnder[screenUnderCount - 1] : SCREEN_FACE;
}

// The steps of a screen that goes away for good: closed, or cleared from under
// the one on top. Covering a screen is not leaving it.
void leaveScreen(Screen s) {
  if (s == SCREEN_EDITOR) { editorRevert(); }   // what ENTER did not save goes back
  if (s == SCREEN_GIF) { gifEnd(); }
  if (s == SCREEN_BANNER) {                          // the items still to come are off too
    if (noticeCardsRound) { infoCardsEnded(); }
    noticeStepCount = 0;
    noticeCardsRound = false;
  }
}

// The steps of the screen that comes to the top, opened or uncovered again.
void showScreen(Screen next) {
  Screen prev = screen;
  screen = next;
  panelDirty = true;
  DEBUG_LOG("screen %u -> %u (%u beneath)\n", (unsigned)prev, (unsigned)next, (unsigned)screenUnderCount);
  // The hotspot screens leave the panel in a rotation of their own (the info
  // screen is always landscape), so whatever comes next starts again from how
  // the panel is actually held.
  bool fromHotspot = (prev == SCREEN_HOTSPOT_INFO || prev == SCREEN_HOTSPOT_PREVIEW);
  if (fromHotspot && next != SCREEN_HOTSPOT_INFO) { applyOrientation(deviceRotation); }
  // The info screen is static and drawn once, at once - before the slow radio
  // bring-up in startAPMode() freezes the loop.
  if (next == SCREEN_HOTSPOT_INFO) { drawAPScreen(); }
  // A face that was hidden is not stepped, so it catches up before it is drawn.
  if (!screenShowsFace(prev) && screenShowsFace(next)) { faceCatchUp(); }
  if (next == SCREEN_FACE) { panelFaceSince = millisNow; }   // the plain face's rest starts over
  // Between the list and an editor the picture slides; any other change clears
  // a slide that was asked for but never drawn (the editor closing straight
  // into a banner).
  slideDir = (prev == SCREEN_MENU   && next == SCREEN_EDITOR) ? +1
           : (prev == SCREEN_EDITOR && next == SCREEN_MENU)   ? -1 : 0;
  // A knock latched while another screen was up is old news on the face.
  if (next == SCREEN_FACE) { knock.discard(); }
}

// A new screen on its own: everything open is closed (with its leaving steps),
// and nothing lies beneath the new one.
void setScreen(Screen next) {
  if (next == screen && screenUnderCount == 0) { return; }
  leaveScreen(screen);
  while (screenUnderCount) { leaveScreen(screenUnder[--screenUnderCount]); }
  showScreen(next);
}

// A screen on top of the current one, which comes back when it closes.
void openScreen(Screen next) {
  if (screenUnderCount == SCREEN_DEPTH) { setScreen(next); return; }   // deeper than any path today
  screenUnder[screenUnderCount++] = screen;
  showScreen(next);
}

// Close the screen on top and uncover the one beneath.
void closeScreen() {
  if (screenUnderCount == 0) { setScreen(SCREEN_FACE); return; }
  leaveScreen(screen);
  showScreen(screenUnder[--screenUnderCount]);
}

// Whether nobody has touched an input for this long.
bool inputIdleFor(unsigned long ms) {
  return !anyButtonDown() && millisNow - lastInputAt >= ms;
}

// The screens that close by themselves.
void updateScreenTimeouts() {
  switch (screen) {
    case SCREEN_BANNER:
      if (noticeStepCount) {   // a notice's step: then its next one
        if (millisNow - bannerAt >= noticeStepMs) { noticeAdvance(false); }
        break;
      }
      if (millisNow - bannerAt >= BANNER_MS) { closeScreen(); }
      break;
    case SCREEN_MENU:
    case SCREEN_EDITOR:
      if (inputIdleFor(MENU_IDLE_MS)) { Console.println("Menu timed out"); setScreen(SCREEN_FACE); }
      break;
    case SCREEN_GESTURE_TEST:
      if (millisNow - gestureTestAt >= GESTURE_TEST_MS) { Console.println("Gesture test: timed out"); gestureTestStop(); }
      break;
    case SCREEN_GIF:
      if (millisNow - gifShownAt >= GIF_PLAY_MAX_MS) { closeScreen(); }
      break;
    default:
      break;
  }
}


/* ======================================================================
   Orientation: read the accelerometer and rotate the display in 90 deg steps
   ====================================================================== */

// Switch the matrix to rotation "rot", and the classic face to its layout for
// it. Rotations 1/3 are portrait (32x64, stacked HH/MM/SS); rotations 0/2 are
// landscape (64x32, HH/MM large + SS small below). The fly-in directions mean
// the same in both: a digit set to come from the top comes from the top of the
// panel as it is held (until 2026-10-08 landscape swapped them, but only until
// the next save, which set them back). Digits snap to the new layout so an
// in-flight animation never streaks across the screen during a rotation. The
// other faces see the turn by themselves, on their next pass.
void applyOrientation(uint8_t rot) {
  setScreenRotation(rot);
  classicFace.enter(faceMoment(), ENTER_TURNED);
}

// The one place the panel's drawing rotation is set.
void setScreenRotation(uint8_t rot) {
  if (rot != screenRotation) { panelDirty = true; }   // the panel still shows the old rotation
  matrix.setRotation(rot);
  screenRotation = rot;
}

// Whether the panel is drawn 64 wide x 32 tall right now (rotations 0 and 2).
bool screenIsLandscape() {
  return screenRotation == 0 || screenRotation == 2;
}

// One-shot orientation read: the rotation (0..3) gravity points to, or
// ROTATION_NONE when the reading is undecided (panel flat / near the 45 deg
// diagonal) or there is no sensor. Gravity in the panel plane picks one of four
// quadrants. What "undecided" means is up to the caller - it is never mixed up
// with the rotation some screen happens to be drawn in.
uint8_t sensorRotation() {
  if (!accelOK) { return ROTATION_NONE; }
  lis.read();
  int16_t ax = lis.x, ay = lis.y;
  int16_t axAbs = abs(ax), ayAbs = abs(ay);
  int16_t hi = max(axAbs, ayAbs), lo = min(axAbs, ayAbs);
  // Too flat (panel face up/down) or too close to the diagonal -> undecided.
  // Require the dominant axis to be >25% larger: hi > lo*1.25  <=>  hi*4 > lo*5.
  if (hi < 2000 || (int32_t)hi * 4 <= (int32_t)lo * 5) { return ROTATION_NONE; }
  // Gravity quadrant: 0=+X down, 1=-X down, 2=+Y down, 3=-Y down.
  uint8_t quadrant;
  if (axAbs > ayAbs) { quadrant = (ax > 0) ? 0 : 1; }
  else               { quadrant = (ay > 0) ? 2 : 3; }
  // quadrant -> matrix rotation. Re-order these four values if the panel turns
  // the wrong way during on-device calibration (this is the one line to tweak).
  static const uint8_t ORIENT_MAP[4] = {3, 1, 0, 2};
  return ORIENT_MAP[quadrant];
}

// Take the sensor's reading as the device rotation at once, without the
// debounce. For the boot screens, which come and go faster than it settles.
void adoptSensorRotation() {
  uint8_t r = sensorRotation();
  if (r == ROTATION_NONE) { return; }   // undecided: keep what we have
  deviceRotation  = r;
  orientCandidate = r;                  // the debounce carries on from here
  orientStable    = 0;
}

// Poll the LIS3DH (throttled) and update the device rotation, with a debounce so
// it doesn't flicker near 45 deg. Runs on every screen; each screen then follows
// the new rotation in its own way.
void updateOrientation() {
  if (!accelOK) { return; }
  if (millisNow - orientLast < ORIENT_POLL_MS) { return; }   // throttle to ~4 Hz
  orientLast = millisNow;
  uint8_t sensed = sensorRotation();
  uint8_t wanted = (sensed == ROTATION_NONE) ? deviceRotation : sensed;   // undecided: keep it
  // Debounce: a new orientation must persist a few polls before we commit.
  if (wanted == orientCandidate) { if (orientStable < 255) { orientStable++; } }
  else { orientCandidate = wanted; orientStable = 1; }
  if (wanted != deviceRotation && orientStable >= ORIENT_DEBOUNCE) {
    Console.print("Orientation -> rotation "); Console.println(wanted);
    deviceRotation = wanted;
    rotatedAt = millisNow;   // turning is not a knock
    followDeviceRotation();
  }
}

// The screen on the panel takes up a new device rotation. The hotspot info is
// always landscape, so it only flips the right way up; everything else turns
// with the panel.
void followDeviceRotation() {
  if (screen == SCREEN_HOTSPOT_INFO) { drawAPScreen(); }
  else                               { applyOrientation(deviceRotation); }
}



// Draw a short text centred in the current rotation's canvas. Used by the boot
// status screens and the banners so they align like the clock.
void drawCenteredText(const char *msg, uint16_t color) {
  drawCenteredLines(msg, nullptr, color);
}

// One or two lines of Picopixel, centred. A single line is centred on its own
// bounds; two lines sit one 8 px line pitch apart around the middle.
void drawCenteredLines(const char *first, const char *second, uint16_t color) {
  matrix.setFont(&Picopixel);
  matrix.setTextSize(1);
  matrix.fillScreen(0);
  matrix.setTextColor(color);
  int16_t x1, y1; uint16_t w, h;
  if (second == nullptr) {
    matrix.getTextBounds(first, 0, 0, &x1, &y1, &w, &h);
    matrix.setCursor((matrix.width() - (int)w) / 2 - x1, (matrix.height() - (int)h) / 2 - y1);
    matrix.print(first);
  } else {
    const char *lines[2] = { first, second };
    int16_t baseline = matrix.height() / 2 - 2;   // capitals 5 px tall: rows mid-7 .. mid+6 in all
    for (const char *line : lines) {
      matrix.getTextBounds(line, 0, 0, &x1, &y1, &w, &h);
      matrix.setCursor((matrix.width() - (int)w) / 2 - x1, baseline);
      matrix.print(line);
      baseline += 8;
    }
  }
  drawFeedbackIndicator();
  matrix.show();
  panelDirty = false;   // the whole panel shows this text now
}

// Boot status line: oriented to the panel and dimmed to the live clock brightness
// (sensor-driven when auto-brightness is on), exactly like every other screen.
void bootStatus(const char *msg) {
  millisNow = millis();                // setup() runs before timekeeper(), so refresh the clock
  adoptSensorRotation();               // follow the accelerometer
  applyOrientation(deviceRotation);
  updateBrightness();                  // same brightness logic as the clock
  drawCenteredText(msg, scaledColor(255, 255, 255));
}



// The panel refresh rate once per second: the one the DMA driver calculated.
void updatePanelRate() {
  if (millisNow - panelRateLast < 1000) { return; }
  uint32_t refreshes = matrix.getFrameCount();
  if (panelRateLast != 0) { panelHz = refreshes * 1000UL / (millisNow - panelRateLast); }
  panelRateLast = millisNow;
}

#if defined(CLOCK_DEBUG)
// Debug build only: frame timing, printed once per second. "frames" is how often
// a face was drawn, "panel" the panel's refresh rate (updatePanelRate()),
// "draw" the time from the face's pass to the frame (faceDrawStart) and "show"
// the time show() spent handing it over (with double buffering it waits for
// the next panel refresh).
struct FrameStats { uint32_t frames, drawSum, drawMax, showSum, showMax, since; };
FrameStats frameStats = {};
uint32_t faceDrawStart = 0;   // micros() when faceUpdate() began the face's pass

// Debug build only: the RAM every 30 s - internal free, its lowest since the
// start and its largest free block, and the free PSRAM. The panel driver's
// buffer, the WiFi and the TLS sessions all want internal RAM.
void debugMemoryLog() {
  static unsigned long last = 0;
  if (last != 0 && millisNow - last < 30000) { return; }
  last = millisNow ? millisNow : 1;
  if (consoleLevel < 2) { return; }   // a running figure (console.h)
  Console.printf("RAM: internal free %u, lowest %u, largest block %u; PSRAM free %u\n",
                (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                (unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL),
                (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL),
                (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
}

// Debug build only: the loop while the hotspot is up, printed every 2 s - how
// often it ran, how long the web side (handleAP) and the panel side (preview)
// took on average and at most, and the free internal RAM.
void apLoopStats(uint32_t webUs, uint32_t panelUs) {
  static uint32_t passes = 0, webSum = 0, webMax = 0, panelSum = 0, panelMax = 0, since = 0;
  passes++;
  webSum += webUs;     if (webUs > webMax)     { webMax = webUs; }
  panelSum += panelUs; if (panelUs > panelMax) { panelMax = panelUs; }
  if (millis() - since < 2000) { return; }
  if (consoleLevel >= 2) {   // a running figure (console.h)
    Console.printf("AP loop: %lu passes, web avg %lu max %lu us, panel avg %lu max %lu us, "
                  "RAM free %u (min %u, largest %u)\n",
                  (unsigned long)passes, (unsigned long)(webSum / passes), (unsigned long)webMax,
                  (unsigned long)(panelSum / passes), (unsigned long)panelMax,
                  (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                  (unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL),
                  (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
  }
  passes = webSum = webMax = panelSum = panelMax = 0;
  since = millis();
}

void frameStatsAdd(uint32_t drawUs, uint32_t showUs) {
  FrameStats &f = frameStats;
  f.frames++;
  f.drawSum += drawUs; if (drawUs > f.drawMax) { f.drawMax = drawUs; }
  f.showSum += showUs; if (showUs > f.showMax) { f.showMax = showUs; }
  if (millis() - f.since < 1000) { return; }
  if (consoleLevel >= 2) {   // a running figure (console.h)
    Console.printf("frames %lu/s panel %lu Hz draw avg %lu max %lu us show avg %lu max %lu us\n",
                  (unsigned long)f.frames, (unsigned long)panelHz,
                  (unsigned long)(f.drawSum / f.frames), (unsigned long)f.drawMax,
                  (unsigned long)(f.showSum / f.frames), (unsigned long)f.showMax);
  }
  f = FrameStats{};
  f.since = millis();
}

// Debug build: the face's passes, printed every 2 s - how often the loop ran
// it, how long a pass took on average and at most (handing a frame over and
// waiting for the panel included), and how many frames it drew.
uint32_t faceDraws = 0;
void facePassStats(uint32_t us) {
  static uint32_t passes = 0, sum = 0, most = 0, since = 0;
  passes++;
  sum += us; if (us > most) { most = us; }
  if (millis() - since < 2000) { return; }
  if (consoleLevel >= 2) {   // a running figure (console.h)
    Console.printf("face %u: %lu passes, avg %lu max %lu us, %lu frames\n", (unsigned)settings.watchface,
                  (unsigned long)passes, (unsigned long)(sum / passes), (unsigned long)most, (unsigned long)faceDraws);
  }
  passes = sum = most = faceDraws = 0;
  since = millis();
}

// Debug build: the time from one pass of the loop to the next, printed every
// 2 s - the longest, and how many took over 20 ms (a jolt on the panel). A
// pass that waits for anything shows here, the web server's among them.
void loopGapStats() {
  static uint32_t last = 0, passes = 0, most = 0, slow = 0, since = 0;
  uint32_t now = micros();
  if (last != 0) {
    uint32_t gap = now - last;
    passes++;
    if (gap > most) { most = gap; }
    if (gap > 20000) { slow++; }
  }
  last = now;
  if (millis() - since < 2000) { return; }
  if (consoleLevel >= 2 && passes) {   // a running figure (console.h)
    Console.printf("loop: %lu passes, longest %lu us apart, %lu over 20 ms\n",
                   (unsigned long)passes, (unsigned long)most, (unsigned long)slow);
  }
  passes = most = slow = 0;
  since = millis();
  last = micros();   // the print above is not the loop's
}

// Debug build: the web server, printed every 2 s while it has work - the
// requests answered and the connections dropped, its steps (one connection a
// pass) on average and at most, and the longest request from its connection
// to its close.
struct WebStats { uint32_t answered, dropped, steps, stepSum, stepMax, longestMs, since; };
WebStats webStats = {};

void webStatsPass(uint32_t us) {
  webStats.steps++;
  webStats.stepSum += us;
  if (us > webStats.stepMax) { webStats.stepMax = us; }
}

void webStatsDone(WebConn &w, bool dropped) {
  if (dropped) { webStats.dropped++; } else { webStats.answered++; }
  uint32_t ms = millisNow - w.acceptedAt;
  if (ms > webStats.longestMs) { webStats.longestMs = ms; }
}

void webStatsPrint() {
  if (millis() - webStats.since < 2000) { return; }
  if (consoleLevel >= 2 && webStats.steps) {   // a running figure (console.h)
    Console.printf("web: %lu answered, %lu dropped; steps avg %lu max %lu us; longest request %lu ms\n",
                   (unsigned long)webStats.answered, (unsigned long)webStats.dropped,
                   (unsigned long)(webStats.stepSum / webStats.steps), (unsigned long)webStats.stepMax,
                   (unsigned long)webStats.longestMs);
  }
  webStats = WebStats{};
  webStats.since = millis();
}
#endif

#if defined(CLOCK_DEBUG) || defined(FACE_FRAME_LOG)
// Debug and test builds: a face drew a frame, before the marks go over it.
// Counted for facePassStats(); a test build (FACE_FRAME_LOG) also logs it as a
// checksum of the canvas whenever that differs from the last one -
// "F hh:mm:ss.mmm crc face" - so two runs of the same scenes, before and after
// a step of the refactor, show whether every frame stayed as it was
// (tools.local/refactor).
void faceFrameDrawn() {
#if defined(CLOCK_DEBUG)
  faceDraws++;
#endif
#if defined(FACE_FRAME_LOG)
  static uint32_t last = 0;
  uint32_t crc = faceCrc32((const uint8_t *)matrix.getBuffer(), (size_t)matrix.width() * matrix.height() * 2);
  if (crc == last && !testFrameAnew) { return; }
  last = crc;
  testFrameAnew = false;
  char line[56];
  int n = snprintf(line, sizeof(line), "F %02u:%02u:%02u.%03lu %08lx %u", (unsigned)hourNow, (unsigned)minuteNow,
                   (unsigned)secondNow, (unsigned long)(millisNow - secondStartedAt), (unsigned long)crc,
                   (unsigned)settings.watchface);
  if (testVirtualOn && n > 0 && n < (int)sizeof(line)) {   // and the ms into the scene, on the test's clock
    snprintf(line + n, sizeof(line) - n, " %lu", (unsigned long)(millisNow - testVirtualMs0));
  }
  Console.println(line);
#endif
}
#endif

// Status pixel: off in normal operation, red only on an error - a due sync
// that has not succeeded within SYNC_LATE_MS, or the first failure of a WiFi
// config that has never worked yet (syncOverdue()), then until a sync
// succeeds; or a sensor found at an earlier start that is missing now
// (sensorsCheck()), until it is back or the web app forgets it.
// It sits in the bottom-left corner of the current rotation: (0,63) in
// portrait, (0,31) in landscape. A fixed (0,63) lies outside the 32 px tall
// landscape canvas and was silently clipped, so landscape never showed the sync
// status. Every watchface draws it the same way.
//
// Dimmed with the master brightness, but never below STATUS_PIXEL_MIN:
// color565() keeps only the top five bits of red, so any red under 8 comes out
// black. With the old floor of 3 the red state vanished below a master
// brightness of 48, while the old green one (six bits) still showed.
const uint8_t STATUS_PIXEL_MIN = 24;
void drawStatusPixel() {
  if (!syncOverdue() && !sensorsMissing) { return; }
  uint8_t level = effectiveBrightness / 6;
  if (level < STATUS_PIXEL_MIN) { level = STATUS_PIXEL_MIN; }
  // Through scaledColorB(), as every colour: on the S3 the panel itself dims,
  // and a red of `level` drawn as it is would be dimmed twice.
  matrix.drawPixel(0, matrix.height() - 1, scaledColorB(255, 0, 0, level));
}

// While the hotspot is up and the panel previews the clock for the phone, a
// small blue square blinks in the top right corner: the clock looks as usual,
// but only holding UP (close the hotspot) works.
void drawHotspotMark() {
  if (screen != SCREEN_HOTSPOT_PREVIEW || (localTime % 2) != 0) { return; }
  uint8_t level = effectiveBrightness / 3;
  if (level < STATUS_PIXEL_MIN) { level = STATUS_PIXEL_MIN; }
  matrix.fillRect(matrix.width() - 2, 0, 2, 2, scaledColorB(0, 80, 255, level));
}

// While there are notices, a small square in the same corner of the face: in
// the alert colour while an alert holds, white while there are messages
// alone, so it shows that "show notifications" has something to show. Not
// in the hotspot's preview: the hotspot's mark has the corner then.
void drawNoticeMarker() {
  if (noticeMark == MARK_NONE || screen != SCREEN_FACE) { return; }
  uint8_t level = effectiveBrightness / 3;
  if (level < STATUS_PIXEL_MIN) { level = STATUS_PIXEL_MIN; }
  uint16_t c = noticeMark == MARK_ALERT ? scaledColorB(ALERT_R, ALERT_G, ALERT_B, level) : scaledColorB(255, 255, 255, level);
  matrix.fillRect(matrix.width() - 2, 0, 2, 2, c);
}

/* ======================================================================
   Tetris watchface (face_tetris.h): its settings, stored in a blob of their
   own, and the face.
   ====================================================================== */
static_assert(TETRIS_KNOCK_LEVEL_MAX == KNOCK_LEVEL_MAX, "the stored knock sensitivity and knock.h agree on its range");

TetrisSettings tetrisSettings;
FaceBlob tetrisBlob = { "tetris", TETRIS_SETTINGS_MAGIC, TETRIS_SETTINGS_REV, WATCHFACE_TETRIS_ID, sizeof(TetrisSettings),
                        SETTING_TABLE(tetrisSettings, TETRIS_DEFAULTS, TETRIS_PARAMS), tetrisMigrate };
TetrisFace tetrisFace(tetrisSettings);

/* ----------------------------------------------------------------------
   The knock: knock.h senses it; here, whether it counts, and on to the
   inputs as EV_KNOCK. Its sensitivity is the Tetris face's setting shk.
   ------------------------------------------------------------------------- */
// Whether the sensor's knock is worth reporting. These are the reasons it
// fires without anyone knocking; one per line, each reading one explicit
// state or one gate with one reason. What a knock then does is the profile's
// business.
bool knockIsReal() {
  if (knock.level() == 0)                              { return false; }  // switched off
  if (anyButtonDown())                                 { return false; }  // a button press IS a knock: the
                                                                          // switches sit 20 mm from the sensor
  if (millisNow - lastInputAt  < INPUT_SETTLE_MS)      { return false; }  // hands still on the clock
  if (orientCandidate != deviceRotation)               { return false; }  // a rotation is being debounced
  if (millisNow - setupDoneAt  < KNOCK_START_QUIET_MS) { return false; }  // just plugged in
  if (millisNow - rotatedAt    < KNOCK_AFTER_TURN_MS)      { return false; }  // turning is not a knock
  return true;
}

// On every pass outside the hotspot.
void updateKnock() {
  if (knock.sensed(millisNow) && knockIsReal()) { handleInput(EV_KNOCK, 1); }
}

// The sensitivity setting into the sensor, wherever it changes.
void knockFollowSetting() {
  knock.setLevel(tetrisSettings.knockLevel);
}

/* ======================================================================
   Muncher watchface (face_muncher.h): its settings, stored in a blob of
   their own, and the face.
   ====================================================================== */
MuncherSettings muncherSettings;
FaceBlob muncherBlob = { "muncher", MUNCHER_SETTINGS_MAGIC, MUNCHER_SETTINGS_REV, WATCHFACE_MUNCHER_ID, sizeof(MuncherSettings),
                         SETTING_TABLE(muncherSettings, MUNCHER_DEFAULTS, MUNCHER_PARAMS), muncherMigrate };
MuncherFace muncherFace(muncherSettings);

#if defined(CLOCK_DEBUG)
// Every change of a digit walked through, at start-up: see muncher::selfTest().
void muncherSelfTest() {
  muncher::Face f;
  uint16_t longest = 0, failed = 0;
  unsigned long t0 = millis();
  bool ok = muncher::selfTest(f, longest, failed);
  Console.printf("Muncher self-test: %s, longest digit walk %u steps, %lu ms", ok ? "all changes right" : "FAILED",
                longest, millis() - t0);
  if (!ok) { Console.printf(", first wrong: %u", failed); }
  Console.println();
  // Where the power pellet's scene catches the ghosts, and how long it takes.
  const muncher::Layout *layouts[2] = { &muncher::LANDSCAPE, &muncher::PORTRAIT };
  for (uint8_t l = 0; l < 2; l++) {
    uint8_t d[4] = { 1, 2, 3, 4 };
    f.setLayout(*layouts[l]);
    f.show(d);
    f.planPower();
    f.start(muncher::MODE_POWER);
    uint16_t n = 0;
    while (f.mode != muncher::MODE_IDLE && n < 3000) { f.advanceTo(++n); }
    Console.printf("Muncher power pellet scene, %s: %u caught", l ? "upright" : "landscape", f.caughtCount);
    for (uint8_t g = 0; g < f.caughtCount; g++) { Console.printf(" at x %d", f.caughtX[g]); }
    Console.printf(", %u steps\n", n);
  }
}
#endif

/* ======================================================================
   Aliens watchface (face_aliens.h): its settings, stored in a blob of their
   own, and the face.
   ====================================================================== */
AliensSettings aliensSettings;
FaceBlob aliensBlob = { "aliens", ALIENS_SETTINGS_MAGIC, ALIENS_SETTINGS_REV, WATCHFACE_ALIENS_ID, sizeof(AliensSettings),
                        SETTING_TABLE(aliensSettings, ALIENS_DEFAULTS, ALIENS_PARAMS), aliensMigrate };
AliensFace aliensFace(aliensSettings);

#if defined(CLOCK_DEBUG)
void aliensSelfTest() {
  aliens::Face f;
  uint32_t longest = 0, longestScene = 0;
  uint16_t failed = 0;
  unsigned long t0 = millis();
  bool ok = aliens::selfTest(f, longest, longestScene, failed);   // every change, the wave, the saucer, shooting back, a nap
  Console.printf("Aliens self-test: %s, longest change %lu ms, longest scene between changes %lu ms, %lu ms",
                ok ? "all changes and scenes right" : "FAILED", (unsigned long)longest, (unsigned long)longestScene,
                millis() - t0);
  if (!ok) { Console.printf(", first wrong: %u", failed); }
  Console.println();
}
#endif

#if defined(FACE_FRAME_LOG)
/* Test builds: a scene of tools.local/refactor starts with the face shown
   afresh, as on a turn of the panel - the time at once, nothing running,
   nothing asked for, every rest over - so it starts the same way on every
   run; its steps (plan=) happen at their moment on the test's clock. */
void testFaceReset() {
  faceCatchUp();
  tetrisFace.testReset();
  muncherFace.testReset();
  aliensFace.testReset();
  panelDirty = true;
}

void testPlanRun() {
  while (testVirtualOn && testPlanNext < testPlanCount && millisNow - testVirtualMs0 >= testPlan[testPlanNext].atMs) {
    const TestStep &s = testPlan[testPlanNext++];
    if (s.what == 'a') { faceAnimationStart(); }                        // "trigger watchface animation"
    else if (s.what == 'e') { extrasDueAt = millisNow ? millisNow : 1; }   // an extra due now
    else if (s.what == 's') { testShift += s.arg; panelDirty = true; }    // the panel's time jumps
  }
}

// plan=a2000,s8000:60,e25000: a letter, the ms after the scene's start, an argument.
void testPlanParse(const String &plan) {
  testPlanCount = testPlanNext = 0;
  int from = 0;
  while (from < (int)plan.length() && testPlanCount < sizeof(testPlan) / sizeof(testPlan[0])) {
    int to = plan.indexOf(',', from);
    if (to < 0) { to = plan.length(); }
    String step = plan.substring(from, to);
    int colon = step.indexOf(':');
    TestStep &s = testPlan[testPlanCount++];
    s.what = step.charAt(0);
    s.atMs = (unsigned long)step.substring(1, colon < 0 ? step.length() : colon).toInt();
    s.arg = colon < 0 ? 0 : step.substring(colon + 1).toInt();
    from = to + 1;
  }
}
#endif

#if defined(CLOCK_DEBUG) || defined(FACE_FRAME_LOG)
// POST /api/test, in debug and test builds: what tools.local/refactor needs
// to play the same scenes again. shift= moves the panel's time by that many
// seconds (RAM only: the clock, its RTC and the sync keep the real time);
// seed= sets the faces' random numbers (test builds); quiet=1 keeps GIFs and
// extras from coming of their own accord until a restart; extra=1 makes an
// extra due now; anim=1 runs "trigger watchface animation", as a knock does.
void apiTest(Print &c, const String &body) {
  String v = getParam(body, "shift");
  if (v.length()) { testShift = v.toInt(); panelDirty = true; }
#if defined(FACE_FRAME_LOG)
  v = getParam(body, "seed");
  if (v.length()) { FaceRandom::seed((uint32_t)strtoul(v.c_str(), nullptr, 10)); }
  // virtual=1&start=<local time>[&plan=..][&reset=1]: a scene on the test's
  // own clock from the panel time `start`; virtual=0 back to the real one
  // (millis then jumps: restart the clock after a test).
  v = getParam(body, "virtual");
  if (v == "1") {   // taken on the next pass, so the face sees nothing of this one's real time
    testVirtualStart = true;
    testVirtualLocal = (time_t)strtoll(getParam(body, "start").c_str(), nullptr, 10);
    testVirtualReset = getParam(body, "reset") == "1";
    String s = getParam(body, "seed");
    testVirtualSeed = s.length() ? (uint32_t)strtoul(s.c_str(), nullptr, 10) : 1;
    testPendingPlan = getParam(body, "plan");
    testPlanCount = testPlanNext = 0;   // the scene still running asks for nothing more
  } else if (v == "0") {
    testVirtualOn = false;
    testPlanCount = testPlanNext = 0;
  }
  if (getParam(body, "quiet") == "1") { testQuiet = true; }
#endif
  if (getParam(body, "quiet") == "1") {
    gifSettings.randomOn = 0;
    decorSettings.extras = 1;                    // the rarest pace, so extra=1 still has one to give
    extrasDueAt = millisNow + 3600000UL;
  }
  if (getParam(body, "extra") == "1") { extrasDueAt = millisNow ? millisNow : 1; }
  if (getParam(body, "anim") == "1") { faceAnimationStart(); }
  JsonDocument d;
  d["shift"] = testShift;
  d["local"] = (long long)localTime;
#if defined(FACE_FRAME_LOG)
  d["virtual"] = testVirtualOn;
  d["sceneMs"] = testVirtualOn ? (unsigned long)(millisNow - testVirtualMs0) : 0UL;
#endif
  sendJson(c, d);
}
#endif

/* ----------------------------------------------------------------------
   The faces' settings: each face's blob and its table (face.h), loaded,
   saved, taken from the web app and reported the same way for all.
   ------------------------------------------------------------------------- */
FaceBlob *const FACE_BLOBS[] = {
  &tetrisBlob,
  &muncherBlob,
  &aliensBlob,
};
const uint8_t FACE_BLOB_COUNT = sizeof(FACE_BLOBS) / sizeof(FACE_BLOBS[0]);
SettingsBlobStore faceStores[FACE_BLOB_COUNT];

// Every table of settings (settings_table.h), for /api/state: the values, the
// names of the choices and what the web app needs to send them back.
SettingsTable *const SETTINGS_TABLES[] = {
  &clockTable, &tetrisBlob.table, &muncherBlob.table, &aliensBlob.table, &uiTable, &gifTable, &decorTable, &feedTable,
#if BOARD_WAVESHARE_WALL
  &sensorTable,
#endif
};
const uint8_t SETTINGS_TABLE_COUNT = sizeof(SETTINGS_TABLES) / sizeof(SETTINGS_TABLES[0]);

void loadFaceSettings() {
  for (uint8_t i = 0; i < FACE_BLOB_COUNT; i++) { faceBlobLoad(*FACE_BLOBS[i], faceStores[i]); }
  knockFollowSetting();    // the sensor, not the sketch, holds the threshold
}

bool saveFaceSettings() {
  bool stored = true;
  for (uint8_t i = 0; i < FACE_BLOB_COUNT; i++) { stored &= faceBlobSave(*FACE_BLOBS[i], faceStores[i]); }
  return stored;
}

// The web app's face settings, live or to be saved. A setting that redraws
// redraws, the knock's sensitivity goes to the sensor, and - with previews,
// live on a face not just chosen, on the face shown - a new watchface
// animation plays once, and pv asks to see what a control changed: now or not
// at all, and the page is told which. The page says outright when such a control was used:
// comparing the values instead, as this did at first, was unreliable, since
// the updates sent while a slider was dragged had already pushed the final
// value across, and the release looked like no change at all.
FaceOutcome applyFaceParams(const String &q, bool live, bool previews) {
  FaceOutcome shown = OUTCOME_NONE;
  for (uint8_t i = 0; i < FACE_BLOB_COUNT; i++) {
    FaceBlob &b = *FACE_BLOBS[i];
    uint8_t came = settingsApply(b.table, [&q](const char *key) { return getParam(q, key); }, live);
    if (came & PARAM_REDRAW) { panelDirty = true; }
    if (came & PARAM_KNOCK)  { knockFollowSetting(); }
    if (previews && (came & PARAM_PLAYS) && settings.watchface == b.face) {
      shown = previewOutcome(shown, faceFor(b.face).request(faceMoment(), TRIGGER_PREVIEW_ANIMATION));
    }
  }
  if (!previews || !getParam(q, "pv").length()) { return shown; }
  return previewOutcome(shown, faceFor(settings.watchface).request(faceMoment(), TRIGGER_PREVIEW_CHANGE));
}

// What the page is told of two previews asked for at once: one played, or
// one refused, or none of them asked.
FaceOutcome previewOutcome(FaceOutcome a, FaceOutcome b) {
  if (a == OUTCOME_PLAYED || b == OUTCOME_PLAYED)   { return OUTCOME_PLAYED; }
  if (a == OUTCOME_REFUSED || b == OUTCOME_REFUSED) { return OUTCOME_REFUSED; }
  return OUTCOME_NONE;
}

// The moment for the face: the timekeeper's time as it stands now. Built
// where the face is stepped or asked, not once per pass, since whether the
// clock has a time can change within a pass (a sync).
FaceNow faceMoment() {
  FaceNow n;
  n.ms         = millisNow;
  n.secondAt   = secondStartedAt;
  n.local      = localTime;
  n.set        = clockIsSet();
  n.hour       = hourNow;
  n.minute     = minuteNow;
  n.second     = secondNow;
  n.secondTick = secondTrigger;
  n.minuteTick = minuteTrigger;
  n.hourTick   = hourTrigger;
  n.turn       = screenRotation;
  n.w          = matrix.width();
  n.h          = matrix.height();
  return n;
}

// One pass of the face on the panel: the plain face, or its preview while the
// hotspot is up and a phone is on it. The face is stepped on every pass, so it
// runs at real time, and drawn when its picture changed or the panel no longer
// shows it (panelDirty).
void faceUpdate() {
#if defined(CLOCK_DEBUG)
  faceDrawStart = micros();
#endif
  Watchface &face = faceFor(activeWatchface());
  FaceNow now = faceMoment();
  bool changed = face.update(now);
  if (!changed && !panelDirty) { return; }
  face.draw(matrix, now, FacePaint{effectiveBrightness});
  faceShow();
}

// The face of a watchface number (Settings::watchface); the classic one for
// any this build does not have.
Watchface &faceFor(uint8_t id) {
  switch (id) {
    case WATCHFACE_TETRIS_ID:  return tetrisFace;
    case WATCHFACE_MUNCHER_ID: return muncherFace;
    case WATCHFACE_ALIENS_ID:  return aliensFace;
    default:                   return classicFace;
  }
}

// A face has drawn its picture: the marks go over it - the status pixel, the
// hotspot's mark, the notices' marker, an input's feedback - and it goes to
// the panel.
void faceShow() {
#if defined(CLOCK_DEBUG) || defined(FACE_FRAME_LOG)
  faceFrameDrawn();   // counted, and in a test build logged as a checksum, before the marks go over it
#endif
  drawStatusPixel();
  drawHotspotMark();
  drawNoticeMarker();
  drawFeedbackIndicator();
#if defined(CLOCK_DEBUG)
  uint32_t showStart = micros();
#endif
  matrix.show();
  panelDirty = false;
#if defined(CLOCK_DEBUG)
  frameStatsAdd(showStart - faceDrawStart, micros() - showStart);
#endif
}

// The watchface to draw. It can change at runtime (the config page previews the
// choice live), and only the visible face is stepped, so whichever takes over is
// resynchronised here: the panel turned as it is held and the classic face
// up to the time (faceCatchUp()), and the face chosen told so - the Tetris
// one rebuilds its layout and drops its digits again, the Muncher and the
// Aliens show the time at once.
uint8_t activeWatchface() {
  static uint8_t shown = 0xFF;
  if (settings.watchface == shown) { return shown; }
  shown = settings.watchface;
  faceCatchUp();
  faceFor(shown).enter(faceMoment(), ENTER_CHOSEN);
  panelDirty = true;               // the other face's picture is still on the panel
  return shown;
}

// Bring the classic face up to the time before it is drawn again, after it was
// hidden or replaced, with the panel turned as it is held: no fly-in from
// before resumes. The Tetris face catches up by itself, digit by digit.
void faceCatchUp() {
  setScreenRotation(deviceRotation);
  classicFace.enter(faceMoment(), ENTER_SHOWN);
}

// The millis() the second now on the panel began at: the clock's own, which
// knows it to the millisecond (clock_time.h), not the pass that noticed it -
// after a long pass (a WiFi scan, an upload) that was a second or more late,
// and the changes planned from it as late (code review of 2026-10-14). Never
// after this pass's millisNow: the clock reads millis() a moment later. The
// test's own clock keeps its passes' time.
unsigned long secondBeganAt() {
#if defined(FACE_FRAME_LOG)
  if (testVirtualOn) { return millisNow; }
#endif
  unsigned long at = clockState().millisAt;
  return (long)(at - millisNow) > 0 ? millisNow : at;
}

/* Updates millisNow, localTime and the ticks. Keeps track of the looptime using delay(). */
void timekeeper(void) {
  /*Call this always in the beginning of an iteration in loop()
    for scheduling of short actions use millisNow; for long-term schedules use hourNow, minuteNow, secondNow or plain localTime with the clock_time.h helpers eg: clockHour(localTime)
    Triggers are only active for one iteration: hourTrigger -> when the hour has changed
*/
  deltaT=millisNow;
  millisNow=millis();
  deltaT=millisNow-deltaT; // duration of last iteration in ms
  delay(1); // show() paces the loop to the panel refresh; just give other tasks a turn
  
  millisNow=millis();
  localTime=clockNow() + tzTotalOffset();   // the clock keeps UTC; the panel shows local time
#if defined(FACE_FRAME_LOG)
  bool testBegins = testVirtualStart;        // a scene begins on this pass, before anything draws
  if (testBegins) {
    testVirtualStart = false;
    testVirtualOn = true;
    testVirtualMs = testVirtualMs0 = millisNow;
    testShift = 0;
    FaceRandom::seed(testVirtualSeed);
    secondNow = minuteNow = hourNow = 0xFF;  // its second, minute and hour all begin now
    testPlanParse(testPendingPlan);          // its steps count from now, not from the request
    testFrameAnew = true;                    // and its first frame is logged, whatever came before
  }
  if (testVirtualOn) {                       // the test's own clock: a pass is TEST_STEP_MS
    testVirtualMs += TEST_STEP_MS;
    millisNow = testVirtualMs;
    localTime = testVirtualLocal + (time_t)((testVirtualMs - testVirtualMs0) / 1000);
  }
#endif
#if defined(CLOCK_DEBUG) || defined(FACE_FRAME_LOG)
  localTime += testShift;                    // a test's moment (POST /api/test shift=): the panel only, never the clock
#endif
#if defined(FACE_FRAME_LOG)
  if (testBegins && testVirtualReset) { testFaceReset(); }   // the face afresh, at the scene's time
#endif

  if(secondNow != clockSecond(localTime)) { secondNow=clockSecond(localTime); secondTrigger=true; secondStartedAt=secondBeganAt(); }
  else { secondTrigger=false; }
 
  if(secondTrigger && minuteNow != clockMinute(localTime)) { minuteNow=clockMinute(localTime); minuteTrigger=true; }
  else { minuteTrigger=false; }
#if defined(FACE_FRAME_LOG)
  if (minuteTrigger) { FaceRandom::seed((uint32_t)((localTime / 60) % 1440) + 1); }   // the same minute of any day, the same numbers
#endif

  if(minuteTrigger && hourNow != clockHour(localTime)) { hourNow=clockHour(localTime); hourTrigger=true; }
  else { hourTrigger=false; }
}


/* ======================================================================
   Settings persistence
   ====================================================================== */

// Load settings from flash; fall back to factory defaults on first run / struct change.
void loadSettings() {
  clockStore.begin();
  clockStore.read(settings);
  if (settings.magic != SETTINGS_MAGIC) {
    settings = DEFAULTS;
    saveSettings();
    Console.println("Settings: defaults written to flash");
  } else if (settings.settingsRev < SETTINGS_REV) {
    // Migration ladder. A rev 0 blob was written when settingsRev and watchface
    // were still struct padding, so do not read a meaning into what is in them -
    // give every field added since an explicit value. Everything the user
    // configured keeps its place, because the struct did not grow.
    if (settings.settingsRev < 1) { settings.watchface = WATCHFACE_CLASSIC; }
    // Rev 1 held the fly-in speed in ms per pixel (4..60), and a fly-in went
    // 32 px in landscape: that times 32 is the time it took then.
    if (settings.settingsRev < 2) { settings.classic.flyInTime = flyInUnits((long)settings.classic.flyInTime * 32); }
    saveSettings();   // stamps the new revision
    Console.print("Settings: stored layout migrated to rev "); Console.println(SETTINGS_REV);
  }
  loadFaceSettings();   // the faces' own blobs
  applySettings();
}

// Push settings into the runtime globals that drive the clock.
void applySettings() {
  settingsCheck(clockTable);   // the daylight saving mode, the watchface, the fly-in time (CLOCK_PARAMS)
  syncTimeHour   = settings.syncHour;
  syncTimeMinute = settings.syncMinute;
  // Enforce full colors (the trail is dimmed at render time, not by the swatch).
  normalizeColorFull(settings.classic.digitR, settings.classic.digitG, settings.classic.digitB);
  normalizeColorFull(settings.classic.trailR, settings.classic.trailG, settings.classic.trailB);
  panelDirty = true;   // the faces draw on a change only: what is on the panel may be of other settings
}

// Scale a base color by an explicit brightness (0..255), as the faces do (FacePaint).
uint16_t scaledColorB(uint8_t r, uint8_t g, uint8_t b, uint8_t bright) {
  return FacePaint{effectiveBrightness}.color(r, g, b, bright);
}

// Scale a base color by the brightness actually used for rendering.
uint16_t scaledColor(uint8_t r, uint8_t g, uint8_t b) {
  return scaledColorB(r, g, b, effectiveBrightness);
}

// Like scaledColor(), but never dims below a floor that keeps the color visible
// on the panel (FacePaint::visible()). Used for the critical AP info
// (SSID/PW/IP) so it can never quantise to black even when the clock
// brightness is very low.
uint16_t scaledColorVisible(uint8_t r, uint8_t g, uint8_t b) {
  return FacePaint{effectiveBrightness}.visible(r, g, b);
}

// Snap a color to a "full" color (largest channel = 255) while preserving hue.
// Digit and fly-in colors are always full colors now; the trail's dimness comes
// from the classic face's trail brightness, not from a pre-dimmed swatch. This also migrates any
// previously-stored dark/grey color so old settings never render near-black.
void normalizeColorFull(uint8_t &r, uint8_t &g, uint8_t &b) {
  uint8_t m = max(r, max(g, b));
  if (m == 0)   { r = g = b = 255; return; } // black -> white
  if (m == 255) { return; }                  // already a full color
  r = (uint16_t)r * 255 / m;
  g = (uint16_t)g * 255 / m;
  b = (uint16_t)b * 255 / m;
}


























// (The web app on the home network: lan_access.h.)

