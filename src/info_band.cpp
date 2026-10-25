// The info band on the wall clock (info_band.h).
#include "info_band.h"

#if BAND_ON_PANEL
#include "band_items.h"
#include "brightness.h"
#include "clock_state.h"
#include "clock_time.h"
#include "console.h"
#include "item_icons.h"   // ICON_SIZE and the icons' names
#include "notices.h"
#include "sensors.h"
#include "settings.h"
#include "sky.h"          // sunrise, sunset and the moon's phase
#include "time_sync.h"
#include "time_zones.h"
#include "wifi_station.h"

// What only this module calls, ahead of its use.
struct BandLocal;
static int8_t bandItemOf(const BandTurn &t);
static bool bandTurnSame(const BandTurn &a, const BandTurn &b);
static BandLocal *bandLocalOf(const BandTurn &t);
static uint8_t bandTurnList(BandTurn *t);
static uint8_t bandIconOf(const BandItem &it);
static int16_t bandTextX(const BandTurn &t);
static uint32_t bandLook(const BandTurn &t);
static bool bandWrap(const char *s, uint8_t chars, char lines[2][32]);
static int isoWeek(int year, int yday, int wday);
static void bandDateLines(BandLocal &l);
static bool clockShiftNear(const char *rule, time_t now, time_t &at, bool &forward);
static void bandShiftLines(BandLocal &l);
static void bandHhmm(char *buf, size_t size, double minutes);
static void bandSunLines(BandLocal &l);
static void bandMoonLines(BandLocal &l);
static void bandStatusLines(BandLocal &l);
static void bandLocalUpdate();
static void bandRoomLines(BandLocal &l);
static void bandDraw();
static void bandStart(const BandTurn &t, uint8_t runs);
static void bandNext();

// The wall clock's info band (bandUpdate()): the turn on show - an item by
// source and tag, or one of the clock's own: the clock change, the date, the
// sun, the moon and the room - and the picture it draws into.
static GFXcanvas16 bandPicture(WALL_W, WALL_BAND_H);
// (BandTurnKind and BandTurn: info_band.h.)
const uint8_t BAND_LOCAL_FIRST = TURN_STATUS, BAND_LOCAL_COUNT = 6;   // the clock's own, in their order
// The clock's own turns as they stand, worked out once a minute (bandLocalUpdate()).
struct BandLocal { bool on; uint8_t icon, severity; char text[64]; };   // its parts set apart by " | " (bandWrap())
unsigned long bandStatusTestAt = 0;   // millis() the console showed the clock status as a test; 0 = none
static BandLocal bandLocal[BAND_LOCAL_COUNT];
bool bandLocalDone = false;
const unsigned long BAND_TURN_MS = 6000;
const int16_t BAND_ICON_X = 4, BAND_ICON_Y = (WALL_BAND_H - ICON_SIZE) / 2;
const int16_t BAND_TEXT_X = 32, BAND_TEXT_X_BARE = 4;   // beside an icon, and without one
const int16_t BAND_LINE_Y[2] = { 6, 19 }, BAND_MID_Y = (WALL_BAND_H - 7) / 2;
static BandTurn bandTurn = { TURN_NONE, 0, 0 };   // the item on show
BandTurn bandWant = { TURN_NONE, 0, 0 };   // one that takes the band at once (a new notice)
uint8_t  bandWantRuns = 1;
static unsigned long bandTurnAt = 0;       // millis() it came; 0 = the next one is due
static unsigned long bandTurnMs = BAND_TURN_MS;   // how long it stays
static uint8_t  bandTurnRuns = 1;          // how often its text runs through (or stands BAND_TURN_MS)
static uint32_t bandTurnLook = 0;          // what it showed (bandLook()), to see it change
static char     bandLine[2][32];           // its two lines in the font's code page (CP437), or
static char     bandRunText[sizeof(BandItem::text)];   // its text running through, when they cannot hold it
static uint16_t bandRunWidth = 0;
static int16_t  bandRunLastX = INT16_MIN;
static bool     bandHidden = false;        // dark while the hotspot has the panel

/* ----------------------------------------------------------------------
   The info band on the wall clock: the 192x32 below the face (concept doc,
   section "Info band"). It shows what the clock knows, one item after the
   other, each with its icon and its text in two lines of the 5x7 font beside
   it (the mock-up of 2026-10-05): the alerts first, then the messages, each
   in the order they came, then the information in its order (infoOrder()),
   then the date. An item stands BAND_TURN_MS, or, when its text does not fit
   the two lines, as long as the text takes to run through once. An alert is
   in its level's colour, framed when the frames are on. A new alert or
   message takes the band at once, a message's text twice (bandWant); the
   alert's banner comes over the face as on the 64x32 clock. Show
   notifications starts the band again at its first item. A GIF covers the
   band as it covers the face; the hotspot leaves it dark. Its state is with
   the notices' (bandTurn).
   ---------------------------------------------------------------------- */

