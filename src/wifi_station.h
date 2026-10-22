/* -------------------------------------------------------------------------
   wifi_station.h - the home WiFi and the station joined to it.

   The network's name and password, stored in flash (NVS key "wifi") and
   changed on the config page. arduino_secrets.h is optional: on the first
   start of a clock that has nothing stored, the credentials compiled in
   from it are copied into flash, once - after that the stored ones count,
   and a WiFi forgotten on the config page stays forgotten.

   The station - the clock joined to the home WiFi - is shared: the NTP
   sync, the info band's sources, the web app on the home network and the
   Home Assistant link each say while they need it (radioUsers), and the
   radio is off while nobody does (stationUpdate()). The sync's need follows
   its state (setSyncState()). The hotspot takes the radio over while it is
   open.

   A module of its own since 2026-10-22 (code review of 2026-10-14, point 10).
   ------------------------------------------------------------------------- */
#ifndef WIFI_STATION_H
#define WIFI_STATION_H

#include <Arduino.h>

struct WifiCreds {
  uint16_t magic;
  uint8_t  rev;
  uint8_t  verified;  // 1 once a sync over this WiFi has worked: a known-good config.
                      // 0 for a new one - its failures show at once (syncOverdue())
  char     ssid[33];   // up to 32 bytes, NUL-terminated; empty = no WiFi stored
  char     pass[64];   // WPA2: 8 to 63 characters, NUL-terminated
};
extern WifiCreds wifiCreds;
void loadWifiCreds();
bool saveWifiCreds();
// Forget WiFi: an empty entry is stored, not none, so the credentials of the
// build are not copied in again at the next start. Says whether it is stored.
bool forgetWifi();
bool wifiStored();   // whether a home WiFi is stored at all

// How the last join of the home WiFi went, for the config page. The radio is
// off between syncs, so "connected" is something that happened, with a time.
enum JoinOutcome : uint8_t { JOIN_NOT_TRIED, JOIN_SUCCEEDED, JOIN_FAILED };
extern JoinOutcome   joinOutcome;
extern uint8_t       joinFailStatus;   // the station's wl_status_t when it failed
extern int8_t        joinRssi;         // the signal when it succeeded
extern unsigned long joinOutcomeAt;    // millis() of the outcome
const char *joinFailText(uint8_t status);   // why a join failed, from that status

// Who needs the station joined, each with its bit in radioUsers.
enum RadioUser : uint8_t { RADIO_SYNC = 0x01, RADIO_FEEDS = 0x02, RADIO_LAN = 0x04, RADIO_HA = 0x08 };
extern uint8_t radioUsers;
enum StationState : uint8_t {
  STA_OFF,       // radio off
  STA_JOINING,   // joining the home WiFi, again every JOIN_RETRY_MS until it connects
  STA_UP,        // connected
};
extern StationState stationState;   // the hotspot sets it off when it takes the radio
bool stationUp();
void stationJoin();     // start joining the home WiFi, or start again; never waits
// Keeps the station joined while anyone needs it, and the radio off while
// nobody does. Runs on every pass outside the hotspot and never waits.
void stationUpdate();

#endif  // WIFI_STATION_H
