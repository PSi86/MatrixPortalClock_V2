// The Muncher watchface: HH:MM in dots, which a muncher eats and lays anew
// when the time changes; the logic is in src/muncher.h, here is when it moves
// and what the panel shows. Between changes nothing moves. A change is
// planned backwards from second 00 (PlannedFace), so the last digit is
// complete as the new minute begins, and it is stepped by the time since it
// started: a screen that was on top for a while, or a loop held up by a
// request, does not put it behind.
//
// S3 and wall clock only.
#ifndef FACE_MUNCHER_H
#define FACE_MUNCHER_H

#include <Adafruit_GFX.h>
#include "face_planned.h"
#include "muncher.h"

// Settings of this watchface, in their own blob, as the Tetris ones are.
#define MUNCHER_SETTINGS_MAGIC 0x3C1A
#define MUNCHER_SETTINGS_REV   2   // 2 = adds pellets

// What the watchface animation plays: the input action "trigger watchface
// animation", the knock by default.
// Stored values: a new one goes at the end, so "random" keeps its 2.
const uint8_t MUNCHER_ANIM_POWER  = 0;   // the power pellet: the ghosts turn and flee
const uint8_t MUNCHER_ANIM_REPLAY = 1;   // the minutes' ones eaten and laid again
const uint8_t MUNCHER_ANIM_RANDOM = 2;   // one of the others, drawn each time
const uint8_t MUNCHER_ANIM_DEATH  = 3;   // the ghosts win
const uint8_t MUNCHER_ANIM_COUNT  = 4;
// The LEDs of a pellet, in the digits and the corridor: one, or more to be
// seen better from afar (Peter, 2026-10-11) - two side by side, which makes
// the level strokes solid lines, two one above the other, which makes the
// upright ones solid, or a block of 2 x 2, which makes every stroke a solid
// line 2 px thick: the digits drawn for reading first, the game kept as a
// token. The other LEDs go right of and below the pellet's own, so a digit
// grows by a pixel; the muncher's way stays on the pellet's own.
const uint8_t MUNCHER_PELLETS_ONE   = 0;
const uint8_t MUNCHER_PELLETS_WIDE  = 1;
const uint8_t MUNCHER_PELLETS_TALL  = 2;
const uint8_t MUNCHER_PELLETS_SOLID = 3;
const uint8_t MUNCHER_PELLETS_COUNT = 4;
const uint8_t MUNCHER_PACE_MIN = 20, MUNCHER_PACE_MAX = 80;   // ms per pixel

struct MuncherSettings {
  uint16_t magic;
  uint8_t  rev;
  uint8_t  style;     // muncher::STYLE_*: how a changing digit is walked
  uint8_t  animation;   // MUNCHER_ANIM_*: what the watchface animation plays
  uint8_t  paceMs;    // milliseconds per pixel the muncher walks
  uint8_t  pellets;   // rev 2: MUNCHER_PELLETS_*, the LEDs of a pellet (was a spare byte, 0)
  uint8_t  spare[1];
};
static_assert(sizeof(MuncherSettings) == 8, "the stored blob keeps its size");

const MuncherSettings MUNCHER_DEFAULTS = {
  MUNCHER_SETTINGS_MAGIC, MUNCHER_SETTINGS_REV,
  muncher::STYLE_MIXED,   // the whole digit when one changes, only the difference when several do
  MUNCHER_ANIM_RANDOM,
  40,                     // 25 pixels a second
  0,                      // one LED a pellet
  { 0 }
};