// The item a turn shows; -1 for the date, or when the item has gone.
static int8_t bandItemOf(const BandTurn &t) {
  if (t.kind != TURN_ITEM) { return -1; }
  for (uint8_t i = 0; i < bandItemCount; i++) {
    if (bandItems[i].feed == t.feed && bandItems[i].tag == t.tag) { return (int8_t)i; }
  }
  return -1;
}

static bool bandTurnSame(const BandTurn &a, const BandTurn &b) {
  return a.kind == b.kind && a.feed == b.feed && a.tag == b.tag;
}

// One of the clock's own turns; nullptr for an item's.
static BandLocal *bandLocalOf(const BandTurn &t) {
  if (t.kind < BAND_LOCAL_FIRST || t.kind >= BAND_LOCAL_FIRST + BAND_LOCAL_COUNT) { return nullptr; }
  return &bandLocal[t.kind - BAND_LOCAL_FIRST];
}

// Every turn in the band's order; returns how many.
static uint8_t bandTurnList(BandTurn *t) {
  uint8_t order[BAND_ITEMS_MAX], n = 0;
  uint8_t k = noticeOrder(order, false);   // alerts, then messages
  for (uint8_t i = 0; i < k; i++) { t[n++] = { TURN_ITEM, bandItems[order[i]].feed, bandItems[order[i]].tag }; }
  k = infoOrder(order, false);
  for (uint8_t i = 0; i < k; i++) { t[n++] = { TURN_ITEM, bandItems[order[i]].feed, bandItems[order[i]].tag }; }
  for (uint8_t i = 0; i < BAND_LOCAL_COUNT; i++) {
    if (bandLocal[i].on) { t[n++] = { (uint8_t)(BAND_LOCAL_FIRST + i), 0, 0 }; }
  }
  return n;
}

// The icon an item has in the band: an alert's own or the warning sign, any
// other item's own or none.
static uint8_t bandIconOf(const BandItem &it) {
  if (it.level == LEVEL_ALERT) { return noticeIcon(it); }
  return it.icon < ICON_COUNT ? it.icon : ICON_NONE;
}

// Where a turn's text starts: beside its icon, or at the left without one.
static int16_t bandTextX(const BandTurn &t) {
  if (const BandLocal *own = bandLocalOf(t)) { return own->icon != ICON_NONE ? BAND_TEXT_X : BAND_TEXT_X_BARE; }
  int8_t i = bandItemOf(t);
  return (i >= 0 && bandIconOf(bandItems[i]) != ICON_NONE) ? BAND_TEXT_X : BAND_TEXT_X_BARE;
}

// What a turn shows, as a number, so the band sees its item change (a new
// temperature, a warning that got worse) or its own lines (the day turns).
static uint32_t bandLook(const BandTurn &t) {
  if (BandLocal *l = bandLocalOf(t)) { return itemTag(l->text) ^ l->icon; }
  int8_t i = bandItemOf(t);
  if (i < 0) { return 0; }
  const BandItem &it = bandItems[i];
  uint32_t h = itemTag(it.text);
  const uint8_t parts[] = { it.icon, it.level, it.severity, it.intensity, it.chance };
  for (uint8_t p : parts) { h = (h ^ p) * 16777619u; }
  return h;
}

// The parts of a text in the band are set apart by " | " (Peter, 2026-10-18,
// for every text there: the clock's own, the sources' it puts together, and
// Home Assistant's). A text in two lines of up to `chars` characters: in one
// when it fits, " | " and all; else broken at the first " | " after which the
// rest fits the second line, the separator left out there ("10°C overcast" /
// "10/8°C | rain 100%"); else between words, a separator at the break left
// out too. False when none of that fits: the text runs through instead.
static bool bandWrap(const char *s, uint8_t chars, char lines[2][32]) {
  lines[0][0] = lines[1][0] = '\0';
  while (*s == ' ') { s++; }
  if (strlen(s) <= chars) {
    strlcpy(lines[0], s, sizeof(lines[0]));
    return true;
  }
  for (const char *p = strstr(s, " | "); p && p - s <= chars; p = strstr(p + 1, " | ")) {
    if (strlen(p + 3) > chars) { continue; }
    memcpy(lines[0], s, (size_t)(p - s));
    lines[0][p - s] = '\0';
    strlcpy(lines[1], p + 3, sizeof(lines[1]));
    return true;
  }
  int cut = -1;
  for (int i = chars; i > 0 && cut < 0; i--) { if (s[i] == ' ') { cut = i; } }
  if (cut < 0) { return false; }   // a word longer than a line
  int end = cut;                   // the first line without a separator, and the spaces, at its end
  while (end > 0 && (s[end - 1] == ' ' || s[end - 1] == '|')) { end--; }
  const char *rest = s + cut;
  while (*rest == ' ' || *rest == '|') { rest++; }   // nor at the second's start
  if (strlen(rest) > chars) { return false; }
  memcpy(lines[0], s, (size_t)end);
  lines[0][end] = '\0';
  strlcpy(lines[1], rest, sizeof(lines[1]));
  return true;
}

