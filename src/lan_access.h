/* -------------------------------------------------------------------------
   lan_access.h - the web app on the home network.

   Off until switched on with a password, from the hotspot or from the home
   network itself. While it is on, the station stays joined (RADIO_LAN) and
   the web server listens there too; every /api/ request needs the password
   (HTTP Basic, any user name), kept as a salted SHA-256 hash, never as it
   was typed, in a blob of its own (NVS key "lan"). Five wrong passwords in a
   row lock the check for 30 s. A module of its own since 2026-10-22 (code
   review of 2026-10-14, point 10).
   ------------------------------------------------------------------------- */
#ifndef LAN_ACCESS_H
#define LAN_ACCESS_H

#include <Arduino.h>

// At the start: the stored switch and password; a clock that has none gets
// the hotspot's password, and one without buttons (the wall clock) starts
// with the access on - it is its way in.
void loadLanSettings();

bool lanIsOn();          // the web app is reachable on the home network
bool lanHasPassword();   // a password was set
bool lanPasswordIs(const char *pw);   // the password is pw (the page says when it is still the hotspot's)

// On or off, stored, the station held or let go (the console's "lan on").
void lanSwitch(bool on);

// Whether a request from the home network carries the password; answers it
// with 401 or 429 if not.
bool lanAuthorized(Print &c, const String &auth);

// /api/lan: on (on or off) and a new password (pw, 8 to 63 characters,
// optional once one is set).
void apiLan(Print &c, const String &body);

// On every pass outside the hotspot: the server on the home network while
// the access is on and the station is up, and one step of its requests.
void lanUpdate();
void lanStop();   // the server off (the hotspot takes over)

#endif  // LAN_ACCESS_H
