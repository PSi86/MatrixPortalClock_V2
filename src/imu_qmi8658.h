/* ----------------------------------------------------------------------
   The wall clock board's motion sensor: a QST QMI8658 at 0x6B on the board's
   I2C bus (board_hal.h, Wire1), its INT1 on GPIO13 (INT2 is not connected).
   It gives the clock its knock through its Wake on Motion: the chip compares
   each sample with a threshold and moves INT1 - the way the S3's LIS3DH
   knocks, where the chip decides and a line says so.

   Measured on the wall on 2026-10-19 (the measuring round, commit ec531e0):
   at rest 4-13 mg, knocks 180 mg to 3.3 g; Wake on Motion at 128 Hz and
   100 mg took 5 of 5 knocks, each as 1 to 5 INT1 edges 7-15 ms apart (the
   line toggles with every sample over the threshold, and needs no status read
   to go on), and gave no edge in two quiet minutes. Its tap engine accepted
   its commands but reported nothing, not even at a fraction of the knocks:
   this chip (printed without A or C, revision 0x7C) has none that works.

   Registers and the CTRL9 command handshake as SensorLib's SensorQMI8658 and
   RIOT's qmi8658 driver use them: CTRL9 takes a command, STATUSINT bit 7 says
   it is done (with CTRL8 bit 7 set), CTRL9 = 0 acknowledges it.
   ------------------------------------------------------------------------- */
#pragma once

#include <Arduino.h>
#include <Wire.h>
#include "board_hal.h"   // BoardBusLock: the board's own bus is shared

class Qmi8658 {
 public:
  static const uint8_t ADDR = 0x6B;

  // Resets the chip, checks who it is and leaves the sensors off; false when
  // it does not answer as a QMI8658.
  bool begin(TwoWire &wire) {
    _wire = &wire;
    _found = false;
    if (!writeReg(REG_RESET, RESET_VALUE)) { return false; }
    delay(15);                                   // up to 15 ms; it does not answer meanwhile
    _whoAmI   = readReg(REG_WHO_AM_I);
    _revision = readReg(REG_REVISION);
    if (_whoAmI != WHO_AM_I_VALUE) { return false; }
    _found = writeReg(REG_CTRL1, CTRL1_AUTO_INCREMENT) &&
             writeReg(REG_CTRL8, CTRL8_HANDSHAKE_STATUSINT) &&
             writeReg(REG_CTRL7, 0);
    return _found;
  }

  bool found() const { return _found; }
  uint8_t whoAmI() const { return _whoAmI; }
  uint8_t revision() const { return _revision; }

  // Wake on Motion, as SensorLib configures it: the accelerometer at 128 Hz
  // in its low-power mode (ODR code 12) and +/-4 g, the threshold in CAL1_L
  // (1 mg per step, so at most 255 mg), CAL1_H = INT1 with the line low at
  // rest (bits 7:6 = 00) and the samples ignored after the start (bits 5:0),
  // the command WRITE_WOM_SETTING, INT1 on, the accelerometer on. There is no
  // data output meanwhile. Ended by begin(), which resets the chip.
  bool womStart(uint8_t thresholdMg, uint8_t blanking) {
    if (!_found) { return false; }
    bool ok = writeReg(REG_CTRL7, 0);
    ok = writeReg(REG_CTRL2, (uint8_t)((ACCEL_FS_4G << 4) | ODR_LOW_POWER_128HZ)) && ok;
    const uint8_t cal[2] = { thresholdMg, (uint8_t)(blanking & 0x3F) };
    ok = writeRegs(REG_CAL1_L, cal, sizeof(cal)) && ok;
    ok = command(CMD_WRITE_WOM_SETTING) && ok;
    ok = writeReg(REG_CTRL1, CTRL1_AUTO_INCREMENT | CTRL1_INT1_ENABLE) && ok;
    ok = writeReg(REG_CTRL7, CTRL7_ACCEL_ENABLE) && ok;
    return ok;
  }

  // How many reads or writes failed on the bus since the start.
  uint32_t busErrors() const { return _errors; }

 private:
  static const uint8_t REG_WHO_AM_I  = 0x00;
  static const uint8_t REG_REVISION  = 0x01;
  static const uint8_t REG_CTRL1     = 0x02;
  static const uint8_t REG_CTRL2     = 0x03;
  static const uint8_t REG_CTRL7     = 0x08;
  static const uint8_t REG_CTRL8     = 0x09;
  static const uint8_t REG_CTRL9     = 0x0A;
  static const uint8_t REG_CAL1_L    = 0x0B;
  static const uint8_t REG_STATUSINT = 0x2D;
  static const uint8_t REG_RESET     = 0x60;

  static const uint8_t WHO_AM_I_VALUE = 0x05;
  static const uint8_t RESET_VALUE    = 0xB0;
  static const uint8_t CTRL1_AUTO_INCREMENT = 0x40;
  static const uint8_t CTRL1_INT1_ENABLE    = 0x08;
  static const uint8_t ACCEL_FS_4G          = 1;
  static const uint8_t ODR_LOW_POWER_128HZ  = 12;
  static const uint8_t CTRL7_ACCEL_ENABLE   = 0x01;
  static const uint8_t CTRL8_HANDSHAKE_STATUSINT = 0x80;
  static const uint8_t STATUSINT_CMD_DONE   = 0x80;
  static const uint8_t CMD_ACK               = 0x00;
  static const uint8_t CMD_WRITE_WOM_SETTING = 0x08;

  // A CTRL9 command: written, done when STATUSINT bit 7 rises, acknowledged
  // with 0, which lets the bit fall again.
  bool command(uint8_t cmd) {
    if (!writeReg(REG_CTRL9, cmd)) { return false; }
    if (!waitDone(true)) { return false; }
    if (!writeReg(REG_CTRL9, CMD_ACK)) { return false; }
    return waitDone(false);
  }
  bool waitDone(bool done) {
    for (uint8_t i = 0; i < 100; i++) {          // 100 ms; it takes about one
      uint8_t s = readReg(REG_STATUSINT);
      if (((s & STATUSINT_CMD_DONE) != 0) == done) { return true; }
      delay(1);
    }
    _errors++;
    return false;
  }

  uint8_t readReg(uint8_t reg) {
    uint8_t v = 0;
    readRegs(reg, &v, 1);
    return v;
  }
  // One transfer each, with the bus held for all of it (BoardBusLock).
  bool readRegs(uint8_t reg, uint8_t *buf, uint8_t n) {
    BoardBusLock lock;
    _wire->beginTransmission(ADDR);
    _wire->write(reg);
    if (_wire->endTransmission(false) != 0 || _wire->requestFrom(ADDR, (size_t)n) != n) { _errors++; return false; }
    for (uint8_t i = 0; i < n; i++) { buf[i] = (uint8_t)_wire->read(); }
    return true;
  }
  bool writeRegs(uint8_t reg, const uint8_t *buf, uint8_t n) {
    BoardBusLock lock;
    _wire->beginTransmission(ADDR);
    _wire->write(reg);
    _wire->write(buf, n);
    if (_wire->endTransmission() != 0) { _errors++; return false; }
    return true;
  }
  bool writeReg(uint8_t reg, uint8_t value) { return writeRegs(reg, &value, 1); }

  TwoWire *_wire = nullptr;
  bool     _found = false;
  uint8_t  _whoAmI = 0, _revision = 0;
  uint32_t _errors = 0;
};