// The Muncher settings for the web app and the store (face.h).
const char *const MUNCHER_MST_LABELS[] = { "Mixed", "Whole digit", "Only the difference" };              // muncher::STYLE_*
const char *const MUNCHER_MKN_LABELS[] = { "Power pellet", "Last digit again", "Random each time", "Ghosts win" };   // MUNCHER_ANIM_*
const char *const MUNCHER_MPEL_LABELS[] = { "One LED", "Side by side", "One above the other", "Solid" };   // MUNCHER_PELLETS_*
static_assert(sizeof(MUNCHER_MST_LABELS) / sizeof(MUNCHER_MST_LABELS[0]) == muncher::STYLE_COUNT, "a name per change style");
static_assert(sizeof(MUNCHER_MKN_LABELS) / sizeof(MUNCHER_MKN_LABELS[0]) == MUNCHER_ANIM_COUNT, "a name per scene");
static_assert(sizeof(MUNCHER_MPEL_LABELS) / sizeof(MUNCHER_MPEL_LABELS[0]) == MUNCHER_PELLETS_COUNT, "a name per pellet");
const FaceParam MUNCHER_PARAMS[] = {
  { "mst",   offsetof(MuncherSettings, style),   1, PARAM_CHOICE, 0,            0, muncher::STYLE_COUNT - 1,  MUNCHER_MST_LABELS },
  { "mkn",   offsetof(MuncherSettings, animation),   1, PARAM_CHOICE, PARAM_PLAYS,  0, MUNCHER_ANIM_COUNT - 1,   MUNCHER_MKN_LABELS },
  { "mpace", offsetof(MuncherSettings, paceMs),  1, PARAM_RANGE,  0,            MUNCHER_PACE_MIN, MUNCHER_PACE_MAX, nullptr },
  { "mpel",  offsetof(MuncherSettings, pellets), 1, PARAM_CHOICE, PARAM_REDRAW, 0, MUNCHER_PELLETS_COUNT - 1, MUNCHER_MPEL_LABELS },
};

// Rev 1 had a spare byte where the pellets are.
inline void muncherMigrate(uint8_t *data, uint8_t rev) {
  if (rev < 2) { ((MuncherSettings *)data)->pellets = MUNCHER_PELLETS_ONE; }
}

// The sprites, 5 x 5, their rows with the leftmost pixel in bit 4.
const uint8_t MUNCH_OPEN[5]  = { 0x0E, 0x1C, 0x18, 0x1C, 0x0E };   // .###. ###.. ##... ###.. .###.
const uint8_t MUNCH_SHUT[5]  = { 0x0E, 0x1F, 0x1F, 0x1F, 0x0E };
const uint8_t GHOST_BODY[5]  = { 0x0E, 0x1F, 0x15, 0x1F, 0x15 };   // .###. ##### #.#.# ##### #.#.#
const uint8_t GHOST_EYES[5]  = { 0x00, 0x00, 0x0A, 0x00, 0x00 };   // the two gaps of row 2
// Red and pink follow the muncher; cyan and orange join them to a full hour.
const uint8_t GHOST_RGB[muncher::GHOSTS][3] = { { 255, 0, 0 }, { 255, 184, 255 }, { 0, 255, 255 }, { 255, 184, 82 } };
// The muncher folding up after the ghosts won, facing up: its mouth opens
// wider and wider until nothing is left, then a little burst.
const uint8_t MUNCH_FOLD[muncher::DEATH_FRAMES][5] = {
  { 0x0E, 0x1F, 0x1F, 0x1F, 0x0E },   // .###. ##### ##### ##### .###.
  { 0x0A, 0x1B, 0x1F, 0x1F, 0x0E },   // .#.#. ##.## ##### ##### .###.
  { 0x11, 0x1B, 0x1F, 0x1F, 0x0E },   // #...# ##.## ##### ##### .###.
  { 0x00, 0x11, 0x1B, 0x1F, 0x0E },   // ..... #...# ##.## ##### .###.
  { 0x00, 0x00, 0x11, 0x1B, 0x0E },   // ..... ..... #...# ##.## .###.
  { 0x00, 0x00, 0x00, 0x11, 0x0E },   // ..... ..... ..... #...# .###.
  { 0x00, 0x0A, 0x00, 0x0A, 0x00 },   // ..... .#.#. ..... .#.#. .....
  { 0x11, 0x00, 0x00, 0x00, 0x11 },   // #...# ..... ..... ..... #...#
};

