// The inputs: buttons, the gesture sensor, the face shortcuts, and the
// feedback while a button is down (input.h).
#include "input.h"

#include <RevEng_PAJ7620.h>
#include "brightness.h"
#include "clock_state.h"
#include "console.h"
#include "face.h"
#include "gif_player.h"
#include "menu.h"
#include "notices.h"
#include "sensors.h"
#include "settings.h"

// What only this module calls, ahead of its use.
struct Button;
static InputContext contextOf(Screen s);
static InputContext inputContext();
static const InputProfile &activeProfile();
static InputFunction mappedFunction(InputEvent ev, InputContext ctx);
static bool clicksCounted(const Button &b);
static uint8_t clickCount(InputEvent ev);
static void runFunction(InputFunction fn, uint8_t steps);
static void holdStep(InputFunction fn);
static void holdEnd(InputFunction fn);
static InputEvent closeSequence(Button &b);
static InputEvent stepButton(Button &b);
static uint8_t burstSteps(const Button &b);
static InputEvent gestureEvent(Gesture g);
static const char *gestureLeftOut(uint8_t ev);
static void updateGestures();
static void updateApproach();
static const char *gestureWord(uint8_t ev);
static void gestureRemember(uint8_t raw, uint8_t ev, bool taken, uint8_t r43, uint8_t r44);
static bool gestureReadRegs(uint8_t reg, uint8_t *values, uint8_t count);
static bool gestureWriteReg(uint8_t reg, uint8_t value);
static bool gestureReadRaw(uint8_t &r43, uint8_t &r44);
static Gesture gestureDecode(uint8_t r43, uint8_t r44);
static void feedbackConfirm(uint8_t clicks);
static bool feedbackLit();
static void updateFeedbackLed();

// Face shortcuts: what each face event does (input_map.h: FACE_EVENTS), as
// FACE_ACTIONS codes. A blob of its own (NVS key "keys"); nothing stored means
// the profile's own face rows.
#define FACE_KEYS_MAGIC 0xFA5C
#define FACE_KEYS_REV   1
// (FACE_KEYS_MAX, room for face events added later: input_map.h.)
struct FaceKeys {
  uint16_t magic;
  uint8_t  rev;
  uint8_t  count;                          // events stored (FACE_EVENT_COUNT when written)
  uint8_t  action[FACE_KEYS_MAX];          // a FACE_ACTIONS code per event, in FACE_EVENTS order
};
static_assert(FACE_EVENT_COUNT <= FACE_KEYS_MAX, "FaceKeys has no room for every face event");
uint8_t faceKeys[FACE_EVENT_COUNT];        // what applies now
bool    faceKeysOwn = false;               // false: the profile's own face rows apply
static SettingsStore<FaceKeys> keysStore;

// Buttons ------------------------------------------------------------------
// UP_BUTTON_PIN / DOWN_BUTTON_PIN come from board_hal.h (the MatrixPortal S3:
// GPIO6/GPIO7; the wall clock has none). Active LOW with the internal pull-up. Every button runs the same small state
// machine, which only turns presses into events.
const unsigned long BTN_DEBOUNCE_MS  = 25;   // ignore bounces shorter than this
const unsigned long BTN_LONGPRESS_MS = 600;  // pressed this long -> the hold event, then a repeat every 600 ms
const unsigned long BTN_MULTI_GAP_MS = 400;  // window to collect a click sequence, and the gap of a burst
// Tap burst: short presses less than BTN_MULTI_GAP_MS apart. From the fourth
// press of a burst on, each moves five steps instead of one, which makes the
// long ranges (16 brightness levels, 26 zones) quick to cross.
const uint8_t BURST_FAST_FROM  = 4;
const uint8_t BURST_FAST_STEPS = 5;

// The last time anyone handled an input - a button going down or up. Whoever
// operates the clock touches it, and the sensor feels that as knocks: the
// press, the release, steadying the clock, letting go after a fade. So knocks
// are ignored until the inputs have been left alone for INPUT_SETTLE_MS (see
// knockIsReal()). Gesture input will set this as well.
// (INPUT_SETTLE_MS: input.h.)
unsigned long lastInputAt = 0;

enum ButtonPhase : uint8_t {
  BTN_RELEASED,    // up, no click sequence open
  BTN_PRESSED,     // down, not yet long enough for the hold event
  BTN_HELD,        // down past BTN_LONGPRESS_MS; the hold event has fired
  BTN_COUNTING,    // up after a click; another may follow within BTN_MULTI_GAP_MS
  BTN_SPENT,       // down, but this press has done its job (it was held through
                   // the start); nothing more until it is let go
};

struct Button {
  uint8_t       pin;
  InputEvent    evShort, ev2x, ev3x, evHold, evRepeat;   // what this button reports
  ButtonPhase   phase;
  uint8_t       clicks;       // clicks in the open sequence
  uint8_t       burst;        // short presses in the running tap burst
  unsigned long pressedAt;    // millis() the current press began
  unsigned long releasedAt;   // millis() of the last counted click
  unsigned long heldAt;       // millis() of the last hold or repeat event
  InputFunction holding;      // what its hold event started, running until it is let go
};

#if BOARD_BUTTON_COUNT
static Button buttons[] = {
  { UP_BUTTON_PIN,   EV_UP_SHORT,   EV_UP_2X,   EV_UP_3X,   EV_UP_HOLD,   EV_UP_HOLD_REPEAT,
    BTN_RELEASED, 0, 0, 0, 0, 0, FN_NONE },
  { DOWN_BUTTON_PIN, EV_DOWN_SHORT, EV_DOWN_2X, EV_DOWN_3X, EV_DOWN_HOLD, EV_DOWN_HOLD_REPEAT,
    BTN_RELEASED, 0, 0, 0, 0, 0, FN_NONE },
};
#endif

