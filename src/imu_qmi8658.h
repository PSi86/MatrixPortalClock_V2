/* ----------------------------------------------------------------------
   The wall clock board's motion sensor: a QST QMI8658 at 0x6B on the board's
   I2C bus (board_hal.h, Wire1), its INT1 on GPIO13 (INT2 is not connected).
   Only its accelerometer is used, for the knock; the gyroscope stays off.

   Registers and the CTRL9 command handshake as SensorLib's SensorQMI8658 and
   RIOT's qmi8658 driver use them (read on 2026-10-19): CTRL9 takes a command,
   STATUSINT bit 7 says it is done (with CTRL8 bit 7 set), CTRL9 = 0 acknowledges
   it. The FIFO holds 6 bytes per sample with the accelerometer alone (x, y, z,
   little-endian), its fill level counts 2-byte words, and it is read after
   the command REQ_FIFO; writing FIFO_CTRL afresh ends that read mode.

   The board's chip is printed without A or C. Only the QMI8658A has the tap
   engine; tapConfigure() tries it, and whether it reports taps tells.
   ------------------------------------------------------------------------- */
#pragma once

#include <Arduino.h>
#include <Wire.h>

// The tap engine's settings (the two CONFIGURE_TAP commands): windows in
// samples, thresholds in g^2 x 1000, alpha and gamma in 1/128.
struct Qmi8658Tap {
  uint8_t  peakWindow = 20;
  uint8_t  priority = 5;        // Z > Y > X
  uint16_t tapWindow = 55;
  uint16_t dTapWindow = 140;
  uint8_t  alpha = 8;           // 0.0625
  uint8_t  gamma = 32;          // 0.25
  uint16_t peak = 300;          // 0.3 g^2
  uint16_t quiet = 180;         // 0.18 g^2 (UDM)
};

class Qmi8658 {
 public:
  static const uint8_t ADDR = 0x6B;
  static const uint8_t SAMPLES_PER_READ = 20;   // 120 bytes: within TwoWire's 128-byte buffer