// The hour show's giant muncher, 13 x 13, facing right; '#' lights a pixel.
constexpr uint16_t giantRow(const char (&row)[muncher::GIANT + 1]) {
  uint16_t bits = 0;
  for (uint8_t i = 0; i < muncher::GIANT; i++) { bits = (uint16_t)((bits << 1) | (row[i] == '#' ? 1 : 0)); }
  return bits;
}
constexpr uint16_t GIANT_OPEN[muncher::GIANT] = {
  giantRow("....#####...."), giantRow("..#########.."), giantRow(".###########."), giantRow(".#########..."),
  giantRow("########....."), giantRow("######......."), giantRow("#####........"), giantRow("######......."),
  giantRow("########....."), giantRow(".#########..."), giantRow(".###########."), giantRow("..#########.."),
  giantRow("....#####...."),
};
constexpr uint16_t GIANT_SHUT[muncher::GIANT] = {
  giantRow("....#####...."), giantRow("..#########.."), giantRow(".###########."), giantRow(".###########."),
  giantRow("#############"), giantRow("#############"), giantRow("#############"), giantRow("#############"),
  giantRow("#############"), giantRow(".###########."), giantRow(".###########."), giantRow("..#########.."),
  giantRow("....#####...."),
};

// The bonus fruits, 7 x 7, in muncher::FRUIT_KINDS' order: cherry,
// strawberry, orange, apple. A letter is a colour (fruitColour()), '.' nothing.
const char *const FRUITS[muncher::FRUIT_KINDS][7] = {
  { ".....GG", "....GG.", "..GG.G.", ".R...R.", "RRR.RRR", "RRR.RRR", ".R...R." },
  { "..GGG..", ".RGGGR.", "RRWRWRR", "RWRRRWR", ".RRWRR.", "..RWR..", "...R..." },
  { "...GG..", "..OGO..", ".OOOOO.", ".OOOOO.", ".OOOOO.", "..OOO..", "......." },
  { "...BG..", "..RBR..", ".RRRRR.", ".RRRRR.", ".RRRRR.", "..R.R..", "......." },
};

class MuncherFace : public PlannedFace {
 public:
  explicit MuncherFace(const MuncherSettings &s) : _s(s) {}

  // One pass. Between changes it only reports a change when what it shows has
  // changed (the colon's beat), so an idle clock costs almost nothing.
  bool update(const FaceNow &now) override {
    if (turned(now)) {
      // A rotation moves everything: the time is shown as it is, at once.
      _m.setLayout(now.turn == 0 || now.turn == 2 ? muncher::LANDSCAPE : muncher::PORTRAIT);
      uint8_t d[4];
      faceDigits(now.local, now.set, d, muncher::NO_DIGIT);
      _m.show(d);
      _plannedFor = 0;
      _dirty = true;
    }
    bool moved = false;
    if (_m.mode != muncher::MODE_IDLE) {
      long since = (long)(now.ms - _startAt);
      if (since >= 0) {
        uint32_t steps = (uint32_t)since / _s.paceMs;
        moved = _m.advanceTo((uint16_t)min(steps, (uint32_t)0xFFFF));
        if (_doneSteps && (_m.stepsDone >= _doneSteps || _m.mode == muncher::MODE_IDLE)) {
          logStands("Muncher", now);
          _doneSteps = 0;
        }
        if (_m.fruitsEaten != _fruitsLogged) {
          _fruitsLogged = _m.fruitsEaten;
          Serial.println("Muncher: a bonus fruit eaten");
        }
        if (_m.mode == muncher::MODE_IDLE) {
          sceneEnded(now);
          if (_m.mode != muncher::MODE_IDLE) { moved = true; }
        }
      }
    }
    if (_m.mode == muncher::MODE_IDLE) {
      uint8_t d[4];
      faceDigits(now.local, now.set, d, muncher::NO_DIGIT);
      idle(now, d);
      if (_m.mode != muncher::MODE_IDLE) { moved = true; }
    }
    _colonOn = (now.local % 2) == 0;   // 1 Hz, in step with the seconds, as on the Tetris face
    return moved || _dirty || _colonOn != _colonDrawn;
  }

