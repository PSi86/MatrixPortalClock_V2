// Who takes the panel, and when (panel_rules.h).
#include "panel_rules.h"

#include "brightness.h"
#include "clock_state.h"
#include "web_server.h"
#include "clock_time.h"
#include "console.h"
#include "face.h"
#include "notices.h"
#include "settings.h"

// What only this module calls, ahead of its use.
static unsigned long faceChangeIn();
static unsigned long panelRestMs();
static bool decorQuiet();
static void panelMinuteOf(unsigned long at, time_t &minute, unsigned long &inMinute);
static bool panelInChangeSlot(unsigned long at, unsigned long ms);
static void extrasScheduleNext();
static bool extrasDue();

/* ======================================================================
   Who takes the panel, and when (concept doc, tab "Panel priorities";
   decided by Peter 2026-10-12)
   ----------------------------------------------------------------------
   Nine levels, highest first: the hotspot; the person at the clock (the
   menu, an editor, banners, the gesture test, what an input asks for);
   alerts, at once; the time's change; messages; information (the info
   round, new cards); the faces' extras; GIFs of their own accord; the
   plain face.

   The time's change - every face's, the hour's included - is protected
   from what the clock plans itself: that ends PANEL_GUARD_BEFORE_MS before
   it starts, and waits as well while an animation the user asked for runs
   (Watchface::busy()). What the user asks for is granted unless there is a
   clear reason against it (Peter, 2026-10-22): a GIF may run into the
   change or start during it, the Tetris and classic animations may meet it;
   the Muncher's and the Aliens' scenes must have ended when their change
   has to start, and an animation already running is not started again.
   Messages and information come at a fixed place in the minute,
   PANEL_INFO_AFTER_MS after the change and before second
   PANEL_INFO_HALF_S, so they show at times one can count on; what comes
   later waits for the next minute. The rest of the minute is for
   animations, the whole minute when no information waits. Nothing optional
   waits for the panel: an animation or a request that cannot play when it
   is due is dropped, and a random time that falls into the slot around a
   change is drawn anew.
   ====================================================================== */
const unsigned long PANEL_GUARD_BEFORE_MS = FACE_GUARD_MS;   // optional things end this long before a change starts (face.h)
const unsigned long PANEL_INFO_AFTER_MS   = 3000;   // messages, cards, the info round: after a change's end
const unsigned long PANEL_DECOR_REST_MS   = 10000;  // GIFs and extras of their own accord: the plain face this long first
const unsigned long PANEL_EXTRA_MS        = 8000;   // the longest extra of a face, about: for drawing their times
const uint8_t       PANEL_INFO_HALF_S     = 30;     // messages and cards start before this second
static unsigned long faceChangeEndedAt = 0;   // the face's last change or scene ended
unsigned long panelFaceSince    = 0;   // the face came back on top

// Decoration: its settings in settings.h.
unsigned long extrasDueAt = 0;          // the next extra of a face; 0 = not drawn yet
// (infoCardsEndedAt, after which a GIF keeps ROUND_GIF_GAP_MS: notices.h.)

/* ----------------------------------------------------------------------
   Who takes the panel, and when: the rules (the constants at the top).
   ---------------------------------------------------------------------- */

// How long until the time's next change starts, in ms: 0 while the face's
// change or a scene of it runs, ~0UL without a time. Each face says how long
// before the minute its change starts (Watchface::changeSpan()): the Muncher
// and the Aliens as planned backwards from it, the classic digits in the
// second before, the Tetris ones at the minute. (K5 of the face refactor:
// until 2026-10-20 the Muncher and the Aliens counted 15 s until they had
// planned, and the classic face from the minute.)
static unsigned long faceChangeIn() {
  if (!clockIsSet()) { return ~0UL; }
  if (!faceTimeShown()) { return 0; }
  time_t next = localTime - secondNow + 60;
  long in = (long)(secondStartedAt + (60UL - secondNow) * 1000UL - millisNow)
          - (long)faceFor(settings.watchface).changeSpan(faceMoment(), next).lead;
  return in > 0 ? (unsigned long)in : 0;
}

