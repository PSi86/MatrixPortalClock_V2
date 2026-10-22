// The info band's own sources: when they are fetched, and what comes back
// (feeds.h).
#include "feeds.h"

#include "band_items.h"
#include "clock_state.h"
#include "clock_time.h"
#include "console.h"
#include "lan_access.h"
#include "settings.h"
#include "time_sync.h"
#include "wifi_station.h"

// What only this module calls, ahead of its use.
static bool feedsReady();
static bool feedDueWithin(uint8_t f, unsigned long early);
static int8_t feedDue(unsigned long early);
static void feedFailed(uint8_t f, const char *why);
static void feedApply(const FeedResult &res);
static bool microDegreesOf(const String &v, int32_t limit, int32_t &out);

// Per source: when it is due, and how its last fetch went (FeedRun: feeds.h).
FeedRun       feedRuns[FEED_COUNT];
static bool   feedTaskOK = false;        // the fetch task is running
bool          feedBusy = false;          // a job is with it
#if BOARD_WAVESHARE_WALL
unsigned long feedSentAt = 0;            // millis() the last job was handed to it (wallOutputWatch())
#endif
static unsigned long feedWantedAt = 0;   // millis() the station was asked for
char          feedPlace[32] = "";        // the warnings' place, as the DWD names it
const unsigned long FEED_STATION_MS = 60UL * 1000UL;       // the longest wait for the station
const unsigned long FEED_RETRY_MS   = 60UL * 1000UL;       // the first wait after a failure, doubled each time
const unsigned long FEED_GATHER_MS  = 3UL * 60UL * 1000UL; // with the radio on anyway, sources due this soon come along

/* ----------------------------------------------------------------------
   Info band data: when the clock's own sources are fetched, and the items
   they bring (src/band_data.h has the sources and the fetch task). Until
   the band exists - it comes with the wall clock - the console and the
   config page show the items.

   A source due wants the station (RADIO_FEEDS), and its fetch goes to the
   task once the station is up; sources due within FEED_GATHER_MS come along
   while the radio is on anyway, except one whose last fetch failed: that one
   waits out its back-off. Then the radio goes off again unless the
   sync still needs it (stationUpdate()).
   ---------------------------------------------------------------------- */

void loadFeedSettings() {
  feedStore.begin("feeds");
  feedStore.read(feedSettings);
  if (feedSettings.magic != FEED_SETTINGS_MAGIC) { feedSettings = FEED_DEFAULTS; }
  if (feedSettings.rev < 2) { feedSettings.frames = FEED_DEFAULTS.frames; }
  if (feedSettings.rev < 3) { feedSettings.roundMinutes = FEED_DEFAULTS.roundMinutes; }
  if (!roundChoice(feedSettings.roundMinutes)) { feedSettings.roundMinutes = FEED_DEFAULTS.roundMinutes; }
  feedTaskOK = feedBegin();
  if (!feedTaskOK) {
    Console.println("Info band: no RAM for the fetch task");
  } else if (!feedSettings.located) {
    Console.println("Info band: no place set - set one on the config page");
  } else {
    char lat[16], lon[16];
    feedDegrees(lat, sizeof(lat), feedSettings.lat);
    feedDegrees(lon, sizeof(lon), feedSettings.lon);
    Console.printf("Info band: weather %s, warnings %s, air quality %s, for %s, %s\n", feedOn(FEED_WEATHER) ? "on" : "off",
                  feedOn(FEED_WARNINGS) ? "on" : "off", feedOn(FEED_AIR) ? "on" : "off", lat, lon);
  }
}

bool feedOn(uint8_t f) {
  return (feedSettings.on & (1 << f)) != 0;
}

// Whether the sources can be fetched at all: the task, a place, a home WiFi,
// and a time on the clock, by which the items run out.
static bool feedsReady() {
  return feedTaskOK && feedSettings.located && wifiStored() && clockIsSet();
}

// Whether source f is on and due within `early` ms. One whose last fetch
// failed waits out its back-off (feedFailed()), never coming along early:
// with the radio on for its retry it would otherwise be fetched again at
// once (an HTTP 503 three times within a second, 2026-10-18).
static bool feedDueWithin(uint8_t f, unsigned long early) {
  if (FEEDS[f].needsAlways && !lanIsOn()) { return false; }   // needs continuous network access
  if (feedRuns[f].failures) { early = 0; }
  return feedOn(f) && (long)(millisNow + early - feedRuns[f].dueAt) >= 0;
}

// The first source due within `early` ms; -1 when none is.
static int8_t feedDue(unsigned long early) {
  for (uint8_t f = 0; f < FEED_COUNT; f++) {
    if (feedDueWithin(f, early)) { return (int8_t)f; }
  }
  return -1;
}