  void draw(Adafruit_GFX &g, const FaceNow &, const FacePaint &paint) override {
    const muncher::Layout &L = _m.L;
    uint16_t dot   = paint.color(255, 184, 151);
    uint16_t wall  = paint.color(40, 40, 255);
    uint16_t eyes  = paint.color(255, 255, 255);
    uint16_t walls = _m.wallsWhite() ? eyes : wall;   // the hour show flashes them
    g.fillScreen(0);
    for (uint8_t i = 0; i < 4; i++) {
      uint64_t bits = _m.dots[i];
      for (uint8_t k = 0; k < muncher::COLS * muncher::ROWS; k++) {
        if (bits & (1ULL << k)) {
          pellet(g, L.digitX[i] + 2 * (k % muncher::COLS), L.digitY[i] + 2 * (k / muncher::COLS), dot);
        }
      }
    }
    // The colon: two power pellets, 4 x 4 without their corners, on the
    // second's beat. The one the power pellet's muncher ate stays away until its
    // scene is over.
    if (_colonOn) {
      for (uint8_t p = 0; p < 2; p++) {
        if (_m.pelletEaten && p == L.powerPellet) { continue; }
        g.fillRect(L.colonX[p] + 1, L.colonY[p], 2, 4, dot);
        g.fillRect(L.colonX[p], L.colonY[p] + 1, 4, 2, dot);
      }
    }
    for (int16_t x = 0; x < L.w; x++) {
      if (!muncher::wallOpen(L, x)) { g.drawPixel(x, L.wallTop, walls); }
      g.drawPixel(x, L.wallBottom, walls);
    }
    for (uint8_t k = 0; k < 32 && 2 + 4 * k < L.w; k++) {
      if (_m.lanePellets & (1UL << k)) { pellet(g, 2 + 4 * k, L.laneY, dot); }
    }
    if (_m.fruitX != muncher::NO_FRUIT) {
      const char *const *rows = FRUITS[_m.fruitKind];
      for (uint8_t r = 0; r < 7; r++) {
        for (uint8_t c = 0; c < 7; c++) {
          if (rows[r][c] != '.') { g.drawPixel(_m.fruitX - 3 + c, L.laneY - 3 + r, fruitColour(rows[r][c], paint)); }
        }
      }
    }
    // The ghosts under the muncher, except one caught: its white flash goes on
    // top, or the muncher standing next to it would hide most of it.
    uint16_t yellow = paint.color(255, 255, 0);
    for (uint8_t pass = 0; pass < 2; pass++) {
      if (pass == 1 && _m.muncherOn()) {
        sprite(g, _m.mouthOpen() ? MUNCH_OPEN : MUNCH_SHUT, _m.mx, _m.my, _m.dx, _m.dy, yellow);
      }
      if (pass == 1 && _m.deathFrame() != 0xFF) {
        sprite(g, MUNCH_FOLD[_m.deathFrame()], _m.mx, _m.my, 1, 0, yellow);
      }
      for (uint8_t gh = 0; gh < muncher::GHOSTS; gh++) {
        const muncher::Ghost &h = _m.ghosts[gh];
        bool caught = h.mode == muncher::GHOST_CAUGHT;
        if (h.mode == muncher::GHOST_OFF || !h.lit() || caught != (pass == 1)) { continue; }
        // A blue one blinks white as the muncher closes in.
        uint16_t body = caught ? eyes
                      : (h.mode == muncher::GHOST_SCARED) ? (_m.scaredBlink(h) ? eyes : wall)
                      : paint.color(GHOST_RGB[gh][0], GHOST_RGB[gh][1], GHOST_RGB[gh][2]);
        sprite(g, GHOST_BODY, h.x, L.laneY, 1, 0, body);
        sprite(g, GHOST_EYES, h.x, L.laneY, 1, 0, eyes);
      }
    }
    if (_m.giantOn()) {
      const uint16_t *rows = _m.giantMouthOpen() ? GIANT_OPEN : GIANT_SHUT;
      int16_t half = muncher::GIANT / 2;
      for (uint8_t r = 0; r < muncher::GIANT; r++) {
        for (uint8_t c = 0; c < muncher::GIANT; c++) {
          if (rows[r] & (1 << (muncher::GIANT - 1 - c))) {
            g.drawPixel(_m.giantX - half + c, L.laneY - half + r, yellow);
          }
        }
      }
    }
    _colonDrawn = _colonOn;
    _dirty = false;
  }

