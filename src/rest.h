/* -------------------------------------------------------------------------
   rest.h - the clock at rest (decided by Peter on 2026-10-25). Three
   levels: Normal; Calm, the quiet of before - no GIFs and no
   extras of their own accord - and, as an option, every full hour changed
   like a minute, without its show; Off, the panel black. Calm and Off each
   have their own "when": hours of the day and a dark room; the strictest
   level that holds wins. One choice the web app and Home Assistant share
   sets a level instead, until it is set back to Automatic; it is kept over
   a restart.

   While the clock is Off, an input at the clock - a button, a knock, a
   gesture - wakes it: the face shows the time for restSettings.wakeS and
   the input does nothing else; inputs while it is awake work as always and
   keep it awake. Alerts, messages and info cards wait until Off is over,
   and nothing of its own accord comes while it is awake. GIFs asked for -
   from the web app, Home Assistant, an input mapped to a GIF - play as
   restSettings.gifsAsked allows: always, not while Off, or not while Calm
   or Off; while Off one plays over the black panel, which is black again
   after it. GIFs of their own accord never come while the clock is calm or
   off. Off is a screen over the face (SCREEN_OFF), which the panel shows
   black (PanelCanvas::setOff()); the hotspot's screens go first.
   ------------------------------------------------------------------------- */
#ifndef REST_H
#define REST_H

#include <Arduino.h>
#include <ArduinoJson.h>
#include "settings_table.h"

enum RestLevel : uint8_t { REST_NORMAL, REST_CALM, REST_OFF };
// The choice the web app and Home Assistant share: the clock's own rules, or
// a level until Automatic is chosen again.
enum RestMode : uint8_t { REST_AUTO, REST_SET_NORMAL, REST_SET_CALM, REST_SET_OFF, REST_MODE_COUNT };
// Why the clock is at its level.
enum RestWhy : uint8_t { REST_WHY_NONE, REST_WHY_HOURS, REST_WHY_DARK, REST_WHY_SET };
// Who chose the level, while it is set (RAM only: after a restart, nobody known).
enum RestBy : uint8_t { REST_BY_NONE, REST_BY_WEB, REST_BY_HA };

// A blob of its own (NVS key "rest"). Calm's hours are the decoration's
// quietFrom and quietTo (settings.h, as they always were) with the minutes
// here; Calm's dark room is the decoration's darkLux.
#define REST_SETTINGS_MAGIC 0x7E57
#define REST_SETTINGS_REV   1
struct RestSettings {
  uint16_t magic;
  uint8_t  rev;
  uint8_t  mode;          // RestMode, kept over a restart
  uint8_t  calmFromMin;   // Calm from quietFrom:calmFromMin ...
  uint8_t  calmToMin;     // ... to quietTo:calmToMin; the same time twice: none
  uint16_t offFrom;       // Off from this minute of the day ...
  uint16_t offTo;         // ... to this one; the same twice: none
  uint16_t offLux;        // Off while the room is darker than this, lux; 0 = not by the light
  uint8_t  wakeS;         // an input shows the time this long while Off, s
  uint8_t  plainHour;     // 1: in Calm every full hour changes like a minute
  uint8_t  gifsAsked;     // when GIFs asked for play: REST_GIFS_*
  uint8_t  reserved;
};
static_assert(sizeof(RestSettings) == 16, "the stored blob keeps its size");
const uint8_t REST_WAKE_S_DEFAULT = 10, REST_WAKE_S_MIN = 3, REST_WAKE_S_MAX = 120;
// When GIFs asked for play (RestSettings::gifsAsked).
const uint8_t REST_GIFS_ALWAYS      = 0;   // also while Off, the panel black again after them
const uint8_t REST_GIFS_NOT_OFF     = 1;   // not while Off
const uint8_t REST_GIFS_ONLY_NORMAL = 2;   // not while Calm or Off
const uint8_t REST_GIFS_COUNT       = 3;
extern RestSettings restSettings;
extern SettingsTable restTable;
void loadRestSettings();
bool saveRestSettings();
void applyRestParams(const String &q);   // the web app's (/api/save)

RestLevel restLevel();                   // the level now
bool restLit();                          // the panel shows something: not the black of Off
void restUpdate();                       // on every pass outside the hotspot: the level, and Off on the panel or not
bool restTakesInput();                   // an input (handleInput()): while Off it wakes the clock and is used up
bool restRefusesGif(RestLevel &level);   // a GIF asked for, not at the level now (`level`), as gifsAsked says
void restWake(RestBy by);                // Home Assistant's button: the time for a while, if the clock is Off
bool restSetMode(uint8_t mode, RestBy by);   // the web app's or Home Assistant's choice; false: not stored
uint32_t restChanges();                  // counts up whenever the level, why, the choice or restLit() change
// For Home Assistant: the choice's and the level's names, a choice by its
// name (-1: none), and why, as a word.
const char *restModeLabel(uint8_t mode);
const char *restLevelLabel(RestLevel level);
int8_t restModeByLabel(const char *label);
const char *restWhyCode();
const char *restByName();
void restStatusInto(JsonObject o);       // for the web app's status

#endif  // REST_H
