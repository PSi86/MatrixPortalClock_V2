/* ----------------------------------------------------------------------
   Info band data: the items the band shows, and the sources the clock
   fetches itself (concept doc, section "Sources and the Home Assistant
   link"): the weather from Open-Meteo and the DWD weather warnings from
   Bright Sky, both without a key. Everything else is to come through Home
   Assistant.

   A fetch blocks for a few seconds (name lookup, TLS handshake, the answer),
   so it runs in a task of its own on the other core while the loop keeps
   drawing: the loop hands the task a FeedJob and later takes a FeedResult
   back. Only the loop ever touches the items. The S3 boards only.
   ---------------------------------------------------------------------- */
#pragma once

#include <Arduino.h>
#include <HTTPClient.h>
#include <NetworkClientSecure.h>
#include <ArduinoJson.h>
#include <esp_heap_caps.h>
#include <mbedtls/platform.h>
#include <freertos/idf_additions.h>
#include "clock_time.h"

// ---- Items ----------------------------------------------------------------

enum ItemLevel : uint8_t { LEVEL_INFO, LEVEL_ALERT };

// What an item's icon shows; the pictures come with the band.
enum ItemIcon : uint8_t {
  ICON_NONE, ICON_CLEAR_DAY, ICON_CLEAR_NIGHT, ICON_PARTLY_DAY, ICON_PARTLY_NIGHT, ICON_CLOUDY,
  ICON_FOG, ICON_DRIZZLE, ICON_RAIN, ICON_SNOW, ICON_THUNDER, ICON_WARNING, ICON_COUNT
};

// The icons by name, as Home Assistant gives them, in ItemIcon's order.
const char *const ITEM_ICON_NAMES[ICON_COUNT] = {
  "", "clear-day", "clear-night", "partly-day", "partly-night", "cloudy",
  "fog", "drizzle", "rain", "snow", "thunder", "warning",
};

// ICON_NONE for a name that is none of them.
inline uint8_t itemIconByName(const char *name) {
  for (uint8_t i = 1; name && i < ICON_COUNT; i++) {
    if (strcmp(name, ITEM_ICON_NAMES[i]) == 0) { return i; }
  }
  return ICON_NONE;
}

// The sources the clock fetches itself.
enum FeedId : uint8_t { FEED_WEATHER, FEED_WARNINGS, FEED_COUNT };

// One thing the band can show. Its text is UTF-8 as the source wrote it; an
// item past `until` is not shown any more, so the band never passes off an
// old value as the current one.
struct BandItem {
  char    key[24];     // what it is, unique within its source: "weather", "rain", "warn1", or a name Home Assistant gave
  char    text[96];
  uint8_t icon;        // ItemIcon
  uint8_t level;       // ItemLevel
  uint8_t feed;        // the source it came from (FeedId)
  time_t  until;       // UTC
};

// How often a source is fetched, how long its items count without a new
// fetch (three intervals, so a fetch or two may fail without a gap), and
// whether it needs the clock to stay connected (continuous network access):
// a source that is fetched wakes the radio by itself and does not, one that
// is pushed to the clock (Home Assistant over MQTT) will.
struct FeedInfo { const char *name; uint32_t intervalMs; uint32_t staleS; bool needsAlways; };
const FeedInfo FEEDS[FEED_COUNT] = {
  { "weather",  30UL * 60UL * 1000UL, 90UL * 60UL, false },
  { "warnings", 10UL * 60UL * 1000UL, 30UL * 60UL, false },
};

// ---- Between the loop and the fetch task ------------------------------------

struct FeedJob {
  uint8_t feed;        // FeedId
  int32_t lat, lon;    // the place, in millionths of a degree
  time_t  now;         // UTC when the job was given
  int32_t utcOffset;   // the clock's zone and daylight saving, for the times in texts
};

const uint8_t FEED_ITEMS_MAX = 4;   // the most items one fetch brings
const int16_t FEED_ERR_JSON  = -100;

