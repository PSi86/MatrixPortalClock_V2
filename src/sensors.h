/* ----------------------------------------------------------------------
   The clock's sensors: what each board supports, set up at every start with
   more than one try, and remembered from start to start.

   Every sensor is looked for once, at start-up. One that does not answer then
   stays missing until the next start - on 2026-10-21 the MatrixPortal S3 came
   up without its LIS3DH (in portrait, without the knock) while the bus scan a
   moment later found it, and the cause could not be found. So each is tried
   up to SENSOR_TRIES times, SENSOR_RETRY_MS apart, and a try that fails is
   said on the console with whether the part answered on its address at all.

   The sketch remembers which sensors were found at earlier starts (its
   "sensors" record). One of those missing now turns the status pixel red, the
   start says SENSOR, and the web app lists it, with "Forget" for a sensor the
   clock no longer has.
   ---------------------------------------------------------------------- */
#ifndef SENSORS_H
#define SENSORS_H

#include <Arduino.h>
#include <Wire.h>

enum SensorId : uint8_t {
  SENSOR_ACCEL,      // LIS3DH, on the MatrixPortals' board
  SENSOR_LIGHT,      // BH1750
  SENSOR_RTC,        // DS3231
  SENSOR_GESTURE,    // PAJ7620U2
  SENSOR_BOARD_RTC,  // PCF85063, on the wall clock's board
  SENSOR_MOTION,     // QMI8658, on the wall clock's board
  SENSOR_COUNT
};

struct SensorKind {
  const char *key;    // the web app's and the API's name
  const char *chip;   // as the console and the web app say it
  const char *what;   // what the clock does with it
  uint8_t addr;       // I2C address
  bool boardBus;      // on the wall clock board's own bus (Wire1), else on Wire
};

const SensorKind SENSOR_KINDS[SENSOR_COUNT] = {
  { "accel",    "LIS3DH",    "orientation, knock",  0x19, false },
  { "light",    "BH1750",    "auto-brightness",     0x23, false },
  { "rtc",      "DS3231",    "real-time clock",     0x68, false },
  { "gesture",  "PAJ7620U2", "gestures",            0x73, false },
  { "boardRtc", "PCF85063",  "real-time clock",     0x51, true },
  { "motion",   "QMI8658",   "knock",               0x6B, true },
};

// The sensors this board's firmware works with.
inline uint16_t sensorsSupported() {
  uint16_t m = (1 << SENSOR_LIGHT) | (1 << SENSOR_RTC) | (1 << SENSOR_GESTURE);
#if BOARD_HAS_ACCEL
  m |= 1 << SENSOR_ACCEL;
#endif
#if BOARD_WAVESHARE_WALL
  m |= 1 << SENSOR_BOARD_RTC;
  m |= 1 << SENSOR_MOTION;
#endif
  return m;
}

const uint8_t  SENSOR_TRIES    = 3;
const uint16_t SENSOR_RETRY_MS = 50;

// Whether anything answers on the sensor's address.
inline bool sensorAnswers(const SensorKind &k) {
#if BOARD_WAVESHARE_WALL
  if (k.boardBus) {
    BoardBusLock lock;
    Wire1.beginTransmission(k.addr);
    return Wire1.endTransmission() == 0;
  }
#endif
  Wire.beginTransmission(k.addr);
  return Wire.endTransmission() == 0;
}

// Sets a sensor up: begin() tried up to SENSOR_TRIES times, SENSOR_RETRY_MS
// apart. Returns the try it was found on, 0 when it was not found.
//
// Test builds: -DSENSOR_FAIL_FIRST lets every first try fail, to see the
// retry; -DSENSOR_HIDE=<SensorId> lets every try of that one fail, to see a
// sensor go missing.
inline uint8_t sensorBegin(SensorId id, bool (*begin)()) {
  const SensorKind &k = SENSOR_KINDS[id];
  for (uint8_t t = 1; t <= SENSOR_TRIES; t++) {
    bool ok = begin();
#if defined(SENSOR_FAIL_FIRST)
    if (t == 1) { ok = false; }
#endif
#if defined(SENSOR_HIDE)
    if (id == SENSOR_HIDE) { ok = false; }
#endif
    if (ok) {
      if (t > 1) { Console.printf("%s: found on try %u\n", k.chip, (unsigned)t); }
      return t;
    }
    Console.printf("%s: try %u of %u failed - %s on 0x%02X\n", k.chip, (unsigned)t, (unsigned)SENSOR_TRIES,
                  sensorAnswers(k) ? "something answers" : "nothing answers", k.addr);
    if (t < SENSOR_TRIES) { delay(SENSOR_RETRY_MS); }
  }
  return 0;
}

// What the sketch keeps from start to start: the sensors found at an earlier
// start and not forgotten, one bit each by SensorId.
const uint32_t SENSOR_RECORD_MAGIC = 0x534E5352UL;   // "SNSR"
struct SensorRecord {
  uint32_t magic;
  uint16_t seen;
  uint16_t unused;
};

#endif  // SENSORS_H
