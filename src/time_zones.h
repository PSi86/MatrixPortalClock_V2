/* -------------------------------------------------------------------------
   time_zones.h - the zones the clock offers and their daylight saving.

   Each zone by its base UTC offset in seconds, which is what the settings
   store, so every offset appears only once; its name as the web app and the
   menu list it; and, for a zone with daylight saving, its POSIX TZ rule from
   the IANA tz database (tzdata 2026.4), with which the C library decides
   whether summer time is in effect (DST_AUTO). The clock's own state - the
   mode chosen, whether summer time is in effect now - stays with its
   settings in the sketch (dstInEffect(), setDstMode(), updateAutoDst()).
   The first part of the sketch to move into a module of its own (code
   review of 2026-10-14, point 10; 2026-10-22).
   ------------------------------------------------------------------------- */
#ifndef TIME_ZONES_H
#define TIME_ZONES_H

#include <stdint.h>
#include <time.h>

struct TzOption { int32_t off; const char *label; const char *rule; };
inline constexpr TzOption TZONES[] = {
  {-39600, "(UTC-11:00) Midway",              nullptr},
  {-36000, "(UTC-10:00) Hawaii",              nullptr},
  {-32400, "(UTC-09:00) Alaska",              "AKST9AKDT,M3.2.0,M11.1.0"},
  {-28800, "(UTC-08:00) Pacific (LA)",        "PST8PDT,M3.2.0,M11.1.0"},
  {-25200, "(UTC-07:00) Mountain (Denver)",   "MST7MDT,M3.2.0,M11.1.0"},
  {-21600, "(UTC-06:00) Central (Chicago)",   "CST6CDT,M3.2.0,M11.1.0"},
  {-18000, "(UTC-05:00) Eastern (New York)",  "EST5EDT,M3.2.0,M11.1.0"},
  {-14400, "(UTC-04:00) Atlantic (Halifax)",  "AST4ADT,M3.2.0,M11.1.0"},
  {-10800, "(UTC-03:00) Buenos Aires",        nullptr},
  {  -3600, "(UTC-01:00) Azores",             "<-01>1<+00>,M3.5.0/0,M10.5.0/1"},
  {      0, "(UTC+00:00) London, Lisbon",     "GMT0BST,M3.5.0/1,M10.5.0"},
  {   3600, "(UTC+01:00) Berlin, Paris",      "CET-1CEST,M3.5.0,M10.5.0/3"},
  {   7200, "(UTC+02:00) Athens, Helsinki",   "EET-2EEST,M3.5.0/3,M10.5.0/4"},
  {  10800, "(UTC+03:00) Moscow, Istanbul",   nullptr},
  {  12600, "(UTC+03:30) Tehran",             nullptr},
  {  14400, "(UTC+04:00) Dubai",              nullptr},
  {  18000, "(UTC+05:00) Karachi",            nullptr},
  {  19800, "(UTC+05:30) India",              nullptr},
  {  21600, "(UTC+06:00) Dhaka",              nullptr},
  {  25200, "(UTC+07:00) Bangkok",            nullptr},
  {  28800, "(UTC+08:00) Beijing, Singapore", nullptr},
  {  32400, "(UTC+09:00) Tokyo, Seoul",       nullptr},
  {  34200, "(UTC+09:30) Adelaide",           "ACST-9:30ACDT,M10.1.0,M4.1.0/3"},
  {  36000, "(UTC+10:00) Sydney",             "AEST-10AEDT,M10.1.0,M4.1.0/3"},
  {  39600, "(UTC+11:00) Solomon Is.",        nullptr},
  {  43200, "(UTC+12:00) Auckland",           "NZST-12NZDT,M9.5.0,M4.1.0/3"},
};
inline constexpr uint8_t ZONE_COUNT = sizeof(TZONES) / sizeof(TZONES[0]);

// POSIX TZ rule of the zone with this base offset, or nullptr when the zone
// has no daylight saving (or the offset matches no zone).
const char *tzRule(int32_t offset);

// Whether daylight saving is in effect at this UTC instant under a POSIX TZ
// rule (nullptr: never), decided by the C library.
bool dstActiveAt(const char *rule, time_t utc);

#if defined(CLOCK_DEBUG)
// Debug build only: dstActiveAt() against the 2026 changes, printed.
void dstSelfTest();
#endif

#endif  // TIME_ZONES_H