// The ISO 8601 week of a date: the week with the year's first Thursday is 1.
// yday and wday as struct tm has them (0 = 1 January, 0 = Sunday).
static int isoWeek(int year, int yday, int wday) {
  auto weeks = [](int y) {   // 53 when the year starts on a Thursday, or a leap year on a Wednesday
    auto p = [](int v) { return (v + v / 4 - v / 100 + v / 400) % 7; };
    return 52 + ((p(y) == 4 || p(y - 1) == 3) ? 1 : 0);
  };
  int isoDay = wday == 0 ? 7 : wday;
  int week = (yday + 1 - isoDay + 10) / 7;
  if (week < 1) { return weeks(year - 1); }
  if (week > weeks(year)) { return 1; }
  return week;
}

const char *const BAND_DAYS[] = { "Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday" };
const char *const BAND_MONTHS[] = { "January", "February", "March", "April", "May", "June", "July",
                                    "August", "September", "October", "November", "December" };

// The date in the clock's language: "Sunday 18 October" and "2026, week 42".
static void bandDateLines(BandLocal &l) {
  l.on = clockIsSet();
  l.icon = ICON_CALENDAR;
  struct tm t;
  gmtime_r(&localTime, &t);
  snprintf(l.text, sizeof(l.text), "%s %d %s %d | week %d", BAND_DAYS[t.tm_wday], t.tm_mday, BAND_MONTHS[t.tm_mon],
           t.tm_year + 1900, isoWeek(t.tm_year + 1900, t.tm_yday, t.tm_wday));
}

// The change of the clocks nearest to `now` under a daylight saving rule
// (POSIX TZ), between 12 hours before and 24 hours after it: its UTC
// instant, and whether the clocks go forward. False when there is none, or
// no rule (a zone without daylight saving, or it set by hand).
static bool clockShiftNear(const char *rule, time_t now, time_t &at, bool &forward) {
  if (!rule) { return false; }
  const time_t BEFORE = 12L * 3600L, AHEAD = 24L * 3600L;
  bool was = dstActiveAt(rule, now - BEFORE), is = dstActiveAt(rule, now), will = dstActiveAt(rule, now + AHEAD);
  time_t lo, hi;
  bool loState;
  if (was != is)       { lo = now - BEFORE; hi = now; loState = was; }
  else if (is != will) { lo = now; hi = now + AHEAD; loState = is; }
  else                 { return false; }
  while (hi - lo > 60) {   // the change is on a whole minute: found to within one
    time_t mid = lo + (hi - lo) / 2;
    if (dstActiveAt(rule, mid) == loState) { lo = mid; } else { hi = mid; }
  }
  at = hi - hi % 60;
  forward = !loState;      // into summer time
  return true;
}

// The clock change, from 24 hours before until 12 hours after it: "Clocks go
// back 1 h" / "tonight at 3:00", the wall time before the change; afterwards
// "Clocks went back 1 h" / "last night".
static void bandShiftLines(BandLocal &l) {
  time_t at;
  bool forward;
  const char *rule = settings.dst == DST_AUTO ? tzRule(settings.tzOffset) : nullptr;
  l.on = clockIsSet() && clockShiftNear(rule, clockNow(), at, forward);
  if (!l.on) { return; }
  l.icon = ICON_CALENDAR;
  time_t before = at + (time_t)settings.tzOffset + (forward ? 0 : 3600);   // the wall time it happens at
  int hour = (int)(before % 86400 / 3600), minute = (int)(before % 3600 / 60);
  bool coming = at > clockNow();
  int n = snprintf(l.text, sizeof(l.text), "Clocks %s %s 1 h | ", coming ? "go" : "went", forward ? "forward" : "back");
  char *rest = l.text + n;
  size_t room = sizeof(l.text) - (size_t)n;
  if (!coming && hour < 6) {
    strlcpy(rest, "last night", room);
  } else if (!coming) {
    snprintf(rest, room, "at %d:%02d", hour, minute);
  } else {
    const char *when = hour < 6 ? "tonight" : (before / 86400 == localTime / 86400 ? "today" : "tomorrow");
    snprintf(rest, room, "%s at %d:%02d", when, hour, minute);
  }
}

