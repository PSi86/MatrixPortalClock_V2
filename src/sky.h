/* ----------------------------------------------------------------------
   The sky over the clock's place, worked out on the clock: sunrise and
   sunset, and the moon's phase - for the wall clock's info band, without
   any source on the internet.

   The sun after NOAA's solar calculator (Jean Meeus' formulas): its
   declination and the equation of time at the moment of the event, with
   the usual 90.833 degrees from the zenith (the sun's upper edge, with the
   air's refraction). Checked on 2026-10-18 against sunrise-and-sunset.com:
   Stuttgart and Berlin within a minute.

   The moon from its longitude, the main terms of Meeus' low-precision
   series, against the sun's: the elongation 0 at new moon, 90 at the first
   quarter, 180 at full moon, 270 at the last quarter. Checked the same day
   against moongiant.com's phases of October 2026: within half an hour.
   ---------------------------------------------------------------------- */
#pragma once

#include <math.h>
#include <time.h>
#include <stdint.h>

namespace sky {

const double DEG = M_PI / 180.0;

// The Julian day of a Unix time.
inline double julianDay(double unixSeconds) { return unixSeconds / 86400.0 + 2440587.5; }

// The sun's apparent longitude, its declination (degrees) and the equation
// of time (minutes) at Julian day jd.
inline void sunPosition(double jd, double &lambda, double &decl, double &eqTime) {
  double T = (jd - 2451545.0) / 36525.0;
  double L0 = fmod(280.46646 + T * (36000.76983 + T * 0.0003032), 360.0);
  double M = 357.52911 + T * (35999.05029 - 0.0001537 * T);
  double e = 0.016708634 - T * (0.000042037 + 0.0000001267 * T);
  double C = sin(M * DEG) * (1.914602 - T * (0.004817 + 0.000014 * T)) + sin(2 * M * DEG) * (0.019993 - 0.000101 * T) +
             sin(3 * M * DEG) * 0.000289;
  double omega = 125.04 - 1934.136 * T;
  lambda = L0 + C - 0.00569 - 0.00478 * sin(omega * DEG);
  double eps0 = 23.0 + (26.0 + (21.448 - T * (46.815 + T * (0.00059 - T * 0.001813))) / 60.0) / 60.0;
  double eps = eps0 + 0.00256 * cos(omega * DEG);
  decl = asin(sin(eps * DEG) * sin(lambda * DEG)) / DEG;
  double y = tan(eps / 2 * DEG) * tan(eps / 2 * DEG);
  eqTime = 4.0 / DEG * (y * sin(2 * L0 * DEG) - 2 * e * sin(M * DEG) + 4 * e * y * sin(M * DEG) * cos(2 * L0 * DEG) -
                        0.5 * y * y * sin(4 * L0 * DEG) - 1.25 * e * e * sin(2 * M * DEG));
}

enum SunDay : uint8_t { SUN_RISES_AND_SETS, SUN_UP_ALL_DAY, SUN_DOWN_ALL_DAY };

// Sunrise (rising) or sunset of the day `day` (days since 1970) at a place
// (degrees, east and north positive), in minutes after that day's 0:00 UTC;
// for the local date `day` at any longitude. SUN_UP_ALL_DAY or
// SUN_DOWN_ALL_DAY when the sun does neither (midnight sun, polar night).
inline uint8_t sunEvent(long day, double lat, double lon, bool rising, double &minutes) {
  double jd0 = 2440587.5 + (double)day;
  minutes = 720.0 - 4.0 * lon;
  for (uint8_t i = 0; i < 3; i++) {   // at the event's own time: the sun moves on during the day
    double lambda, decl, eqTime;
    sunPosition(jd0 + minutes / 1440.0, lambda, decl, eqTime);
    double c = cos(90.833 * DEG) / (cos(lat * DEG) * cos(decl * DEG)) - tan(lat * DEG) * tan(decl * DEG);
    if (c > 1.0)  { return SUN_DOWN_ALL_DAY; }
    if (c < -1.0) { return SUN_UP_ALL_DAY; }
    double ha = acos(c) / DEG;
    minutes = 720.0 - 4.0 * (lon + (rising ? ha : -ha)) - eqTime;
  }
  return SUN_RISES_AND_SETS;
}

// The moon's longitude (degrees) at Julian day jd, to about 0.3 degrees.
inline double moonLongitude(double jd) {
  double d = jd - 2451545.0;
  double L = 218.316 + 13.176396 * d;
  double Mm = (134.963 + 13.064993 * d) * DEG, D = (297.850 + 12.190749 * d) * DEG, M = (357.529 + 0.98560028 * d) * DEG;
  return L + 6.289 * sin(Mm) - 1.274 * sin(Mm - 2 * D) + 0.658 * sin(2 * D) - 0.186 * sin(M) - 0.059 * sin(2 * Mm - 2 * D) -
         0.057 * sin(Mm - 2 * D + M) + 0.053 * sin(Mm + 2 * D) + 0.046 * sin(2 * D - M) + 0.041 * sin(Mm - M) -
         0.035 * sin(D) - 0.031 * sin(Mm + M);
}

// How far the moon is ahead of the sun, 0 to 360 degrees.
inline double moonElongation(double unixSeconds) {
  double jd = julianDay(unixSeconds), lambda, decl, eqTime;
  sunPosition(jd, lambda, decl, eqTime);
  double e = fmod(moonLongitude(jd) - lambda, 360.0);
  return e < 0 ? e + 360.0 : e;
}

// The share of the moon's disc that is lit, 0 to 100 %.
inline int moonLit(double elongation) { return (int)lround((1.0 - cos(elongation * DEG)) / 2.0 * 100.0); }

// The phase's name: new and full moon and the quarters 45 degrees wide
// each, the crescents and gibbous phases between them.
inline const char *moonPhaseName(double elongation) {
  static const char *const NAMES[] = { "new moon", "waxing crescent", "first quarter", "waxing gibbous",
                                       "full moon", "waning gibbous", "last quarter", "waning crescent" };
  int i = (int)floor((elongation + 22.5) / 45.0) % 8;
  return NAMES[i];
}

// The Unix time of the next full moon (full) or new moon after `from`.
inline double moonNext(double from, bool full) {
  const double DEG_PER_DAY = 12.190749;   // the moon gains on the sun, on average
  double target = full ? 180.0 : 0.0;
  double ahead = fmod(target - moonElongation(from) + 720.0, 360.0);
  if (ahead < 1.0) { ahead += 360.0; }    // just past it: the next one
  double t = from + ahead / DEG_PER_DAY * 86400.0;
  for (uint8_t i = 0; i < 4; i++) {
    double off = fmod(target - moonElongation(t) + 540.0, 360.0) - 180.0;   // -180 .. 180
    t += off / DEG_PER_DAY * 86400.0;
  }
  return t;
}

}  // namespace sky
