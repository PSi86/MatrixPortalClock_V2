// The clock at rest: Normal, Calm and Off (rest.h).
#include "rest.h"

#include "board_hal.h"
#include "brightness.h"
#include "clock_state.h"
#include "clock_time.h"
#include "console.h"
#include "face.h"
#include "input.h"
#include "settings.h"
#include "web_server.h"

// What only this module calls, ahead of its use.
static bool restInWindow(uint16_t from, uint16_t to, uint16_t m);
static void restLevelAt(time_t local, RestLevel &level, RestWhy &why);
static void restDarkFollow();
static void restReckon();

// ---- Its settings -------------------------------------------------------------------

// No times and no dark room: the clock rests only when it is told to; an input
// shows the time 10 s while Off; every full hour with its show; GIFs asked for
// always play.
static const RestSettings REST_DEFAULTS = { REST_SETTINGS_MAGIC, REST_SETTINGS_REV, REST_AUTO, 0, 0, 0, 0, 0,
                                            REST_WAKE_S_DEFAULT, 0, REST_GIFS_ALWAYS, 0 };
static const char *const REST_GIFS_LABELS[] = { "Always", "Not while off", "Not while calm or off" };   // by value: REST_GIFS_*
static_assert(sizeof(REST_GIFS_LABELS) / sizeof(REST_GIFS_LABELS[0]) == REST_GIFS_COUNT, "a name per choice");
RestSettings restSettings;
static SettingsStore<RestSettings> restStore;
// The web app's keys for them (settings_table.h). Calm's hours are dqf and
// dqt (the decoration's), its minutes dqfm and dqtm; Off's times are minutes
// of the day.
static const SettingParam REST_PARAMS[] = {
  { "dqfm", SETTING_FIELD(RestSettings, calmFromMin), PARAM_RANGE,    0, 0, 59,   LOAD_DEFAULT, nullptr },
  { "dqtm", SETTING_FIELD(RestSettings, calmToMin),   PARAM_RANGE,    0, 0, 59,   LOAD_DEFAULT, nullptr },
  { "dof",  SETTING_FIELD(RestSettings, offFrom),     PARAM_RANGE,    0, 0, 1439, LOAD_DEFAULT, nullptr },
  { "dot",  SETTING_FIELD(RestSettings, offTo),       PARAM_RANGE,    0, 0, 1439, LOAD_DEFAULT, nullptr },
  { "dok",  SETTING_FIELD(RestSettings, offLux),      PARAM_RANGE,    0, 0, 60000, LOAD_KEEP,   nullptr },
  { "dws",  SETTING_FIELD(RestSettings, wakeS),       PARAM_RANGE,    0, REST_WAKE_S_MIN, REST_WAKE_S_MAX, LOAD_CLAMP, nullptr },
  { "dph",  SETTING_FIELD(RestSettings, plainHour),   PARAM_CHECKBOX, 0, 0, 1,    LOAD_BOOL,    nullptr },
  { "dga",  SETTING_FIELD(RestSettings, gifsAsked),   PARAM_CHOICE,   0, 0, REST_GIFS_COUNT - 1, LOAD_DEFAULT, REST_GIFS_LABELS },
};
SettingsTable restTable = SETTING_TABLE(restSettings, REST_DEFAULTS, REST_PARAMS);

void loadRestSettings() {
  restStore.begin("rest");
  restStore.read(restSettings);
  if (restSettings.magic != REST_SETTINGS_MAGIC) { restSettings = REST_DEFAULTS; }   // written with the next save
  settingsCheck(restTable);
  if (restSettings.mode >= REST_MODE_COUNT) { restSettings.mode = REST_AUTO; }
  if (restSettings.mode != REST_AUTO) { Console.printf("Rest: %s, as chosen before the start\n", restModeLabel(restSettings.mode)); }
}

bool saveRestSettings() {
  restSettings.magic = REST_SETTINGS_MAGIC;
  restSettings.rev   = REST_SETTINGS_REV;
  return restStore.write(restSettings);
}

void applyRestParams(const String &q) {
  settingsApply(restTable, [&q](const char *key) { return getParam(q, key); }, false);   // REST_PARAMS
}

// ---- The level ------------------------------------------------------------------------