// A fetch that did not work: again after 1, 2, 4 ... minutes, never later
// than the source's own interval. Its items stay until they run out.
static void feedFailed(uint8_t f, const char *why) {
  FeedRun &r = feedRuns[f];
  if (r.failures < 255) { r.failures++; }
  unsigned long wait = FEED_RETRY_MS << min(r.failures - 1, 6);
  if (wait > FEEDS[f].intervalMs) { wait = FEEDS[f].intervalMs; }
  r.dueAt = millisNow + wait;
  strlcpy(r.error, why, sizeof(r.error));
  Console.printf("Info band: %s failed (%s), again in %lu min\n", FEEDS[f].name, r.error, wait / 60000UL);
}

#if defined(CLOCK_DEBUG)
// At start-up: a source whose fetch failed waits out its back-off, also while
// the radio is on for the others (feedsUpdate() takes the sources due within
// FEED_GATHER_MS along); one without a failure still comes along early. On
// 2026-10-18 an HTTP 503 was fetched three times within a second. Leaves the
// source as it found it.
void feedRetrySelfTest() {
  int8_t f = -1;
  for (uint8_t i = 0; i < FEED_COUNT && f < 0; i++) {
    unsigned long due = feedRuns[i].dueAt;
    feedRuns[i].dueAt = millisNow;
    if (feedDueWithin(i, 0)) { f = (int8_t)i; }   // on, and fetched on this clock at all
    feedRuns[i].dueAt = due;
  }
  if (f < 0) {
    Console.println("Feed retry self-test: skipped, no source on");
    return;
  }
  FeedRun saved = feedRuns[f];
  feedRuns[f].failures = 0;
  feedFailed(f, "self-test");                      // due again FEED_RETRY_MS from now
  bool waits = !feedDueWithin(f, FEED_GATHER_MS);
  feedRuns[f].failures = 0;                        // the same wait without a failure
  bool gathers = feedDueWithin(f, FEED_GATHER_MS);
  feedRuns[f] = saved;
  Console.printf("Feed retry self-test: %s (%s: a failed one %s, one without a failure %s)\n",
                waits && gathers ? "all right" : "FAILED", FEEDS[f].name,
                waits ? "waits out its back-off" : "is fetched again at once",
                gathers ? "comes along early" : "does not come along");
}
#endif

// A result back from the fetch task. A fetch takes seconds: one for a place
// left meanwhile, or of a source switched off meanwhile, is dropped whole -
// its items, its place name and its time, after which the new place would
// have waited a whole interval (applyFeedParams() has it due at once).
static void feedApply(const FeedResult &res) {
  if (res.feed >= FEED_COUNT) { return; }
  bool here = feedSettings.located && res.lat == feedSettings.lat && res.lon == feedSettings.lon;
  if (!here || !feedOn(res.feed)) {
    Console.printf("Info band: %s result dropped, %s since its fetch began\n", FEEDS[res.feed].name,
                  here ? "switched off" : "the place changed");
    return;
  }
  FeedRun &r = feedRuns[res.feed];
  r.status = res.status;
  r.ms     = res.ms;
  r.bytes  = res.bytes;
  Console.printf("Info band: %s fetched, status %d, %ld bytes in %lu ms\n", FEEDS[res.feed].name, res.status,
                (long)res.bytes, (unsigned long)res.ms);
  Console.printf("  internal RAM %lu free before, %lu lowest since start; stack %lu bytes unused\n",
                (unsigned long)res.heapFree, (unsigned long)res.heapLow, (unsigned long)res.stackLeft);
  if (res.error[0]) {
    feedFailed(res.feed, res.error);
    return;
  }
  r.error[0] = '\0';
  r.failures = 0;
  r.okAt     = clockNow();
  r.dueAt    = millisNow + FEEDS[res.feed].intervalMs;
  if (res.place[0]) { strlcpy(feedPlace, res.place, sizeof(feedPlace)); }
  bandDropFeed(res.feed, &res);
  for (uint8_t k = 0; k < res.count; k++) { bandPut(res.item[k]); }
}

