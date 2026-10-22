/* -------------------------------------------------------------------------
   input.h - the inputs: the buttons (each a small state machine that turns
   presses into events), the gesture sensor (a PAJ7620U2, optional), and the
   function each event maps to on the screen on top - the profile's rows
   (input_map.h), on the face the shortcuts set on the Inputs page. Also the
   press indicator, lit while a button is down, and the gesture test. The
   knock comes in from main.cpp (updateKnock()), after its gates.

   A module of its own since 2026-10-22 (code review of 2026-10-14, point 10).
   ------------------------------------------------------------------------- */
#ifndef INPUT_H
#define INPUT_H

#include <Arduino.h>
#include "board_hal.h"
#include "input_map.h"

// ---- Events and what they do -------------------------------------------------------
// The last time anyone handled an input; knocks count only after the inputs
// have been left alone this long (knockIsReal()).
const unsigned long INPUT_SETTLE_MS = 3000;
extern unsigned long lastInputAt;

InputFunction handleInput(InputEvent ev, uint8_t steps);   // runs what the event maps to here; returns it
bool closesBanner(InputEvent ev);   // the event closes a banner (not a knock, not a hand that comes near)
void faceAnimationStart();          // FN_FACE_ANIMATION: the face's own animation, on the face only
void updateInput();                 // once per loop: the buttons, the gestures, the press indicator
bool anyButtonDown();
#if BOARD_BUTTON_COUNT
void buttonsSpentAtBoot();          // a button held through the start counts for nothing until let go
#endif
void drawFeedbackIndicator();       // the press indicator in the corner, right before matrix.show()

// ---- Face shortcuts (the Inputs page) -------------------------------------------------
extern uint8_t faceKeys[FACE_EVENT_COUNT];   // what each face event does now, as FACE_ACTIONS codes
extern bool    faceKeysOwn;                  // false: the profile's own face rows apply
int8_t faceEventIndex(InputEvent ev);        // its position in FACE_EVENTS; -1 for none
bool faceEventFitted(InputEvent ev);         // its source is on this clock
bool faceActionOffered(uint8_t code, InputEvent ev);
void faceKeysOfProfile(uint8_t profile, uint8_t *codes);
#if BOARD_BUTTON_COUNT == 0
bool faceEventCountsForMenu(InputEvent ev);  // a fitted input that counts for "one of them opens the menu"
#else
#define faceEventCountsForMenu faceEventFitted
#endif
bool faceKeysReachMenu(const uint8_t *codes);   // some input found opens the menu with these
void loadFaceKeys();
bool saveFaceKeys();

// ---- The gesture sensor -----------------------------------------------------------------
extern bool gestureOK;                              // found at boot
const int   GESTURE_INT = GESTURE_INT_PIN;          // -1: no INT line
void gestureBegin();
bool gestureHeldAtBoot();                           // a hand held over it at boot, then taken away
const char *gestureRawName(uint8_t g);
extern bool gestureTuned;                           // the clock's two values set, not the library's
void gestureTune(bool on);
// The last gestures the sensor reported, newest first: its own direction, the
// event the panel made of it, whether it was taken, and the two registers.
struct GestureSeen { unsigned long at; uint8_t raw; uint8_t ev; bool taken; uint8_t r43, r44; };
const uint8_t GESTURE_SEEN_MAX = 8;
extern GestureSeen gestureSeen[GESTURE_SEEN_MAX];
extern uint8_t     gestureSeenCount;
// The gesture test (SCREEN_GESTURE_TEST): every gesture shown instead of
// done; any button ends it, and so does GESTURE_TEST_MS without a gesture.
const unsigned long GESTURE_TEST_MS = 3UL * 60UL * 1000UL;
extern uint16_t      gestureTestCount;    // gestures since the test began
extern unsigned long gestureTestAt;       // millis() of the start or of the last gesture
extern int16_t       gestureNearBright;   // the object in view: its brightness and size; -1 = nothing in view
extern int16_t       gestureNearSize;
extern uint8_t       gestureWaves;        // the sensor's wave count (0xB7) as last read in the test
bool gestureTestStart();
void gestureTestStop();
void gestureTestUpdate();
void drawGestureTest();

#endif  // INPUT_H
