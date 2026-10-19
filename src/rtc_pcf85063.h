/* ----------------------------------------------------------------------
   The wall clock board's own real-time clock: an NXP PCF85063ATL at 0x51 on
   the board's I2C bus (board_hal.h, Wire1). RTClib has no class for it - its
   PCF8563 answers at the same address but keeps its time elsewhere - so the
   few registers the clock needs are read and written here.

   Its VDD comes through two Schottky diodes from 3V3 and from the board's
   battery socket J4 (SH1.0, pin 1 plus). The charging path is not fitted, so
   a primary coin cell may go in (schematic, read on 2026-10-19). Without a
   cell it keeps the time over a restart or a flash, not over a power cut. Its
   32.768 kHz crystal has no temperature compensation.

   Registers: 0x00 Control_1, 0x01 Control_2, 0x02 Offset, 0x03 a free RAM
   byte, 0x04..0x0A seconds to years in BCD (2000-2099). Bit 7 of the seconds
   is OS: the oscillator has stopped since it was last cleared, so the time
   cannot be trusted.

   The RAM byte holds a mark the clock writes with every time it sets. A chip
   the clock has never set keeps whatever time it was given before - the demo
   firmware Waveshare ships writes a fixed date at every start - and OS alone
   would let that through as a plausible time.
   ------------------------------------------------------------------------- */
#pragma once

#include <Arduino.h>
#include <Wire.h>
#include <RTClib.h>   // DateTime: the same calendar sums as for the DS3231

class Pcf85063 {
 public:
  static const uint8_t ADDR = 0x51;

  // Looks for the chip and reads its state; false when it does not answer.
  bool begin(TwoWire &wire) {
    _wire = &wire;
    uint8_t r[5];
    _found = readRegs(REG_CONTROL_1, r, sizeof(r));
    if (!_found) { return false; }
    _control1 = r[0];
    _control2 = r[1];
    _offset   = (int8_t)r[2];
    _kept     = !(r[4] & SECONDS_OS) && r[3] == MARK;
    return true;
  }

  bool found() const { return _found; }
  // Whether it holds a time this clock gave it, its oscillator running since.
  bool kept() const { return _kept; }
  // Its registers as begin() found them, for the console.
  uint8_t control1() const { return _control1; }
  uint8_t control2() const { return _control2; }
  int8_t  offset() const { return _offset; }

  // Its time as UTC, or 0 when it holds none, a register reads out of range
  // or the bus fails.
  time_t read() {
    if (!_found) { return 0; }
    uint8_t r[7];
    if (!readRegs(REG_SECONDS, r, sizeof(r))) { return 0; }
    if (r[0] & SECONDS_OS) { _kept = false; return 0; }
    uint8_t sec = bcd(r[0] & 0x7F), min = bcd(r[1] & 0x7F), hour = bcd(r[2] & 0x3F);
    uint8_t day = bcd(r[3] & 0x3F), month = bcd(r[5] & 0x1F), year = bcd(r[6]);
    if (sec > 59 || min > 59 || hour > 23 || day < 1 || day > 31 || month < 1 || month > 12 || year > 99) { return 0; }
    return (time_t)DateTime(2000 + year, month, day, hour, min, sec).unixtime();
  }

  // Sets it to `utc` (2000-2099): holds its divider (STOP), writes the mark and
  // the time with OS clear in one go, and lets it run again - its second starts
  // then.
  bool write(time_t utc) {
    if (!_found) { return false; }
    DateTime t((uint32_t)utc);
    if (t.year() < 2000 || t.year() > 2099) { return false; }
    uint8_t c1 = (uint8_t)(_control1 & ~CONTROL_1_STOP);
    if (!writeReg(REG_CONTROL_1, c1 | CONTROL_1_STOP)) { return false; }
    uint8_t r[8] = { MARK, dec(t.second()), dec(t.minute()), dec(t.hour()), dec(t.day()),
                     t.dayOfTheWeek(), dec(t.month()), dec(t.year() - 2000) };
    bool ok = writeRegs(REG_RAM, r, sizeof(r));
    ok = writeReg(REG_CONTROL_1, c1) && ok;
    _control1 = c1;
    _kept = ok;
    return ok;
  }