// Gesture sensor -----------------------------------------------------------
// A PAJ7620U2 on the I2C bus (0x73), optional, found at boot or not at all. It
// recognises the gestures itself and holds them until they are read; reading
// clears them. The clock reads its two result registers itself (the library
// only sets it up); the wave it reports is not used, since in the tests of
// 2026-10-10 it came for hardly any wave, and neither are the circles
// (gestureLeftOut()). With its INT line wired
// (GESTURE_INT_PIN, board_hal.h) the sensor is read only when the line is low;
// without, it is asked every GESTURE_POLL_MS, and whether a hand is near (for
// the hints) every APPROACH_POLL_MS while the screen on top maps that.
static RevEng_PAJ7620 gestureSensor;
bool          gestureOK = false;
// (GESTURE_INT, -1 without an INT line: input.h.)
const unsigned long GESTURE_POLL_MS     = 50;    // gestures take some 200 ms, and the sensor holds them
// Gestures left out, measured on 2026-10-10: the opposite of a swipe came
// 250 to 305 ms after it as the hand went back, and a circle now and then
// came as swipes between its turns.
const unsigned long GESTURE_LOCKOUT_MS  = 300;   // after a gesture: any other one
const unsigned long GESTURE_RETURN_MS   = 450;   // after a swipe: the opposite one, the hand going back
const unsigned long GESTURE_CIRCLING_MS = 1000;  // after a circle: a swipe, part of the circling
static unsigned long gestureCircleAt = 0;        // millis() of the last circle the sensor reported
static uint8_t gestureLastEv   = 0;              // the last gesture taken (InputEvent)
const unsigned long APPROACH_POLL_MS   = 100;
const unsigned long APPROACH_MS        = 400;   // near this long: a hand that stays, not one passing by
const unsigned long APPROACH_REARM_MS  = 1000;  // gone this long before the next approach counts
const unsigned long APPROACH_BOOT_MS   = 3000;  // held over the sensor this long at boot: the recovery
static unsigned long gesturePolledAt  = 0;
static unsigned long gestureAt        = 0;   // millis() of the last gesture that counted
// The last gestures the sensor reported, newest first, for the gesture test
// and the web app: the sensor's own direction, the event the panel made of it,
// whether it was taken or left out (gestureLeftOut()), and the two registers.
// (GestureSeen and GESTURE_SEEN_MAX: input.h.)
GestureSeen gestureSeen[GESTURE_SEEN_MAX];
uint8_t  gestureSeenCount = 0;
// The gesture test: the panel shows every gesture instead of doing it, and
// what the sensor sees in front of it; any button ends it, and so does
// GESTURE_TEST_MS without a gesture.
// (GESTURE_TEST_MS: input.h.)
uint16_t gestureTestCount = 0;          // gestures since the test began
unsigned long gestureTestAt = 0;        // millis() of the start or of the last gesture
static unsigned long gestureNearAt = 0; // when the sensor was last asked what it sees
int16_t  gestureNearBright = -1;        // the object in view: its brightness and size; -1 = nothing in view
int16_t  gestureNearSize   = -1;
uint8_t  gestureWaves = 0;              // the sensor's wave count (0xB7) as last read in the test
static unsigned long approachPolledAt = 0;
static unsigned long objectNearSince  = 0;   // 0 = nothing near
static unsigned long objectGoneSince  = 0;
static bool          approachReported = false;

// Button feedback ----------------------------------------------------------
// The red board LED (FEEDBACK_LED_PIN) - and, if enabled, a small square in the
// bottom-right corner of the matrix - is lit while a button is down. With the
// Classic clicks profile it then blinks once per click after a click sequence
// has triggered its function, so 1x / 2x / 3x can be told apart; a sequence
// that triggers nothing gets no confirmation. Everywhere else the screen shows
// what happened.
#define BUTTON_FEEDBACK_ON_MATRIX 1          // 0 = board LED only
// Dark pause before the first confirmation blink, on top of BTN_MULTI_GAP_MS
// (the function itself still triggers after the gap). Separates the blinks
// clearly from the last button press so they are easy to count.
const unsigned long FB_LEAD_IN_MS   = 250;
const unsigned long FB_BLINK_ON_MS  = 150;
const unsigned long FB_BLINK_OFF_MS = 250;
static uint8_t       fbBlinkCount = 0;       // confirmation blinks of the running sequence
static unsigned long fbBlinkStart = 0;       // millis() when that sequence started
static bool          fbLit = false;          // feedback state of this loop (LED and matrix)

/* ======================================================================
   Input: buttons -> events -> the function the profile maps them to on the
   current screen. What each button does lives in the profile tables
   (input_map.h), not here.
   ====================================================================== */

// The context of a screen.
static InputContext contextOf(Screen s) {
  switch (s) {
    case SCREEN_BOOT:            return CTX_BOOT;
    case SCREEN_MENU:
    case SCREEN_EDITOR:
    case SCREEN_GESTURE_TEST:    return CTX_MENU;   // the test: no 2x or 3x, so a press ends it at once
    case SCREEN_HOTSPOT_INFO:
    case SCREEN_HOTSPOT_PREVIEW: return CTX_HOTSPOT;
    default:                     return CTX_FACE;
  }
}

// The context an event happens in. A banner has none of its own: it confirms
// the screen beneath it.
static InputContext inputContext() {
  return contextOf(screen == SCREEN_BANNER ? screenBeneath() : screen);
}

// The input profile chosen on the config page (checked when it is loaded).
/* ----------------------------------------------------------------------
   Face shortcuts (concept doc: "Face shortcuts")

   On the face every event of a fitted source does what the Inputs page gave
   it (faceKeys); the menu, the hotspot screen and the start keep the
   profile's rows. Stored shortcuts that would leave no input found able to
   open the menu - a gesture sensor that has gone, say - give way to the
   profile's own face rows, so the menu can never be locked away.
   ---------------------------------------------------------------------- */

// The position of an event in FACE_EVENTS; -1 for one that is no face event.
int8_t faceEventIndex(InputEvent ev) {
  for (uint8_t i = 0; i < FACE_EVENT_COUNT; i++) {
    if (FACE_EVENTS[i].event == ev) { return (int8_t)i; }
  }
  return -1;
}

