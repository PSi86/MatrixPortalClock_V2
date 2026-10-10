/* ----------------------------------------------------------------------
   Input events, the functions they can trigger, and the profiles that map
   one onto the other.

   Every input is first turned into a named event; a profile then says what
   that event does on the current screen. A function moves to another input by
   changing a table row, not the code that reads the input.

   The rules every profile has to keep are checked here when the firmware is
   built (static_assert at the end), so a profile that would lock the user out
   never reaches a clock. Written for C++11 (the M4 builds with -std=gnu++11),
   which is why the checks are single-expression recursive constexpr
   functions. They live in a header because the .ino preprocessor writes a
   plain prototype for every function in the sketch, which a constexpr
   definition would then contradict.
   ------------------------------------------------------------------------- */
#pragma once

#include <stdint.h>

enum InputEvent : uint8_t {
  EV_NONE,
  EV_UP_SHORT,   EV_UP_2X,   EV_UP_3X,   EV_UP_HOLD,   EV_UP_HOLD_REPEAT,
  EV_DOWN_SHORT, EV_DOWN_2X, EV_DOWN_3X, EV_DOWN_HOLD, EV_DOWN_HOLD_REPEAT,
  EV_UP_AT_BOOT,       // UP held while the clock starts
  EV_KNOCK,            // the accelerometer felt a knock
  // Gesture sensor. Swipes come in screen directions: up is up on the panel as
  // the viewer sees it, whichever way the clock and the sensor are turned.
  EV_SWIPE_UP, EV_SWIPE_DOWN, EV_SWIPE_LEFT, EV_SWIPE_RIGHT,
  EV_PUSH,             // a hand moved towards the sensor
  EV_PULL,             // and away from it
  EV_CIRCLE_CW, EV_CIRCLE_CCW,
  EV_WAVE,             // a hand waved to and fro over it
  EV_APPROACH,         // a hand came near and stays (not a gesture: it closes nothing)
  EV_APPROACH_AT_BOOT, // a hand held over the sensor while the clock starts
};

// Which source an event comes from. Every clock has a different set of them
// (buttons on the MatrixPortals, only the gesture sensor on the wall clock),
// so the rules below are checked for each source on its own.
constexpr bool isGestureEvent(InputEvent ev) { return ev >= EV_SWIPE_UP && ev <= EV_APPROACH_AT_BOOT; }

// Where an event happens. The editors count as the menu; a banner takes the
// context of the screen it covers.
enum InputContext : uint8_t { CTX_BOOT, CTX_FACE, CTX_MENU, CTX_HOTSPOT };

enum InputFunction : uint8_t {
  FN_NONE,
  // Navigation. What these mean is up to the screen on top: in the menu list
  // they move the focus, in an editor they change the value, on the hotspot
  // screen BACK closes the hotspot.
  FN_PREV, FN_NEXT, FN_ENTER, FN_BACK, FN_HOME,
  FN_OPEN_MENU,
  FN_TOGGLE_AUTO,      // auto brightness on/off, with a banner
  FN_CYCLE_DST,        // daylight saving auto -> summer -> winter, with a banner
  FN_TOGGLE_HOTSPOT,   // config AP on/off
  FN_BRIGHT_FADE,      // cyclic brightness fade while the button stays held, saved on release
  FN_FACE_ANIMATION,   // the watchface's animation: the Tetris digits break up, the classic ones fly in
  FN_PLAY_GIF,         // a GIF now, on boards with GIF playback
  FN_GIF_AGAIN,        // the last GIF shown once more, on boards with GIF playback
  FN_SHOW_HINTS,       // what the gestures do, for a moment
  FN_SHOW_MESSAGES,    // the alerts and messages there are, once more, on boards with the info band data
};

struct InputMapping { InputEvent event; InputContext context; InputFunction function; };

// Profile "Default": on the face any press opens the menu - except holding
// UP, whose repeats would close the menu again at once; it plays a GIF, which
// the repeats leave alone - and UP twice shows the messages again, where a
// clock has them. In the menu a short press moves; holding DOWN goes in,
// holding UP climbs out, one level for every 600 ms it stays held. Nothing
// else uses 2x or 3x, so a short press of DOWN acts at once, and one of UP
// waits 0.4 s for a second on a clock with messages. An event without a row
// does nothing.
constexpr InputMapping PROFILE_DEFAULT[] = {
  { EV_UP_SHORT,       CTX_FACE,    FN_OPEN_MENU      },
  { EV_DOWN_SHORT,     CTX_FACE,    FN_OPEN_MENU      },
  { EV_DOWN_HOLD,      CTX_FACE,    FN_OPEN_MENU      },
  { EV_UP_HOLD,        CTX_FACE,    FN_PLAY_GIF       },
  { EV_UP_2X,          CTX_FACE,    FN_SHOW_MESSAGES  },
  { EV_KNOCK,          CTX_FACE,    FN_FACE_ANIMATION },
  { EV_UP_SHORT,       CTX_MENU,    FN_PREV           },
  { EV_DOWN_SHORT,     CTX_MENU,    FN_NEXT           },
  { EV_DOWN_HOLD,      CTX_MENU,    FN_ENTER          },
  { EV_UP_HOLD,        CTX_MENU,    FN_BACK           },
  { EV_UP_HOLD_REPEAT, CTX_MENU,    FN_BACK           },
  { EV_UP_HOLD,        CTX_HOTSPOT, FN_BACK           },
  { EV_UP_AT_BOOT,     CTX_BOOT,    FN_TOGGLE_HOTSPOT },
};

