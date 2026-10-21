// What the faces share that plan the time's change backwards from the minute,
// so the new time stands complete as the minute begins: the Muncher and the
// Aliens. They plan the change to the next minute once a minute, work out its
// start anew on every pass from the second's beginning - only right once a
// real second has begun: measured once at planning time, the Muncher's first
// change after a restart stood complete at :00.973 (twice on 2026-10-10) -
// start it at its moment even when that has passed, step it by the time since
// it started, and log when the new time stood ("the new time stands at
// :00.0xx" is on time).
#ifndef FACE_PLANNED_H
#define FACE_PLANNED_H

#include "face.h"

class PlannedFace : public Watchface {
 public:
  // Chosen: a fresh layout and the time at once on the next pass. Shown
  // again or turned, it catches up by itself: a turn is seen on its next
  // pass, a minute it missed is put right then, a scene that was running
  // goes on from where it ought to be by now.
  void enter(const FaceNow &, FaceEnter why) override {
    if (why == ENTER_CHOSEN) { _rotation = 0xFF; }
  }
  // Scenes for an input, now and then, and both previews.
  bool offers(FaceTrigger) const override { return true; }

 protected:
  // The minute after the one on show, and when it begins (millis()).
  static time_t nextMinute(const FaceNow &now) { return now.local - (uint8_t)clockSecond(now.local) + 60; }
  static unsigned long nextMinuteAt(const FaceNow &now) {
    return now.secondAt + (60UL - (uint8_t)clockSecond(now.local)) * 1000UL;
  }

  // Whether the panel turned since the layout was made (it is taken over).
  bool turned(const FaceNow &now) {
    if (now.turn == _rotation) { return false; }
    _rotation = now.turn;
    return true;
  }

  static void logStands(const char *face, const FaceNow &now) {
    Console.printf("%s: the new time stands at :%02d.%03lu\n", face, clockSecond(now.local), now.ms - now.secondAt);
  }

  uint8_t       _rotation     = 0xFF;   // rotation the layout was built for; 0xFF = build anew
  unsigned long _startAt      = 0;      // when the planned or running scene starts
  time_t        _plannedFor   = 0;      // the minute the planned change is for; 0 = none
  unsigned long _sceneEndedAt = 0;
  bool          _dirty        = false;  // the picture changed outside a pass: drawn on the next
};

#endif  // FACE_PLANNED_H
