// The clock's stored settings (settings.h).
#include "settings.h"

#include "console.h"

// ---- The main blob ---------------------------------------------------------------

// Factory defaults reproduce the original hard-coded behaviour.
const Settings DEFAULTS = {
  SETTINGS_MAGIC,
  SETTINGS_REV,
  WATCHFACE_CLASSIC,
  3600,            // CET base (UTC+1)
  DST_AUTO,        // CET/CEST switched automatically (the original was a fixed UTC+2)
  128,             // neutral: absolute half in manual mode, sensor-as-is trim in auto mode
  { 40,            // the classic face: fly-in time 400 ms
    0, 0, 255,     // blue digits
    255, 255, 255, // fly-in trail (full color; dimmed relative to brightness at render time)
    {3, 1, 3, 1, 1, 1} },
  5, 11,           // original sync time 05:11
  1,               // auto-brightness on (BH1750)
  1, 400,          // lux mapping range: 1 lux (dark) .. 400 lux (bright)
  8, 255           // brightness 8 (dark) .. 255 (bright)
};

Settings settings;

// The settings are stored OUTSIDE the program image so they survive a firmware
// upload, not just a restart: in NVS (board_hal.h).
SettingsStore<Settings> clockStore;

// The web app's keys for them (settings_table.h). A stored value is checked
// in applySettings(): the daylight saving mode and the fly-in time are moved
// into their ranges, a watchface the firmware does not know becomes the
// classic one, the colours are made full ones; nothing else ever was.
static const SettingParam CLOCK_PARAMS[] = {
  { "wf",     SETTING_FIELD(Settings, watchface),         PARAM_RANGE,    PARAM_LIVE,                0,                  WATCHFACE_COUNT - 1, LOAD_DEFAULT, nullptr },
  { "tz",     0, 0,                                       PARAM_SPECIAL,  0,                         0,                  0,                   LOAD_KEEP,    nullptr },   // seconds, as the zone list offers them (applyParams())
  { "dst",    SETTING_FIELD(Settings, dst),               PARAM_RANGE,    0,                         DST_WINTER,         DST_AUTO,            LOAD_CLAMP,   nullptr },
  { "bright", SETTING_FIELD(Settings, brightness),        PARAM_RANGE,    PARAM_LIVE,                0,                  255,                 LOAD_KEEP,    nullptr },
  { "speed",  SETTING_FIELD(Settings, classic.flyInTime), PARAM_SCALED,   PARAM_LIVE,                FLY_IN_MIN_MS / 10, FLY_IN_MAX_MS / 10,  LOAD_CLAMP,   nullptr },
  { "digit",  (uint8_t)offsetof(Settings, classic.digitR), 3, PARAM_COLOUR, PARAM_LIVE | PARAM_REDRAW, 0,                  0,                   LOAD_KEEP,    nullptr },
  { "trail",  (uint8_t)offsetof(Settings, classic.trailR), 3, PARAM_COLOUR, PARAM_LIVE | PARAM_REDRAW, 0,                  0,                   LOAD_KEEP,    nullptr },
  { "dirs",   0, 0,                                       PARAM_SPECIAL,  0,                         0,                  0,                   LOAD_KEEP,    nullptr },   // sent as dir0 .. dir5 (applyParams())
  { "synch",  SETTING_FIELD(Settings, syncHour),          PARAM_RANGE,    0,                         0,                  23,                  LOAD_KEEP,    nullptr },
  { "syncm",  SETTING_FIELD(Settings, syncMinute),        PARAM_RANGE,    0,                         0,                  59,                  LOAD_KEEP,    nullptr },
  { "autob",  SETTING_FIELD(Settings, autoBright),        PARAM_CHECKBOX, PARAM_LIVE,                0,                  1,                   LOAD_KEEP,    nullptr },
  { "luxd",   SETTING_FIELD(Settings, luxDark),           PARAM_RANGE,    PARAM_LIVE,                0,                  65535,               LOAD_KEEP,    nullptr },
  { "luxb",   SETTING_FIELD(Settings, luxBright),         PARAM_RANGE,    PARAM_LIVE,                1,                  65535,               LOAD_KEEP,    nullptr },
  { "brmin",  SETTING_FIELD(Settings, brightMin),         PARAM_RANGE,    PARAM_LIVE,                0,                  255,                 LOAD_KEEP,    nullptr },
  { "brmax",  SETTING_FIELD(Settings, brightMax),         PARAM_RANGE,    PARAM_LIVE,                0,                  255,                 LOAD_KEEP,    nullptr },
};
SettingsTable clockTable = SETTING_TABLE(settings, DEFAULTS, CLOCK_PARAMS);