// Profile "Classic clicks": on the face the controls the clock has always had -
// UP 1x, 2x, 3x and held - plus DOWN held for the menu, which works as in the
// default profile. 3x still closes the hotspot again.
constexpr InputMapping PROFILE_CLASSIC_CLICKS[] = {
  { EV_UP_SHORT,       CTX_FACE,    FN_CYCLE_DST      },
  { EV_UP_2X,          CTX_FACE,    FN_TOGGLE_AUTO    },
  { EV_UP_3X,          CTX_FACE,    FN_TOGGLE_HOTSPOT },
  { EV_UP_HOLD,        CTX_FACE,    FN_BRIGHT_FADE    },
  { EV_DOWN_HOLD,      CTX_FACE,    FN_OPEN_MENU      },
  { EV_KNOCK,          CTX_FACE,    FN_FACE_ANIMATION },
  { EV_UP_SHORT,       CTX_MENU,    FN_PREV           },
  { EV_DOWN_SHORT,     CTX_MENU,    FN_NEXT           },
  { EV_DOWN_HOLD,      CTX_MENU,    FN_ENTER          },
  { EV_UP_HOLD,        CTX_MENU,    FN_BACK           },
  { EV_UP_HOLD_REPEAT, CTX_MENU,    FN_BACK           },
  { EV_UP_3X,          CTX_HOTSPOT, FN_TOGGLE_HOTSPOT },
  { EV_UP_HOLD,        CTX_HOTSPOT, FN_BACK           },
  { EV_UP_AT_BOOT,     CTX_BOOT,    FN_TOGGLE_HOTSPOT },
};

// Gestures, for every profile. Swipes and push move as UP and DOWN do; a
// circle is the knob - clockwise is up, five steps at a time in an editor;
// waving is the knock on the face and the way home in the menu. A hand held
// over the sensor at boot is the recovery, as holding UP is, and a swipe to
// the left closes the hotspot again.
constexpr InputMapping GESTURES_DEFAULT[] = {
  { EV_SWIPE_UP,         CTX_FACE,    FN_OPEN_MENU      },
  { EV_SWIPE_DOWN,       CTX_FACE,    FN_OPEN_MENU      },
  { EV_SWIPE_RIGHT,      CTX_FACE,    FN_OPEN_MENU      },
  { EV_PUSH,             CTX_FACE,    FN_OPEN_MENU      },
  { EV_WAVE,             CTX_FACE,    FN_FACE_ANIMATION },
  { EV_CIRCLE_CW,        CTX_FACE,    FN_PLAY_GIF       },
  { EV_CIRCLE_CCW,       CTX_FACE,    FN_PLAY_GIF       },
  { EV_APPROACH,         CTX_FACE,    FN_SHOW_HINTS     },
  { EV_SWIPE_UP,         CTX_MENU,    FN_PREV           },
  { EV_SWIPE_DOWN,       CTX_MENU,    FN_NEXT           },
  { EV_SWIPE_RIGHT,      CTX_MENU,    FN_ENTER          },
  { EV_PUSH,             CTX_MENU,    FN_ENTER          },
  { EV_SWIPE_LEFT,       CTX_MENU,    FN_BACK           },
  { EV_WAVE,             CTX_MENU,    FN_HOME           },
  { EV_CIRCLE_CW,        CTX_MENU,    FN_PREV           },
  { EV_CIRCLE_CCW,       CTX_MENU,    FN_NEXT           },
  { EV_SWIPE_LEFT,       CTX_HOTSPOT, FN_BACK           },
  { EV_APPROACH_AT_BOOT, CTX_BOOT,    FN_TOGGLE_HOTSPOT },
};

#define PROFILE_ROWS(p) ((unsigned)(sizeof(p) / sizeof((p)[0])))

// Whether every row's event comes from the source asked for.
constexpr bool rowsFromSource(const InputMapping *r, unsigned n, bool gesture) {
  return n == 0 || (isGestureEvent(r->event) == gesture && rowsFromSource(r + 1, n - 1, gesture));
}

