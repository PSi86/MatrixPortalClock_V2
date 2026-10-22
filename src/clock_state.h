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

// millis() at the start of the loop's pass.
extern unsigned long millisNow;

// The station: who needs it joined, each with its bit in radioUsers. The
// radio is on while any does.
enum RadioUser : uint8_t { RADIO_SYNC = 0x01, RADIO_FEEDS = 0x02, RADIO_LAN = 0x04, RADIO_HA = 0x08 };
extern uint8_t radioUsers;
bool stationUp();

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
