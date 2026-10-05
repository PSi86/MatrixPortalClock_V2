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
};

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
  FN_KNOCK_EFFECT,     // the Tetris digits come apart
  FN_PLAY_GIF,         // a GIF now, on boards with GIF playback
};

struct InputMapping { InputEvent event; InputContext context; InputFunction function; };

// Profile "Default": on the face any press opens the menu - except holding
// UP, whose repeats would close the menu again at once; it plays a GIF, which
// the repeats leave alone. In the menu a short press moves; holding DOWN goes
// in, holding UP climbs out, one level for every 600 ms it stays held. Nothing
// uses 2x or 3x, so a short press acts at once. An event without a row does
// nothing.
constexpr InputMapping PROFILE_DEFAULT[] = {
  { EV_UP_SHORT,       CTX_FACE,    FN_OPEN_MENU      },
  { EV_DOWN_SHORT,     CTX_FACE,    FN_OPEN_MENU      },
  { EV_DOWN_HOLD,      CTX_FACE,    FN_OPEN_MENU      },
  { EV_UP_HOLD,        CTX_FACE,    FN_PLAY_GIF       },
  { EV_KNOCK,          CTX_FACE,    FN_KNOCK_EFFECT   },
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
  { EV_KNOCK,          CTX_FACE,    FN_KNOCK_EFFECT   },
  { EV_UP_SHORT,       CTX_MENU,    FN_PREV           },
  { EV_DOWN_SHORT,     CTX_MENU,    FN_NEXT           },
  { EV_DOWN_HOLD,      CTX_MENU,    FN_ENTER          },
  { EV_UP_HOLD,        CTX_MENU,    FN_BACK           },
  { EV_UP_HOLD_REPEAT, CTX_MENU,    FN_BACK           },
  { EV_UP_3X,          CTX_HOTSPOT, FN_TOGGLE_HOTSPOT },
  { EV_UP_HOLD,        CTX_HOTSPOT, FN_BACK           },
  { EV_UP_AT_BOOT,     CTX_BOOT,    FN_TOGGLE_HOTSPOT },
};

#define PROFILE_ROWS(p) ((unsigned)(sizeof(p) / sizeof((p)[0])))

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

// Today every row above comes from the two buttons, which every MatrixPortal
// has. Once a profile maps sensor events, these checks have to be made per set
// of fitted sources.
#define CHECK_PROFILE(p)                                                              \
  static_assert(profileUnique(p, PROFILE_ROWS(p)),       #p ": an event is mapped twice on one screen"); \
  static_assert(profileMenuOperable(p, PROFILE_ROWS(p)), #p ": the menu cannot be opened, moved through or left"); \
  static_assert(profileRecoverable(p, PROFILE_ROWS(p)),  #p ": no boot event opens the hotspot, or it cannot be closed")

CHECK_PROFILE(PROFILE_DEFAULT);
CHECK_PROFILE(PROFILE_CLASSIC_CLICKS);