// "07:42" from minutes after local midnight.
static void bandHhmm(char *buf, size_t size, double minutes) {
  long m = lround(minutes) % 1440;
  if (m < 0) { m += 1440; }
  snprintf(buf, size, "%02ld:%02ld", m / 60, m % 60);
}

// Sunrise and sunset at the place set for the weather (sky.h): today's, and
// after today's sunset tomorrow's; the midnight sun and the polar night in
// words. Without a place, none.
static void bandSunLines(BandLocal &l) {
  l.on = clockIsSet() && feedSettings.located;
  if (!l.on) { return; }
  l.icon = ICON_CLEAR_DAY;
  double lat = feedSettings.lat / 1e6, lon = feedSettings.lon / 1e6, off = tzTotalOffset() / 60.0;
  long day = (long)(localTime / 86400);
  double rise, set;
  uint8_t kind = sky::sunEvent(day, lat, lon, true, rise);
  sky::sunEvent(day, lat, lon, false, set);
  bool tomorrow = kind == sky::SUN_RISES_AND_SETS && (double)(localTime % 86400) / 60.0 >= set + off;
  if (tomorrow) {
    kind = sky::sunEvent(day + 1, lat, lon, true, rise);
    sky::sunEvent(day + 1, lat, lon, false, set);
  }
  if (kind == sky::SUN_UP_ALL_DAY) {
    strlcpy(l.text, "Midnight sun | no sunset today", sizeof(l.text));
    return;
  }
  if (kind == sky::SUN_DOWN_ALL_DAY) {
    l.icon = ICON_CLEAR_NIGHT;
    strlcpy(l.text, "Polar night | no sunrise today", sizeof(l.text));
    return;
  }
  char r[8], s[8];
  bandHhmm(r, sizeof(r), rise + off);
  bandHhmm(s, sizeof(s), set + off);
  snprintf(l.text, sizeof(l.text), "Sunrise %s%s | Sunset %s%s", tomorrow ? "tomorrow " : "", r, tomorrow ? "tomorrow " : "", s);
}

// The moon's phase and how much of it is lit, and the next full or new moon,
// whichever comes first: "Waxing gibbous, 78%" / "Full moon Mon 26 Oct".
static void bandMoonLines(BandLocal &l) {
  l.on = clockIsSet();
  if (!l.on) { return; }
  l.icon = ICON_CLEAR_NIGHT;
  double now = (double)clockNow(), e = sky::moonElongation(now);
  int n = snprintf(l.text, sizeof(l.text), "%s | %d%% lit | ", sky::moonPhaseName(e), sky::moonLit(e));
  l.text[0] = (char)toupper((unsigned char)l.text[0]);
  double full = sky::moonNext(now, true), fresh = sky::moonNext(now, false);
  bool isFull = full < fresh;
  time_t at = (time_t)(isFull ? full : fresh) + (time_t)tzTotalOffset();
  struct tm t;
  gmtime_r(&at, &t);
  snprintf(l.text + n, sizeof(l.text) - (size_t)n, "%s moon %.3s %d %.3s", isFull ? "Full" : "New", BAND_DAYS[t.tm_wday], t.tm_mday,
           BAND_MONTHS[t.tm_mon]);
}

// The clock's trouble in words while the status pixel is red (syncOverdue(),
// sensorsMissing): a new home WiFi that does not join, "Home WiFi does not
// join:" and its name, or a time sync that keeps failing, "Time sync is
// failing" and when the last good one was; else a sensor found before that is
// missing now, "Sensor missing" and which. The warning sign in yellow, the
// first of the clock's own turns. Nothing while all is well.
const unsigned long BAND_STATUS_TEST_MS = 2UL * 60UL * 1000UL;   // the console's "status test"
static void bandStatusLines(BandLocal &l) {
  bool test = bandStatusTestAt && millisNow - bandStatusTestAt < BAND_STATUS_TEST_MS;
  bool sync = syncOverdue() || test;
  l.on = clockIsSet() && (sync || sensorsMissing);
  if (!l.on) { return; }
  l.icon = ICON_WARNING;
  l.severity = 1;
  if (!sync) {
    int n = snprintf(l.text, sizeof(l.text), "Sensor missing |");
    for (uint8_t i = 0; i < SENSOR_COUNT && n > 0 && n < (int)sizeof(l.text); i++) {
      if (sensorsMissing & (1 << i)) { n += snprintf(l.text + n, sizeof(l.text) - (size_t)n, " %s", SENSOR_KINDS[i].chip); }
    }
    return;
  }
  if (wifiStored() && !wifiCreds.verified && joinOutcome == JOIN_FAILED) {
    snprintf(l.text, sizeof(l.text), "Home WiFi does not join | %s", wifiCreds.ssid);
    return;
  }
  if (ntpSyncedAt == 0) {
    strlcpy(l.text, "Time sync is failing | no good sync yet", sizeof(l.text));
    return;
  }
  time_t at = ntpSyncedAt + (time_t)tzTotalOffset();
  struct tm t;
  gmtime_r(&at, &t);
  snprintf(l.text, sizeof(l.text), "Time sync is failing | last good %d %.3s %02d:%02d", t.tm_mday, BAND_MONTHS[t.tm_mon],
           t.tm_hour, t.tm_min);
}