// Write current settings to flash.
bool saveSettings() {
  settings.magic       = SETTINGS_MAGIC;
  settings.settingsRev = SETTINGS_REV;
  return clockStore.write(settings);
}

// ---- The operating concept's --------------------------------------------------------

static const UiSettings UI_DEFAULTS = { UI_SETTINGS_MAGIC, UI_SETTINGS_REV, 0, 7, 0, 0 };
UiSettings uiSettings;
static SettingsStore<UiSettings> uiStore;
// The web app's keys for them (settings_table.h), with the Inputs page's two
// lists, whose blobs of their own are taken by their own code
// (applyKeysForm(), applyNoticeKeysForm()). The input profile and the
// sensor's mount count only once the Inputs page passed its check: a save
// it fails stores nothing at all (apiSave()).
static const SettingParam UI_PARAMS[] = {
  { "prof",   SETTING_FIELD(UiSettings, inputProfile), PARAM_RANGE,   0, 0, INPUT_PROFILE_COUNT - 1, LOAD_DEFAULT, nullptr },
  { "syncd",  SETTING_FIELD(UiSettings, syncDays),     PARAM_RANGE,   0, 0, SYNC_DAYS_MAX,           LOAD_DEFAULT, nullptr },
  { "gmount", SETTING_FIELD(UiSettings, gestureMount), PARAM_RANGE,   0, 0, 3,                       LOAD_MASK,    nullptr },
  { "keys",   0, 0,                                    PARAM_SPECIAL, 0, 0, 0,                       LOAD_KEEP,    nullptr },   // sent as k0, k1 ...
  { "closes", 0, 0,                                    PARAM_SPECIAL, 0, 0, 0,                       LOAD_KEEP,    nullptr },   // sent as cset, c0, c1 ...
};
SettingsTable uiTable = SETTING_TABLE(uiSettings, UI_DEFAULTS, UI_PARAMS);

// UI settings from flash; the defaults on a clock that never had them, which
// are only written once something is changed. Fields an older revision did
// not have get their default, here in RAM; the next save stores them.
void loadUiSettings() {
  uiStore.begin("ui");
  uiStore.read(uiSettings);
  if (uiSettings.magic != UI_SETTINGS_MAGIC) { uiSettings = UI_DEFAULTS; }
  if (uiSettings.rev < 2) { uiSettings.syncDays = UI_DEFAULTS.syncDays; }
  if (uiSettings.rev < 3) { uiSettings.gestureMount = UI_DEFAULTS.gestureMount; uiSettings.reserved = 0; }
  settingsCheck(uiTable);   // the profile, the sync days, the sensor's mount (UI_PARAMS)
  Console.print("Input profile: "); Console.println(INPUT_PROFILES[uiSettings.inputProfile].name);
}

bool saveUiSettings() {
  uiSettings.magic = UI_SETTINGS_MAGIC;
  uiSettings.rev   = UI_SETTINGS_REV;
  return uiStore.write(uiSettings);
}

// ---- The GIFs' -------------------------------------------------------------------------

static const GifSettings GIF_DEFAULTS = { GIF_SETTINGS_MAGIC, GIF_SETTINGS_REV, 1, 2, 30, 7,
                                          GIF_BRIGHT_MIN_DEFAULT, GIF_BRIGHT_TIMES_DEFAULT, { 0 } };
