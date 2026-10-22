/* -------------------------------------------------------------------------
   brightness.h - the panel's brightness: the BH1750 light sensor and the
   brightness it sets, the manual levels and the auto-brightness trims the
   menu steps through, the brightness while a GIF plays, the perceptual fade
   (Classic clicks: UP held) - and, on the wall clock, the room's temperature
   and humidity from the SHTC3 beside the sensor.

   A module of its own since 2026-10-22 (code review of 2026-10-14, point 10).
   ------------------------------------------------------------------------- */
#ifndef BRIGHTNESS_H
#define BRIGHTNESS_H

#include <Arduino.h>
#include "board_hal.h"

// ---- The light sensor and the brightness ----------------------------------------
extern bool    luxOK;                 // true once the BH1750 was found on I2C
extern float   lastLux;               // most recent UNFILTERED lux reading (web UI), -1 = none yet
extern float   luxAvg;                // the filtered light, -1 = none yet (the dark room)
// Brightness actually used for rendering. In manual mode it equals
// settings.brightness (absolute). In auto-brightness mode settings.brightness is
// instead a RELATIVE trim around the sensor value (128 = neutral, 0 = much
// darker, 255 = much brighter) and effectiveBrightness is the result.
extern uint8_t effectiveBrightness;

void lightSensorBegin();   // at start-up: the BH1750, if there is one
// Resolve the brightness used for ALL rendering (clock, boot, AP info, banner)
// with one shared rule. Call once per frame in every path.
void updateBrightness();
// The brightness the panel runs at: the clock's, and while a GIF plays the
// higher of the GIFs' own lowest and a multiple of the clock's.
uint8_t panelBrightness();

// Brightness steps. Manual: 16 levels evenly spaced on the perceptual scale of
// the brightness fade (FADE_GAMMA), from the lowest brightness to full: the
// panel's floor (PANEL_BRIGHT_FLOOR, 13, about 5 %).
// Auto brightness: settings.brightness is a trim around the sensor value (128 =
// as measured), stepped by a factor of 2^(1/8) from half to twice the sensor.
const uint8_t BRIGHT_LEVELS    = 16;
const float   BRIGHT_PHASE_MIN = 0.2585f;  // perceptual position of brightness 13
const int8_t  BRIGHT_TRIM_MAX  = 8;
bool    brightnessIsTrim();          // settings.brightness is a trim around the sensor
uint8_t brightnessLevel(uint8_t b);  // the manual level nearest to a brightness
int8_t  brightnessTrim(uint8_t b);   // the trim nearest to a brightness
void    brightnessStep(int delta);   // by whole levels, or trims with auto brightness

// The perceptually linear fade (UP held in the profile "Classic clicks").
void fadeStart();
void fadeStep();

#if BOARD_WAVESHARE_WALL
// ---- The room climate (the wall clock's SHTC3) -------------------------------------
extern bool  climateOK;   // found at start-up
extern bool  roomValid;   // a reading came
extern float roomTemp;    // degrees Celsius, filtered (the sensor's own, the board's warmth not taken off)
extern float roomHum;     // percent relative humidity, filtered (the sensor's own)
void  climateBegin();     // at start-up: the SHTC3
void  climateUpdate();    // on every pass: a measurement started, or its reading taken
float roomTempNow();      // the room's temperature: the sensor's less the board's warmth
float roomHumNow();       // the room's humidity, worked out for the room's temperature
// The web app's Sensors section: the SHTC3's warmth. Says whether it is
// stored, or nothing changed.
bool  applySensorParams(const String &q);
#endif

#endif  // BRIGHTNESS_H
