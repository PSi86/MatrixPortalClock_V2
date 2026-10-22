/* ----------------------------------------------------------------------
   Software clock: UTC in seconds since 1970, counted from millis().

   Replaces the Time library (TimeLib 1.6.1, unmaintained since 2021), which
   declares its own time_t and does not compile against picolibc (the default
   C library from ESP-IDF 6 on). Only the C library's <time.h> is used here, so
   it builds the same with newlib and picolibc.

   The clock holds UTC, exactly as NTP delivers it. Timezone and daylight
   saving are added where the time is shown (localTime in main.cpp), so
   changing either never touches the clock. The hour/minute/second helpers
   break down whatever value they are given as gmtime_r() would; pass them the
   local time to get the local fields.
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

inline bool clockIsSet() { return clockState().set; }

// Days from 1970-01-01 to a date of the Gregorian calendar (H. Hinnant's
// days_from_civil), so a date needs no time zone of the C library.
inline long daysFromCivil(int y, unsigned m, unsigned d) {
  y -= (m <= 2) ? 1 : 0;
  long era = (y >= 0 ? y : y - 399) / 400;
  unsigned yoe = (unsigned)(y - era * 400);                          // 0..399
  unsigned doy = (153 * (m > 2 ? m - 3 : m + 9) + 2) / 5 + d - 1;    // 0..365
  unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;              // 0..146096
  return era * 146097L + (long)doe - 719468L;
}

// The fields of a time by arithmetic, the same as gmtime_r() gives for every
// value (checked against it on 3.6 million, 2026-10-20) - negative ones too,
// which the local time is before the clock is set in a zone west of UTC. The
// faces and the second's tick ask several times on every pass, and gmtime_r()
// works out the whole date each time.
inline int64_t clockFloorDiv(int64_t t, int64_t d) { return t >= 0 ? t / d : -((-t + d - 1) / d); }
inline int clockHour(time_t t)   { return (int)(clockFloorDiv(t, 3600) - clockFloorDiv(t, 86400) * 24); }
inline int clockMinute(time_t t) { return (int)(clockFloorDiv(t, 60) - clockFloorDiv(t, 3600) * 60); }
inline int clockSecond(time_t t) { return (int)((int64_t)t - clockFloorDiv(t, 60) * 60); }
