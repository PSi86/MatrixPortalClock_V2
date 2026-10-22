/* -------------------------------------------------------------------------
   clock_state.h - what the sketch (main.cpp) shares with the parts of the
   clock that have moved into modules of their own: its state and the
   functions they call, declared here and defined in main.cpp. It shrinks
   as more parts become modules and take what is theirs along (code review
   of 2026-10-14, point 10; 2026-10-22).
   ------------------------------------------------------------------------- */
#ifndef CLOCK_STATE_H
#define CLOCK_STATE_H

#include <Arduino.h>
#include <ArduinoJson.h>
#include <WiFi.h>
#include "board_hal.h"
#include "band_data.h"   // BandItem
#include "face.h"        // FaceNow, Watchface
#include "input_map.h"   // InputFunction
#include "knock.h"       // Knock

const char *fwVersion();   // the commit the build comes from and its date (FW_VERSION)

// millis() at the start of the loop's pass.
extern unsigned long millisNow;
// The time on the panel as the timekeeper counts it, and what changed on this
// pass (timekeeper()).
extern uint8_t hourNow, minuteNow, secondNow;
extern bool secondTrigger, minuteTrigger, hourTrigger;
extern unsigned long secondStartedAt;   // millis() when the second on show began
extern time_t localTime;                // what the panel shows: the clock's UTC plus zone and daylight saving
#if defined(FACE_FRAME_LOG)
extern bool testQuiet;   // test builds: no notices and no info cards over the face (POST /api/test quiet=1)
#endif

// The panel: the canvas the clock draws into, and whether what is on it has
// to be drawn anew (a setting, a status that shows).
extern PanelCanvas matrix;
extern bool panelDirty;
uint16_t scaledColorVisible(uint8_t r, uint8_t g, uint8_t b);   // a colour at the panel's brightness, never black
bool gifPlaying();   // a GIF has the panel, its decoder up

// The watchface on show (main.cpp), and the moment it is drawn for.
FaceNow faceMoment();
Watchface &faceFor(uint8_t id);

// Boot breadcrumb stages, written to flash while starting (see board_hal.h).
const uint8_t BOOT_STAGE_CLEAR = 0;   // the last run was healthy
const uint8_t BOOT_STAGE_START = 1;   // setup() entered
const uint8_t BOOT_STAGE_PANEL = 2;   // panel and sensors up
const uint8_t BOOT_STAGE_WIFI  = 3;   // the WiFi join started (the clock runs while it joins)
const uint8_t BOOT_STAGE_RUN   = 4;   // WiFi joined
extern bool bootStageCleared;         // breadcrumb cleared after the first healthy seconds

extern bool apActive;   // the hotspot is open: it has the radio
void startAPMode();
void stopAPMode();

// The knock (knock.h): the LIS3DH's, or the wall clock's QMI8658's.
extern Knock knock;
#define knockOK knock.found()

// What the panel is drawn in right now (the hotspot info is landscape
// however the panel is held).
extern uint8_t screenRotation;

// The hotspot's password; also the web app's on the home network until
// another is set there.
#define AP_PASS "clock1234"   // must be >= 8 characters

// The screens: exactly one owns the panel at a time; they lie on top of each
// other, and closing one uncovers the one beneath (setScreen(),
// openScreen(), closeScreen()).
enum Screen : uint8_t {
  SCREEN_BOOT,             // status lines while setup() runs
  SCREEN_FACE,             // the clock face, classic or Tetris
  SCREEN_MENU,             // the menu list
  SCREEN_EDITOR,           // the editor of one menu item
  SCREEN_BANNER,           // a short confirmation after a change, over the screen it confirms
  SCREEN_HOTSPOT_INFO,     // config AP up, no client yet: SSID, password and IP
  SCREEN_HOTSPOT_PREVIEW,  // config AP up and a client connected: the live clock
  SCREEN_GIF,              // a GIF in place of the face, over it in the stack
  SCREEN_GESTURE_TEST,     // every gesture shown instead of done, over the face (the web app's Inputs section)
};
extern Screen screen;      // the screen on top, the one on the panel
void openScreen(Screen next);
void closeScreen();
Screen screenBeneath();          // the screen a banner on top confirms
void bootStatus(const char *msg);   // a status line while setup() runs

// The menu, and the functions an input runs there or on the face.
void menuOpen();
void navigate(InputFunction fn, uint8_t steps);
void cycleDstMode();
void toggleAutoBrightWithBanner();
void showHints();
// The menu's look: its colours and its 3x5 font.
uint16_t uiInk();
uint16_t uiQuiet();
void uiTiny(int16_t x, int16_t top, const char *s, uint16_t colour);

// A banner: a short text over the screen it confirms, or a step of the
// notices on show (notices.h).
enum BannerKind : uint8_t { BANNER_TEXT, BANNER_TIME, BANNER_HINT, BANNER_ALERT, BANNER_RUN, BANNER_CARD };
extern BannerKind    bannerKind;
extern unsigned long bannerAt;   // millis() the banner came up
void bannerOpen();
void showBanner(const char *first, const char *second, uint8_t r, uint8_t g, uint8_t b);

#if BOARD_WAVESHARE_WALL
extern uint32_t wallOutputStops;   // the panel output's stops since the start (the console's "panel")
#endif

// The web server, beside the loop, and the answers of its JSON interface.
extern WiFiServer apServer;
void webUpdate(bool listening, bool lan);
String getParam(const String &query, const String &key);
void sendJson(Print &c, JsonDocument &doc, int code = 200);
void sendJsonError(Print &c, int code, const char *text);

#endif  // CLOCK_STATE_H