static RestLevel     restLevelNow = REST_NORMAL;
static RestWhy       restWhyNow = REST_WHY_NONE;
static RestBy        restByNow = REST_BY_NONE;    // who chose the level while it is set
static uint32_t      restChangeCount = 0;
static bool          restLitSeen = true;
static unsigned long restWakeUntil = 0;           // millis() until which an input keeps the time on show while Off
// Off by the room's light: the panel's own light reaches the sensor, so the
// light has to stay under the line for REST_DARK_MS before Off begins, and
// over twice it (at least 1 lx more) for REST_LIGHT_MS before it ends; and
// while the panel is lit within Off (woken, a GIF asked for), or for
// REST_SETTLE_MS after it lit or went black, the light says nothing about the
// room (the filtered light follows a change within about 4 s, brightness.cpp).
const unsigned long REST_DARK_MS   = 10000;
const unsigned long REST_LIGHT_MS  = 3000;
const unsigned long REST_SETTLE_MS = 6000;
static bool          restDarkOff = false;         // the room dark enough for Off
static unsigned long restDarkSince = 0;           // millis() the light crossed the line the other way; 0 = it has not
static unsigned long restPanelChangedAt = 0;      // millis() the panel last went black or lit again

RestLevel restLevel() { return restLevelNow; }
bool restLit() { return screen != SCREEN_OFF; }
uint32_t restChanges() { return restChangeCount; }

// Whether minute of the day m lies in from .. to (to not included), which
// may run over midnight; from == to: no window.
bool restInWindow(uint16_t from, uint16_t to, uint16_t m) {
  if (from == to) { return false; }
  return from < to ? (m >= from && m < to) : (m >= from || m < to);
}

// The level at the panel's local time `local`: the choice, or the strictest
// of the rules - the hours at that time, the room's light as it is now.
void restLevelAt(time_t local, RestLevel &level, RestWhy &why) {
  why = REST_WHY_SET;
  switch (restSettings.mode) {
    case REST_SET_NORMAL: level = REST_NORMAL; return;
    case REST_SET_CALM:   level = REST_CALM; return;
    case REST_SET_OFF:    level = REST_OFF; return;
    default: break;
  }
  bool timed = clockIsSet();
  uint16_t m = (uint16_t)(clockHour(local) * 60 + clockMinute(local));
  const DecorSettings &d = decorSettings;
  uint16_t calmFrom = (uint16_t)(d.quietFrom * 60 + restSettings.calmFromMin);
  uint16_t calmTo = (uint16_t)(d.quietTo * 60 + restSettings.calmToMin);
  if (timed && restInWindow(restSettings.offFrom, restSettings.offTo, m)) { level = REST_OFF;  why = REST_WHY_HOURS; }
  else if (restDarkOff)                                                  { level = REST_OFF;  why = REST_WHY_DARK; }
  else if (timed && restInWindow(calmFrom, calmTo, m))                   { level = REST_CALM; why = REST_WHY_HOURS; }
  else if (d.darkLux && luxAvg >= 0 && luxAvg < d.darkLux)               { level = REST_CALM; why = REST_WHY_DARK; }
  else                                                                   { level = REST_NORMAL; why = REST_WHY_NONE; }
}

// face.h: whether the change to `minute` plays a full hour like a minute -
// Calm's option, with the clock calm (or off) at that minute.
bool restPlainHourAt(time_t minute) {
  if (!restSettings.plainHour) { return false; }
  RestLevel level;
  RestWhy why;
  restLevelAt(minute, level, why);
  return level >= REST_CALM;
}

// On every pass: whether the room is dark enough for Off (restDarkOff).
void restDarkFollow() {
  uint16_t line = restSettings.offLux;
  if (!line || !luxOK || luxAvg < 0) { restDarkOff = false; restDarkSince = 0; return; }
  bool litInOff = restLevelNow == REST_OFF && restLit();
  if (litInOff || millisNow - restPanelChangedAt < REST_SETTLE_MS) { restDarkSince = 0; return; }
  float back = max(2.0f * line, line + 1.0f);
  bool crossing = restDarkOff ? luxAvg > back : luxAvg < line;
  if (!crossing) { restDarkSince = 0; return; }
  if (!restDarkSince) { restDarkSince = millisNow ? millisNow : 1; return; }
  if (millisNow - restDarkSince < (restDarkOff ? REST_LIGHT_MS : REST_DARK_MS)) { return; }
  restDarkOff = !restDarkOff;
  restDarkSince = 0;
  Console.printf("Rest: the room %s (%.1f lx)\n", restDarkOff ? "dark enough for Off" : "light again", luxAvg);
}

// The level now; a change is counted (restChanges()) and logged.
void restReckon() {
  RestLevel level;
  RestWhy why;
  restLevelAt(localTime, level, why);
  bool lit = restLit();
  if (level == restLevelNow && why == restWhyNow && lit == restLitSeen) { return; }
  if (level != restLevelNow || why != restWhyNow) {
    static const char *const WHY[] = { "", " (the hours)", " (the room's light)", " (chosen)" };
    Console.printf("Rest: %s%s\n", restLevelLabel(level), WHY[why]);
  }
  restLevelNow = level;
  restWhyNow = why;
  restLitSeen = lit;
  restChangeCount++;
}