  // For a chip that holds no time of this clock's: the software reset the
  // datasheet gives (58h into Control_1), which also clears what its power-on
  // may have left in its registers - Linux's driver does the same when it
  // finds OS set. Then its crystal setting (CAP_SEL 12.5 pF, as Waveshare's
  // driver sets it) and CLKOUT off, which the reset turns on at 32 kHz and
  // which nothing on this board takes (pin 9 is not connected) - it costs a
  // coin cell current. The Offset register stays at the reset's 0.
  bool reset() {
    if (!_found) { return false; }
    bool ok = writeReg(REG_CONTROL_1, CONTROL_1_RESET);
    delay(2);
    ok = writeReg(REG_CONTROL_1, CONTROL_1_CAP_12PF5) && ok;
    ok = writeReg(REG_CONTROL_2, CONTROL_2_CLKOUT_OFF) && ok;
    _control1 = CONTROL_1_CAP_12PF5;
    _control2 = CONTROL_2_CLKOUT_OFF;
    _offset = 0;
    _kept = false;
    return ok;
  }

  // CLKOUT off on a chip that keeps its time (and so is not reset): a chip set
  // before the clock took care of it may still drive it.
  bool clkoutOff() {
    if (!_found || (_control2 & CONTROL_2_COF) == CONTROL_2_CLKOUT_OFF) { return true; }
    uint8_t c2 = (uint8_t)((_control2 & ~CONTROL_2_COF) | CONTROL_2_CLKOUT_OFF);
    if (!writeReg(REG_CONTROL_2, c2)) { return false; }
    _control2 = c2;
    return true;
  }

 private:
  static const uint8_t REG_CONTROL_1 = 0x00;
  static const uint8_t REG_CONTROL_2 = 0x01;
  static const uint8_t REG_RAM       = 0x03;
  static const uint8_t REG_SECONDS   = 0x04;
  static const uint8_t SECONDS_OS          = 0x80;
  static const uint8_t CONTROL_1_STOP      = 0x20;
  static const uint8_t CONTROL_1_RESET     = 0x58;
  static const uint8_t CONTROL_1_CAP_12PF5 = 0x01;   // 24-hour mode, everything else off
  static const uint8_t CONTROL_2_COF        = 0x07;
  static const uint8_t CONTROL_2_CLKOUT_OFF = 0x07;   // COF 111: CLKOUT low; no interrupts
  static const uint8_t MARK = 0xC5;                   // in the RAM byte: set by this clock

  static uint8_t bcd(uint8_t v) { return (uint8_t)((v >> 4) * 10 + (v & 0x0F)); }
  static uint8_t dec(int v)     { return (uint8_t)(((v / 10) << 4) | (v % 10)); }

  // One transfer each, with the bus held for all of it (BoardBusLock): the
  // bytes read wait in TwoWire's buffer until read(), which takes no lock.
  bool readRegs(uint8_t reg, uint8_t *buf, uint8_t n) {
    BoardBusLock lock;
    _wire->beginTransmission(ADDR);
    _wire->write(reg);
    if (_wire->endTransmission(false) != 0) { return false; }
    if (_wire->requestFrom(ADDR, (size_t)n) != n) { return false; }
    for (uint8_t i = 0; i < n; i++) { buf[i] = (uint8_t)_wire->read(); }
    return true;
  }
  bool writeRegs(uint8_t reg, const uint8_t *buf, uint8_t n) {
    BoardBusLock lock;
    _wire->beginTransmission(ADDR);
    _wire->write(reg);
    _wire->write(buf, n);
    return _wire->endTransmission() == 0;
  }
  bool writeReg(uint8_t reg, uint8_t value) { return writeRegs(reg, &value, 1); }

  TwoWire *_wire = nullptr;
  bool    _found = false;
  bool    _kept = false;
  uint8_t _control1 = 0;
  uint8_t _control2 = 0;
  int8_t  _offset = 0;
};