// Whether something optional of `ms` ends PANEL_GUARD_BEFORE_MS before the
// time's next change starts.
bool panelFitsBeforeChange(unsigned long ms) {
  unsigned long in = faceChangeIn();
  return in == ~0UL || faceFits(millisNow + in, millisNow, ms);
}

// On every pass: when the face's change or scene ends.
void panelWatch() {
  static bool shownBefore = true;
  bool shown = faceTimeShown();
  if (shown && !shownBefore) { faceChangeEndedAt = millisNow; }
  shownBefore = shown;
}

// How long the plain face has been at rest: on top, its change or scene over.
static unsigned long panelRestMs() {
  if (screen != SCREEN_FACE || !faceTimeShown()) { return 0; }
  unsigned long since = (long)(faceChangeEndedAt - panelFaceSince) > 0 ? faceChangeEndedAt : panelFaceSince;
  return millisNow - since;
}

// Quiet hours, or a dark room: no decoration.
static bool decorQuiet() {
  const DecorSettings &d = decorSettings;
  if (d.quietFrom != d.quietTo && clockIsSet()) {
    uint8_t h = (uint8_t)clockHour(localTime);
    bool in = d.quietFrom < d.quietTo ? (h >= d.quietFrom && h < d.quietTo) : (h >= d.quietFrom || h < d.quietTo);
    if (in) { return true; }
  }
  return d.darkLux && luxAvg >= 0 && luxAvg < d.darkLux;
}

// Whether messages and new cards may come up now: the face's change over
// PANEL_INFO_AFTER_MS ago, before second PANEL_INFO_HALF_S, and `ms` ending
// before the next change starts. Else they wait for the next minute's place.
bool infoSlotOpen(unsigned long ms) {
  if (!faceTimeShown()) { return false; }
  if (faceChangeEndedAt && millisNow - faceChangeEndedAt < PANEL_INFO_AFTER_MS) { return false; }
  if (clockIsSet() && secondNow >= PANEL_INFO_HALF_S) { return false; }
  return panelFitsBeforeChange(ms);
}

// Whether decoration of `ms` may start now, of its own accord: nothing
// quiet, the plain face at rest PANEL_DECOR_REST_MS, ending before the next
// change, not in the minute's first half while information waits, and a
// GIF not within ROUND_GIF_GAP_MS of info cards. When it may not, it is
// dropped.
bool decorFree(unsigned long ms, bool gif) {
  if (decorQuiet()) { return false; }
  if (panelRestMs() < PANEL_DECOR_REST_MS) { return false; }
  if (!panelFitsBeforeChange(ms)) { return false; }
  if (secondNow < PANEL_INFO_HALF_S && (roundMinuteAt != 0 || noticesPending)) { return false; }
  if (gif) {
    if (infoCardsEndedAt && millisNow - infoCardsEndedAt < ROUND_GIF_GAP_MS) { return false; }
    if (roundNextIn() < ms + ROUND_GIF_GAP_MS) { return false; }
  }
  return true;
}

// The minute the moment `at` (millis) falls in - the local time it began
// at - and how far into it `at` lies, in ms.
static void panelMinuteOf(unsigned long at, time_t &minute, unsigned long &inMinute) {
  unsigned long fromMinute = secondNow * 1000UL + (millisNow - secondStartedAt) + (at - millisNow);
  minute = localTime - secondNow + (time_t)(fromMinute / 60000UL) * 60;
  inMinute = fromMinute % 60000UL;
}

