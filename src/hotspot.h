/* -------------------------------------------------------------------------
   hotspot.h - the clock's own access point ("MatrixClock"), for setting it
   up with a phone: a captive portal whose DNS answers every lookup with the
   clock's address, so the phone opens the web app by itself; the networks in
   range, scanned as it opens; and its two screens - the SSID, password and
   address until a phone is on it, then the live clock. The hotspot has the
   radio while it is open. A clock without inputs opens it by itself when it
   cannot reach its home WiFi.

   A module of its own since 2026-10-22 (code review of 2026-10-14, point 10).
   ------------------------------------------------------------------------- */
#ifndef HOTSPOT_H
#define HOTSPOT_H

#include <Arduino.h>
#include <WiFi.h>
#include "board_hal.h"

// The hotspot's password; also the web app's on the home network until
// another is set there.
#define AP_PASS "clock1234"   // must be >= 8 characters

extern bool       apActive;   // the hotspot is open: it has the radio
extern WiFiServer apServer;   // its web server, port 80
void startAPMode();
void stopAPMode();
void handleAP();              // on every pass while it is open: the captive portal's DNS, the web server
void updateApDisplay();       // ... and its screens
void apShowClock();           // a phone is on it: the live clock in place of the info screen
void drawAPScreen();          // SSID, password and address, landscape
void sendCaptiveRedirect(Print &c);   // a 302 to the portal, for the phones' probe URLs
#if BOARD_BUTTON_COUNT == 0
void apFallback();            // a clock without inputs: the hotspot when the home WiFi does not connect
void apAutoClose();           // ... and closed again when its time is up
#endif

// The networks in range, from the scan the hotspot makes when it opens,
// strongest first; a name seen from several access points once.
struct SeenNetwork { char ssid[33]; int8_t rssi; bool open; };
const uint8_t SEEN_MAX = 12;
extern SeenNetwork seenNetworks[SEEN_MAX];
extern uint8_t     seenCount;
extern bool        scanFailed;   // the last scan did not run at all (not: found nothing)

#endif  // HOTSPOT_H
