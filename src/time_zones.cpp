// The zones' daylight saving (time_zones.h).
#include "time_zones.h"

#include <stdio.h>
#include <stdlib.h>
#include "console.h"

const char *tzRule(int32_t offset) {
  for (unsigned int i = 0; i < ZONE_COUNT; i++) {
    if (TZONES[i].off == offset) { return TZONES[i].rule; }
  }
  return nullptr;
}

// TZ is set on every call because Arduino's configTime() (SNTP start on the
// S3) resets it to UTC.
bool dstActiveAt(const char *rule, time_t utc) {
  if (!rule) { return false; }
  setenv("TZ", rule, 1);
  tzset();
  struct tm local;
  localtime_r(&utc, &local);
  return local.tm_isdst > 0;
}

#if defined(CLOCK_DEBUG)
// Checks dstActiveAt() against the 2026 changes from the IANA tz database
// (tzdata 2026.4), one second before and at each change; the debug build
// prints the result once, 10 s after boot.
void dstSelfTest() {
  struct Check { int32_t off; uint32_t utc; bool dst; };
  static const Check checks[] = {
    // Berlin, London, Azores and Athens all change at 01:00 UTC
    {  3600, 1774745999UL, false }, {  3600, 1774746000UL, true  },
    {  3600, 1792889999UL, true  }, {  3600, 1792890000UL, false },
    {     0, 1774745999UL, false }, {     0, 1774746000UL, true  },
    {     0, 1792889999UL, true  }, {     0, 1792890000UL, false },
    { -3600, 1774745999UL, false }, { -3600, 1774746000UL, true  },
    { -3600, 1792889999UL, true  }, { -3600, 1792890000UL, false },
    {  7200, 1774745999UL, false }, {  7200, 1774746000UL, true  },
    {  7200, 1792889999UL, true  }, {  7200, 1792890000UL, false },
    // New York, Los Angeles: 02:00 local
    {-18000, 1772953199UL, false }, {-18000, 1772953200UL, true  },
    {-18000, 1793512799UL, true  }, {-18000, 1793512800UL, false },
    {-28800, 1772963999UL, false }, {-28800, 1772964000UL, true  },
    {-28800, 1793523599UL, true  }, {-28800, 1793523600UL, false },
    // Sydney, Adelaide, Auckland: southern hemisphere, summer time over New Year
    { 36000, 1775318399UL, true  }, { 36000, 1775318400UL, false },
    { 36000, 1791043199UL, false }, { 36000, 1791043200UL, true  },
    { 34200, 1775320199UL, true  }, { 34200, 1775320200UL, false },
    { 34200, 1791044999UL, false }, { 34200, 1791045000UL, true  },
    { 43200, 1775311199UL, true  }, { 43200, 1775311200UL, false },
    { 43200, 1790431199UL, false }, { 43200, 1790431200UL, true  },
    // Tokyo: no daylight saving
    { 32400, 1774746000UL, false }, { 32400, 1792890000UL, false },
  };
  unsigned int ok = 0, total = 0;
  for (const Check &c : checks) {
    total++;
    if (dstActiveAt(tzRule(c.off), (time_t)c.utc) == c.dst) { ok++; continue; }
    Console.print("DST self-test FAIL offset "); Console.print(c.off);
    Console.print(" utc "); Console.println(c.utc);
  }
  Console.print("DST self-test: "); Console.print(ok); Console.print('/'); Console.print(total); Console.println(" ok");
}
#endif

// An offset from UTC as "UTC+1", "UTC-11" or "UTC+5:30".
void formatOffset(char *buf, size_t size, long offset) {
  char sign = (offset < 0) ? '-' : '+';
  long magnitude = (offset < 0) ? -offset : offset;
  unsigned hours = magnitude / 3600, minutes = (magnitude % 3600) / 60;
  if (minutes) { snprintf(buf, size, "UTC%c%u:%02u", sign, hours, minutes); }
  else         { snprintf(buf, size, "UTC%c%u", sign, hours); }
}
