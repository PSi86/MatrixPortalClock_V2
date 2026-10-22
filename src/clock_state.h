/* -------------------------------------------------------------------------
   clock_state.h - what main.cpp shares with the clock's modules: its state
   and the functions they call, declared here and defined there. main.cpp
   is the clock's glue - setup() and loop(), the screen stack, the
   orientation, the watchfaces, the timekeeper and the clock's settings -
   and each module has a header of its own for what it offers (code review
   of 2026-10-14, point 10; 2026-10-22).
   ------------------------------------------------------------------------- */
#ifndef CLOCK_STATE_H
#define CLOCK_STATE_H

#include <Arduino.h>
#include <ArduinoJson.h>
#include <WiFi.h>
#include "board_hal.h"
#include "band_data.h"   // BandItem
#include "settings_table.h"   // SettingsTable
#include "face.h"        // FaceNow, Watchface
#include "knock.h"       // Knock

// The commit the build comes from and its date (FW_VERSION). Defined in
// web_api.cpp, the one file that includes the generated webui_page.h.
const char *fwVersion();

// millis() at the start of the loop's pass.
extern unsigned long millisNow;
// The time on the panel as the timekeeper counts it, and what changed on this
// pass (timekeeper()).
extern uint8_t hourNow, minuteNow, secondNow;
extern bool secondTrigger, minuteTrigger, hourTrigger;
extern unsigned long secondStartedAt;   // millis() when the second on show began
extern uint16_t panelHz;                // the panel's refresh rate, 0 until the first measurement
extern time_t localTime;                // what the panel shows: the clock's UTC plus zone and daylight saving
#if defined(FACE_FRAME_LOG)
extern bool testQuiet;   // test builds: no notices and no info cards over the face (POST /api/test quiet=1)
#endif
#if defined(CLOCK_DEBUG) || defined(FACE_FRAME_LOG)
void apiTest(Print &c, const String &body);   // POST /api/test: what the scene tests need (main.cpp)
#endif

// The panel: the canvas the clock draws into, and whether what is on it has
// to be drawn anew (a setting, a status that shows).
extern PanelCanvas matrix;
extern bool panelDirty;
uint16_t scaledColorVisible(uint8_t r, uint8_t g, uint8_t b);   // a colour at the panel's brightness, never black

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
extern uint8_t prevBootStage;         // the stage the previous run reached
extern bool accelOK;                  // the LIS3DH was found on I2C

// The clock's settings (settings.h) and the faces' (face.h): loaded, taken
// up, saved; every table of them, for /api/state.
void loadSettings();
void applySettings();     // into the runtime globals that drive the clock
bool saveFaceSettings();
FaceOutcome applyFaceParams(const String &q, bool live, bool previews);
extern SettingsTable *const SETTINGS_TABLES[];
extern const uint8_t SETTINGS_TABLE_COUNT;

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
// The Picopixel font, kept in main.cpp alone: Adafruit's fonts are constants
// of the file that includes them, and each file using one would carry a copy.
extern const GFXfont &fontPicopixel;
bool screenIsLandscape();        // the panel is drawn wider than tall

#endif  // CLOCK_STATE_H