// Whether some row maps a function in a context.
constexpr bool profileMaps(const InputMapping *r, unsigned n, InputContext ctx, InputFunction fn) {
  return n > 0 && ((r->context == ctx && r->function == fn) || profileMaps(r + 1, n - 1, ctx, fn));
}

// Whether some row maps this event in this context.
constexpr bool profileHasRow(const InputMapping *r, unsigned n, InputEvent ev, InputContext ctx) {
  return n > 0 && ((r->event == ev && r->context == ctx) || profileHasRow(r + 1, n - 1, ev, ctx));
}

// Rule 1: one function per event and screen.
constexpr bool profileUnique(const InputMapping *r, unsigned n) {
  return n == 0 || (!profileHasRow(r + 1, n - 1, r->event, r->context) && profileUnique(r + 1, n - 1));
}

// Rule 2: the menu stays operable - it can be opened from the face and moved
// through, entered and left inside.
constexpr bool profileMenuOperable(const InputMapping *r, unsigned n) {
  return profileMaps(r, n, CTX_FACE, FN_OPEN_MENU) &&
         profileMaps(r, n, CTX_MENU, FN_PREV) && profileMaps(r, n, CTX_MENU, FN_NEXT) &&
         profileMaps(r, n, CTX_MENU, FN_ENTER) && profileMaps(r, n, CTX_MENU, FN_BACK);
}

// Rule 3: recovery stays reachable - a boot event opens the hotspot, and the
// hotspot can be closed again on the clock itself.
constexpr bool profileRecoverable(const InputMapping *r, unsigned n) {
  return profileMaps(r, n, CTX_BOOT, FN_TOGGLE_HOTSPOT) &&
         (profileMaps(r, n, CTX_HOTSPOT, FN_BACK) || profileMaps(r, n, CTX_HOTSPOT, FN_TOGGLE_HOTSPOT));
}