  bool busy(const FaceNow &) const override { return _m.mode != muncher::MODE_IDLE; }

  // The input's scene; the extra now and then; what the web app asks to see -
  // a change (the minutes' ones eaten and laid again, so the time on show
  // stays right), or the scene a new choice of the watchface animation plays.
  // Each now or not at all: until 2026-10-20 the web app's waited for room.
  FaceOutcome request(const FaceNow &now, FaceTrigger why) override {
    bool played;
    switch (why) {
      case TRIGGER_ACTION:
      case TRIGGER_PREVIEW_ANIMATION: played = playScene(now, _s.animation); break;
      case TRIGGER_PREVIEW_CHANGE:    played = playScene(now, MUNCHER_ANIM_REPLAY); break;
      case TRIGGER_NOW_AND_THEN:      played = extra(now); break;
      default:                        return OUTCOME_NONE;
    }
    return played ? OUTCOME_PLAYED : OUTCOME_REFUSED;
  }

  // The span of its change to `minute`: the walk before it, as planned for
  // the next minute or as it would be planned on a copy (muncher.h draws no
  // random numbers), with four ghosts to a full hour; and after a full hour
  // the hour show, walked on a copy. A copy is about 1 KB on the stack, while
  // it is used.
  FaceSpan changeSpan(const FaceNow &now, time_t minute) const override {
    FaceSpan s = { 0, 0 };
    if (!now.set) { return s; }
    bool hour = clockMinute(minute) == 0;
    if (_plannedFor == minute && _plannedDone) {
      s.lead = (unsigned long)_plannedDone * _s.paceMs;
    } else {
      muncher::Face f = _m;
      uint8_t from[4], to[4];
      faceDigits(minute - 60, true, from, muncher::NO_DIGIT);
      faceDigits(minute, true, to, muncher::NO_DIGIT);
      f.show(from);
      s.lead = (unsigned long)f.planChange(to, _s.style, 0, hour ? 4 : 2) * _s.paceMs;
    }
    if (hour) {
      if (_showTurn != _rotation) {
        muncher::Face f = _m;
        f.startHour();
        uint16_t n = 0;
        while (f.mode != muncher::MODE_IDLE && n < 3000) { f.advanceTo(++n); }
        _showSteps = n;
        _showTurn = _rotation;
      }
      s.tail = (unsigned long)_showSteps * _s.paceMs;
    }
    return s;
  }

#if defined(FACE_FRAME_LOG)
  // Test builds: a scene starts with the face afresh, nothing asked for,
  // every rest over.
  void testReset() {
    _rotation = 0xFF;
    _after = AFTER_NOTHING;
    _sceneEndedAt = 0;
  }
#endif

 private:
  // What a scene that ends leads to: the change to a full hour is followed by
  // the hour show. (Until 2026-10-20 a minute change could leave a bonus
  // fruit; the fruit is an extra of its own now, at its drawn time.)
  static const uint8_t AFTER_NOTHING = 0;
  static const uint8_t AFTER_HOUR    = 2;

  void startScene(muncher::Mode mode, unsigned long at, uint8_t after = AFTER_NOTHING) {
    _m.start(mode);
    _startAt = at;
    _plannedFor = 0;
    _doneSteps = 0;   // not a planned minute change: nothing to time
    _after = after;
  }

  // A bonus fruit somewhere along the corridor, of any kind, unless one lies
  // there already.
  void placeFruit() {
    if (_m.fruitX != muncher::NO_FRUIT) { return; }
    int16_t slots = (_m.L.w - 20) / 4 + 1;   // the corridor's pellet places, 10 px off either end
    _m.placeFruit(10 + 4 * (int16_t)faceRandom(slots), (uint8_t)faceRandom(muncher::FRUIT_KINDS));
    _dirty = true;
  }