// Whether the source of a face event was found: the buttons always, the knock
// with the accelerometer, the gestures with the gesture sensor. (The wall clock
// has no buttons.)
bool faceEventFitted(InputEvent ev) {
  // The knock comes from knock.h.
  if (ev == EV_KNOCK) { return knockOK; }
  if (ev == EV_WAVE) { return false; }   // not read: too unreliable (updateGestures())
  if (ev == EV_CIRCLE_CW || ev == EV_CIRCLE_CCW) { return false; }   // left out (gestureLeftOut())
  if (isGestureEvent(ev)) { return gestureOK; }
#if BOARD_BUTTON_COUNT
  return true;
#else
  return false;   // what is left are the buttons' events, and this board has none
#endif
}

// Whether an action can sit on a face event of this clock.
bool faceActionOffered(uint8_t code, InputEvent ev) {
  if (code >= FACE_ACTION_COUNT) { return false; }
  switch (FACE_ACTIONS[code].needs) {
    case NEEDS_HOLD:     return ev == EV_UP_HOLD || ev == EV_DOWN_HOLD;
    default:             return true;   // GIFs and messages: every board has them
  }
}

// A profile's own face rows, as shortcuts. A row whose action this clock does
// not offer does nothing here: without messages, UP twice would only make
// UP's single press wait for a second one.
void faceKeysOfProfile(uint8_t profile, uint8_t *codes) {
  const InputProfile &p = INPUT_PROFILES[profile];
  for (uint8_t i = 0; i < FACE_EVENT_COUNT; i++) {
    InputEvent ev = FACE_EVENTS[i].event;
    const InputMapping *rows = isGestureEvent(ev) ? p.gestures : p.rows;
    uint8_t count = isGestureEvent(ev) ? p.gestureCount : p.count;
    codes[i] = 0;
    for (uint8_t r = 0; r < count; r++) {
      if (rows[r].event != ev || rows[r].context != CTX_FACE) { continue; }
      for (uint8_t a = 0; a < FACE_ACTION_COUNT; a++) {
        if (FACE_ACTIONS[a].function == rows[r].function && faceActionOffered(a, ev)) { codes[i] = a; }
      }
    }
  }
}

// Whether some input found opens the menu with these shortcuts.
#if BOARD_BUTTON_COUNT == 0
// Whether a fitted input counts for the rule that one of them opens the menu.
// On a clock without buttons the knock does not: it cannot work a menu, and
// as the only input found it would refuse every shortcut stored and saved.
bool faceEventCountsForMenu(InputEvent ev) {
  if (ev == EV_KNOCK) { return false; }
  return faceEventFitted(ev);
}
#endif

bool faceKeysReachMenu(const uint8_t *codes) {
  for (uint8_t i = 0; i < FACE_EVENT_COUNT; i++) {
    if (faceEventCountsForMenu(FACE_EVENTS[i].event) && FACE_ACTIONS[codes[i]].function == FN_OPEN_MENU) { return true; }
  }
#if BOARD_BUTTON_COUNT == 0
  // A clock without buttons whose gesture sensor was not found has no input
  // at all: no shortcut can open its menu, and the web app is its way to the
  // settings. Its shortcuts are kept for when the sensor is there.
  for (uint8_t i = 0; i < FACE_EVENT_COUNT; i++) {
    if (faceEventCountsForMenu(FACE_EVENTS[i].event)) { return false; }
  }
  return true;
#else
  return false;
#endif
}

// At start-up, once the sensors have been looked for.
void loadFaceKeys() {
  keysStore.begin("keys");
  FaceKeys stored;
  keysStore.read(stored);
  faceKeysOfProfile(uiSettings.inputProfile, faceKeys);
  faceKeysOwn = false;
  if (stored.magic == FACE_KEYS_MAGIC && stored.rev == FACE_KEYS_REV) {
    uint8_t codes[FACE_EVENT_COUNT];
    memcpy(codes, faceKeys, sizeof(codes));   // events stored by an older firmware keep the profile's
    for (uint8_t i = 0; i < FACE_EVENT_COUNT && i < stored.count; i++) {
      codes[i] = faceActionOffered(stored.action[i], FACE_EVENTS[i].event) ? stored.action[i] : 0;
    }
    if (faceKeysReachMenu(codes)) {
      memcpy(faceKeys, codes, sizeof(codes));
      faceKeysOwn = true;
    } else {
      Console.println("Face shortcuts: with the inputs found none opens the menu - the profile's apply");
    }
  }
  Console.print("Face shortcuts: ");
  Console.println(faceKeysOwn ? "set on the Inputs page" : "the profile's");
}

bool saveFaceKeys() {
  FaceKeys k;
  memset(&k, 0, sizeof(k));
  k.magic = FACE_KEYS_MAGIC;
  k.rev   = FACE_KEYS_REV;
  k.count = FACE_EVENT_COUNT;
  memcpy(k.action, faceKeys, FACE_EVENT_COUNT);
  return keysStore.write(k);
}

static const InputProfile &activeProfile() {
  return INPUT_PROFILES[uiSettings.inputProfile];
}

// What this event does in this context: on the face the shortcuts, elsewhere
// the active profile's rows; FN_NONE if nothing.
static InputFunction mappedFunction(InputEvent ev, InputContext ctx) {
  if (ctx == CTX_FACE) {
    int8_t i = faceEventIndex(ev);
    return i < 0 ? FN_NONE : FACE_ACTIONS[faceKeys[i]].function;
  }
  const InputProfile &profile = activeProfile();
  const InputMapping *rows = isGestureEvent(ev) ? profile.gestures : profile.rows;
  uint8_t count = isGestureEvent(ev) ? profile.gestureCount : profile.count;
  for (uint8_t i = 0; i < count; i++) {
    if (rows[i].event == ev && rows[i].context == ctx) { return rows[i].function; }
  }
  return FN_NONE;
}

// Whether a short press of this button has to wait BTN_MULTI_GAP_MS for a
// second one: only where the mapping gives its 2x or 3x something to do.
static bool clicksCounted(const Button &b) {
  InputContext ctx = inputContext();
  return mappedFunction(b.ev2x, ctx) != FN_NONE || mappedFunction(b.ev3x, ctx) != FN_NONE;
}

// How many clicks an event stands for, for the confirmation blinks; 0 = not a click.
static uint8_t clickCount(InputEvent ev) {
  switch (ev) {
    case EV_UP_SHORT: case EV_DOWN_SHORT: return 1;
    case EV_UP_2X:    case EV_DOWN_2X:    return 2;
    case EV_UP_3X:    case EV_DOWN_3X:    return 3;
    default:                              return 0;
  }
}

