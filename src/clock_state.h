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

// millis() at the start of the loop's pass.
extern unsigned long millisNow;

// The panel: what is on it has to be drawn anew (a setting, a status that shows).
extern bool panelDirty;
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

// The hotspot's password; also the web app's on the home network until
// another is set there.
#define AP_PASS "clock1234"   // must be >= 8 characters

// The web server, beside the loop, and the answers of its JSON interface.
extern WiFiServer apServer;
void webUpdate(bool listening, bool lan);
String getParam(const String &query, const String &key);
void sendJson(Print &c, JsonDocument &doc, int code = 200);
void sendJsonError(Print &c, int code, const char *text);

#endif  // CLOCK_STATE_H
