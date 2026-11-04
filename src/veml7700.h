/* ----------------------------------------------------------------------
   A Vishay VEML7700 ambient light sensor at 0x10 on the main I2C bus, as
   the light sensor of the auto-brightness instead of (or beside) a BH1750
   (brightness.cpp). The few registers the clock needs are set here. The
   figures are Vishay's: datasheet 84286 (Rev. 1.8, 28-Nov-2024) and the
   application note "Designing the VEML7700 Into an Application" (84323,
   06-Mar-2025).

   Registers are 16 bits, least significant byte first. A write is the
   command code and both bytes; a read is the command code written, then a
   repeated start and both bytes. ALS_CONF_0 (0x00): gain in bits 12:11,
   integration time in bits 9:6, shut down in bit 0 (set at power-up, so the
   chip measures nothing until it is cleared); PSM (0x03) off, so a new
   reading comes every integration time; ALS (0x04) the reading; ID (0x07)
   0x81 in its low byte and 0xC4 in its high byte for the address 0x10 (only
   the datasheet's later revisions list it, so it is said, not checked).

   The clock reads it five times a second at the standard integration time,
   100 ms, in one of three ranges chosen by the counts. The application note:
   begin at the lowest gain while the light is not known, use the gains 1 and
   2 only up to 100 lx (above that they are not linear), and with the gains
   1/4 and 1/8 apply its correction formula.
     gain x2     0.0336 lx a count   kept up to 100 lx
     gain x1/4   0.2688 lx a count   kept from 50 lx up to 16.1 klx
     gain x1/8   0.5376 lx a count   kept down to 11.8 klx (its top 35.2 klx)
   The ways up and down lie apart (nextRange()), so a light that wavers
   does not switch the range back and forth. A change goes as the note's flow chart does
   it: shut down, the new gain, on again. Then the chip needs 2.5 ms to start
   and one integration time for its first reading, up to 130 ms with the
   30 % the note allows; the readings of the next SETTLE_MS are left out.
   ---------------------------------------------------------------------- */
#pragma once

#include <Arduino.h>
#include <Wire.h>

class Veml7700 {
 public:
  static const uint8_t ADDR = 0x10;

  // Sets the chip up in its lowest gain and switches it on; false when it
  // does not answer or does not keep the setting.
  bool begin(TwoWire &wire) {
    _wire = &wire;
    _found = false;
    if (!readReg(REG_ID, _id)) { _id = 0; }
    if (!writeReg(REG_PSM, 0)) { return false; }
    if (!setRange(RANGE_LOW)) { return false; }
    uint16_t c;
    if (!readReg(REG_CONF, c) || c != conf(_range)) { return false; }
    _checkedAt = millis();
    _found = true;
    return true;
  }

  // The light, in lux. False while a change of range settles, for a reading
  // at the very top of its range (the next range takes over), and when the
  // chip does not answer. Every CHECK_MS its setting is read back first: a
  // chip whose supply dipped starts again shut down, and would otherwise keep
  // the last reading for ever. At five reads a second that leaves at most
  // five stale ones, fewer than the brightness's median of 13 lets through.
  bool read(float &lux) {
    if (!_found) { return false; }
    unsigned long now = millis();
    if (now - _changedAt < SETTLE_MS) { return false; }
    if (now - _checkedAt >= CHECK_MS) {
      _checkedAt = now;
      uint16_t c;
      if (readReg(REG_CONF, c) && c != conf(_range)) {
        _restored++;
        setRange(_range);
        return false;
      }
    }
    uint16_t counts;
    if (!readReg(REG_ALS, counts)) { return false; }
    _counts = counts;
    _countsRange = _range;
    uint8_t next = nextRange(counts);
    if (next != _range) { setRange(next); }
    if (counts == 0xFFFF) { return false; }
    const Range &r = RANGES[_countsRange];
    float x = counts * r.lxPerCount;
    lux = r.corrected ? corrected(x) : x;
    return true;
  }

  uint16_t id() const { return _id; }
  uint16_t counts() const { return _counts; }                         // the last reading's counts
  const char *gainText() const { return RANGES[_countsRange].name; }  // and its gain
  uint16_t restored() const { return _restored; }                  // settings found lost and set again

 private:
  static const uint8_t REG_CONF = 0x00, REG_PSM = 0x03, REG_ALS = 0x04, REG_ID = 0x07;
  static const uint16_t SHUT_DOWN = 0x0001;
  static const unsigned long SETTLE_MS = 150;   // 2.5 ms to start and one integration time, 130 ms at most
  static const unsigned long CHECK_MS = 1000;
  static constexpr float MAX_LUX = 140000.0f;   // the datasheet's highest illuminance it detects

  // ALS_CONF_0 for each range: the gain bits, integration time 100 ms
  // (0000), persistence 1, no interrupt, on.
  struct Range { uint16_t conf; float lxPerCount; bool corrected; const char *name; };
  static constexpr uint8_t RANGE_HIGH = 0, RANGE_MID = 1, RANGE_LOW = 2;
  static constexpr Range RANGES[3] = {
    { 0x0800, 0.0336f, false, "x2" },
    { 0x1800, 0.2688f, true,  "x1/4" },
    { 0x1000, 0.5376f, true,  "x1/8" },
  };

  static uint16_t conf(uint8_t range) { return RANGES[range].conf; }

  // The range for the next reading, by this one's counts.
  uint8_t nextRange(uint16_t counts) const {
    switch (_range) {
      case RANGE_HIGH: return counts > 2976 ? RANGE_MID : RANGE_HIGH;   // over 100 lx
      case RANGE_MID:  return counts < 186 ? RANGE_HIGH                 // under 50 lx
                            : counts > 60000 ? RANGE_LOW : RANGE_MID;   // over 16.1 klx
      default:         return counts < 93 ? RANGE_HIGH                  // under 50 lx
                            : counts < 22000 ? RANGE_MID : RANGE_LOW;   // under 11.8 klx
    }
  }

  // The application note's correction for the gains 1/4 and 1/8: above some
  // 1000 lx the chip reads too low (1500 lx counted are 1658 lx).
  static float corrected(float x) {
    float y = (((6.0135e-13f * x - 9.3924e-9f) * x + 8.1488e-5f) * x + 1.0023f) * x;
    return y < MAX_LUX ? y : MAX_LUX;
  }

  bool setRange(uint8_t range) {
    _range = range;
    _changedAt = millis();
    return writeReg(REG_CONF, conf(range) | SHUT_DOWN) && writeReg(REG_CONF, conf(range));
  }

  bool writeReg(uint8_t reg, uint16_t v) {
    _wire->beginTransmission(ADDR);
    _wire->write(reg);
    _wire->write((uint8_t)v);
    _wire->write((uint8_t)(v >> 8));
    return _wire->endTransmission() == 0;
  }
  bool readReg(uint8_t reg, uint16_t &v) {
    _wire->beginTransmission(ADDR);
    _wire->write(reg);
    if (_wire->endTransmission(false) != 0) { return false; }
    if (_wire->requestFrom(ADDR, (size_t)2) != 2) { return false; }
    uint8_t lo = (uint8_t)_wire->read();
    uint8_t hi = (uint8_t)_wire->read();
    v = (uint16_t)(hi << 8 | lo);
    return true;
  }

  TwoWire *_wire = nullptr;
  bool _found = false;
  uint16_t _id = 0;
  uint8_t _range = RANGE_LOW;
  uint8_t _countsRange = RANGE_LOW;
  uint16_t _counts = 0;
  uint16_t _restored = 0;
  unsigned long _changedAt = 0;
  unsigned long _checkedAt = 0;
};