// Whether an event closes a banner. A banner closes on the next thing the user
// does, but not on a knock or a hand coming near, which are not operating the
// clock, and not on the repeats of a hold that was already going when the
// banner came up: holding DOWN to save a zone would otherwise close its banner
// at once.
bool closesBanner(InputEvent ev) {
  switch (ev) {
    case EV_NONE:
    case EV_KNOCK:
    case EV_APPROACH:
    case EV_UP_HOLD_REPEAT:
    case EV_DOWN_HOLD_REPEAT: return false;
    default:                  return true;
  }
}

// Run a function once. `steps` is how far PREV / NEXT move in an editor: 1, or
// more from a tap burst. A hold function (the fade) also runs on while
// its button stays down: holdStep() and holdEnd() below.
static void runFunction(InputFunction fn, uint8_t steps) {
  switch (fn) {
    case FN_PREV:
    case FN_NEXT:
    case FN_ENTER:
    case FN_BACK:
    case FN_HOME:           navigate(fn, steps); break;
    case FN_OPEN_MENU:      menuOpen(); break;
    case FN_TOGGLE_AUTO:    toggleAutoBrightWithBanner(); break;
    case FN_CYCLE_DST:      cycleDstMode(); break;
    case FN_TOGGLE_HOTSPOT: if (apActive) { stopAPMode(); } else { startAPMode(); } break;
    case FN_BRIGHT_FADE:    fadeStart(); break;
    case FN_FACE_ANIMATION: faceAnimationStart(); break;
    case FN_PLAY_GIF:       gifPlayNow(); break;
    case FN_GIF_AGAIN:      gifPlayAgain(); break;
    case FN_SHOW_HINTS:     showHints(); break;
    case FN_SHOW_MESSAGES:  noticesShowAgain(); break;
    default:                break;
  }
}

// FN_FACE_ANIMATION, on the face only: the watchface's own animation, each
// face deciding whether it plays now (Watchface::request()): the classic
// digits all fly in at the next second, the Tetris digits break up, the
// Muncher and the Aliens play the scene their setting picks.
void faceAnimationStart() {
  if (screen != SCREEN_FACE) { return; }
  faceFor(settings.watchface).request(faceMoment(), TRIGGER_ACTION);
}

static void holdStep(InputFunction fn) {
  if (fn == FN_BRIGHT_FADE) { fadeStep(); }
}

static void holdEnd(InputFunction fn) {
  if (fn == FN_BRIGHT_FADE) {   // keep the faded brightness
    saveSettings();
    Console.print("Brightness set to "); Console.println(settings.brightness);
  }
}

// Look an event up for the current screen and run what it maps to. Returns the
// function, so a hold event's button can keep it running.
//
// A banner takes the next input for itself: it closes, and the input is used
// up, since it was meant for a screen the banner hid. With the Classic clicks
// profile a click sequence that ran something is confirmed by blinks.
InputFunction handleInput(InputEvent ev, uint8_t steps) {
  if (screen == SCREEN_GESTURE_TEST) {   // the gestures go to the test itself; a button ends it
    if (closesBanner(ev)) { gestureTestStop(); }
    return FN_NONE;
  }
  if (screen == SCREEN_BANNER && noticeStepCount) {
    // A notice on show: the events set on the Inputs page go on to the next
    // one (or back to the face after the last); cards that came of their own
    // accord all go. The input is used up.
    if (noticeClosedBy(ev)) {
      if (noticeCardsRound) { closeScreen(); } else { noticeAdvance(true); }
    }
    return FN_NONE;
  }
  if (screen == SCREEN_BANNER && bannerKind == BANNER_HINT) {
    // A hint only says what comes next: it goes, and the input goes on to the
    // screen beneath.
    if (closesBanner(ev)) { closeScreen(); }
  } else if (screen == SCREEN_BANNER || screen == SCREEN_GIF) {
    // A GIF takes it the same way: the hold that started one does not end it.
    if (closesBanner(ev)) { closeScreen(); }
    return FN_NONE;
  }
  InputFunction fn = mappedFunction(ev, inputContext());
  if (fn == FN_NONE) { return FN_NONE; }
  runFunction(fn, steps);
  uint8_t clicks = clickCount(ev);
  if (clicks && activeProfile().blinkClicks) { feedbackConfirm(clicks); }
  return fn;
}

// End a click sequence and name it: 1x, 2x, or 3x for three and more, as it
// has always been.
static InputEvent closeSequence(Button &b) {
  uint8_t n = b.clicks;
  b.clicks = 0;
  b.phase  = BTN_RELEASED;
  if (n >= 3) { return b.ev3x; }
  if (n == 2) { return b.ev2x; }
  return b.evShort;
}

// Advance one button by one pass and return the event it produced, if any.
// Where its 2x and 3x map to nothing, a button reports a click at once instead
// of waiting BTN_MULTI_GAP_MS for a second one. A hold repeats every
// BTN_LONGPRESS_MS while the button stays down.
static InputEvent stepButton(Button &b) {
  bool down = (digitalRead(b.pin) == LOW);
  unsigned long t = millisNow;
  bool wasDown = (b.phase == BTN_PRESSED || b.phase == BTN_HELD || b.phase == BTN_SPENT);
  if (down != wasDown) { lastInputAt = t; }   // every press and every release is handling
  switch (b.phase) {
    case BTN_SPENT:
      if (!down) { b.phase = BTN_RELEASED; }
      return EV_NONE;
    case BTN_RELEASED:
    case BTN_COUNTING:
      if (down) { b.phase = BTN_PRESSED; b.pressedAt = t; return EV_NONE; }
      if (b.phase == BTN_COUNTING && t - b.releasedAt >= BTN_MULTI_GAP_MS) { return closeSequence(b); }
      return EV_NONE;
    case BTN_PRESSED:
      if (down) {
        if (t - b.pressedAt < BTN_LONGPRESS_MS) { return EV_NONE; }
        b.phase  = BTN_HELD;
        b.clicks = 0;           // a hold ends any open click sequence
        b.burst  = 0;           // and any tap burst
        b.heldAt = t;
        return b.evHold;
      }
      if (t - b.pressedAt < BTN_DEBOUNCE_MS) {   // a bounce, not a click
        b.phase = b.clicks ? BTN_COUNTING : BTN_RELEASED;
        return EV_NONE;
      }
      // A tap burst goes on when this press began soon after the last click.
      if (b.burst > 0 && b.pressedAt - b.releasedAt < BTN_MULTI_GAP_MS) {
        if (b.burst < 255) { b.burst++; }
      } else {
        b.burst = 1;
      }
      b.clicks++;
      b.releasedAt = t;
      if (!clicksCounted(b)) { return closeSequence(b); }
      b.phase = BTN_COUNTING;
      return EV_NONE;
    case BTN_HELD:
      if (!down) { b.phase = BTN_RELEASED; return EV_NONE; }
      if (t - b.heldAt < BTN_LONGPRESS_MS) { return EV_NONE; }
      b.heldAt = t;
      return b.evRepeat;
  }
  return EV_NONE;
}

