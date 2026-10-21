// The knock on the clock, from its motion sensor (concept doc, tab "Watchface
// animations": the knock is an input event; what it does is the Inputs page's
// business). The chip watches for it and raises a line; this module sets the
// chip up at the sensitivity asked for and says when a knock came. Whether it
// counts - a button held, a turn, the start - is the sketch's knockIsReal().
//
//   MatrixPortal S3: the onboard LIS3DH (also the rotation's sensor), INT1 on
//     ACCEL_INT_PIN, its interrupt generator behind a high-pass filter.
//   Wall clock: the Waveshare board's QMI8658 in its Wake on Motion, INT1 on
//     GPIO13, its edges counted by an interrupt.
//
// The sensitivity, 0 (off) to KNOCK_LEVEL_MAX, is stored with the Tetris
// face's settings (shk), where it has always been. Included by the sketch
// only, after board_hal.h: the interrupt is defined here, outside its struct.
#ifndef KNOCK_H
#define KNOCK_H

#include <Arduino.h>
#include <Wire.h>
#if BOARD_WAVESHARE_WALL
#include "imu_qmi8658.h"
#endif

const uint8_t KNOCK_LEVEL_MAX = 10;

#if BOARD_WAVESHARE_WALL
/* The QMI8658 compares every sample, at 128 Hz, with the threshold and toggles
   INT1 for each one over it - a knock comes as 1 to 5 edges 7-15 ms apart
   (measured on 2026-10-19). An interrupt counts the edges, so none is lost
   between two passes, and the first of a burst is the knock. The chip
   decides, as the S3's LIS3DH does. */
const uint8_t IMU_INT_PIN = 13;              // the QMI8658's INT1 (schematic, 2026-10-19)
const unsigned long KNOCK_BURST_MS = 150;    // edges this close are the same knock
const unsigned long KNOCK_FRESH_MS = 100;    // an edge older than this when looked at is dropped
const unsigned long KNOCK_ARM_MS   = 300;    // after a (re)start the chip's own edges

// Sensitivity 1..10 as the threshold in mg (Wake on Motion takes 1-255).
// Measured on the wall on 2026-10-19: at rest 4-13 mg, knocks 180 mg to 3.3 g;
// at 128 Hz 100 mg took 5 of 5 normal knocks, one of them as a single edge,
// and nothing in two quiet minutes. Level 10 is about three times the noise,
// level 1 the most the chip takes.
const uint8_t KNOCK_WOM_MG[KNOCK_LEVEL_MAX + 1] = { 0, 255, 200, 160, 130, 100, 80, 64, 50, 40, 30 };

// INT1's interrupt. A struct's member, defined outside it: in IRAM an inline
// function's literals land elsewhere, and the linker refuses that.
struct KnockIsr {
  static volatile uint32_t edges;            // INT1's edges since the start
  static volatile unsigned long edgeAt;      // millis() of the last
  static void edge();
};
volatile uint32_t KnockIsr::edges = 0;
volatile unsigned long KnockIsr::edgeAt = 0;
void IRAM_ATTR KnockIsr::edge() {
  edges = edges + 1;
  edgeAt = millis();
}
#else
// The LIS3DH's own interrupt generator compares against a fixed threshold,
// which would fire on gravity alone as soon as the clock is tilted. Its
// high-pass filter solves that, and it is applied to the interrupt generator
// ONLY (HP_IA1), not to the output registers (FDS stays 0) - so the rotation
// still reads gravity while the interrupt only sees what changes. Watching
// the line is better than sampling: between two reads the sketch was blind
// for 20 ms, long enough for a short tap to pass, and each read cost four
// I2C transactions. Now a knock costs one digitalRead per loop.
const uint8_t LIS3DH_CTRL_REG2     = 0x21;
const uint8_t LIS3DH_CTRL_REG3     = 0x22;
const uint8_t LIS3DH_CTRL_REG5     = 0x24;
const uint8_t LIS3DH_REFERENCE     = 0x26;
const uint8_t LIS3DH_INT1_CFG      = 0x30;
const uint8_t LIS3DH_INT1_SRC      = 0x31;
const uint8_t LIS3DH_INT1_THS      = 0x32;
const uint8_t LIS3DH_INT1_DURATION = 0x33;