// On every pass outside the hotspot: takes a result back, hands out the next
// fetch, and wants the station only while a source is due.
void feedsUpdate() {
  static FeedResult res;   // 0.6 KB, off the loop's stack
  if (feedBusy && xQueueReceive(feedResults(), &res, 0) == pdTRUE) {
    feedBusy = false;
    feedApply(res);
  }
  // An item goes when its until has come. Once a minute every item is looked
  // at as well, whatever bandNextUntil says, so none can outstay its until
  // by more than that.
  if (minuteTrigger || (bandNextUntil != 0 && clockNow() >= bandNextUntil)) { bandExpire(); }
  if (feedBusy) { return; }
  bool session = (radioUsers & RADIO_FEEDS) != 0;
  int8_t f = feedsReady() ? feedDue(session ? FEED_GATHER_MS : 0) : -1;
  if (f < 0) {
    radioUsers &= (uint8_t)~RADIO_FEEDS;
    return;
  }
  if (!session) {
    radioUsers |= RADIO_FEEDS;
    feedWantedAt = millisNow;
  }
  if (!stationUp()) {
    if (millisNow - feedWantedAt >= FEED_STATION_MS) {
      // The station did not come: every source due waits its back-off.
      for (uint8_t d = 0; d < FEED_COUNT; d++) {
        if (feedDueWithin(d, 0)) { feedFailed(d, "no WiFi connection"); }
      }
      radioUsers &= (uint8_t)~RADIO_FEEDS;
    }
    return;
  }
  FeedJob job = { (uint8_t)f, feedSettings.lat, feedSettings.lon, clockNow(), (int32_t)tzTotalOffset() };
  if (xQueueSend(feedJobs(), &job, 0) == pdTRUE) {
    feedBusy = true;
#if BOARD_WAVESHARE_WALL
    feedSentAt = millisNow;
#endif
  }
}

// "52.52", "-13,405" or "48" in millionths of a degree, up to `limit` degrees
// either way; false for anything else. No float parser (see tenthsOf()).
static bool microDegreesOf(const String &v, int32_t limit, int32_t &out) {
  String t = v;
  t.trim();
  unsigned i = 0;
  bool neg = false;
  if (i < t.length() && (t[i] == '-' || t[i] == '+')) { neg = (t[i] == '-'); i++; }
  int64_t whole = 0, frac = 0;
  int digits = 0, fracDigits = 0;
  for (; i < t.length() && isDigit(t[i]); i++) {
    whole = whole * 10 + (t[i] - '0');
    digits++;
    if (whole > limit) { return false; }
  }
  if (i < t.length() && (t[i] == '.' || t[i] == ',')) {
    for (i++; i < t.length() && isDigit(t[i]); i++) {
      if (fracDigits < 6) { frac = frac * 10 + (t[i] - '0'); fracDigits++; }
    }
  }
  if (i != t.length() || digits + fracDigits == 0) { return false; }
  for (; fracDigits < 6; fracDigits++) { frac *= 10; }
  int64_t micro = whole * 1000000 + frac;
  if (micro > (int64_t)limit * 1000000) { return false; }
  out = (int32_t)(neg ? -micro : micro);
  return true;
}

// The info band section of the config page (/save). Both fields empty clear
// the place; a place that cannot be read keeps the one stored.
void applyFeedParams(const String &q) {
  uint8_t wasOn = feedSettings.on;
  feedSettings.on &= (uint8_t)((1 << FEED_COUNT) - 1);   // a bit for no source never stays (the form never set one)
  settingsApply(feedTable, [&q](const char *key) { return getParam(q, key); }, false);   // the sources and the frames (FEED_PARAMS)
  String iround = getParam(q, "iround");
  if (iround.length() && roundChoice(iround.toInt()) && iround.toInt() != feedSettings.roundMinutes) {
    feedSettings.roundMinutes = (uint8_t)iround.toInt();
    roundMinuteAt = 0;     // a round waiting goes by the old interval
  }
  String la = getParam(q, "lat"), lo = getParam(q, "lon");
  la.trim();
  lo.trim();
  int32_t lat = 0, lon = 0;
  bool located = feedSettings.located;
  if (la.length() == 0 && lo.length() == 0) {
    located = false;
  } else if (microDegreesOf(la, 90, lat) && microDegreesOf(lo, 180, lon)) {
    located = true;
  } else {
    Console.println("Info band: the place could not be read - the stored one stays");
    lat = feedSettings.lat;
    lon = feedSettings.lon;
  }
  bool moved = located != (feedSettings.located != 0) ||
               (located && (lat != feedSettings.lat || lon != feedSettings.lon));
  if (moved) {
    // Another place: what was fetched for the old one goes, and the sources
    // are due at once.
    feedSettings.located = located ? 1 : 0;
    feedSettings.lat = located ? lat : 0;
    feedSettings.lon = located ? lon : 0;
    feedPlace[0] = '\0';
    for (uint8_t f = 0; f < FEED_COUNT; f++) {
      bandDropFeed(f);
      memset(&feedRuns[f], 0, sizeof(FeedRun));
      feedRuns[f].dueAt = millisNow;
    }
  }
  for (uint8_t f = 0; f < FEED_COUNT; f++) {
    if (!feedOn(f)) { bandDropFeed(f); }   // a source switched off takes its items with it
    // One switched on is due at once. Its due time was left as it was: after
    // more than 24.8 days of uptime the signed comparison in feedDueWithin()
    // read an old one as ahead, and the source waited up to that long again
    // (code review of 2026-10-14).
    else if (!(wasOn & (1 << f))) { feedRuns[f].dueAt = millisNow; }
  }
}
