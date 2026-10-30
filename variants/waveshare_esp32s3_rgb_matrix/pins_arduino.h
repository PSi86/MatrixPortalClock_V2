#ifndef Pins_Arduino_h
#define Pins_Arduino_h

// Waveshare ESP32-S3-RGB-Matrix as the wall clock (board
// waveshare_esp32s3_rgb_matrix_clock). A copy of the core's generic esp32s3
// variant with two changes:
// - SDA is IO46 and SCL IO45 on the GPIO header, the bus for the clock's own
//   sensors (DS3231, BH1750, PAJ7620U2): the board's connector has IO45 on
//   its clock line and IO46 on its data line (2026-10-30; the other way round
//   no sensor answered). The generic variant's 8/9 are the HUB75 lines B and
//   E on this board.
// - No LED_BUILTIN: the board has no user LED, and the generic variant's
//   (an RGB LED on GPIO48) is the clock line of the board's own I2C bus.
// The other pins of the board: HUB75 R1 4, G1 5, B1 6, R2 7, G2 15, B2 16,
// A 18, B 8, C 3, D 42, E 9, LAT 40, OE 2, CLK 41 (src/board_hal.h); board I2C
// SDA 47 / SCL 48 (QMI8658, PCF85063, SHTC3, audio codecs); microSD SD_MMC
// 1-bit CLK 1, CMD 44, D0 17; I2S 12/21/38/39/43, amplifier 11; BOOT GPIO0.

#include <stdint.h>
#include "soc/soc_caps.h"

#define USB_VID 0x303a
#define USB_PID 0x1001

static const uint8_t TX = 43;
static const uint8_t RX = 44;

static const uint8_t SDA = 46;
static const uint8_t SCL = 45;

static const uint8_t SS = 10;
static const uint8_t MOSI = 11;
static const uint8_t MISO = 13;
static const uint8_t SCK = 12;

static const uint8_t A0 = 1;
static const uint8_t A1 = 2;
static const uint8_t A2 = 3;
static const uint8_t A3 = 4;
static const uint8_t A4 = 5;
static const uint8_t A5 = 6;
static const uint8_t A6 = 7;
static const uint8_t A7 = 8;
static const uint8_t A8 = 9;
static const uint8_t A9 = 10;
static const uint8_t A10 = 11;
static const uint8_t A11 = 12;
static const uint8_t A12 = 13;
static const uint8_t A13 = 14;
static const uint8_t A14 = 15;
static const uint8_t A15 = 16;
static const uint8_t A16 = 17;
static const uint8_t A17 = 18;
static const uint8_t A18 = 19;
static const uint8_t A19 = 20;

static const uint8_t T1 = 1;
static const uint8_t T2 = 2;
static const uint8_t T3 = 3;
static const uint8_t T4 = 4;
static const uint8_t T5 = 5;
static const uint8_t T6 = 6;
static const uint8_t T7 = 7;
static const uint8_t T8 = 8;
static const uint8_t T9 = 9;
static const uint8_t T10 = 10;
static const uint8_t T11 = 11;
static const uint8_t T12 = 12;
static const uint8_t T13 = 13;
static const uint8_t T14 = 14;

#endif /* Pins_Arduino_h */