  // The watchface animation's scene: refused while a scene runs, just
  // after one, and when it could not end before the next change starts.
  bool playScene(const FaceNow &now, uint8_t kind) {
    if (_m.mode != muncher::MODE_IDLE || _m.shown[0] == muncher::NO_DIGIT) { return false; }
    if (now.ms - _sceneEndedAt < FACE_REST_MS) { return false; }
    if (kind == MUNCHER_ANIM_RANDOM) {
      const uint8_t KINDS[3] = { MUNCHER_ANIM_POWER, MUNCHER_ANIM_REPLAY, MUNCHER_ANIM_DEATH };
      kind = KINDS[faceRandom(3)];
    }
    // Only against a planned change: until the face has planned one (it does
    // on its next pass), there is nothing to measure the scene against.
    if (!_m.legsPlanned() || _plannedFor == 0) { return false; }
    // How long it takes, exactly: planned and walked to its end on a copy -
    // muncher.h draws no random numbers, so the copy goes as the scene will
    // (K3 of the face refactor: until 2026-10-20 it was the walk plus a margin
    // of 60 to 140 steps). Refused, the copy is dropped, and the change's plan
    // stays as it was; until then a refused scene had taken its place.
    const char *what;
    muncher::Mode mode;
    uint16_t steps = 0;
    {
      muncher::Face f = _m;   // about 1 KB on the stack, only while it is walked
      mode = planScene(f, kind, what);
      f.start(mode);
      while (f.mode != muncher::MODE_IDLE && steps < 3000) { f.advanceTo(++steps); }
    }
    if (!faceFits(_startAt, now.ms, (unsigned long)steps * _s.paceMs)) { return false; }
    planScene(_m, kind, what);           // the same plan, now for the face
    startScene(mode, now.ms);
    Serial.printf("Muncher: %s\n", what);
    return true;
  }

  // Plan the scene of `kind` on `f`: its mode, and what the console calls it.
  static muncher::Mode planScene(muncher::Face &f, uint8_t kind, const char *&what) {
    if (kind == MUNCHER_ANIM_POWER) {
      f.planPower();
      what = "the power pellet";
      return muncher::MODE_POWER;
    }
    if (kind == MUNCHER_ANIM_DEATH) {
      f.planDeath();
      what = "the ghosts win";
      return muncher::MODE_DEATH;
    }
    uint8_t same[4];
    memcpy(same, f.shown, 4);
    f.planChange(same, muncher::STYLE_ALL, 1 << 3);
    what = "the minute's ones again";
    return muncher::MODE_CHANGE;
  }

  // The extra now and then: a bonus fruit in the corridor, which lies still
  // until the next change eats it. None while a scene runs or a fruit lies
  // there already.
  bool extra(const FaceNow &) {
    if (_m.mode != muncher::MODE_IDLE || _m.shown[0] == muncher::NO_DIGIT) { return false; }
    if (_m.fruitX != muncher::NO_FRUIT) { return false; }
    placeFruit();
    Serial.println("Muncher: a bonus fruit (now and then)");
    return true;
  }

  // Nothing moving: put a time that moved on without a change right (a time
  // set by hand, a sync that corrected it, a minute the face missed), plan
  // the next change once per minute, and start it when its moment has come.
  void idle(const FaceNow &now, const uint8_t d[4]) {
    if (memcmp(_m.shown, d, 4) != 0) {
      if (_m.shown[0] == muncher::NO_DIGIT || d[0] == muncher::NO_DIGIT) {
        _m.show(d);                      // dashes come and go without a muncher
        _dirty = true;
      } else if (_m.planChange(d, _s.style)) {
        startScene(muncher::MODE_CHANGE, now.ms);
      }
      _plannedFor = 0;
      return;
    }
    if (d[0] == muncher::NO_DIGIT) { return; }
    // The next change first: an animation asked for is measured against it.
    time_t next = nextMinute(now);
    if (_plannedFor != next) {
      // To a full hour four ghosts follow the muncher, and the hour show
      // comes after.
      uint8_t to[4];
      faceDigits(next, now.set, to, muncher::NO_DIGIT);
      bool hour = clockMinute(next) == 0;
      _plannedDone = _m.planChange(to, _s.style, 0, hour ? 4 : 2);
      _plannedAfter = hour ? AFTER_HOUR : AFTER_NOTHING;
      _plannedFor = next;
    }
    // The start, worked out anew on every pass until the change begins; a
    // sync that moves the second, or a new pace, is followed the same way.
    _startAt = nextMinuteAt(now) - (unsigned long)_plannedDone * _s.paceMs;
    // Started at its planned moment even when that has passed (the face was
    // away): stepped from there, the muncher is where it ought to be by now.
    if (_m.legsPlanned() && (long)(now.ms - _startAt) >= 0) {
      _m.start(muncher::MODE_CHANGE);
      _doneSteps = _plannedDone;
      _after = _plannedAfter;
      _plannedFor = 0;
    }
  }