struct FeedResult {
  uint8_t  feed;
  int16_t  status;          // HTTP status; below 0 HTTPClient's error, or FEED_ERR_JSON
  uint8_t  count;           // items in item[]
  BandItem item[FEED_ITEMS_MAX];
  char     place[32];       // the warnings' place, as the DWD names it
  char     error[40];       // empty when it worked
  int32_t  bytes;           // the answer's size, -1 when the server did not say
  uint32_t ms;              // the whole fetch
  uint32_t heapFree;        // internal RAM free before the fetch
  uint32_t heapLow;         // internal RAM's lowest since start, after the fetch
  uint32_t stackLeft;       // the task's stack never used so far
};

// ---- Helpers ------------------------------------------------------------------

// "48.137154" from millionths of a degree, without the C library's float printing.
inline void feedDegrees(char *buf, size_t size, int32_t micro) {
  uint32_t a = micro < 0 ? (uint32_t)(-(int64_t)micro) : (uint32_t)micro;
  snprintf(buf, size, "%s%lu.%06lu", micro < 0 ? "-" : "", (unsigned long)(a / 1000000UL),
           (unsigned long)(a % 1000000UL));
}

// "19:00" in the clock's local time, or "Thu 19:00" when it is not today.
inline void feedWhen(char *buf, size_t size, time_t utc, const FeedJob &job) {
  static const char *const WEEKDAY[7] = { "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat" };
  long local = (long)utc + job.utcOffset, today = (long)job.now + job.utcOffset;
  int hh = (int)((local % 86400L) / 3600L), mm = (int)((local % 3600L) / 60L);
  if (local / 86400L == today / 86400L) {
    snprintf(buf, size, "%02d:%02d", hh, mm);
  } else {
    snprintf(buf, size, "%s %02d:%02d", WEEKDAY[(local / 86400L + 4) % 7], hh, mm);   // 1970-01-01 was a Thursday
  }
}

// UTC of a time such as "2023-08-07T08:00:00+00:00" (Bright Sky's); 0 when it is none.
inline time_t feedIsoTime(const char *s) {
  if (!s) { return 0; }
  int y, mo, d, h, mi, sec, n = 0;
  if (sscanf(s, "%4d-%2d-%2dT%2d:%2d:%2d%n", &y, &mo, &d, &h, &mi, &sec, &n) != 6) { return 0; }
  long t = daysFromCivil(y, (unsigned)mo, (unsigned)d) * 86400L + h * 3600L + mi * 60L + sec;
  const char *z = s + n;
  if (*z == '.') { z++; while (isdigit((unsigned char)*z)) { z++; } }   // fractions of a second
  if (*z == '+' || *z == '-') {
    int oh = 0, om = 0;
    sscanf(z + 1, "%2d:%2d", &oh, &om);
    long off = oh * 3600L + om * 60L;
    t += (*z == '+') ? -off : off;
  }
  return (time_t)t;
}

// Copies src into dst (size bytes with the NUL), cutting at a character's
// first byte, so a long UTF-8 text never ends in half a character.
inline void itemTextCopy(char *dst, const char *src, size_t size) {
  if (strlcpy(dst, src, size) < size) { return; }
  size_t cut = size - 1;                                     // the first byte left out
  if (((uint8_t)src[cut] & 0xC0) != 0x80) { return; }       // it starts a character: nothing cut through
  while (cut > 0 && ((uint8_t)dst[cut - 1] & 0xC0) == 0x80) { cut--; }
  if (cut > 0) { cut--; }                                    // the first byte of the character cut through
  dst[cut] = '\0';
}

inline void feedItem(FeedResult &r, const char *key, const char *text, uint8_t icon, uint8_t level, time_t until) {
  if (r.count >= FEED_ITEMS_MAX) { return; }
  BandItem &it = r.item[r.count++];
  strlcpy(it.key, key, sizeof(it.key));
  strlcpy(it.text, text, sizeof(it.text));
  it.icon  = icon;
  it.level = level;
  it.feed  = r.feed;
  it.until = until;
}