// How far a short press of this button moves: from the BURST_FAST_FROM-th
// press of a tap burst on, BURST_FAST_STEPS.
static uint8_t burstSteps(const Button &b) {
  return (b.burst >= BURST_FAST_FROM) ? BURST_FAST_STEPS : 1;
}

// Whether any button is down right now.
bool anyButtonDown() {
#if BOARD_BUTTON_COUNT
  for (const Button &b : buttons) {
    if (b.phase == BTN_PRESSED || b.phase == BTN_HELD || b.phase == BTN_SPENT) { return true; }
  }
#endif
  return false;
}

#if BOARD_BUTTON_COUNT
// At boot: a button held through the start has done its job; it counts for
// nothing more until it is let go (BTN_SPENT).
void buttonsSpentAtBoot() {
  for (Button &b : buttons) {
    if (digitalRead(b.pin) == LOW) { b.phase = BTN_SPENT; }
  }
}
#endif

// Once per loop: read every button, run what its events map to, keep a hold
// function going while its button stays down, and drive the press indicator.
void updateInput() {
#if BOARD_BUTTON_COUNT
  for (Button &b : buttons) {
    ButtonPhase before = b.phase;
    InputEvent ev = stepButton(b);
    if (b.phase == BTN_PRESSED && before != BTN_PRESSED) { fbBlinkCount = 0; }   // a new press cancels a pending confirmation
    if (ev == b.evHold) { b.holding = handleInput(ev, 1); }
    else if (ev != EV_NONE) { handleInput(ev, (ev == b.evShort) ? burstSteps(b) : 1); }
    if (b.phase == BTN_HELD) { holdStep(b.holding); }
    if (before == BTN_HELD && b.phase != BTN_HELD) { holdEnd(b.holding); b.holding = FN_NONE; }
  }
#endif
  updateGestures();
  updateFeedbackLed();
}

/* ---- Gesture sensor ------------------------------------------------------ */

// At boot: whether a PAJ7620U2 answers. The library sets it up as PixArt's
// datasheet v0.8 does; two values are then set as in the datasheet v1.5
// (gestureTune()): with them circles came as circles in the tests of
// 2026-10-10 (9 of 9), with the library's often as swipes (6 of 14). So a
// circling hand is left out, instead of moving through the menu.
void gestureBegin() {
  sensorTries[SENSOR_GESTURE] = sensorBegin(SENSOR_GESTURE, []() { return gestureSensor.begin(&Wire) != 0; });
  gestureOK = sensorTries[SENSOR_GESTURE] != 0;
  if (!gestureOK) { Console.println("PAJ7620U2 not found - no gestures"); return; }
  gestureTune(true);
  if (GESTURE_INT >= 0) { pinMode((uint8_t)GESTURE_INT, INPUT_PULLUP); }
  Console.print("PAJ7620U2 found, read ");
  Console.println(GESTURE_INT >= 0 ? "on its INT line" : "by asking it every 50 ms");
}

// A gesture of the sensor as an event. A swipe's direction is turned into the
// screen's: the direction the sensor reports, plus the quarter turns it is
// mounted at against the panel (config page), minus the rotation the panel is
// drawn in now. Directions count clockwise from up: 0 up, 1 right, 2 down,
// 3 left. Push, pull, the circles and the wave have no direction on the panel.
static InputEvent gestureEvent(Gesture g) {
  uint8_t dir;
  switch (g) {
    case GES_UP:            dir = 0; break;
    case GES_RIGHT:         dir = 1; break;
    case GES_DOWN:          dir = 2; break;
    case GES_LEFT:          dir = 3; break;
    case GES_FORWARD:       return EV_PUSH;
    case GES_BACKWARD:      return EV_PULL;
    case GES_CLOCKWISE:     return EV_CIRCLE_CW;
    case GES_ANTICLOCKWISE: return EV_CIRCLE_CCW;
    case GES_WAVE:          return EV_WAVE;
    default:                return EV_NONE;
  }
  static const InputEvent SWIPES[4] = { EV_SWIPE_UP, EV_SWIPE_RIGHT, EV_SWIPE_DOWN, EV_SWIPE_LEFT };
  return SWIPES[(dir + uiSettings.gestureMount + 4 - screenRotation) % 4];
}

// Why a gesture is left out, or nullptr when it is taken. A swipe goes out at
// once; only what comes after a gesture is held against it. Circles are not
// used: in the editors of the tests of 2026-10-10 they came in the wrong
// direction now and then, and a hand is hard to keep at the distance and the
// size of circle the sensor wants. The sensor still reports them, and the
// swipes of a circling hand go out with them.
static const char *gestureLeftOut(uint8_t ev) {
  if (ev == EV_CIRCLE_CW || ev == EV_CIRCLE_CCW) { return "circles are not used"; }
  bool swipe = ev >= EV_SWIPE_UP && ev <= EV_SWIPE_RIGHT;
  if (gestureAt && millisNow - gestureAt < GESTURE_LOCKOUT_MS) { return "too soon after the last"; }
  if (swipe && gestureAt && millisNow - gestureAt < GESTURE_RETURN_MS) {
    // EV_SWIPE_UP, _DOWN, _LEFT, _RIGHT: the opposite is the neighbour in each pair.
    uint8_t opposite = (uint8_t)(((ev - EV_SWIPE_UP) ^ 1) + EV_SWIPE_UP);
    if (gestureLastEv == opposite) { return "the hand going back"; }
  }
  if (swipe && gestureCircleAt && millisNow - gestureCircleAt < GESTURE_CIRCLING_MS) { return "part of the circling"; }
  return nullptr;
}