// Sensitivity 1..10 as a threshold register value, measured on the device
// rather than guessed. Over two quiet stretches totalling about 70 s the
// largest deviation in any one second was 780 counts - the sensor's own noise
// at 100 Hz; every deliberate interaction came out at 1900 or more, knocks at
// 5900, a firm knock or picking the clock up at 12700. One step is 16 mg at
// the +/-2 g range and one g is 16000 counts, so level 10 at 1500 counts is
// 0.094 g, 6 steps, and level 1 at 6000 counts is 0.375 g, 24 steps.
const uint8_t KNOCK_THS_MIN  = 6;   // level 10
const uint8_t KNOCK_THS_STEP = 2;   // level 1 -> 24
#endif

class Knock {
 public:
#if BOARD_WAVESHARE_WALL
  // One try at setting the QMI8658 up on the board's bus; the sketch tries
  // again (sensorBegin(), sensors.h).
  bool imuBegin(TwoWire &bus) {
    pinMode(IMU_INT_PIN, INPUT);
    return _imu.begin(bus);
  }
  // At start-up, once imuBegin() has found the QMI8658 or not.
  void begin(TwoWire &bus, bool found) {
    _bus = &bus;
    if (!found) { Console.println("QMI8658 not found - no knock"); return; }
    _found = true;
    attachInterrupt(digitalPinToInterrupt(IMU_INT_PIN), KnockIsr::edge, CHANGE);
    apply();
    Console.printf("QMI8658 found (revision 0x%02X): the knock by Wake on Motion over %u mg\n", _imu.revision(),
                  KNOCK_WOM_MG[_level]);
  }
#else
  // At start-up, once the sketch found the LIS3DH (on the bus at `addr`).
  void begin(TwoWire &bus, uint8_t addr, uint8_t intPin) {
    _bus = &bus;
    _addr = addr;
    _intPin = intPin;
    _found = true;
    pinMode(intPin, INPUT);
    // High-pass filter on interrupt generator 1 only. HPM = 00 and the highest
    // cutoff, so the reference follows within a fraction of a second: tilting
    // is filtered away, a knock is not.
    writeReg(LIS3DH_CTRL_REG2, 0x01);
    writeReg(LIS3DH_INT1_DURATION, 0);   // react to the first sample over the line
    writeReg(LIS3DH_CTRL_REG3, 0x40);    // route generator 1 to the INT1 pin
    writeReg(LIS3DH_CTRL_REG5, 0x08);    // latch it until INT1_SRC is read
    (void)readReg(LIS3DH_REFERENCE);     // reset the filter
    apply();
    (void)readReg(LIS3DH_INT1_SRC);      // start with the line clear
  }
#endif

  bool found() const { return _found; }
  uint8_t level() const { return _level; }

  // The sensitivity, 0 (off) to KNOCK_LEVEL_MAX, into the chip: at start-up
  // and whenever it changes (also from the live preview). Kept until the chip
  // is found, then given to it.
  void setLevel(uint8_t level) {
    _level = level > KNOCK_LEVEL_MAX ? KNOCK_LEVEL_MAX : level;
    if (_found) { apply(); }
  }