// ---- HTTPS ------------------------------------------------------------------

// The certificate bundle of ESP-IDF that the core's libraries carry (every
// root the Mozilla list trusts, CONFIG_MBEDTLS_CERTIFICATE_BUNDLE_DEFAULT_FULL).
extern const uint8_t feedCaBundleStart[] asm("_binary_x509_crt_bundle_start");
extern const uint8_t feedCaBundleEnd[]   asm("_binary_x509_crt_bundle_end");

const uint16_t FEED_TIMEOUT_MS = 10000;
#define FEED_USER_AGENT "MatrixPortalClock/2 (+https://github.com/PSi86/MatrixPortalClock_V2)"

// GET the url over HTTPS and read its JSON through the filter into doc, so a
// large answer never has to fit in RAM. false, with r.status and r.error set,
// when it did not work.
inline bool feedGet(const char *url, JsonDocument &filter, JsonDocument &doc, FeedResult &r) {
  NetworkClientSecure tls;
  tls.setCACertBundle(feedCaBundleStart, (size_t)(feedCaBundleEnd - feedCaBundleStart));
  tls.setHandshakeTimeout(FEED_TIMEOUT_MS / 1000);
  HTTPClient http;
  http.useHTTP10(true);   // no chunked transfer: the JSON comes straight off the socket
  http.setConnectTimeout(FEED_TIMEOUT_MS);
  http.setTimeout(FEED_TIMEOUT_MS);
  http.setUserAgent(FEED_USER_AGENT);
  if (!http.begin(tls, url)) {
    r.status = HTTPC_ERROR_CONNECTION_REFUSED;
    strlcpy(r.error, "bad URL", sizeof(r.error));
    return false;
  }
  int code = http.GET();
  r.status = (int16_t)code;
  r.bytes  = http.getSize();
  if (code != HTTP_CODE_OK) {
    if (code < 0) { strlcpy(r.error, HTTPClient::errorToString(code).c_str(), sizeof(r.error)); }
    else          { snprintf(r.error, sizeof(r.error), "HTTP %d", code); }
    http.end();
    return false;
  }
  DeserializationError e = deserializeJson(doc, http.getStream(), DeserializationOption::Filter(filter));
  http.end();
  if (e) {
    r.status = FEED_ERR_JSON;
    snprintf(r.error, sizeof(r.error), "JSON: %s", e.c_str());
    return false;
  }
  return true;
}

// ---- Weather: Open-Meteo ------------------------------------------------------
// https://open-meteo.com/en/docs - no key, non-commercial use, under 10,000
// calls a day, CC BY 4.0 ("Weather data by Open-Meteo.com"). About 1.5 KB.

struct WmoWeather { uint8_t code; const char *text; uint8_t icon; };
const WmoWeather WMO_WEATHER[] = {
  {  0, "clear",              ICON_CLEAR_DAY }, {  1, "mainly clear",   ICON_PARTLY_DAY },
  {  2, "partly cloudy",      ICON_PARTLY_DAY }, {  3, "overcast",      ICON_CLOUDY },
  { 45, "fog",                ICON_FOG },       { 48, "rime fog",        ICON_FOG },
  { 51, "light drizzle",      ICON_DRIZZLE },   { 53, "drizzle",         ICON_DRIZZLE },
  { 55, "dense drizzle",      ICON_DRIZZLE },   { 56, "freezing drizzle", ICON_DRIZZLE },
  { 57, "freezing drizzle",   ICON_DRIZZLE },   { 61, "light rain",      ICON_RAIN },
  { 63, "rain",               ICON_RAIN },      { 65, "heavy rain",      ICON_RAIN },
  { 66, "freezing rain",      ICON_RAIN },      { 67, "freezing rain",   ICON_RAIN },
  { 71, "light snow",         ICON_SNOW },      { 73, "snow",            ICON_SNOW },
  { 75, "heavy snow",         ICON_SNOW },      { 77, "snow grains",     ICON_SNOW },
  { 80, "light showers",      ICON_RAIN },      { 81, "showers",         ICON_RAIN },
  { 82, "heavy showers",      ICON_RAIN },      { 85, "snow showers",    ICON_SNOW },
  { 86, "heavy snow showers", ICON_SNOW },      { 95, "thunderstorm",    ICON_THUNDER },
  { 96, "thunderstorm, hail", ICON_THUNDER },   { 99, "thunderstorm, hail", ICON_THUNDER },
};