// The clock's own turns anew: once a minute, and when the clock gets its time.
static void bandLocalUpdate() {
  static bool wasSet = false;
  if (bandLocalDone && !minuteTrigger && clockIsSet() == wasSet) { return; }
  bandLocalDone = true;
  wasSet = clockIsSet();
  bandStatusLines(bandLocal[TURN_STATUS - BAND_LOCAL_FIRST]);
  bandShiftLines(bandLocal[TURN_SHIFT - BAND_LOCAL_FIRST]);
  bandDateLines(bandLocal[TURN_DATE - BAND_LOCAL_FIRST]);
  bandSunLines(bandLocal[TURN_SUN - BAND_LOCAL_FIRST]);
  bandMoonLines(bandLocal[TURN_MOON - BAND_LOCAL_FIRST]);
  bandRoomLines(bandLocal[TURN_ROOM - BAND_LOCAL_FIRST]);
}

// The room's temperature and humidity from the board's SHTC3, filtered and
// corrected for its warmth: "Indoor 22.4°C | humidity 48%". Off until it gave
// a reading.
static void bandRoomLines(BandLocal &l) {
  l.on = roomValid;
  l.icon = ICON_INDOOR;
  if (!roomValid) { l.text[0] = '\0'; return; }
  long t = lroundf(roomTempNow() * 10.0f);   // tenths, without the C library's float printing
  snprintf(l.text, sizeof(l.text), "Indoor %s%ld.%ld°C | humidity %ld%%", t < 0 ? "-" : "", labs(t) / 10, labs(t) % 10,
           lroundf(roomHumNow()));
}

// The band's picture as the turn stands, onto the wall.
static void bandDraw() {
  GFXcanvas16 &b = bandPicture;
  b.fillScreen(0);
  int8_t i = bandItemOf(bandTurn);
  const BandLocal *own = bandLocalOf(bandTurn);
  if (bandTurn.kind == TURN_NONE || (bandTurn.kind == TURN_ITEM && i < 0) || (own && !own->on)) {
    matrix.showBand(b.getBuffer());
    return;
  }
  IconLook look = { own ? own->icon : (uint8_t)ICON_NONE, own ? own->severity : (uint8_t)0, INTENSITY_NONE, CHANCE_UNKNOWN };
  uint16_t ink = scaledColorVisible(235, 235, 235);
  bool frame = false;
  if (i >= 0) {
    const BandItem &it = bandItems[i];
    look = { bandIconOf(it), it.severity, it.intensity, it.chance };
    if (it.level == LEVEL_ALERT) {
      IconRgb c = levelRgb(it.severity);
      ink = scaledColorVisible(c.r, c.g, c.b);
      frame = feedSettings.frames;
    }
  }
  int16_t tx = look.icon != ICON_NONE ? BAND_TEXT_X : BAND_TEXT_X_BARE;
  b.setFont();          // the built-in 5x7 font, code page 437
  b.setTextSize(1);
  b.setTextWrap(false);
  b.cp437(true);
  b.setTextColor(ink);
  if (bandRunText[0]) {
    uint32_t moved = (uint32_t)(millisNow - bandTurnAt) * RUN_PX_PER_S / 1000UL;
    uint32_t pass = bandRunWidth + (WALL_W - tx);
    int16_t x = moved >= bandTurnRuns * pass ? -(int16_t)bandRunWidth : WALL_W - (int16_t)(moved % pass);
    bandRunLastX = x;
    b.setCursor(x, BAND_MID_Y);
    b.print(bandRunText);
    b.fillRect(0, 0, tx, WALL_BAND_H, 0);   // it runs in and out beside the icon
  } else if (bandLine[1][0]) {
    b.setCursor(tx, BAND_LINE_Y[0]);
    b.print(bandLine[0]);
    b.setCursor(tx, BAND_LINE_Y[1]);
    b.print(bandLine[1]);
  } else {
    b.setCursor(tx, BAND_MID_Y);
    b.print(bandLine[0]);
  }
  b.cp437(false);
  if (look.icon != ICON_NONE) { drawItemIconOn(b, BAND_ICON_X, BAND_ICON_Y, look); }
  if (frame) { b.drawRect(0, 0, WALL_W, WALL_BAND_H, ink); }
  matrix.showBand(b.getBuffer());
}

