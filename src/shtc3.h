/* ----------------------------------------------------------------------
   The wall clock board's temperature and humidity sensor: a Sensirion SHTC3
   at 0x70 on the board's I2C bus (board_hal.h, Wire1). The few commands the
   clock needs are sent here.

   Commands are 16 bits, most significant byte first. Between them the chip
   sleeps (SLEEP); WAKEUP needs up to 240 us before the next command. A
   measurement without clock stretching (MEASURE: normal mode, temperature
   first) takes up to 12.1 ms; reading it before then is not acknowledged.
   The answer is six bytes: temperature, its CRC, humidity, its CRC, each
   value 16 bits. CRC-8 with the polynomial 0x31, starting at 0xFF.

   The chip sits on the board beside the ESP32-S3 and its regulators, so it
   may read warmer than the room; how much is to be measured on the wall.
   ------------------------------------------------------------------------- */
#pragma once

#include <Arduino.h>
#include <Wire.h>
#include "board_hal.h"   // BoardBusLock: the board's own bus is shared

class Shtc3 {
 public:
  static const uint8_t ADDR = 0x70;
  static const uint16_t MEASURE_MS = 13;   // a measurement's longest, 12.1 ms, and a little

  // Looks for the chip by its ID register and lets it sleep; false when it
  // does not answer or another chip does.
  bool begin(TwoWire &wire) {
    _wire = &wire;
    _found = false;
    uint8_t r[3];
    if (!command(WAKEUP)) { return false; }
    delayMicroseconds(WAKEUP_US);
    bool read = command(READ_ID) && readBytes(r, sizeof(r)) && crc8(r, 2) == r[2];
    command(SLEEP);
    if (!read) { return false; }
    _id = (uint16_t)(r[0] << 8 | r[1]);
    _found = (_id & ID_MASK) == ID_SHTC3;
    return _found;
  }

  bool found() const { return _found; }
  uint16_t id() const { return _id; }

  // Wakes the chip and starts a measurement; read() it MEASURE_MS later.
  bool start() {
    if (!_found) { return false; }
    if (!command(WAKEUP)) { return false; }
    delayMicroseconds(WAKEUP_US);
    return command(MEASURE);
  }

  // The measurement start() began, in degrees Celsius and percent relative
  // humidity, and the chip asleep again; false when it does not answer or a
  // CRC is wrong.
  bool read(float &celsius, float &humidity) {
    if (!_found) { return false; }
    uint8_t r[6];
    bool ok = readBytes(r, sizeof(r)) && crc8(r, 2) == r[2] && crc8(r + 3, 2) == r[5];
    command(SLEEP);
    if (!ok) { return false; }
    celsius  = -45.0f + 175.0f * (float)(uint16_t)(r[0] << 8 | r[1]) / 65536.0f;
    humidity = 100.0f * (float)(uint16_t)(r[3] << 8 | r[4]) / 65536.0f;
    return true;
  }

 private:
  static const uint16_t SLEEP = 0xB098, WAKEUP = 0x3517, READ_ID = 0xEFC8, MEASURE = 0x7866;
  static const uint16_t WAKEUP_US = 240;
  static const uint16_t ID_MASK = 0x083F, ID_SHTC3 = 0x0807;   // bit 11 set, bits 5..0 000111

  // One transfer each, with the bus held for all of it (BoardBusLock).
  bool command(uint16_t c) {
    BoardBusLock lock;
    _wire->beginTransmission(ADDR);
    _wire->write((uint8_t)(c >> 8));
    _wire->write((uint8_t)c);
    return _wire->endTransmission() == 0;
  }
  bool readBytes(uint8_t *buf, uint8_t n) {
    BoardBusLock lock;
    if (_wire->requestFrom(ADDR, (size_t)n) != n) { return false; }
    for (uint8_t i = 0; i < n; i++) { buf[i] = (uint8_t)_wire->read(); }
    return true;
  }
  static uint8_t crc8(const uint8_t *data, uint8_t n) {
    uint8_t crc = 0xFF;
    for (uint8_t i = 0; i < n; i++) {
      crc ^= data[i];
      for (uint8_t b = 0; b < 8; b++) { crc = (crc & 0x80) ? (uint8_t)((crc << 1) ^ 0x31) : (uint8_t)(crc << 1); }
    }
    return crc;
  }

  TwoWire *_wire = nullptr;
  bool _found = false;
  uint16_t _id = 0;
};