  // On every pass: whether a knock came now. Now or not at all - one seen too
  // late is dropped.
  bool sensed(unsigned long now) {
    if (!_found) { return false; }
#if BOARD_WAVESHARE_WALL
    uint32_t edges = KnockIsr::edges;
    if (edges == _edgesSeen) { return false; }             // nothing happened
    _edgesSeen = edges;
    unsigned long at = KnockIsr::edgeAt;
    bool sameKnock = _lastEdgeAt != 0 && (long)(at - _lastEdgeAt) < (long)KNOCK_BURST_MS;
    _lastEdgeAt = at;
    if (sameKnock) { return false; }
    if ((long)(at - _armedAt) < (long)KNOCK_ARM_MS) { return false; }   // the chip starting
    if ((long)(now - at) > (long)KNOCK_FRESH_MS) { return false; }      // seen too late
    _knocks++;
    return true;
#else
    (void)now;
    if (digitalRead(_intPin) == LOW) { return false; }   // nothing happened
    // The interrupt is latched, so it has to be read away even when the knock
    // is going to be ignored - otherwise it would fire the moment the block lifts.
    (void)readReg(LIS3DH_INT1_SRC);
    return true;
#endif
  }

  // Throw away what the chip has latched: sensed() does not run while the
  // hotspot is up, so a button press there - a knock as far as the sensor is
  // concerned - would otherwise still be waiting when the face comes back.
  void discard() {
#if BOARD_WAVESHARE_WALL
    _edgesSeen = KnockIsr::edges;   // the edges so far count as seen
    _lastEdgeAt = KnockIsr::edgeAt;
#else
    if (_found) { (void)readReg(LIS3DH_INT1_SRC); }
#endif
  }

#if BOARD_WAVESHARE_WALL
  // Console "imu": the chip, the threshold and what it gave.
  void report() const {
    if (!_found) { Console.println("QMI8658 not found - no knock"); return; }
    Console.printf("QMI8658 (revision 0x%02X): knock sensitivity %u, Wake on Motion over %u mg; INT1 %s; "
                  "%lu edges and %lu knocks since the start; bus errors %lu\n", _imu.revision(), _level,
                  KNOCK_WOM_MG[_level], digitalRead(IMU_INT_PIN) == HIGH ? "high" : "low",
                  (unsigned long)KnockIsr::edges, (unsigned long)_knocks, (unsigned long)_imu.busErrors());
  }
#endif

 private:
#if BOARD_WAVESHARE_WALL
  // A reset ends what ran, then Wake on Motion at the threshold - or nothing,
  // for 0.
  void apply() {
    bool ok = _imu.begin(*_bus);
    if (ok && _level > 0) { ok = _imu.womStart(KNOCK_WOM_MG[_level], 16); }
    _armedAt = millis();
    if (!ok) { Console.println("QMI8658: the knock did not start"); }
  }
  Qmi8658 _imu;
  uint32_t _edgesSeen = 0;          // as far as sensed() looked
  unsigned long _lastEdgeAt = 0;    // the last edge it saw, a knock or not
  unsigned long _armedAt = 0;
  uint32_t _knocks = 0;             // bursts taken as knocks, for the console
#else
  void apply() {
    if (_level == 0) {
      writeReg(LIS3DH_INT1_CFG, 0x00);   // no axis enabled: the line stays quiet
      return;
    }
    writeReg(LIS3DH_INT1_THS, (uint8_t)(KNOCK_THS_MIN + (KNOCK_LEVEL_MAX - _level) * KNOCK_THS_STEP));
    writeReg(LIS3DH_INT1_CFG, 0x2A);     // X, Y or Z above the threshold
  }
  void writeReg(uint8_t reg, uint8_t value) {
    _bus->beginTransmission(_addr);
    _bus->write(reg);
    _bus->write(value);
    _bus->endTransmission();
  }
  uint8_t readReg(uint8_t reg) {
    _bus->beginTransmission(_addr);
    _bus->write(reg);
    if (_bus->endTransmission(false) != 0) { return 0; }
    if (_bus->requestFrom((int)_addr, 1) != 1) { return 0; }
    return (uint8_t)_bus->read();
  }
  uint8_t _addr = 0;
  uint8_t _intPin = 0;
#endif
  TwoWire *_bus = nullptr;
  bool _found = false;
  uint8_t _level = 0;
};

#endif  // KNOCK_H
