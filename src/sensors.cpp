// The clock's sensors, found and remembered (sensors.h).
#include "sensors.h"

#include "clock_state.h"
#include "info_band.h"

uint8_t i2cFound[16];
#if BOARD_WAVESHARE_WALL
uint8_t i2cBoardFound[16];
#endif
uint8_t  sensorTries[SENSOR_COUNT] = {};
uint16_t sensorsSeen    = 0;
uint16_t sensorsMissing = 0;
static SettingsStore<SensorRecord> sensorStore;

// The onboard LIS3DH (0x19), a VEML7700 (0x10), a BH1750 (0x23), a DS3231
// (0x68; many of its modules carry an EEPROM at 0x57 as well), a PAJ7620U2
// (0x73). A sensor that is not found can so be told from one the bus does
// not see at all.
void i2cScan() {
  Console.print("I2C bus:");
  uint8_t n = 0;
  for (uint8_t a = 0x08; a < 0x78; a++) {
    Wire.beginTransmission(a);
    if (Wire.endTransmission() != 0) { continue; }
    i2cFound[a / 8] |= (uint8_t)(1 << (a % 8));
    Console.printf(" 0x%02X", a);
    n++;
  }
  Console.println(n ? "" : " nothing answers");
}

#if BOARD_WAVESHARE_WALL
// Which of its parts answer - the codecs 0x18 and 0x40, the PCF85063 0x51,
// the QMI8658 0x6B, the SHTC3 0x70 (not used).
void boardBusScan() {
  Console.print("Board I2C bus:");
  uint8_t n = 0;
  for (uint8_t a = 0x08; a < 0x78; a++) {
    uint8_t err;
    {
      BoardBusLock lock;
      Wire1.beginTransmission(a);
      err = Wire1.endTransmission();
    }
    if (err != 0) { continue; }
    i2cBoardFound[a / 8] |= (uint8_t)(1 << (a % 8));
    Console.printf(" 0x%02X", a);
    n++;
  }
  Console.println(n ? "" : " nothing answers");
}
#endif

// The record: the sensors seen.
bool saveSensorRecord() {
  SensorRecord rec = { SENSOR_RECORD_MAGIC, sensorsSeen, 0 };
  return sensorStore.write(rec);
}

// A sensor this board does not support is never counted.
void sensorsCheck() {
  SensorRecord rec = { SENSOR_RECORD_MAGIC, 0, 0 };
  sensorStore.begin("sensors");
  if (!sensorStore.read(rec) || rec.magic != SENSOR_RECORD_MAGIC) { rec = { SENSOR_RECORD_MAGIC, 0, 0 }; }
  uint16_t supported = sensorsSupported(), found = 0;
  for (uint8_t i = 0; i < SENSOR_COUNT; i++) {
    if (sensorTries[i]) { found |= (uint16_t)(1 << i); }
  }
  sensorsMissing = (uint16_t)(rec.seen & supported & ~found);
  sensorsSeen = (uint16_t)((rec.seen | found) & supported);
  if (sensorsSeen != rec.seen) { saveSensorRecord(); }
  for (uint8_t i = 0; i < SENSOR_COUNT; i++) {
    if (sensorsMissing & (1 << i)) {
      Console.printf("%s missing - found at an earlier start; the web app can forget it\n", SENSOR_KINDS[i].chip);
    }
  }
}

// A sensor this clock no longer has is no longer missing, and not remembered
// until it is found again.
bool sensorForget(const String &key) {
  for (uint8_t i = 0; i < SENSOR_COUNT; i++) {
    if (key != SENSOR_KINDS[i].key) { continue; }
    uint16_t bit = (uint16_t)(1 << i);
    if (sensorsSeen & bit) {
      sensorsSeen &= (uint16_t)~bit;
      sensorsMissing &= (uint16_t)~bit;
      saveSensorRecord();
      panelDirty = true;   // the status pixel may go
#if BAND_ON_PANEL
      bandLocalDone = false;   // and the band's status line
#endif
      Console.printf("%s forgotten\n", SENSOR_KINDS[i].chip);
    }
    return true;
  }
  return false;
}