// The rules hold for each source on its own: a profile's table maps the
// buttons (and the knock) and keeps to them, so a MatrixPortal without a
// gesture sensor can be operated and recovered with its buttons; the gesture
// table maps only gestures and keeps the same rules, so a clock that has
// nothing but the sensor can be as well. An event belongs to one source, so
// the two tables together never map one twice.
#define CHECK_RULES(p)                                                                \
  static_assert(profileUnique(p, PROFILE_ROWS(p)),       #p ": an event is mapped twice on one screen"); \
  static_assert(profileMenuOperable(p, PROFILE_ROWS(p)), #p ": the menu cannot be opened, moved through or left"); \
  static_assert(profileRecoverable(p, PROFILE_ROWS(p)),  #p ": no boot event opens the hotspot, or it cannot be closed")
#define CHECK_PROFILE(p)                                                              \
  CHECK_RULES(p);                                                                     \
  static_assert(rowsFromSource(p, PROFILE_ROWS(p), false), #p ": a button profile maps a gesture")
#define CHECK_GESTURES(g)                                                             \
  CHECK_RULES(g);                                                                     \
  static_assert(rowsFromSource(g, PROFILE_ROWS(g), true), #g ": a gesture table maps a button")

CHECK_PROFILE(PROFILE_DEFAULT);
CHECK_PROFILE(PROFILE_CLASSIC_CLICKS);
CHECK_GESTURES(GESTURES_DEFAULT);

// ----------------------------------------------------------------------------
// Face shortcuts (concept doc: "Face shortcuts"). On the face every event of a
// fitted source can be given one of FACE_ACTIONS on the config page's Inputs
// page; the menu, the hotspot screen and the start keep the profile's rows,
// and a profile's face rows are the shortcuts to begin with. Both tables are
// stored data - a shortcut is saved as its event's position in FACE_EVENTS and
// its action's code - so neither is ever reordered: a new event or action goes
// at the end.
struct FaceEvent { InputEvent event; const char *label; const char *word; };
constexpr FaceEvent FACE_EVENTS[] = {
  { EV_UP_SHORT,    "UP pressed",                       "" },
  { EV_UP_2X,       "UP pressed twice",                 "" },
  { EV_UP_3X,       "UP pressed three times",           "" },
  { EV_UP_HOLD,     "UP held",                          "" },
  { EV_DOWN_SHORT,  "DOWN pressed",                     "" },
  { EV_DOWN_2X,     "DOWN pressed twice",               "" },
  { EV_DOWN_3X,     "DOWN pressed three times",         "" },
  { EV_DOWN_HOLD,   "DOWN held",                        "" },
  { EV_KNOCK,       "Knock on the clock",               "" },
  { EV_SWIPE_UP,    "Swipe up",                         "swipe" },
  { EV_SWIPE_DOWN,  "Swipe down",                       "swipe" },
  { EV_SWIPE_LEFT,  "Swipe left",                       "swipe" },
  { EV_SWIPE_RIGHT, "Swipe right",                      "swipe" },
  { EV_PUSH,        "Hand towards the sensor",          "push" },
  { EV_PULL,        "Hand away from the sensor",        "pull" },
  { EV_CIRCLE_CW,   "Circle clockwise",                 "circle" },
  { EV_CIRCLE_CCW,  "Circle counter-clockwise",         "circle" },
  { EV_WAVE,        "Wave",                             "wave" },
  { EV_APPROACH,    "Hand comes near and stays",        "" },
};
constexpr uint8_t FACE_EVENT_COUNT = sizeof(FACE_EVENTS) / sizeof(FACE_EVENTS[0]);

// What an action needs to be offered: a held button (it runs while the button
// stays down), GIF playback, the info band data (alerts and messages).
enum FaceActionNeeds : uint8_t { NEEDS_NOTHING, NEEDS_HOLD, NEEDS_GIFS, NEEDS_MESSAGES };
struct FaceAction { uint8_t code; InputFunction function; FaceActionNeeds needs; const char *label; const char *word; };
constexpr FaceAction FACE_ACTIONS[] = {
  { 0, FN_NONE,           NEEDS_NOTHING, "nothing",                                      ""      },
  { 1, FN_OPEN_MENU,      NEEDS_NOTHING, "open the menu",                                "menu"  },
  { 2, FN_PLAY_GIF,       NEEDS_GIFS,    "play random GIF",                              "GIF"   },
  { 3, FN_GIF_AGAIN,      NEEDS_GIFS,    "play last GIF again",                          "again" },
  { 4, FN_FACE_ANIMATION, NEEDS_NOTHING, "trigger watchface animation",                  "anim"  },
  { 5, FN_TOGGLE_AUTO,    NEEDS_NOTHING, "toggle auto brightness on / off",              "auto"  },
  { 6, FN_CYCLE_DST,      NEEDS_NOTHING, "toggle daylight saving: auto, summer, winter", "DST"   },
  { 7, FN_TOGGLE_HOTSPOT, NEEDS_NOTHING, "toggle hotspot on / off",                      "AP"    },
  { 8, FN_BRIGHT_FADE,    NEEDS_HOLD,    "fade brightness while held",                   "fade"  },
  { 9, FN_SHOW_HINTS,     NEEDS_NOTHING, "show gesture hints",                           "hints" },
  { 10, FN_SHOW_MESSAGES, NEEDS_MESSAGES, "show messages again",                         "msgs"  },
};
constexpr uint8_t FACE_ACTION_COUNT = sizeof(FACE_ACTIONS) / sizeof(FACE_ACTIONS[0]);

constexpr bool faceActionsInOrder(unsigned i = 0) {
  return i >= FACE_ACTION_COUNT || (FACE_ACTIONS[i].code == i && faceActionsInOrder(i + 1));
}
constexpr bool isFaceEvent(InputEvent ev, unsigned i = 0) {
  return i < FACE_EVENT_COUNT && (FACE_EVENTS[i].event == ev || isFaceEvent(ev, i + 1));
}
constexpr bool isFaceFunction(InputFunction fn, unsigned i = 0) {
  return i < FACE_ACTION_COUNT && (FACE_ACTIONS[i].function == fn || isFaceFunction(fn, i + 1));
}
constexpr bool faceEventsUnique(unsigned i = 0) {
  return i >= FACE_EVENT_COUNT || (!isFaceEvent(FACE_EVENTS[i].event, i + 1) && faceEventsUnique(i + 1));
}
// Every face row of a table can be shown and stored as a shortcut.
constexpr bool faceRowsAreShortcuts(const InputMapping *r, unsigned n) {
  return n == 0 || ((r->context != CTX_FACE || (isFaceEvent(r->event) && isFaceFunction(r->function))) &&
                    faceRowsAreShortcuts(r + 1, n - 1));
}
static_assert(faceActionsInOrder(), "FACE_ACTIONS: an action's code has to be its position (add at the end only)");
static_assert(faceEventsUnique(), "FACE_EVENTS: an event is listed twice");
static_assert(faceRowsAreShortcuts(PROFILE_DEFAULT, PROFILE_ROWS(PROFILE_DEFAULT)), "PROFILE_DEFAULT: a face row is no shortcut");
static_assert(faceRowsAreShortcuts(PROFILE_CLASSIC_CLICKS, PROFILE_ROWS(PROFILE_CLASSIC_CLICKS)), "PROFILE_CLASSIC_CLICKS: a face row is no shortcut");
static_assert(faceRowsAreShortcuts(GESTURES_DEFAULT, PROFILE_ROWS(GESTURES_DEFAULT)), "GESTURES_DEFAULT: a face row is no shortcut");