// Off takes the panel when the plain face is on top and no input keeps the
// time on show; anything else on top - the menu, a GIF asked for, a notice
// under way - ends first. Over, it gives the face back.
void restUpdate() {
  restDarkFollow();
  restReckon();
  bool off = restLevelNow == REST_OFF;
  if (screen == SCREEN_OFF) {
    if (!off) { closeScreen(); restPanelChangedAt = millisNow; restReckon(); }
    return;
  }
  if (off && screen == SCREEN_FACE && (long)(millisNow - restWakeUntil) >= 0 && !anyButtonDown()) {
    openScreen(SCREEN_OFF);
    restPanelChangedAt = millisNow;
    restReckon();
  }
}

// handleInput(): while Off, every input keeps the time on show for wakeS
// more; the one that wakes the clock does nothing else.
bool restTakesInput() {
  if (restLevelNow != REST_OFF) { return false; }
  restWakeUntil = millisNow + restSettings.wakeS * 1000UL;
  if (screen != SCREEN_OFF) { return false; }   // awake already: the input does what it does
  Console.println("Rest: woken by an input");
  closeScreen();
  restPanelChangedAt = millisNow;
  restReckon();
  return true;
}

// A GIF asked for at the level now: refused only where GIFs asked for says
// so. One that plays while Off plays over the black panel (gif_player.cpp),
// which is black again after it.
bool restRefusesGif(RestLevel &level) {
  level = restLevelNow;
  switch (restSettings.gifsAsked) {
    case REST_GIFS_NOT_OFF:     return restLevelNow == REST_OFF;
    case REST_GIFS_ONLY_NORMAL: return restLevelNow != REST_NORMAL;
    default:                    return false;
  }
}

void restWake(RestBy by) {
  if (restLevelNow != REST_OFF) { return; }
  restWakeUntil = millisNow + restSettings.wakeS * 1000UL;
  if (screen != SCREEN_OFF) { return; }
  Console.printf("Rest: woken by %s\n", by == REST_BY_HA ? "Home Assistant" : "the web app");
  closeScreen();
  restPanelChangedAt = millisNow;
  restReckon();
}

// A new choice counts at once: Off begins on the next pass, also while an
// input kept the time on show.
bool restSetMode(uint8_t mode, RestBy by) {
  if (mode >= REST_MODE_COUNT) { return false; }
  restByNow = mode == REST_AUTO ? REST_BY_NONE : by;
  restWakeUntil = 0;
  if (mode == restSettings.mode) { restChangeCount++; return true; }   // the same again: said again
  restSettings.mode = mode;
  bool stored = saveRestSettings();
  Console.printf("Rest: %s, chosen %s%s\n", restModeLabel(mode), by == REST_BY_HA ? "in Home Assistant" : "in the web app",
                stored ? "" : " (not stored: the flash could not be written)");
  restReckon();
  restChangeCount++;
  return stored;
}

// ---- Names ------------------------------------------------------------------------------

static const char *const REST_MODE_LABELS[REST_MODE_COUNT] = { "Automatic", "Normal", "Calm", "Off" };
static const char *const REST_MODE_CODES[REST_MODE_COUNT]  = { "auto", "normal", "calm", "off" };

const char *restModeLabel(uint8_t mode) { return mode < REST_MODE_COUNT ? REST_MODE_LABELS[mode] : "?"; }
const char *restLevelLabel(RestLevel level) { return REST_MODE_LABELS[level + 1]; }

// "Calm" or "calm": a choice by its name, -1 for none.
int8_t restModeByLabel(const char *label) {
  for (uint8_t m = 0; m < REST_MODE_COUNT; m++) {
    if (strcasecmp(label, REST_MODE_LABELS[m]) == 0 || strcasecmp(label, REST_MODE_CODES[m]) == 0) { return (int8_t)m; }
  }
  return -1;
}

const char *restWhyCode() {
  static const char *const CODES[] = { "", "hours", "dark", "set" };
  return CODES[restWhyNow];
}

const char *restByName() {
  return restByNow == REST_BY_HA ? "ha" : restByNow == REST_BY_WEB ? "web" : "";
}

void restStatusInto(JsonObject o) {
  static const char *const LEVEL_CODES[] = { "normal", "calm", "off" };
  o["level"] = LEVEL_CODES[restLevelNow];
  o["why"] = restWhyCode();
  o["by"] = restByName();
  o["mode"] = REST_MODE_CODES[restSettings.mode];
  o["lit"] = restLit();
  o["dark"] = restDarkOff;
}
