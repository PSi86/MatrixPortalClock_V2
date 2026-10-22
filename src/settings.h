/* -------------------------------------------------------------------------
   settings.h - the clock's stored settings: the main blob (the watchface,
   the zone, the brightness, the classic face's), the operating concept's,
   the GIFs', the decoration's and the info band's sources, each a blob of
   its own in NVS with its magic and revision, its defaults and its table
   for the web app (settings_table.h). Loaded, checked and stored here; what
   a new value sets off elsewhere - the GIF brightness, the next extra, a new
   place's fetch - is the business of the part that owns it. The faces'
   settings are with the faces (face_*.h).

   A module of its own since 2026-10-22 (code review of 2026-10-14, point 10).
   ------------------------------------------------------------------------- */
#ifndef SETTINGS_H
#define SETTINGS_H

#include <Arduino.h>
#include "board_hal.h"          // SettingsStore: the blobs in NVS
#include "band_data.h"          // FeedId: the info band's sources
#include "classic_settings.h"   // the classic face's settings, inside the main blob
#include "input_map.h"          // INPUT_PROFILES: the profile is stored by its place there
#include "settings_table.h"

// ---- The main blob (NVS key "clock") -------------------------------------------
// Configurable at runtime via the WLAN AP config page and the on-screen menu.
#define SETTINGS_MAGIC 0xC10E   // bump this to force a reset to defaults after a struct change
// Layout revision inside a valid blob. Fields added later go into spare bytes and
// are migrated in loadSettings() by revision, so the magic can stay the same and
// an older firmware keeps reading (and round-tripping) the settings it knows.
#define SETTINGS_REV   2        // 0 = before the watchface byte existed, 1 = flyInTime held ms per pixel

struct Settings {
  uint16_t magic;        // validity marker
  uint8_t  settingsRev;  // layout revision, see SETTINGS_REV (was padding in rev 0)
  uint8_t  watchface;    // WATCHFACE_CLASSIC, WATCHFACE_TETRIS_ID, WATCHFACE_MUNCHER_ID or WATCHFACE_ALIENS_ID
  int32_t  tzOffset;     // base UTC offset in seconds, WITHOUT daylight saving (CET = 3600)
  uint8_t  dst;          // daylight saving mode: DST_WINTER, DST_SUMMER (fixed) or DST_AUTO
  uint8_t  brightness;   // 0..255 master intensity (scales all drawn colors)
  ClassicSettings classic;   // the classic face's fly-in time, colours and fly-in sides (face_classic.h)
  uint8_t  syncHour;     // hour of daily NTP resync
  uint8_t  syncMinute;   // minute of daily NTP resync
  uint8_t  autoBright;   // 1 = BH1750 light sensor controls the master brightness
  uint16_t luxDark;      // lux at/below which brightness = brightMin
  uint16_t luxBright;    // lux at/above which brightness = brightMax
  uint8_t  brightMin;    // master brightness in darkness
  uint8_t  brightMax;    // master brightness in bright light
};

// settingsRev and watchface went into the two padding bytes that sat between
// magic and tzOffset, so the struct keeps the size the stored blobs have. That
// matters: SettingsStore::read() on the S3 accepts an NVS entry only at exactly
// sizeof(Settings), and a mismatch would silently reset EVERY setting.
static_assert(sizeof(Settings) == 32,
              "Settings must stay 32 bytes - stored blobs are read back by exact size");
static_assert(offsetof(Settings, classic) == 10 && offsetof(Settings, syncHour) == 23,
              "The classic face's settings must stay where the stored blobs have them");

// Watchfaces (Settings::watchface). A value a firmware does not know falls
// back to the classic face (applySettings()).
const uint8_t WATCHFACE_CLASSIC    = 0;  // six flying GFX-font digits, HH MM SS
const uint8_t WATCHFACE_TETRIS_ID  = 1;  // HH:MM built from falling tetromino blocks
const uint8_t WATCHFACE_MUNCHER_ID = 2;  // HH:MM in dots, eaten and laid anew by a muncher
const uint8_t WATCHFACE_ALIENS_ID  = 3;  // HH:MM of aliens, shot and marching in anew
const uint8_t WATCHFACE_COUNT      = 4;

// Daylight saving modes (Settings::dst). 0 and 1 keep the meaning the byte had
// before the automatic mode existed, so stored settings stay valid.
const uint8_t DST_WINTER = 0;  // standard time, fixed
const uint8_t DST_SUMMER = 1;  // daylight saving time (+1 h), fixed
const uint8_t DST_AUTO   = 2;  // follow the daylight-saving rule of the selected timezone

extern const Settings DEFAULTS;          // the factory defaults
extern Settings settings;
extern SettingsStore<Settings> clockStore;   // outside the program image: they survive an upload
extern SettingsTable clockTable;         // the web app's keys for them
bool saveSettings();