// Puts a turn on the band, its text running through `runs` times - or, when
// it fits the two lines, standing `runs` x BAND_TURN_MS.
static void bandStart(const BandTurn &t, uint8_t runs) {
  bandTurn     = t;
  bandTurnAt   = millisNow ? millisNow : 1;
  bandTurnRuns = runs;
  bandTurnLook = bandLook(t);
  bandTurnMs   = BAND_TURN_MS * runs;
  bandRunText[0] = '\0';
  bandRunLastX = INT16_MIN;
  int8_t i = bandItemOf(t);
  const BandLocal *own = bandLocalOf(t);
  const char *src = own ? own->text : i >= 0 ? bandItems[i].text : nullptr;
  if (src) {
    char text[sizeof(BandItem::text)];
    textToCp437(text, src, sizeof(text));
    int16_t tx = bandTextX(t);
    if (!bandWrap(text, (uint8_t)((WALL_W - tx - 2) / 6), bandLine)) {
      strlcpy(bandRunText, text, sizeof(bandRunText));
      bandRunWidth = (uint16_t)(strlen(bandRunText) * 6);   // 5 px a character and 1 between
      unsigned long ms = (unsigned long)runs * (bandRunWidth + (WALL_W - tx)) * 1000UL / RUN_PX_PER_S;
      bandTurnMs = max(ms, BAND_TURN_MS);
    }
    panelSeparators(bandLine[0]);   // broken where the bars are; shown as dots
    panelSeparators(bandLine[1]);
    panelSeparators(bandRunText);
  }
  bandDraw();
}

// The turn after the one on show, in the band's order; the first when that
// one has gone. Nothing to show: the band dark, and a look again later.
static void bandNext() {
  BandTurn list[BAND_ITEMS_MAX + BAND_LOCAL_COUNT];
  uint8_t n = bandTurnList(list);
  if (n == 0) {
    bandTurn   = { TURN_NONE, 0, 0 };
    bandTurnAt = millisNow ? millisNow : 1;
    bandTurnMs = BAND_TURN_MS;
    bandDraw();
    return;
  }
  uint8_t next = 0;
  for (uint8_t i = 0; i < n; i++) {
    if (bandTurnSame(list[i], bandTurn)) { next = (uint8_t)((i + 1) % n); break; }
  }
  bandStart(list[next], 1);
}

// Show notifications: the band again from its first item.
void bandRestart() {
  bandTurn   = { TURN_NONE, 0, 0 };
  bandTurnAt = 0;
}

// On every pass outside the hotspot.
void bandUpdate() {
  bandLocalUpdate();
  if (screen == SCREEN_GIF || screen == SCREEN_OFF) { return; }   // a GIF has the whole wall; Off leaves it black
  bandHidden = false;
  if (bandWant.kind != TURN_NONE) {
    BandTurn w = bandWant;
    bandWant.kind = TURN_NONE;
    if (bandItemOf(w) >= 0) {
      bandStart(w, bandWantRuns);
      return;
    }
  }
  const BandLocal *own = bandLocalOf(bandTurn);
  bool gone = (bandTurn.kind == TURN_ITEM && bandItemOf(bandTurn) < 0) || (own && !own->on);
  if (bandTurnAt == 0 || millisNow - bandTurnAt >= bandTurnMs || gone) {
    bandNext();
    return;
  }
  if (bandTurn.kind != TURN_NONE && bandLook(bandTurn) != bandTurnLook) {   // its item changed, or the day
    bandStart(bandTurn, bandTurnRuns);
    return;
  }
  if (bandRunText[0]) {
    int16_t tx = bandTextX(bandTurn);
    uint32_t moved = (uint32_t)(millisNow - bandTurnAt) * RUN_PX_PER_S / 1000UL;
    uint32_t pass = bandRunWidth + (WALL_W - tx);
    int16_t x = moved >= bandTurnRuns * pass ? -(int16_t)bandRunWidth : WALL_W - (int16_t)(moved % pass);
    if (x != bandRunLastX) { bandDraw(); return; }
  }
  if (matrix.bandStale()) { bandDraw(); }   // a GIF or a test picture wrote over it
}

