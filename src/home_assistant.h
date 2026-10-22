/* -------------------------------------------------------------------------
   home_assistant.h - the Home Assistant link: when the MQTT client
   (ha_link.h, beside the loop) runs, what arrives - items for the info
   band, messages - and what the clock sends: its sensors' values, its
   discovery. Its settings - the broker's address, the clock's login there,
   how often values go out - in a blob of its own (NVS key "ha"); the
   password is kept as typed, as the home WiFi's is: the broker needs it so.
   Like every source that is pushed to the clock it needs a stored home WiFi
   and continuous network access. While it runs it holds the station as
   well (RADIO_HA), so that switching the link or the access off still lets
   it say goodbye before the radio goes.

   A module of its own since 2026-10-22 (code review of 2026-10-14, point 10).
   ------------------------------------------------------------------------- */
#ifndef HOME_ASSISTANT_H
#define HOME_ASSISTANT_H

#include <Arduino.h>
#include <ArduinoJson.h>

void loadHaSettings();
// On every pass: starts and stops the client as the settings and the
// station say, takes what it brought, sends the values that are due.
void haUpdate();
// The client asked to stop; with `wait`, until it has said goodbye (up to
// HA_STOP_WAIT_MS), as the hotspot does before it takes the radio.
void haStopLink(bool wait);
void haSettingsInto(JsonObject o);   // the settings, for /api/state (the password never)
void haStatusInto(JsonObject o);     // what the link does, for /api/status
void apiHa(Print &c, const String &body);   // /api/ha: on or off, broker, port, user, password, intervals

#endif  // HOME_ASSISTANT_H