  // A scene is over: what it leads to. After the change to a full hour, the
  // hour show.
  void sceneEnded(const FaceNow &now) {
    uint8_t after = _after;
    _after = AFTER_NOTHING;
    _sceneEndedAt = now.ms;
    _dirty = true;
    if (after == AFTER_HOUR) {
      _m.startHour();
      _startAt = now.ms;
      _plannedFor = 0;
      Serial.println("Muncher: the hour show");
    }
  }

  // A sprite of 5 x 5 drawn round its middle and turned to face (dx, dy); as
  // drawn it faces right.
  void sprite(Adafruit_GFX &g, const uint8_t rows[5], int16_t cx, int16_t cy, int8_t dx, int8_t dy,
              uint16_t colour) const {
    for (uint8_t r = 0; r < 5; r++) {
      for (uint8_t c = 0; c < 5; c++) {
        uint8_t sr = r, sc = c;
        if (dx < 0)      { sc = 4 - c; }
        else if (dy < 0) { sr = c; sc = 4 - r; }
        else if (dy > 0) { sr = c; sc = r; }
        if (rows[sr] & (0x10 >> sc)) { g.drawPixel(cx - 2 + c, cy - 2 + r, colour); }
      }
    }
  }

  // One pellet at (x, y), with the LEDs the setting adds to it.
  void pellet(Adafruit_GFX &g, int16_t x, int16_t y, uint16_t colour) const {
    uint8_t p = _s.pellets;
    g.drawPixel(x, y, colour);
    if (p == MUNCHER_PELLETS_WIDE || p == MUNCHER_PELLETS_SOLID) { g.drawPixel(x + 1, y, colour); }
    if (p == MUNCHER_PELLETS_TALL || p == MUNCHER_PELLETS_SOLID) { g.drawPixel(x, y + 1, colour); }
    if (p == MUNCHER_PELLETS_SOLID)                              { g.drawPixel(x + 1, y + 1, colour); }
  }

  static uint16_t fruitColour(char c, const FacePaint &paint) {
    switch (c) {
      case 'R': return paint.color(255, 0, 0);
      case 'G': return paint.color(0, 200, 0);
      case 'W': return paint.color(255, 255, 255);
      case 'O': return paint.color(255, 140, 0);
      case 'B': return paint.color(150, 75, 0);
      default:  return 0;
    }
  }

  const MuncherSettings &_s;
  muncher::Face _m;
  uint16_t _plannedDone  = 0;   // the step the planned minute change completes the time at
  uint16_t _doneSteps    = 0;   // the same for the running one, until it is logged; 0 = none
  uint8_t  _plannedAfter = AFTER_NOTHING;   // what the planned change leads to
  uint8_t  _after        = AFTER_NOTHING;   // ... and the running scene
  uint16_t _fruitsLogged = 0;   // bonus fruits eaten, as far as the console knows
  bool     _colonOn      = false;
  bool     _colonDrawn   = false;
  // The hour show's steps, kept for the layout they were walked on: the
  // rules for the panel ask on every pass of a minute before a full hour.
  mutable uint8_t  _showTurn  = 0xFE;   // none walked yet (0xFF is a layout still to be made)
  mutable uint16_t _showSteps = 0;
};

#endif  // FACE_MUNCHER_H