// Rain counts from 0.1 mm in a quarter of an hour.
const float FEED_WET_MM = 0.1f;

inline void feedWeather(const FeedJob &job, FeedResult &r) {
  char lat[16], lon[16], url[400];
  feedDegrees(lat, sizeof(lat), job.lat);
  feedDegrees(lon, sizeof(lon), job.lon);
  snprintf(url, sizeof(url),
           "https://api.open-meteo.com/v1/forecast?latitude=%s&longitude=%s"
           "&current=temperature_2m,weather_code,is_day"
           "&daily=temperature_2m_max,temperature_2m_min,precipitation_probability_max,uv_index_max"
           "&minutely_15=precipitation&forecast_minutely_15=16&forecast_days=1"
           "&timezone=auto&timeformat=unixtime", lat, lon);
  JsonDocument filter, doc;
  filter["current"]["temperature_2m"] = true;
  filter["current"]["weather_code"] = true;
  filter["current"]["is_day"] = true;
  filter["daily"]["temperature_2m_max"] = true;
  filter["daily"]["temperature_2m_min"] = true;
  filter["daily"]["precipitation_probability_max"] = true;
  filter["daily"]["uv_index_max"] = true;
  filter["minutely_15"]["time"] = true;
  filter["minutely_15"]["precipitation"] = true;
  if (!feedGet(url, filter, doc, r)) { return; }

  JsonObject cur = doc["current"], day = doc["daily"];
  if (cur["temperature_2m"].isNull() || cur["weather_code"].isNull()) {
    r.status = FEED_ERR_JSON;
    strlcpy(r.error, "no current weather in the answer", sizeof(r.error));
    return;
  }
  int code = cur["weather_code"] | 0;
  bool isDay = (cur["is_day"] | 1) != 0;
  const char *what = "unknown weather";
  uint8_t icon = ICON_NONE;
  for (const WmoWeather &w : WMO_WEATHER) {
    if (w.code == code) { what = w.text; icon = w.icon; break; }
  }
  if (!isDay && icon == ICON_CLEAR_DAY)  { icon = ICON_CLEAR_NIGHT; }
  if (!isDay && icon == ICON_PARTLY_DAY) { icon = ICON_PARTLY_NIGHT; }

  // "16°C mainly clear, 22/11°C, rain 40%, UV 5": the rain chance only when
  // there is one, the UV index only from 3 (moderate) on.
  char text[96];
  int n = snprintf(text, sizeof(text), "%ld\xC2\xB0" "C %s", lroundf(cur["temperature_2m"].as<float>()), what);
  if (!day["temperature_2m_max"][0].isNull() && n > 0 && n < (int)sizeof(text)) {
    n += snprintf(text + n, sizeof(text) - n, ", %ld/%ld\xC2\xB0" "C", lroundf(day["temperature_2m_max"][0].as<float>()),
                  lroundf(day["temperature_2m_min"][0].as<float>()));
  }
  int rainChance = day["precipitation_probability_max"][0] | 0;
  if (rainChance > 0 && n > 0 && n < (int)sizeof(text)) { n += snprintf(text + n, sizeof(text) - n, ", rain %d%%", rainChance); }
  long uv = lroundf(day["uv_index_max"][0] | 0.0f);
  if (uv >= 3 && n > 0 && n < (int)sizeof(text)) { snprintf(text + n, sizeof(text) - n, ", UV %ld", uv); }
  time_t stale = job.now + FEEDS[FEED_WEATHER].staleS;
  feedItem(r, "weather", text, icon, LEVEL_INFO, stale);

  // Rain within the next four hours, from the quarter-hour forecast: when it
  // starts while it is dry, or when it stops while it rains.
  JsonArray times = doc["minutely_15"]["time"], mm = doc["minutely_15"]["precipitation"];
  size_t slots = min(times.size(), mm.size());
  if (slots >= 2) {
    bool wetNow = (mm[0] | 0.0f) >= FEED_WET_MM;
    for (size_t i = 1; i < slots; i++) {
      if (((mm[i] | 0.0f) >= FEED_WET_MM) != wetNow) {
        time_t at = (time_t)times[i].as<long>();
        char when[16];
        feedWhen(when, sizeof(when), at, job);
        snprintf(text, sizeof(text), wetNow ? "Rain until %s" : "Rain from %s", when);
        feedItem(r, "rain", text, ICON_RAIN, LEVEL_INFO, min(at, stale));
        break;
      }
    }
  }
}