// Once per loop: a gesture the sensor holds, as an event, unless it is left
// out (gestureLeftOut()), one step each. Every gesture is handling the clock
// (lastInputAt), as every button press is.
static void updateGestures() {
  if (!gestureOK) { return; }
  if (GESTURE_INT >= 0) {
    if (digitalRead((uint8_t)GESTURE_INT) == HIGH) { return; }   // nothing waiting
  } else {
    updateApproach();
    if (millisNow - gesturePolledAt < GESTURE_POLL_MS) { return; }
    gesturePolledAt = millisNow;
  }
  uint8_t r43 = 0, r44 = 0;
  bool got = gestureReadRaw(r43, r44);   // reading clears them, and the INT line
  if (screen == SCREEN_GESTURE_TEST) {
    // The test logs the two registers as read, with the debug registers 0xB6
    // (the sensor's own result, not kept) and 0xB7 (the wave count, bits 0-3;
    // the abort count, bits 4-6), whenever any of them shows something or
    // changes.
    static unsigned long rawAt = 0;
    static uint8_t b6Last = 0, b7Last = 0;
    uint8_t dbg[2] = { 0, 0 };
    gestureReadRegs(0xB6, dbg, 2);
    if (r43 || r44 || dbg[0] != b6Last || dbg[1] != b7Last) {
      Console.printf("Gesture raw: 0x43=%02X 0x44=%02X 0xB6=%u waves %u aborts %u, %lu ms after the last\n", r43, r44,
                    dbg[0], dbg[1] & 0x0F, (dbg[1] >> 4) & 0x07, millisNow - rawAt);
      rawAt = millisNow;
      if ((dbg[1] & 0x0F) != (b7Last & 0x0F)) { panelDirty = true; }
      b6Last = dbg[0];
      b7Last = dbg[1];
      gestureWaves = dbg[1] & 0x0F;
    }
  }
  if (!got || r43 == 0) { return; }
  Gesture g = gestureDecode(r43, r44);
  InputEvent ev = gestureEvent(g);
  if (ev == EV_NONE) { return; }
  if (ev == EV_CIRCLE_CW || ev == EV_CIRCLE_CCW) { gestureCircleAt = millisNow; }   // taken or not, the hand circles
  // Each one on the console, as it was taken or left out: what a hand did,
  // and what the sensor saw without one; with the sensor's own direction and
  // the two turns that make the panel's of it.
  int8_t name = faceEventIndex(ev);
  const char *label = name >= 0 ? FACE_EVENTS[name].label : "?";
  const char *why = gestureLeftOut(ev);
  gestureRemember(g, ev, why == nullptr, r43, r44);
  // With the time since the last gesture taken, and the register as read when
  // it held more than one bit (the decoding then chose by priority).
  char after[48] = "";
  int n = 0;
  if (gestureAt) { n = snprintf(after, sizeof(after), ", %lu ms after the last", millisNow - gestureAt); }
  if (r43 & (r43 - 1)) { snprintf(after + n, sizeof(after) - n, ", 0x43=%02X", r43); }
  if (why) {
    Console.printf("Gesture: %s - left out, %s%s (sensor %s, turned %u, panel %u)\n", label, why, after,
                  gestureRawName(g), uiSettings.gestureMount, screenRotation);
    if (screen == SCREEN_GESTURE_TEST) { gestureTestCount++; panelDirty = true; }
    return;
  }
  Console.printf("Gesture: %s%s (sensor %s, turned %u, panel %u)\n", label, after, gestureRawName(g),
                uiSettings.gestureMount, screenRotation);
  gestureLastEv = (uint8_t)ev;
  gestureAt   = millisNow;
  lastInputAt = millisNow;
  objectNearSince = 0;        // a gesture is no approach
  approachReported = true;    // nor is the hand that made it, until it has gone
  if (screen == SCREEN_GESTURE_TEST) {   // shown, not done
    gestureTestCount++;
    gestureTestAt = millisNow;
    panelDirty = true;
    return;
  }
  handleInput(ev, 1);
}

// Without the INT line: whether a hand has come near and stays, asked only
// while the screen on top maps the approach (the hints on the face). The INT
// line reports gestures only, so with it there are no hints.
static void updateApproach() {
  if (mappedFunction(EV_APPROACH, inputContext()) == FN_NONE) { objectNearSince = 0; return; }
  if (millisNow - approachPolledAt < APPROACH_POLL_MS) { return; }
  approachPolledAt = millisNow;
  if (gestureSensor.isObjectInView()) {
    objectGoneSince = 0;
    if (!objectNearSince) { objectNearSince = millisNow; }
    if (!approachReported && millisNow - objectNearSince >= APPROACH_MS) {
      approachReported = true;
      Console.printf("Gesture sensor: a hand near for %lu ms\n", millisNow - objectNearSince);
      handleInput(EV_APPROACH, 1);
    }
  } else {
    objectNearSince = 0;
    if (!objectGoneSince) { objectGoneSince = millisNow; }
    if (millisNow - objectGoneSince >= APPROACH_REARM_MS) { approachReported = false; }
  }
}

// The sensor's own name of a gesture, before any turn: up, down, left and
// right as the sensor sees them.
const char *gestureRawName(uint8_t g) {
  switch (g) {
    case GES_UP:            return "up";
    case GES_DOWN:          return "down";
    case GES_LEFT:          return "left";
    case GES_RIGHT:         return "right";
    case GES_FORWARD:       return "push";
    case GES_BACKWARD:      return "pull";
    case GES_CLOCKWISE:     return "cw";
    case GES_ANTICLOCKWISE: return "ccw";
    case GES_WAVE:          return "wave";
    default:                return "?";
  }
}

