// The classic watchface's settings (face_classic.h), on their own: they sit
// inside the clock's Settings (settings.h), which need them without the
// face's fonts and drawing.
#ifndef CLASSIC_SETTINGS_H
#define CLASSIC_SETTINGS_H

#include <Arduino.h>

// The classic face's settings. They sit inside the clock's Settings, in this
// order, where the stored blobs have them.
struct ClassicSettings {
  uint8_t flyInTime;                // how long every fly-in takes, in 10 ms (Settings rev 1: ms per pixel)
  uint8_t digitR, digitG, digitB;   // the time's digits
  uint8_t trailR, trailG, trailB;   // a digit while it flies in
  uint8_t dir[6];                   // the side each digit comes from: 0 top, 1 right, 2 bottom, 3 left
};

// The fly-in time's range: at most 850 ms, so every digit has landed before
// the next second's flights start.
const uint16_t FLY_IN_MIN_MS = 100, FLY_IN_MAX_MS = 850;

// The fly-in time in the 10 ms ClassicSettings::flyInTime keeps, from ms, within its range.
inline uint8_t flyInUnits(long ms) {
  return (uint8_t)constrain((ms + 5) / 10, (long)(FLY_IN_MIN_MS / 10), (long)(FLY_IN_MAX_MS / 10));
}

#endif  // CLASSIC_SETTINGS_H