// Whether the moment `at` (millis) falls into the slot around a change of
// the time, for something of `ms`: from its length, the guard and the lead
// of the change after it until the tail of the change before it and the rest
// after that - each as the face says (Watchface::changeSpan()), the hour's
// longer ones included. (K5 of the face refactor: until 2026-10-20 a flat
// 15 s before on the Muncher and the Aliens, 1.5 s after - 6 s on the Tetris
// face -, which let the Aliens' wave and the Muncher's hour show drop what
// fell into them.)
static bool panelInChangeSlot(unsigned long at, unsigned long ms) {
  time_t minute;
  unsigned long inMinute;
  panelMinuteOf(at, minute, inMinute);
  Watchface &face = faceFor(settings.watchface);
  FaceNow now = faceMoment();
  unsigned long before = face.changeSpan(now, minute + 60).lead + PANEL_GUARD_BEFORE_MS + ms;
  unsigned long after = face.changeSpan(now, minute).tail + PANEL_DECOR_REST_MS;
  return inMinute + before >= 60000UL || inMinute < after;
}

// A random time `lo` to `hi` ms from now, for something of `ms`, drawn anew
// while it falls into the slot around a change; when it keeps falling there
// (the same minutes twice: every n minutes), moved to the slot's end.
unsigned long panelRandomAt(unsigned long lo, unsigned long hi, unsigned long ms) {
  unsigned long at = millisNow + lo;
  for (uint8_t n = 0; n < 8; n++) {
    at = millisNow + lo + (hi > lo ? esp_random() % (hi - lo + 1) : 0);
    if (!panelInChangeSlot(at, ms)) { return at; }
  }
  time_t minute;
  unsigned long inMinute;
  panelMinuteOf(at, minute, inMinute);
  FaceNow now = faceMoment();
  Watchface &face = faceFor(settings.watchface);
  unsigned long after = face.changeSpan(now, minute).tail + PANEL_DECOR_REST_MS;
  if (inMinute < after) { return at + (after - inMinute); }
  return at + (60000UL - inMinute) + face.changeSpan(now, minute + 60).tail + PANEL_DECOR_REST_MS;
}

// The faces' extras: the next at a random time around the slider's mean -
// half of it to one and a half times it - outside the slot around a change.
static void extrasScheduleNext() {
  uint8_t m = DECOR_EXTRAS_MINUTES[min<uint8_t>(decorSettings.extras, DECOR_EXTRAS_MAX)];
  if (!m) { extrasDueAt = 0; return; }
  unsigned long mean = m * 60000UL;
  extrasDueAt = panelRandomAt(mean / 2, mean + mean / 2, PANEL_EXTRA_MS);
  if (!extrasDueAt) { extrasDueAt = 1; }
}

// Whether an extra is due. Then it plays now or not at all (extrasUpdate()).
static bool extrasDue() {
  if (!decorSettings.extras) { return false; }
  if (extrasDueAt == 0) { extrasScheduleNext(); return false; }
  return (long)(millisNow - extrasDueAt) >= 0;
}

// On every pass outside the hotspot, after a GIF of its own accord: the
// extra of the face shown, when one is due - now, or dropped, and the next
// time drawn either way. The panel has to be free for something of its own
// accord (decorFree(): quiet, the face at rest, information waiting, room
// before the change); then the face, which knows how long its extra takes,
// plays it or refuses one that would run into its next change. Faces
// without extras leave the time alone.
void extrasUpdate() {
  Watchface &face = faceFor(settings.watchface);
  if (!face.offers(TRIGGER_NOW_AND_THEN) || !extrasDue()) { return; }
  extrasScheduleNext();
  if (!decorFree(0, false)) {
    Console.println("Extra: due, the panel not free for it - dropped");
    return;
  }
  if (face.request(faceMoment(), TRIGGER_NOW_AND_THEN) != OUTCOME_PLAYED) {
    Console.println("Extra: due, the face has no room for it - dropped");
  }
}

// The web app's extras and quiet; a new pace of the extras draws the next anew.
void applyDecorParams(const String &q) {
  uint8_t pace = decorSettings.extras;
  settingsApply(decorTable, [&q](const char *key) { return getParam(q, key); }, false);   // DECOR_PARAMS
  if (decorSettings.extras != pace) { extrasDueAt = 0; }
}