#if defined(CLOCK_DEBUG)
// At start-up: the ISO weeks of dates around the turn of the year (2026 has
// 53 weeks, 2020 too), and how texts break into the band's two lines.
void bandSelfTest() {
  struct Week { int y, m, d, week; };
  static const Week WEEKS[] = {
    { 2026, 1, 1, 1 }, { 2026, 10, 18, 42 }, { 2026, 10, 19, 43 }, { 2026, 12, 31, 53 }, { 2027, 1, 1, 53 },
    { 2027, 1, 4, 1 }, { 2024, 12, 30, 1 }, { 2021, 1, 3, 53 }, { 2025, 12, 29, 1 },
  };
  uint8_t wrong = 0;
  for (const Week &w : WEEKS) {
    time_t t = (time_t)daysFromCivil(w.y, (unsigned)w.m, (unsigned)w.d) * 86400;
    struct tm v;
    gmtime_r(&t, &v);
    int got = isoWeek(v.tm_year + 1900, v.tm_yday, v.tm_wday);
    if (got != w.week) { wrong++; Console.printf("Band: %d-%02d-%02d is week %d, not %d\n", w.y, w.m, w.d, got, w.week); }
  }
  struct Wrap { const char *text; bool fits; const char *first, *second; };
  static const Wrap WRAPS[] = {
    { "10\xF8" "C overcast, 10/8\xF8" "C, rain 100%", true, "10\xF8" "C overcast, 10/8\xF8" "C,", "rain 100%" },
    { "Rain from 15:15", true, "Rain from 15:15", "" },
    { "abcdefghijklmnopqrstuvwxyz", true, "abcdefghijklmnopqrstuvwxyz", "" },
    { "abcdefghijklmnopqrstuvwxyz0", false, "", "" },
    { "One two three four five six seven eight nine ten eleven", false, "", "" },
    // The parts set apart by " | " (Peter, 2026-10-18): broken at the first
    // after which the rest fits, the separator left out at the break.
    { "10\xF8" "C overcast | 10/8\xF8" "C | rain 100% | UV 3", true, "10\xF8" "C overcast", "10/8\xF8" "C | rain 100% | UV 3" },
    { "First quarter | 53% lit | Full moon Mon 26 Oct", true, "First quarter | 53% lit", "Full moon Mon 26 Oct" },
    { "Sunday 18 October 2026 | week 42", true, "Sunday 18 October 2026", "week 42" },
    { "Air quality fair | index 32", true, "Air quality fair", "index 32" },
    { "fair | index 32", true, "fair | index 32", "" },
    { "Public holiday today | All Saints' Day", true, "Public holiday today", "All Saints' Day" },
    { "One two three | four five six seven eight nine", true, "One two three | four five", "six seven eight nine" },
    { "Wednesday 30 September 2026 | week 40", true, "Wednesday 30 September", "2026 | week 40" },
    { "aaaaaaaaaaaaaaaaaaaa | bbbbbbbbbbbbbbbbbbbbbbbb", true, "aaaaaaaaaaaaaaaaaaaa", "bbbbbbbbbbbbbbbbbbbbbbbb" },
    { "aaaaaaaaaaaaaaaaaaaaaaa bbbb | ccc", true, "aaaaaaaaaaaaaaaaaaaaaaa", "bbbb | ccc" },
  };
  for (const Wrap &w : WRAPS) {
    char lines[2][32];
    bool fits = bandWrap(w.text, 26, lines);
    if (fits != w.fits || (fits && (strcmp(lines[0], w.first) != 0 || strcmp(lines[1], w.second) != 0))) {
      wrong++;
      Console.printf("Band: \"%s\" gave %s \"%s\" / \"%s\"\n", w.text, fits ? "two lines" : "no fit", lines[0], lines[1]);
    }
  }
  // Sunrise and sunset as sunrise-and-sunset.com gave them on 2026-10-18,
  // to within 2 minutes; Tromsø's polar night and midnight sun.
  struct Sun { int y, m, d; double lat, lon; int off; int rise, set; uint8_t kind; };
  static const Sun SUNS[] = {
    { 2026, 10, 18, 48.7758, 9.1829, 120, 7 * 60 + 48, 18 * 60 + 27, sky::SUN_RISES_AND_SETS },   // Stuttgart
    { 2026, 10, 18, 52.5200, 13.4050, 120, 7 * 60 + 37, 18 * 60 + 4, sky::SUN_RISES_AND_SETS },   // Berlin
    { 2026, 10, 25, 52.5200, 13.4050, 60, 6 * 60 + 50, 16 * 60 + 49, sky::SUN_RISES_AND_SETS },   // Berlin, CET
    { 2026, 12, 21, 69.6500, 18.9600, 60, 0, 0, sky::SUN_DOWN_ALL_DAY },                            // Tromsø
    { 2026, 6, 21, 69.6500, 18.9600, 120, 0, 0, sky::SUN_UP_ALL_DAY },
  };
  for (const Sun &s : SUNS) {
    long day = daysFromCivil(s.y, (unsigned)s.m, (unsigned)s.d);
    double rise, set;
    uint8_t kind = sky::sunEvent(day, s.lat, s.lon, true, rise);
    sky::sunEvent(day, s.lat, s.lon, false, set);
    bool ok = kind == s.kind && (kind != sky::SUN_RISES_AND_SETS ||
                                 (fabs(rise + s.off - s.rise) <= 2.0 && fabs(set + s.off - s.set) <= 2.0));
    if (!ok) {
      wrong++;
      Console.printf("Band: sun on %d-%02d-%02d at %.2f, %.2f gave kind %u, %.1f and %.1f min, not %d and %d\n", s.y, s.m,
                    s.d, s.lat, s.lon, (unsigned)kind, rise + s.off, set + s.off, s.rise, s.set);
    }
  }
  // The moon's phases in October 2026 as moongiant.com gave them, to within
  // 2 degrees (4 hours); the next full and new moon to within 2 hours.
  struct Moon { const char *at; double elongation; };
  static const Moon MOONS[] = {
    { "2026-10-03 13:25", 270 }, { "2026-10-10 15:50", 0 }, { "2026-10-18 16:12", 90 }, { "2026-10-26 04:11", 180 },
  };
  auto utcOf = [](const char *s) {
    int y, mo, d, h, mi;
    sscanf(s, "%d-%d-%d %d:%d", &y, &mo, &d, &h, &mi);
    return (double)daysFromCivil(y, (unsigned)mo, (unsigned)d) * 86400.0 + h * 3600.0 + mi * 60.0;
  };
  for (const Moon &m : MOONS) {
    double e = sky::moonElongation(utcOf(m.at));
    double off = fmod(e - m.elongation + 540.0, 360.0) - 180.0;
    if (fabs(off) > 2.0) { wrong++; Console.printf("Band: the moon at %s is %.2f degrees ahead, not %.0f\n", m.at, e, m.elongation); }
  }
  double from = utcOf("2026-10-14 00:00");
  double full = sky::moonNext(from, true), fresh = sky::moonNext(from, false);
  if (fabs(full - utcOf("2026-10-26 04:11")) > 7200.0 || fabs(fresh - utcOf("2026-11-09 07:02")) > 7200.0) {
    wrong++;
    Console.printf("Band: next full moon %.0f, new moon %.0f s off\n", full - utcOf("2026-10-26 04:11"),
                  fresh - utcOf("2026-11-09 07:02"));
  }
  // The clock changes of central Europe (01:00 UTC on the last Sunday of
  // March and October), seen from before, after and between them.
  struct Shift { const char *now; bool found; const char *at; bool forward; };
  static const Shift SHIFTS[] = {
    { "2026-10-24 12:00", true, "2026-10-25 01:00", false }, { "2026-10-25 06:00", true, "2026-10-25 01:00", false },
    { "2026-03-28 12:00", true, "2026-03-29 01:00", true },  { "2026-10-20 12:00", false, "", false },
    { "2026-10-25 14:00", false, "", false },
  };
  for (const Shift &s : SHIFTS) {
    time_t at = 0;
    bool forward = false;
    bool found = clockShiftNear(tzRule(3600), (time_t)utcOf(s.now), at, forward);
    if (found != s.found || (found && (at != (time_t)utcOf(s.at) || forward != s.forward))) {
      wrong++;
      Console.printf("Band: clock change from %s gave %d at %ld, forward %d\n", s.now, found, (long)at, forward);
    }
  }
  unsigned cases = sizeof(WEEKS) / sizeof(WEEKS[0]) + sizeof(WRAPS) / sizeof(WRAPS[0]) + sizeof(SUNS) / sizeof(SUNS[0]) +
                   sizeof(MOONS) / sizeof(MOONS[0]) + 1 + sizeof(SHIFTS) / sizeof(SHIFTS[0]);
  Console.printf("Band self-test: %s, %u cases\n", wrong ? "FAILED" : "all right", cases);
}
#endif

// While the hotspot has the panel: the band dark, once; it starts again after.
void bandHide() {
  if (bandHidden) { return; }
  bandHidden = true;
  bandRestart();
  bandPicture.fillScreen(0);
  matrix.showBand(bandPicture.getBuffer());
}
#endif