// What the gesture test shows for an event: its direction on the panel, or
// what it is.
static const char *gestureWord(uint8_t ev) {
  switch (ev) {
    case EV_SWIPE_UP:    return "UP";
    case EV_SWIPE_DOWN:  return "DOWN";
    case EV_SWIPE_LEFT:  return "LEFT";
    case EV_SWIPE_RIGHT: return "RIGHT";
    case EV_PUSH:        return "PUSH";
    case EV_PULL:        return "PULL";
    case EV_CIRCLE_CW:   return "CW";
    case EV_CIRCLE_CCW:  return "CCW";
    case EV_WAVE:        return "WAVE";
    default:             return "?";
  }
}

static void gestureRemember(uint8_t raw, uint8_t ev, bool taken, uint8_t r43, uint8_t r44) {
  if (gestureSeenCount < GESTURE_SEEN_MAX) { gestureSeenCount++; }
  for (uint8_t i = gestureSeenCount - 1; i > 0; i--) { gestureSeen[i] = gestureSeen[i - 1]; }
  gestureSeen[0] = { millisNow, raw, ev, taken, r43, r44 };
}

// `count` of the sensor's registers from `reg` on, in bank 0, which the
// library keeps selected (the datasheet's burst read: one address, the
// registers after it follow).
static bool gestureReadRegs(uint8_t reg, uint8_t *values, uint8_t count) {
  Wire.beginTransmission((uint8_t)0x73);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) { return false; }
  if (Wire.requestFrom((uint8_t)0x73, count) != count) { return false; }
  for (uint8_t i = 0; i < count; i++) { values[i] = (uint8_t)Wire.read(); }
  return true;
}

static bool gestureWriteReg(uint8_t reg, uint8_t value) {
  Wire.beginTransmission((uint8_t)0x73);
  Wire.write(reg);
  Wire.write(value);
  return Wire.endTransmission() == 0;
}

// The two gesture result registers in one read: 0x43 up, down, left, right,
// push, pull, clockwise, counter-clockwise (bit 0 to 7), 0x44 the wave
// (bit 0). Reading clears them.
static bool gestureReadRaw(uint8_t &r43, uint8_t &r44) {
  uint8_t v[2] = { 0, 0 };
  bool ok = gestureReadRegs(0x43, v, 2);
  r43 = v[0];
  r44 = v[1];
  return ok;
}

// Two of the sensor's settings: as PixArt's datasheet v1.5 starts it, which
// the clock sets at boot (0x91 0x0C: a hand may be out of view for 12
// frames before its trace ends, "If Wave and CW/CCW move over FOV, then
// increase this value will help to make the trace more complete"; 0xCF
// 0x62: a wave from 2 swings), or as the library leaves them (0x06 and
// 0x64: 6 frames, 4 swings), which the gesture test can switch to for a
// comparison, until the next restart.
bool gestureTuned = false;
void gestureTune(bool on) {
  gestureWriteReg(0xEF, 0x00);   // bank 0
  gestureWriteReg(0xCF, on ? 0x62 : 0x64);
  gestureWriteReg(0x91, on ? 0x0C : 0x06);
  gestureTuned = on;
  Console.printf("Gesture sensor: %s\n", on ? "wave settings of the datasheet v1.5 (0xCF=0x62, 0x91=0x0C)"
                                           : "wave settings of the library (0xCF=0x64, 0x91=0x06)");
}

// The gesture the result registers stand for, also when more than one bit is
// set: a circle (left out, gestureLeftOut()) before push and pull, those
// before a swipe. The wave (0x44) is not used: it came for hardly any wave in
// the tests of 2026-10-10.
static Gesture gestureDecode(uint8_t r43, uint8_t r44) {
  (void)r44;
  if (r43 & 0x40) { return GES_CLOCKWISE; }
  if (r43 & 0x80) { return GES_ANTICLOCKWISE; }
  if (r43 & 0x10) { return GES_FORWARD; }
  if (r43 & 0x20) { return GES_BACKWARD; }
  if (r43 & 0x01) { return GES_UP; }
  if (r43 & 0x02) { return GES_DOWN; }
  if (r43 & 0x04) { return GES_LEFT; }
  if (r43 & 0x08) { return GES_RIGHT; }
  return GES_NONE;
}

// The gesture test, over the face; a GIF on show ends for it. False when
// there is no sensor or another screen is up (a menu, a banner, the hotspot).
bool gestureTestStart() {
  if (gestureOK && screen == SCREEN_GIF) { closeScreen(); }
  if (!gestureOK || screen != SCREEN_FACE) { return false; }
  gestureTestCount = 0;
  gestureTestAt    = millisNow;
  gestureSeenCount = 0;
  gestureNearAt    = 0;
  openScreen(SCREEN_GESTURE_TEST);
  Console.println("Gesture test: on");
  return true;
}

void gestureTestStop() {
  if (screen != SCREEN_GESTURE_TEST) { return; }
  closeScreen();
  Console.println("Gesture test: off");
}

// While the test is up: what the sensor sees in front of it, every
// APPROACH_POLL_MS; the panel follows when it changes.
void gestureTestUpdate() {
  if (millisNow - gestureNearAt < APPROACH_POLL_MS) { return; }
  gestureNearAt = millisNow;
  int16_t b = -1, s = -1;
  if (gestureSensor.isObjectInView()) {
    b = (int16_t)gestureSensor.getObjectBrightness();
    s = (int16_t)gestureSensor.getObjectSize();
  }
  if (b == gestureNearBright && s == gestureNearSize) { return; }
  gestureNearBright = b;
  gestureNearSize   = s;
  panelDirty = true;
}

