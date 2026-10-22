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
void faceUpdate();     // one pass of the face on the panel
FaceNow faceMoment();
Watchface &faceFor(uint8_t id);

// Boot breadcrumb stages, written to flash while starting (see board_hal.h).
const uint8_t BOOT_STAGE_CLEAR = 0;   // the last run was healthy
const uint8_t BOOT_STAGE_START = 1;   // setup() entered
const uint8_t BOOT_STAGE_PANEL = 2;   // panel and sensors up
const uint8_t BOOT_STAGE_WIFI  = 3;   // the WiFi join started (the clock runs while it joins)
const uint8_t BOOT_STAGE_RUN   = 4;   // WiFi joined
extern bool bootStageCleared;         // breadcrumb cleared after the first healthy seconds

// The knock (knock.h): the LIS3DH's, or the wall clock's QMI8658's.
extern Knock knock;
#define knockOK knock.found()

// How the panel is held (the accelerometer's reading after the debounce),
// and what it is drawn in right now (the hotspot info is landscape however
// the panel is held).
extern uint8_t deviceRotation;
extern uint8_t screenRotation;
void setScreenRotation(uint8_t rot);

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
void setScreen(Screen next);    // a screen on its own: everything open is closed
void openScreen(Screen next);
void closeScreen();
Screen screenBeneath();          // the screen a banner on top confirms
void bootStatus(const char *msg);   // a status line while setup() runs
void drawCenteredLines(const char *first, const char *second, uint16_t color);   // one or two lines of Picopixel, centred
bool screenIsLandscape();        // the panel is drawn wider than tall

#if BOARD_WAVESHARE_WALL
extern uint32_t wallOutputStops;   // the panel output's stops since the start (the console's "panel")
#endif

// The web server, beside the loop, and the answers of its JSON interface.
void webUpdate(bool listening, bool lan);
String getParam(const String &query, const String &key);
void sendJson(Print &c, JsonDocument &doc, int code = 200);
void sendJsonError(Print &c, int code, const char *text);

#endif  // CLOCK_STATE_H