GifSettings gifSettings;
static SettingsStore<GifSettings> gifStore;
// The web app's keys for them (settings_table.h); the longest time between
// two GIFs is at least the shortest (gifSettingsCheck()).
static const SettingParam GIF_PARAMS[] = {
  { "gifr",   SETTING_FIELD(GifSettings, randomOn),    PARAM_CHECKBOX, 0, 0,                    1,                    LOAD_BOOL,  nullptr },
  { "gifmin", SETTING_FIELD(GifSettings, minMinutes),  PARAM_RANGE,    0, 1,                    GIF_MINUTES_MAX,      LOAD_CLAMP, nullptr },
  { "gifmax", SETTING_FIELD(GifSettings, maxMinutes),  PARAM_RANGE,    0, 1,                    GIF_MINUTES_MAX,      LOAD_CLAMP, nullptr },
  { "gifsec", SETTING_FIELD(GifSettings, seconds),     PARAM_RANGE,    0, GIF_SECONDS_MIN,      GIF_SECONDS_MAX,      LOAD_CLAMP, nullptr },
  { "gifbr",  SETTING_FIELD(GifSettings, minBright),   PARAM_RANGE,    0, 0,                    255,                  LOAD_KEEP,  nullptr },
  { "gifbx",  SETTING_FIELD(GifSettings, brightTimes), PARAM_TENTHS,   0, GIF_BRIGHT_TIMES_MIN, GIF_BRIGHT_TIMES_MAX, LOAD_CLAMP, nullptr },
};
SettingsTable gifTable = SETTING_TABLE(gifSettings, GIF_DEFAULTS, GIF_PARAMS);

// Keeps the settings in range: whole minutes, the maximum not below the minimum.
void gifSettingsCheck() {
  settingsCheck(gifTable);   // each within its range (GIF_PARAMS)
  if (gifSettings.maxMinutes < gifSettings.minMinutes) { gifSettings.maxMinutes = gifSettings.minMinutes; }
}

void loadGifSettings() {
  gifStore.begin("gif");
  gifStore.read(gifSettings);
  if (gifSettings.magic != GIF_SETTINGS_MAGIC) { gifSettings = GIF_DEFAULTS; }
  if (gifSettings.rev < 3) {
    // Rev 3 brings the GIF brightness rule; its start values replace what rev 2
    // (a test build only) had and the 0 rev 1 held in those reserved bytes.
    gifSettings.minBright = GIF_BRIGHT_MIN_DEFAULT;
    gifSettings.brightTimes = GIF_BRIGHT_TIMES_DEFAULT;
    gifSettings.rev = GIF_SETTINGS_REV;
    gifStore.write(gifSettings);
  }
  gifSettingsCheck();
  Console.printf("GIFs: %s, every %u to %u min for %u s\n", gifSettings.randomOn ? "random on" : "random off",
                gifSettings.minMinutes, gifSettings.maxMinutes, gifSettings.seconds);
  Console.printf("GIFs: at least %u bright and %u.%u times the clock's\n", gifSettings.minBright,
                gifSettings.brightTimes / 10, gifSettings.brightTimes % 10);
}

bool saveGifSettings() {
  gifSettings.magic = GIF_SETTINGS_MAGIC;
  gifSettings.rev   = GIF_SETTINGS_REV;
  return gifStore.write(gifSettings);
}

// ---- The decoration's ------------------------------------------------------------------