// The test's screen: on top what the sensor reported (S), in the middle the
// direction the panel made of it - grey when it was left as too soon after
// the last - and below the count (G), the sensor's wave count (W, a T after
// it with the datasheet's wave settings), and what is in view (N
// brightness/size); in portrait also the sensor's turn (M) and the panel's
// rotation (R).
void drawGestureTest() {
  int16_t w = matrix.width(), h = matrix.height();
  bool landscape = w > h;
  const GestureSeen *last = gestureSeenCount ? &gestureSeen[0] : nullptr;
  char raw[16], line[24], nearTxt[16];
  if (last) {   // the two registers as read, in hex: 0x43 and 0x44
    snprintf(raw, sizeof(raw), "%02X %02X %s", last->r43, last->r44, landscape ? gestureRawName(last->raw) : "");
    for (char *p = raw; *p; p++) { *p = (char)toupper((unsigned char)*p); }
  } else {
    strlcpy(raw, landscape ? "GESTURE TEST" : "TEST", sizeof(raw));
  }
  if (gestureNearBright >= 0) { snprintf(nearTxt, sizeof(nearTxt), "N%d/%d", gestureNearBright, gestureNearSize); }
  else                        { strlcpy(nearTxt, "N-", sizeof(nearTxt)); }

  matrix.fillScreen(0);
  uiTiny(0, 0, raw, uiQuiet());
  const char *word = last ? gestureWord(last->ev) : "-";
  uint8_t size = (int16_t)(strlen(word) * 12) <= w ? 2 : 1;
  matrix.setFont();
  matrix.setTextSize(size);
  matrix.setTextColor(last && !last->taken ? uiQuiet() : uiInk());
  int16_t tw = (int16_t)strlen(word) * 6 * size - size, th = 7 * size;
  matrix.setCursor((w - tw) / 2, (h - th) / 2);
  matrix.print(word);
  matrix.setTextSize(1);
  if (landscape) {
    snprintf(line, sizeof(line), "G%u W%u%s %s", gestureTestCount, gestureWaves, gestureTuned ? "T" : "", nearTxt);
    uiTiny(0, h - 5, line, uiQuiet());
  } else {
    snprintf(line, sizeof(line), "G%u", gestureTestCount);
    uiTiny(0, h - 17, line, uiQuiet());
    snprintf(line, sizeof(line), "M%u R%u", uiSettings.gestureMount, screenRotation);
    uiTiny(0, h - 11, line, uiQuiet());
    uiTiny(0, h - 5, nearTxt, uiQuiet());
  }
  drawFeedbackIndicator();
  matrix.show();
  panelDirty = false;
}

// At boot: whether a hand is held over the sensor for APPROACH_BOOT_MS and
// then taken away, the recovery of a clock without buttons. Asked here whether
// the INT line is wired or not, since the line reports gestures only. The
// sensor has only just been set up, so it gets APPROACH_BOOT_LOOK_MS to see a
// hand that is already there; with nothing near the start goes on after that.
// After the hold the panel says LET GO, and only a hand taken away within
// APPROACH_LET_GO_MS opens the hotspot: an object standing in front of the
// sensor does not go, and the clock starts as usual. (Until 2026-10-21 the
// hold alone opened it, and an object the sensor had come to face opened it
// at every start, the clock off its home network.)
const unsigned long APPROACH_BOOT_LOOK_MS = 300;
const unsigned long APPROACH_LET_GO_MS    = 3000;
bool gestureHeldAtBoot() {
  if (!gestureOK) { return false; }
  bool near = false;
  for (unsigned long t0 = millis(); !near && millis() - t0 < APPROACH_BOOT_LOOK_MS; ) {
    near = gestureSensor.isObjectInView();
    if (!near) { delay(30); }
  }
  if (!near) { return false; }
  Console.print("Gesture sensor: something near at boot, brightness ");
  Console.print(gestureSensor.getObjectBrightness());
  Console.print(", size "); Console.println(gestureSensor.getObjectSize());
  bootStatus("HOLD");
  for (unsigned long t0 = millis(); millis() - t0 < APPROACH_BOOT_MS; ) {
    delay(50);
    if (!gestureSensor.isObjectInView()) {
      Console.println("Gesture sensor: gone again");
      bootStatus("CLOCK");
      return false;
    }
  }
  bootStatus("LET GO");
  for (unsigned long t0 = millis(); millis() - t0 < APPROACH_LET_GO_MS; ) {
    delay(50);
    if (!gestureSensor.isObjectInView()) {
      Console.println("Gesture sensor: let go - the hotspot opens");
      return true;
    }
  }
  Console.println("Gesture sensor: still near after the hold - an object, not a hand: the clock starts as usual");
  bootStatus("CLOCK");
  return false;
}


// Start the confirmation for a triggered n-click function. Timed from now rather
// than from the loop's millisNow, so a function that blocked for a while
// (bringing up the AP radio) does not swallow the blinks.
static void feedbackConfirm(uint8_t clicks) {
  fbBlinkCount = clicks;
  fbBlinkStart = millis();
}

// Lit while a button is down, or during an "on" phase of the confirmation.
static bool feedbackLit() {
  if (anyButtonDown()) { return true; }
  if (fbBlinkCount == 0) { return false; }
  const unsigned long period = FB_BLINK_ON_MS + FB_BLINK_OFF_MS;
  unsigned long elapsed = millis() - fbBlinkStart;
  if (elapsed < FB_LEAD_IN_MS) { return false; }  // pause before the first blink
  elapsed -= FB_LEAD_IN_MS;
  if (elapsed >= fbBlinkCount * period) { fbBlinkCount = 0; return false; }
  return (elapsed % period) < FB_BLINK_ON_MS;
}

// Resolve the feedback state once per loop and drive the board LED with it. The
// matrix screens draw the same state (fbLit), so LED and matrix stay in step.
static void updateFeedbackLed() {
  bool lit = feedbackLit();
  if (lit != fbLit) {
    fbLit = lit;
#if BOARD_HAS_FEEDBACK_LED
    digitalWrite(FEEDBACK_LED_PIN, lit ? HIGH : LOW);
#endif
#if BUTTON_FEEDBACK_ON_MATRIX
    panelDirty = true;   // the matrix shows the same square, so the panel has to follow
#endif
  }
}

// Matrix copy of the feedback LED: a 2x2 square in the bottom-right corner of
// the current rotation, clear of the digits in both orientations. Every screen
// calls this right before matrix.show().
void drawFeedbackIndicator() {
#if BUTTON_FEEDBACK_ON_MATRIX
  if (fbLit) {
    matrix.fillRect(matrix.width() - 2, matrix.height() - 2, 2, 2, scaledColorVisible(255, 255, 255));
  }
#endif
}