// ---- The operating concept's (NVS key "ui") ---------------------------------------
// Settings of the operating concept, in a blob of their own, so the 32-byte
// main blob never grows. Fields added later go on the end and are filled in
// by revision, as with the other blobs; an older firmware does not read this
// one at all.
#define UI_SETTINGS_MAGIC 0x5E71
#define UI_SETTINGS_REV   3        // 1 = before syncDays, 2 = before gestureMount
struct UiSettings {
  uint16_t magic;
  uint8_t  rev;
  uint8_t  inputProfile;   // position in INPUT_PROFILES
  uint16_t syncDays;       // with a real-time clock: days between NTP syncs, 0 = no sync (rev 2)
  uint8_t  gestureMount;   // quarter turns clockwise the gesture sensor sits at against the panel (rev 3)
  uint8_t  reserved;
};
const uint16_t SYNC_DAYS_MAX = 366;
extern UiSettings uiSettings;
extern SettingsTable uiTable;
void loadUiSettings();
bool saveUiSettings();

// ---- The GIFs' (NVS key "gif") ----------------------------------------------------
#define GIF_SETTINGS_MAGIC 0x61F5
#define GIF_SETTINGS_REV   3
struct GifSettings {
  uint16_t magic;
  uint8_t  rev;
  uint8_t  randomOn;      // 1 = GIFs come up by themselves
  uint16_t minMinutes;    // shortest time from one GIF to the next
  uint16_t maxMinutes;    // longest
  uint8_t  seconds;       // how long one plays
  uint8_t  minBright;     // rev 2: the panel's brightness at least while a GIF plays
  uint8_t  brightTimes;   // rev 3: and at least this many tenths of the clock's (20 = twice)
  uint8_t  reserved[1];
};
const uint16_t GIF_MINUTES_MAX = 1440;
const uint8_t  GIF_SECONDS_MIN = 2, GIF_SECONDS_MAX = 60;
// The GIF brightness, found on the MatrixPortal S3 (2026-10-07) with the GIFs
// that are hardest to make out (a log fire's dark logs and glow, a city of
// many like colours): 54 in a dark room, twice the clock's with a lamp on.
// Below about 48 the panel's colours step too coarsely for such GIFs anyway.
const uint8_t  GIF_BRIGHT_MIN_DEFAULT = 54, GIF_BRIGHT_TIMES_DEFAULT = 20;
const uint8_t  GIF_BRIGHT_TIMES_MIN = 10, GIF_BRIGHT_TIMES_MAX = 50;
extern GifSettings gifSettings;
extern SettingsTable gifTable;
void gifSettingsCheck();   // each within its range, the longest at least the shortest
void loadGifSettings();
bool saveGifSettings();

// ---- The decoration's (NVS key "deco") ---------------------------------------------
// Decoration - GIFs of their own accord and the faces' extras - and when it
// rests: quiet hours, and a dark room by the light sensor.
#define DECOR_SETTINGS_MAGIC 0xDEC0
#define DECOR_SETTINGS_REV   1
struct DecorSettings {
  uint16_t magic;
  uint8_t  rev;
  uint8_t  extras;      // how often the faces' extras come: 0 never, 1 rare ... DECOR_EXTRAS_MAX often
  uint8_t  quietFrom;   // quiet hours from this hour ...
  uint8_t  quietTo;     // ... to this one; the same hour twice: none
  uint16_t darkLux;     // below this light no decoration; 0 = off
};
static_assert(sizeof(DecorSettings) == 8, "the stored blob keeps its size");
const uint8_t DECOR_EXTRAS_MAX = 6;
// The mean minutes between two extras for each step of the slider (0 = never).
inline constexpr uint8_t DECOR_EXTRAS_MINUTES[DECOR_EXTRAS_MAX + 1] = { 0, 60, 30, 15, 10, 5, 2 };
extern DecorSettings decorSettings;
extern SettingsTable decorTable;
void loadDecorSettings();
bool saveDecorSettings();

// ---- The info band's sources (NVS key "feeds") --------------------------------------
// The place the clock fetches weather and warnings for, and which of its
// sources are on. The sources and the items are in band_data.h, their
// scheduling at the end of the sketch.
#define FEED_SETTINGS_MAGIC 0xFEED
#define FEED_SETTINGS_REV   3          // 1 = before frames, 2 = before the info round
struct FeedSettings {
  uint16_t magic;
  uint8_t  rev;
  uint8_t  on;          // one bit per FeedId: the sources fetched
  int32_t  lat, lon;    // the place, in millionths of a degree
  uint8_t  located;     // 1 once a place is set; nothing is fetched before
  uint8_t  frames;      // 1: an alert's banner has a frame in its level's colour (rev 2)
  uint8_t  roundMinutes;   // minutes between info rounds on a clock without a band; 0 = none (rev 3)
  uint8_t  reserved;
};
static_assert(sizeof(FeedSettings) == 16, "FeedSettings keeps the size stored blobs have");
extern const FeedSettings FEED_DEFAULTS;
// The info round's choices, in minutes (0 = never): each divides the hour.
inline constexpr uint8_t ROUND_CHOICES[] = { 0, 5, 10, 15, 20, 30, 60 };
bool roundChoice(long minutes);   // one of them
extern FeedSettings feedSettings;
extern SettingsStore<FeedSettings> feedStore;
extern SettingsTable feedTable;
bool saveFeedSettings();

#endif  // SETTINGS_H
