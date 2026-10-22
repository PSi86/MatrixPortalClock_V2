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

const char *fwVersion();   // the commit the build comes from and its date (FW_VERSION)

// millis() at the start of the loop's pass.
extern unsigned long millisNow;
// The time on the panel as the timekeeper counts it, and what changed on this
// pass (timekeeper()).
extern uint8_t hourNow, minuteNow, secondNow;
extern bool secondTrigger, minuteTrigger, hourTrigger;

// The panel: the canvas the clock draws into, and whether what is on it has
// to be drawn anew (a setting, a status that shows).
extern PanelCanvas matrix;
extern bool panelDirty;
bool gifPlaying();   // a GIF has the panel, its decoder up
#if BAND_ON_PANEL
extern bool bandLocalDone;   // the wall clock's band works its own turns out anew (bandLocalUpdate())
#endif

// Boot breadcrumb stages, written to flash while starting (see board_hal.h).
const uint8_t BOOT_STAGE_CLEAR = 0;   // the last run was healthy
const uint8_t BOOT_STAGE_START = 1;   // setup() entered
const uint8_t BOOT_STAGE_PANEL = 2;   // panel and sensors up
const uint8_t BOOT_STAGE_WIFI  = 3;   // the WiFi join started (the clock runs while it joins)
const uint8_t BOOT_STAGE_RUN   = 4;   // WiFi joined
extern bool bootStageCleared;         // breadcrumb cleared after the first healthy seconds

extern bool apActive;   // the hotspot is open: it has the radio

// The hotspot's password; also the web app's on the home network until
// another is set there.
#define AP_PASS "clock1234"   // must be >= 8 characters

// The notices on the 64x32 clock: an item that goes is forgotten as seen
// (an item that comes again later is new again).
void noticeForget(const BandItem &it);

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

// Who takes the panel, and when (the panel rules, main.cpp).
const unsigned long ROUND_GIF_GAP_MS = 60000;   // at least this between an info round and a GIF
bool panelFitsBeforeChange(unsigned long ms);   // something this long ends before the face's next change
bool decorFree(unsigned long ms, bool gif);     // decoration of its own accord may take the panel now
unsigned long panelRandomAt(unsigned long lo, unsigned long hi, unsigned long ms);   // a random time outside the changes' slots
unsigned long roundNextIn();                    // ms to the next info round

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