// ---- Weather warnings: Bright Sky ---------------------------------------------
// https://brightsky.dev - DWD warnings by place, no key, free for all
// purposes, the DWD's terms apply. 172 bytes without a warning.

inline uint8_t feedSeverityRank(const char *s) {
  if (!s) { return 0; }
  if (strcmp(s, "extreme") == 0)  { return 4; }
  if (strcmp(s, "severe") == 0)   { return 3; }
  if (strcmp(s, "moderate") == 0) { return 2; }
  if (strcmp(s, "minor") == 0)    { return 1; }
  return 0;
}

inline void feedWarnings(const FeedJob &job, FeedResult &r) {
  char lat[16], lon[16], url[160];
  feedDegrees(lat, sizeof(lat), job.lat);
  feedDegrees(lon, sizeof(lon), job.lon);
  snprintf(url, sizeof(url), "https://api.brightsky.dev/alerts?lat=%s&lon=%s", lat, lon);
  JsonDocument filter, doc;
  JsonObject a = filter["alerts"].add<JsonObject>();   // for every alert in the array
  a["status"] = true;
  a["severity"] = true;
  a["headline_en"] = true;
  a["onset"] = true;
  a["expires"] = true;
  filter["location"]["name"] = true;
  if (!feedGet(url, filter, doc, r)) { return; }
  if (!doc["alerts"].is<JsonArray>()) {
    r.status = FEED_ERR_JSON;
    strlcpy(r.error, "no alert list in the answer", sizeof(r.error));
    return;
  }
  strlcpy(r.place, doc["location"]["name"] | "", sizeof(r.place));

  // The most severe first, at most FEED_ITEMS_MAX of them; a test alert or
  // one that has run out is left out. "minor" (DWD level 1, such as frost or
  // gusts) comes as information, from "moderate" on as an alert.
  JsonArray alerts = doc["alerts"];
  bool taken[32] = { false };
  time_t stale = job.now + FEEDS[FEED_WARNINGS].staleS;
  for (uint8_t slot = 1; slot <= FEED_ITEMS_MAX; slot++) {
    int best = -1;
    uint8_t bestRank = 0;
    for (size_t i = 0; i < alerts.size() && i < 32; i++) {
      JsonObject al = alerts[i];
      if (taken[i] || strcmp(al["status"] | "actual", "actual") != 0) { continue; }
      time_t expires = feedIsoTime(al["expires"].as<const char *>());
      if (expires != 0 && expires <= job.now) { continue; }
      uint8_t rank = feedSeverityRank(al["severity"].as<const char *>()) + 1;   // 1 even for an unknown severity
      if (rank > bestRank) { best = (int)i; bestRank = rank; }
    }
    if (best < 0) { break; }
    taken[best] = true;
    JsonObject al = alerts[best];
    time_t onset = feedIsoTime(al["onset"].as<const char *>()), expires = feedIsoTime(al["expires"].as<const char *>());
    char text[96], when[16], key[8];
    const char *headline = al["headline_en"] | "Weather warning";
    if (onset > job.now) {
      feedWhen(when, sizeof(when), onset, job);
      snprintf(text, sizeof(text), "%s from %s", headline, when);
    } else if (expires != 0) {
      feedWhen(when, sizeof(when), expires, job);
      snprintf(text, sizeof(text), "%s until %s", headline, when);
    } else {
      strlcpy(text, headline, sizeof(text));
    }
    snprintf(key, sizeof(key), "warn%u", slot);
    uint8_t level = feedSeverityRank(al["severity"].as<const char *>()) >= 2 ? LEVEL_ALERT : LEVEL_INFO;
    feedItem(r, key, text, ICON_WARNING, level, expires != 0 ? min(expires, stale) : stale);
  }
}