// About every 5 min, as the Aliens' extras and the Muncher's fruit came before; no quiet.
static const DecorSettings DECOR_DEFAULTS = { DECOR_SETTINGS_MAGIC, DECOR_SETTINGS_REV, 5, 0, 0, 0 };
DecorSettings decorSettings;
static SettingsStore<DecorSettings> decorStore;
// The web app's keys for them (settings_table.h); a new pace of the extras
// draws the next one anew (applyDecorParams()).
static const SettingParam DECOR_PARAMS[] = {
  { "dxr", SETTING_FIELD(DecorSettings, extras),    PARAM_RANGE, 0, 0, DECOR_EXTRAS_MAX, LOAD_DEFAULT, nullptr },
  { "dqf", SETTING_FIELD(DecorSettings, quietFrom), PARAM_RANGE, 0, 0, 23,               LOAD_DEFAULT, nullptr },
  { "dqt", SETTING_FIELD(DecorSettings, quietTo),   PARAM_RANGE, 0, 0, 23,               LOAD_DEFAULT, nullptr },
  { "ddk", SETTING_FIELD(DecorSettings, darkLux),   PARAM_RANGE, 0, 0, 60000,            LOAD_KEEP,    nullptr },
};
SettingsTable decorTable = SETTING_TABLE(decorSettings, DECOR_DEFAULTS, DECOR_PARAMS);

void loadDecorSettings() {
  decorStore.begin("deco");
  decorStore.read(decorSettings);
  if (decorSettings.magic != DECOR_SETTINGS_MAGIC) { decorSettings = DECOR_DEFAULTS; }   // written with the next save
  settingsCheck(decorTable);   // the extras' pace and the quiet hours (DECOR_PARAMS)
}

bool saveDecorSettings() {
  decorSettings.magic = DECOR_SETTINGS_MAGIC;
  decorSettings.rev   = DECOR_SETTINGS_REV;
  return decorStore.write(decorSettings);
}

// ---- The info band's sources ---------------------------------------------------------------

const FeedSettings FEED_DEFAULTS = { FEED_SETTINGS_MAGIC, FEED_SETTINGS_REV,
                                     (uint8_t)((1 << FEED_WEATHER) | (1 << FEED_WARNINGS) | (1 << FEED_AIR)), 0, 0, 0, 1, 15, 0 };
FeedSettings feedSettings;
SettingsStore<FeedSettings> feedStore;
// The web app's keys for them (settings_table.h). The place and the info
// round have their own code (applyFeedParams()), and the round is the 64x32
// clock's alone: the wall clock shows the items in its band.
static const SettingParam FEED_PARAMS[] = {
  { "feedw",  SETTING_FIELD(FeedSettings, on),     PARAM_BIT,      0, FEED_WEATHER,  0, LOAD_KEEP, nullptr },
  { "feeda",  SETTING_FIELD(FeedSettings, on),     PARAM_BIT,      0, FEED_WARNINGS, 0, LOAD_KEEP, nullptr },
  { "feedq",  SETTING_FIELD(FeedSettings, on),     PARAM_BIT,      0, FEED_AIR,      0, LOAD_KEEP, nullptr },
  { "afr",    SETTING_FIELD(FeedSettings, frames), PARAM_CHECKBOX, 0, 0,             1, LOAD_KEEP, nullptr },
  { "lat",    0, 0,                                PARAM_SPECIAL,  0, 0,             0, LOAD_KEEP, nullptr },   // degrees, "" for none
  { "lon",    0, 0,                                PARAM_SPECIAL,  0, 0,             0, LOAD_KEEP, nullptr },
#if !BAND_ON_PANEL
  { "iround", 0, 0,                                PARAM_SPECIAL,  0, 0,             0, LOAD_KEEP, nullptr },   // minutes, one of ROUND_CHOICES
#endif
};
SettingsTable feedTable = SETTING_TABLE(feedSettings, FEED_DEFAULTS, FEED_PARAMS);

bool saveFeedSettings() {
  feedSettings.magic = FEED_SETTINGS_MAGIC;
  feedSettings.rev   = FEED_SETTINGS_REV;
  return feedStore.write(feedSettings);
}

bool roundChoice(long minutes) {
  for (uint8_t c : ROUND_CHOICES) { if (c == minutes) { return true; } }
  return false;
}