  // Resets the chip, checks who it is and leaves the sensors off; false when
  // it does not answer as a QMI8658.
  bool begin(TwoWire &wire) {
    _wire = &wire;
    _found = false;
    if (!writeReg(REG_RESET, RESET_VALUE)) { return false; }
    delay(15);                                   // up to 15 ms; it does not answer meanwhile
    _resetResult = readReg(REG_RESET_RESULT);
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
  uint8_t resetResult() const { return _resetResult; }   // 0x80 after a good reset

  // The accelerometer on at an ODR code (CTRL2: 3 = 1000 Hz, 4 = 500 Hz,
  // 5 = 250 Hz, 6 = 125 Hz; measured on the wall on 2026-10-19: about 900 and
  // 450 samples a second) and +/-4 g, into the FIFO in stream mode (128
  // samples, the oldest overwritten), with a watermark the INT1 line reports
  // when `fifoInt1` - for a look at whether the line works at all. `int1`
  // alone switches INT1 on for the tap engine (tapConfigure()).
  bool accelStart(uint8_t odrCode, uint8_t watermark, bool fifoInt1, bool int1) {
    if (!_found) { return false; }
    bool ok = writeReg(REG_CTRL7, 0);
    ok = writeReg(REG_CTRL2, (uint8_t)((ACCEL_FS_4G << 4) | (odrCode & 0x0F))) && ok;
    ok = command(CMD_RST_FIFO) && ok;
    uint8_t ctrl1 = CTRL1_AUTO_INCREMENT | (fifoInt1 ? CTRL1_FIFO_INT1 : 0) | (fifoInt1 || int1 ? CTRL1_INT1_ENABLE : 0);
    ok = writeReg(REG_CTRL1, ctrl1) && ok;
    _fifoCtrl = FIFO_SIZE_128 | FIFO_MODE_STREAM;
    ok = writeReg(REG_FIFO_CTRL, _fifoCtrl) && ok;
    ok = writeReg(REG_FIFO_WTM_TH, watermark) && ok;
    ok = writeReg(REG_CTRL7, CTRL7_ACCEL_ENABLE) && ok;
    delay(3);                                    // the accelerometer wakes up
    _accelOn = ok;
    return ok;
  }

  bool accelStop() {
    _accelOn = false;
    bool ok = writeReg(REG_CTRL8, CTRL8_HANDSHAKE_STATUSINT);   // the tap engine off too
    ok = writeReg(REG_CTRL7, 0) && ok;
    ok = writeReg(REG_CTRL1, CTRL1_AUTO_INCREMENT) && ok;
    return ok;
  }

  // Up to SAMPLES_PER_READ samples out of the FIFO into xyz (raw counts,
  // 8192 per g at +/-4 g); how many, or -1 when the bus or the handshake failed.
  int fifoRead(int16_t xyz[][3]) {
    uint8_t s[2];
    if (!readRegs(REG_FIFO_SMPL_CNT, s, 2)) { return -1; }
    if (!(s[1] & FIFO_STATUS_NOT_EMPTY)) { return 0; }
    uint16_t bytes = (uint16_t)(2 * ((((uint16_t)s[1] & 0x03) << 8) | s[0]));
    uint16_t n = bytes / 6;
    if (n == 0) { return 0; }
    if (n > SAMPLES_PER_READ) { n = SAMPLES_PER_READ; }
    if (!command(CMD_REQ_FIFO)) { return -1; }
    uint8_t raw[SAMPLES_PER_READ * 6];
    bool ok = readRegs(REG_FIFO_DATA, raw, (uint8_t)(n * 6));
    ok = writeReg(REG_FIFO_CTRL, _fifoCtrl) && ok;   // out of the read mode
    if (!ok) { return -1; }
    for (uint16_t i = 0; i < n; i++) {
      for (uint8_t a = 0; a < 3; a++) { xyz[i][a] = (int16_t)(raw[i * 6 + a * 2] | (raw[i * 6 + a * 2 + 1] << 8)); }
    }
    return n;
  }

  // The tap engine in two CONFIGURE_TAP commands (CAL1_L .. CAL4_H each, as
  // SensorLib writes them; its example takes the defaults of Qmi8658Tap at
  // 500 Hz), then CTRL8 bit 0, and with `toInt1` its interrupt on INT1 (CTRL8
  // bit 6) - the line the S3's LIS3DH knocks with. The accelerometer is off
  // meanwhile and started again by the caller, with INT1 on.
  bool tapConfigure(const Qmi8658Tap &t, bool toInt1) {
    if (!_found) { return false; }
    bool ok = writeReg(REG_CTRL7, 0);
    const uint8_t first[8]  = { t.peakWindow, t.priority, (uint8_t)(t.tapWindow & 0xFF), (uint8_t)(t.tapWindow >> 8),
                                (uint8_t)(t.dTapWindow & 0xFF), (uint8_t)(t.dTapWindow >> 8), 0, 0x01 };
    const uint8_t second[8] = { t.alpha, t.gamma, (uint8_t)(t.peak & 0xFF), (uint8_t)(t.peak >> 8),
                                (uint8_t)(t.quiet & 0xFF), (uint8_t)(t.quiet >> 8), 0, 0x02 };
    ok = writeRegs(REG_CAL1_L, first, sizeof(first)) && ok;
    ok = command(CMD_CONFIGURE_TAP) && ok;
    ok = writeRegs(REG_CAL1_L, second, sizeof(second)) && ok;
    ok = command(CMD_CONFIGURE_TAP) && ok;
    uint8_t ctrl8 = CTRL8_HANDSHAKE_STATUSINT | CTRL8_TAP_ENABLE | (toInt1 ? CTRL8_ACTIVITY_INT1 : 0);
    ok = writeReg(REG_CTRL8, ctrl8) && ok;
    return ok;
  }

  // Wake on Motion, as SensorLib configures it: the accelerometer at an ODR
  // code (12 = 128 Hz low power; the normal ones as above), the threshold in
  // CAL1_L (1 mg per step, so at most 255 mg), CAL1_H = INT1 with the line low
  // at rest (bits 7:6 = 00) and the samples ignored after the start (bits
  // 5:0), the command WRITE_WOM_SETTING, INT1 on, the accelerometer on.
  // SensorLib: there is no data output meanwhile. A motion over the threshold
  // sets STATUS1 bit 2 and moves INT1 - how (a level that toggles, a pulse, or
  // one that stays) is what the measuring round finds out. Ended by begin().
  bool womStart(uint8_t odrCode, uint8_t thresholdMg, uint8_t blanking) {
    if (!_found) { return false; }
    bool ok = writeReg(REG_CTRL7, 0);
    ok = writeReg(REG_CTRL2, (uint8_t)((ACCEL_FS_4G << 4) | (odrCode & 0x0F))) && ok;
    const uint8_t cal[2] = { thresholdMg, (uint8_t)(blanking & 0x3F) };
    ok = writeRegs(REG_CAL1_L, cal, sizeof(cal)) && ok;
    ok = command(CMD_WRITE_WOM_SETTING) && ok;
    ok = writeReg(REG_CTRL1, CTRL1_AUTO_INCREMENT | CTRL1_INT1_ENABLE) && ok;
    ok = writeReg(REG_CTRL7, CTRL7_ACCEL_ENABLE) && ok;
    return ok;
  }

  // STATUS1 as it stands (bit 1 tap, bit 2 Wake on Motion); 0 on a bus error.
  uint8_t status1() { return readReg(REG_STATUS1); }

  // A tap the engine has seen since the last look: TAP_STATUS (bits 1:0 single
  // 1 / double 2, bits 5:4 axis X 1 / Y 2 / Z 3, bit 7 negative), or 0.
  uint8_t tapRead() {
    uint8_t s1 = readReg(REG_STATUS1);
    if (!(s1 & STATUS1_TAP)) { return 0; }
    return readReg(REG_TAP_STATUS);
  }

  // How many reads or writes failed on the bus since the start.
  uint32_t busErrors() const { return _errors; }

 private:
  static const uint8_t REG_WHO_AM_I     = 0x00;
  static const uint8_t REG_REVISION     = 0x01;
  static const uint8_t REG_CTRL1        = 0x02;
  static const uint8_t REG_CTRL2        = 0x03;
  static const uint8_t REG_CTRL7        = 0x08;
  static const uint8_t REG_CTRL8        = 0x09;
  static const uint8_t REG_CTRL9        = 0x0A;
  static const uint8_t REG_CAL1_L       = 0x0B;
  static const uint8_t REG_FIFO_WTM_TH  = 0x13;
  static const uint8_t REG_FIFO_CTRL    = 0x14;
  static const uint8_t REG_FIFO_SMPL_CNT = 0x15;   // and FIFO_STATUS 0x16
  static const uint8_t REG_FIFO_DATA    = 0x17;
  static const uint8_t REG_STATUSINT    = 0x2D;
  static const uint8_t REG_STATUS1      = 0x2F;
  static const uint8_t REG_RESET_RESULT = 0x4D;
  static const uint8_t REG_TAP_STATUS   = 0x59;
  static const uint8_t REG_RESET        = 0x60;

  static const uint8_t WHO_AM_I_VALUE = 0x05;
  static const uint8_t RESET_VALUE    = 0xB0;
  static const uint8_t CTRL1_AUTO_INCREMENT = 0x40;
  static const uint8_t CTRL1_INT1_ENABLE    = 0x08;
  static const uint8_t CTRL1_FIFO_INT1      = 0x04;   // the FIFO's interrupt on INT1 (clear: INT2)
  static const uint8_t ACCEL_FS_4G          = 1;
  static const uint8_t CTRL7_ACCEL_ENABLE   = 0x01;
  static const uint8_t CTRL8_TAP_ENABLE     = 0x01;
  static const uint8_t CTRL8_ACTIVITY_INT1  = 0x40;   // tap and motion on INT1 (clear: INT2)
  static const uint8_t CTRL8_HANDSHAKE_STATUSINT = 0x80;
  static const uint8_t FIFO_MODE_STREAM     = 0x02;
  static const uint8_t FIFO_SIZE_128        = 0x0C;
  static const uint8_t FIFO_STATUS_NOT_EMPTY = 0x10;
  static const uint8_t STATUS1_TAP          = 0x02;
  static const uint8_t STATUSINT_CMD_DONE   = 0x80;
  static const uint8_t CMD_ACK           = 0x00;
  static const uint8_t CMD_RST_FIFO      = 0x04;
  static const uint8_t CMD_REQ_FIFO      = 0x05;
  static const uint8_t CMD_WRITE_WOM_SETTING = 0x08;
  static const uint8_t CMD_CONFIGURE_TAP = 0x0C;

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
  bool     _accelOn = false;
  uint8_t  _whoAmI = 0, _revision = 0, _resetResult = 0;
  uint8_t  _fifoCtrl = 0;
  uint32_t _errors = 0;
};