// ---- The fetch task -----------------------------------------------------------

// The task's ends: one job and one result at a time.
inline QueueHandle_t &feedJobs()    { static QueueHandle_t q = nullptr; return q; }
inline QueueHandle_t &feedResults() { static QueueHandle_t q = nullptr; return q; }

// Its stack: the TLS handshake runs on it.
const uint32_t FEED_TASK_STACK = 10240;

inline void feedRun(const FeedJob &job, FeedResult &r) {
  memset(&r, 0, sizeof(r));
  r.feed     = job.feed;
  r.bytes    = -1;
  r.heapFree = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
  uint32_t t0 = millis();
  switch (job.feed) {
    case FEED_WEATHER:  feedWeather(job, r);  break;
    case FEED_WARNINGS: feedWarnings(job, r); break;
    default:            strlcpy(r.error, "unknown source", sizeof(r.error)); break;
  }
  r.ms        = millis() - t0;
  r.heapLow   = heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL);
  r.stackLeft = uxTaskGetStackHighWaterMark(nullptr);   // bytes on ESP-IDF
}

inline void feedTask(void *) {
  static FeedResult r;   // 0.6 KB, kept off the task's stack
  FeedJob job;
  for (;;) {
    if (xQueueReceive(feedJobs(), &job, portMAX_DELAY) != pdTRUE) { continue; }
    feedRun(job, r);
    xQueueSend(feedResults(), &r, portMAX_DELAY);
  }
}

// mbedTLS's memory from PSRAM, internal RAM only when PSRAM is short. The
// core is built with CONFIG_MBEDTLS_INTERNAL_MEM_ALLOC, so a TLS handshake
// took several 10 KB of internal RAM, which the panel driver, the WiFi and
// the Home Assistant link need too: on 2026-10-09 a fetch at start-up with
// the link on left only 18 KB of it free. PSRAM is what the core's own
// CONFIG_MBEDTLS_EXTERNAL_MEM_ALLOC would use.
inline void *feedTlsCalloc(size_t n, size_t size) {
  void *p = heap_caps_calloc(n, size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  return p ? p : heap_caps_calloc(n, size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
}
inline void feedTlsFree(void *p) { heap_caps_free(p); }

// Starts the fetch task, on core 0 beside the WiFi so the loop on core 1
// keeps drawing during a fetch, with its stack in PSRAM when there is some
// (it never touches the flash). false when there was no RAM for it.
inline bool feedBegin() {
  if (feedJobs()) { return true; }
  mbedtls_platform_set_calloc_free(feedTlsCalloc, feedTlsFree);   // before any TLS
  feedJobs()    = xQueueCreate(1, sizeof(FeedJob));
  feedResults() = xQueueCreate(1, sizeof(FeedResult));
  if (!feedJobs() || !feedResults()) { return false; }
  if (xTaskCreatePinnedToCoreWithCaps(feedTask, "feeds", FEED_TASK_STACK, nullptr, 1, nullptr, 0,
                                      MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) == pdPASS) {
    return true;
  }
  return xTaskCreatePinnedToCore(feedTask, "feeds", FEED_TASK_STACK, nullptr, 1, nullptr, 0) == pdPASS;
}
