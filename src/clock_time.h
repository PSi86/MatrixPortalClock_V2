/* ----------------------------------------------------------------------
   Software clock: local time in seconds since 1970, counted from millis().

   Replaces the Time library (TimeLib 1.6.1, unmaintained since 2021), which
   declares its own time_t and does not compile against picolibc (the default
   C library from ESP-IDF 6 on). Only the C library's <time.h> is used here, so
   it builds the same with newlib and picolibc, on the M4 and on the S3.

   The clock is set from NTP (UTC) and then shifted by the configured timezone
   and DST offset, so the value it holds is LOCAL time; the hour/minute/second
   helpers therefore break it down with gmtime_r(), not localtime_r().
   ------------------------------------------------------------------------- */
#pragma once

#include <Arduino.h>
#include <time.h>

// Clock state. Held in a function-local static so this header stays
// self-contained (one shared instance no matter how often it is included).
struct ClockState {
  time_t   seconds = 0;     // clock value at the millis() stamp below
  uint32_t millisAt = 0;    // millis() when 'seconds' was last advanced or set
  bool     set = false;     // false until the first clockSet()
};
inline ClockState &clockState() { static ClockState s; return s; }

// Current clock value. Advances in whole seconds and keeps the remainder in
// millisAt, so no fraction is lost between calls (unsigned math survives the
// millis() wrap after ~49 days).
inline time_t clockNow() {
  ClockState &s = clockState();
  uint32_t elapsed = millis() - s.millisAt;
  if (elapsed >= 1000) {
    uint32_t whole = elapsed / 1000;
    s.seconds  += whole;
    s.millisAt += whole * 1000;
  }
  return s.seconds;
}

inline void clockSet(time_t t) {
  ClockState &s = clockState();
  s.seconds  = t;
  s.millisAt = millis();
  s.set      = true;
}

inline void clockAdjust(long deltaSeconds) { clockState().seconds += deltaSeconds; }

inline bool clockIsSet() { return clockState().set; }

inline int clockHour(time_t t)   { struct tm tmv; gmtime_r(&t, &tmv); return tmv.tm_hour; }
inline int clockMinute(time_t t) { struct tm tmv; gmtime_r(&t, &tmv); return tmv.tm_min; }
inline int clockSecond(time_t t) { struct tm tmv; gmtime_r(&t, &tmv); return tmv.tm_sec; }
