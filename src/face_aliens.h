// The Aliens watchface (src/aliens.h, concept doc "Aliens: draft"): HH:MM of
// blocks; at a change the blocks the change takes wake as aliens, the cannon
// shoots them, and the new ones march in. Planned backwards from the minute
// (PlannedFace) as the Muncher's change; to a full hour the whole time goes as
// a wave. Between changes the blocks stand still, but for the scenes now and
// then and those an input asks for.
#ifndef FACE_ALIENS_H
#define FACE_ALIENS_H

#include <Adafruit_GFX.h>
#include <new>
#include "face_planned.h"
#include "aliens.h"

// Its settings are in a blob of their own (NVS "aliens"), as the Muncher's are.
#define ALIENS_SETTINGS_MAGIC 0xA11E
#define ALIENS_SETTINGS_REV   4   // 1 = the trial build with four looks, 2 = without them, 3 = adds the animation (akn), 4 = nap

// The digits are blocks of 3 x 3 with a gap, and only the blocks a change
// takes wake as aliens: digits of aliens could not be read as a time (Peter,
// 2026-10-11, after small aliens, wide aliens and solid cells on the panel).
// White aliens with a green cannon, as the arcade's overlay, or the rows in
// colours of their own.
const uint8_t ALIENS_COLOURS_ARCADE = 0;
const uint8_t ALIENS_COLOURS_ROWS   = 1;
const uint8_t ALIENS_COLOURS_COUNT  = 2;
// What the watchface animation plays: the input action "trigger watchface
// animation", the knock by default.
const uint8_t ALIENS_ANIM_REPLAY = 0;   // the minutes' ones shot and marching in again
const uint8_t ALIENS_ANIM_SAUCER = 1;   // a saucer, shot by the cannon
const uint8_t ALIENS_ANIM_BACK   = 2;   // an alien shoots back and hits the cannon
const uint8_t ALIENS_ANIM_RANDOM = 3;   // one of the others, drawn each time
const uint8_t ALIENS_ANIM_COUNT  = 4;
// Not one the watchface animation picks: a block waking for a moment, one of the extras that
// come now and then at the pace the faces share when its option is on
// (Peter, 2026-10-12).
const uint8_t ALIENS_EXTRA_NAP = 4;

struct AliensSettings {
  uint16_t magic;
  uint8_t  rev;
  uint8_t  style;     // aliens::STYLE_*: how a changing digit goes
  uint8_t  colours;   // ALIENS_COLOURS_*
  uint8_t  animation;   // rev 3: ALIENS_ANIM_*, what the watchface animation plays (was a spare byte, 0)
  uint8_t  nap;       // rev 4: 1 = a block wakes for a moment now and then (was a spare byte, 0)
  uint8_t  spare[1];
};
static_assert(sizeof(AliensSettings) == 8, "the stored blob keeps its size");

const AliensSettings ALIENS_DEFAULTS = {
  ALIENS_SETTINGS_MAGIC, ALIENS_SETTINGS_REV, aliens::STYLE_MIXED, ALIENS_COLOURS_ARCADE, ALIENS_ANIM_RANDOM, 1, { 0 }
};

// The Aliens settings for the web app and the store (face.h).
const char *const ALIENS_AST_LABELS[] = { "Mixed", "Whole digit", "Only the difference" };                     // aliens::STYLE_*
const char *const ALIENS_ACOL_LABELS[] = { "White", "Coloured rows" };                                         // ALIENS_COLOURS_*
const char *const ALIENS_AKN_LABELS[] = { "Last digit again", "Saucer", "Aliens shoot back", "Random each time" };   // ALIENS_ANIM_*
const char *const ALIENS_ANAP_LABELS[] = { "Off", "Now and then" };
static_assert(sizeof(ALIENS_AST_LABELS) / sizeof(ALIENS_AST_LABELS[0]) == aliens::STYLE_COUNT, "a name per change style");
static_assert(sizeof(ALIENS_ACOL_LABELS) / sizeof(ALIENS_ACOL_LABELS[0]) == ALIENS_COLOURS_COUNT, "a name per colouring");
static_assert(sizeof(ALIENS_AKN_LABELS) / sizeof(ALIENS_AKN_LABELS[0]) == ALIENS_ANIM_COUNT, "a name per scene");
const SettingParam ALIENS_PARAMS[] = {
  { "ast",  SETTING_FIELD(AliensSettings, style),     PARAM_CHOICE, PARAM_LIVE,                0, aliens::STYLE_COUNT - 1,  LOAD_DEFAULT, ALIENS_AST_LABELS },
  { "acol", SETTING_FIELD(AliensSettings, colours),   PARAM_CHOICE, PARAM_LIVE | PARAM_REDRAW, 0, ALIENS_COLOURS_COUNT - 1, LOAD_DEFAULT, ALIENS_ACOL_LABELS },
  { "akn",  SETTING_FIELD(AliensSettings, animation), PARAM_CHOICE, PARAM_LIVE | PARAM_PLAYS,  0, ALIENS_ANIM_COUNT - 1,    LOAD_DEFAULT, ALIENS_AKN_LABELS },
  { "anap", SETTING_FIELD(AliensSettings, nap),       PARAM_FLAG,   PARAM_LIVE,                0, 1,                        LOAD_BOOL,    ALIENS_ANAP_LABELS },
};

// An older blob: the trial build's (rev 1: look, style, idle, colours after
// the rev) keeps its change and its colours; rev 2 gets the animation's default,
// rev 3 the waking blocks'.
inline void aliensMigrate(uint8_t *data, uint8_t rev) {
  AliensSettings &a = *(AliensSettings *)data;
  if (rev < 2) {
    AliensSettings was = ALIENS_DEFAULTS;
    was.style = data[4];
    was.colours = data[6];
    a = was;
  } else if (rev < 3) {
    a.animation = ALIENS_DEFAULTS.animation;
    a.nap = ALIENS_DEFAULTS.nap;
  } else if (rev < 4) {
    a.nap = ALIENS_DEFAULTS.nap;
  }
}

// The sprites, '#' lit; the alien in two frames, so it marches, and the hit
// cannon's wreck in two, so it flickers.
const char *const ALIEN_SMALL[2][3] = { { "#.#", "###", "#.#" }, { ".#.", "###", "#.#" } };
const char *const ALIEN_CANNON[3]   = { "..#..", ".###.", "#####" };
const char *const ALIEN_WRECK[2][3] = { { "#...#", ".#.#.", "#.#.#" }, { ".#.#.", "#...#", ".###." } };
const char *const ALIEN_BURST[3]    = { "#.#", ".#.", "#.#" };
const char *const ALIEN_SAUCER[3]   = { "..###..", ".#####.", "#.#.#.#" };
const char *const ALIEN_SAUCER_BURST[2][3] = { { "#..#..#", ".#.#.#.", "#.#.#.#" }, { ".#.#.#.", "#.###.#", ".#...#." } };
const uint8_t ALIEN_ROW_RGB[aliens::ROWS][3] = {   // the colourful rows: magenta, cyan, green
  { 255, 60, 200 }, { 255, 60, 200 }, { 60, 220, 255 }, { 60, 220, 255 }, { 40, 230, 60 },
};

class AliensFace : public PlannedFace {
 public:
  explicit AliensFace(const AliensSettings &s) : _s(s) {}

  // One pass: it reports a change only when what it shows has changed (a step
  // of a scene, the aliens' march, the colon's beat), as the others.
  bool update(const FaceNow &now) override {
    if (turned(now)) {
      _a.setLayout(now.turn == 0 || now.turn == 2 ? aliens::LANDSCAPE : aliens::PORTRAIT);
      uint8_t d[4];
      faceDigits(now.local, now.set, d, aliens::NO_DIGIT);
      _a.show(d);
      _plannedFor = 0;
      _dirty = true;
    }
    uint32_t sceneMs = 0;
    if (_a.running) {
      long since = (long)(now.ms - _startAt);
      if (since >= 0) {
        sceneMs = (uint32_t)since;
        _a.at(sceneMs);
        if (_logStand && sceneMs >= _a.standMs) {
          logStands("Aliens", now);
          _logStand = false;
        }
        if (!_a.running) { _sceneEndedAt = now.ms; _dirty = true; }
      }
    }
    if (!_a.running) {
      uint8_t d[4];
      faceDigits(now.local, now.set, d, aliens::NO_DIGIT);
      idle(now, d);
    }
    _colonOn = (now.local % 2) == 0;
    if (!_a.running) { sceneMs = 0; }
    _sceneMs = sceneMs;
    // What the picture depends on, folded into one number (FNV-1a).
    uint32_t key = 2166136261u;
    auto fold = [&key](uint32_t v) { for (uint8_t b = 0; b < 4; b++) { key = (key ^ (uint8_t)(v >> (8 * b))) * 16777619u; } };
    const aliens::Face &f = _a;
    fold(f.running); fold((uint16_t)f.cannonX | (uint32_t)f.cannonOn << 16 | (uint32_t)f.wreckOn << 17 | (uint32_t)f.wreckFrame << 18);
    fold(f.shotOn ? (uint16_t)f.shotY : 0xFFFF); fold(f.bombOn ? (uint16_t)f.bombY : 0xFFFF);
    fold(f.burstOn ? (uint16_t)f.burstX | (uint32_t)(uint16_t)f.burstY << 16 : 0xFFFFFFFF);
    fold(f.saucerOn ? (uint16_t)f.saucerX : 0xFFFF); fold((uint8_t)f.arriveRow | (uint16_t)(uint8_t)f.drop << 8);
    fold(f.burstWide ? sceneMs / aliens::BLINK_STEP_MS % 2 : 2);   // the saucer's burst flickers
    for (uint8_t i = 0; i < 4; i++) { fold(f.cells[i]); fold(f.awake[i]); fold(f.arriving[i]); }
    fold(_colonOn); fold(sceneMs / aliens::MARCH_MS % 4);   // the march: the legs' frame and the sway
    _key = key;
    return key != _drawnKey || _dirty;
  }

  // The panel at the pass's ms into the running scene (0 between scenes); the
  // woken aliens step their legs and sway - the wave lower by the rows it has
  // stepped down - the arriving ones only step.
  void draw(Adafruit_GFX &g, const FaceNow &, const FacePaint &paint) override {
    const aliens::Layout &L = _a.L;
    uint32_t sceneMs = _sceneMs;
    uint8_t frame = aliens::Face::marchFrame(sceneMs);
    g.fillScreen(0);
    for (uint8_t i = 0; i < 4; i++) {
      for (uint8_t k = 0; k < aliens::COLS * aliens::ROWS; k++) {
        uint8_t r = k / aliens::COLS, c = k % aliens::COLS;
        int16_t x = L.digitX[i] + aliens::CELL * c;
        if (_a.cells[i] & (1 << k)) {
          bool awake = _a.running && (_a.awake[i] & (1 << k));
          int16_t dx = awake ? _a.sway(i, k, sceneMs) : 0;
          int16_t dy = awake ? aliens::CELL * _a.drop : 0;
          cell(g, x + dx, L.digitY[i] + aliens::CELL * r + dy, r, c, awake, frame, colour(r, paint));
        }
        if (_a.arriving[i] & (1 << k)) {
          // On the way down; behind a block that stays where it would cover one.
          int16_t y = L.digitY[i] + aliens::CELL * ((int8_t)r + _a.arriveRow);
          if (_a.blockAt(x, y)) { continue; }
          cell(g, x, y, r, c, true, frame, colour(r, paint));
        }
      }
    }
    uint16_t white = paint.color(235, 235, 235), orange = paint.color(255, 140, 0);
    if (_colonOn) {
      for (uint8_t p = 0; p < 2; p++) { g.fillRect(L.colonX[p], L.colonY[p], 2, 2, white); }
    }
    if (_a.saucerOn) { art(g, ALIEN_SAUCER, 3, _a.saucerX, 0, paint.color(255, 30, 60)); }
    if (_a.cannonOn) { art(g, ALIEN_CANNON, 3, _a.cannonX, L.cannonY, paint.color(40, 230, 60)); }
    if (_a.wreckOn) {
      art(g, ALIEN_WRECK[_a.wreckFrame], 3, _a.cannonX, L.cannonY, _a.wreckFrame ? orange : paint.color(40, 230, 60));
    }
    if (_a.shotOn) {
      g.drawPixel(_a.shotX, _a.shotY, white);
      g.drawPixel(_a.shotX, _a.shotY + 1, white);
    }
    if (_a.bombOn) {
      uint16_t yellow = paint.color(255, 230, 0);
      g.drawPixel(_a.bombX, _a.bombY, yellow);
      g.drawPixel(_a.bombX, _a.bombY + 1, yellow);
    }
    if (_a.burstOn && _a.burstWide) {   // the saucer's, flickering in two frames
      uint8_t b = (uint8_t)((sceneMs / aliens::BLINK_STEP_MS) & 1);
      art(g, ALIEN_SAUCER_BURST[b], 3, _a.burstX, _a.burstY, b ? paint.color(255, 30, 60) : orange);
    } else if (_a.burstOn) {
      art(g, ALIEN_BURST, 3, _a.burstX, _a.burstY, orange);
    }
    _drawnKey = _key;
    _dirty = false;
  }

  bool busy(const FaceNow &) const override { return _a.running; }

  // The input's scene; the extra now and then, measured by its own length
  // against the next change; what the web app asks to see - a change (the
  // minutes' ones shot and marching in again), or the scene a new choice of
  // the watchface animation plays. Each now or not at all: until 2026-10-20
  // the web app's waited for room. One refused has taken the face's plan, and
  // the change is planned anew on the face's next pass.
  FaceOutcome request(const FaceNow &now, FaceTrigger why) override {
    bool played;
    switch (why) {
      case TRIGGER_ACTION:            played = scene(now, _s.animation, "a knock", true); break;
      case TRIGGER_NOW_AND_THEN:      played = scene(now, extraKind(), "now and then", false); break;
      case TRIGGER_PREVIEW_CHANGE:    played = scene(now, ALIENS_ANIM_REPLAY, "the web app's preview", true); break;
      case TRIGGER_PREVIEW_ANIMATION: played = scene(now, _s.animation, "the web app's choice", true); break;
      default:                        return OUTCOME_NONE;
    }
    return played ? OUTCOME_PLAYED : OUTCOME_REFUSED;
  }

  // The span of its change to `minute`: the march before it, the full hour's
  // wave included, as planned for the next minute or as it would be planned
  // on a copy - 2.8 KB, so on the heap, only while it is used; without one,
  // the guesses of before (15 s, the wave 17 s). The new time stands as the
  // minute begins.
  FaceSpan changeSpan(const FaceNow &now, time_t minute) const override {
    FaceSpan s = { 0, 0 };
    if (!now.set) { return s; }
    bool hour = clockMinute(minute) == 0;
    if (_plannedFor == minute && _plannedStand) {
      s.lead = _plannedStand;
      return s;
    }
    aliens::Face *f = new (std::nothrow) aliens::Face(_a);
    if (!f) {
      s.lead = hour ? 17000UL : 15000UL;
      return s;
    }
    uint8_t from[4], to[4];
    faceDigits(minute - 60, true, from, aliens::NO_DIGIT);
    faceDigits(minute, true, to, aliens::NO_DIGIT);
    f->show(from);
    s.lead = f->planChange(to, _s.style, 0, hour);
    delete f;
    return s;
  }

#if defined(FACE_FRAME_LOG)
  // Test builds: a scene starts with the face afresh, nothing asked for,
  // every rest over.
  void testReset() {
    _rotation = 0xFF;
    _sceneEndedAt = 0;
  }
#endif

 private:

  // An alien that can shoot back, drawn at random: a digit and a column with
  // a block, the lowest of which drops the bomb. Returns the scene's length.
  uint32_t planBack() {
    uint8_t first = (uint8_t)faceRandom(4 * aliens::COLS);
    for (uint8_t n = 0; n < 4 * aliens::COLS; n++) {
      uint8_t o = (first + n) % (4 * aliens::COLS);
      uint32_t len = _a.planBack(o / aliens::COLS, o % aliens::COLS);
      if (len) { return len; }
    }
    return 0;
  }

  // A block that wakes for a moment, drawn at random among the lit ones.
  // Returns the scene's length.
  uint32_t planNap() {
    const uint8_t cells = aliens::COLS * aliens::ROWS;
    uint8_t first = (uint8_t)faceRandom(4 * cells);
    for (uint8_t n = 0; n < 4 * cells; n++) {
      uint8_t o = (first + n) % (4 * cells);
      uint32_t len = _a.planNap(o / cells, o % cells);
      if (len) { return len; }
    }
    return 0;
  }

  // The extra of this time: a saucer, an alien shooting back, or - with the
  // option on - a block waking for a moment.
  uint8_t extraKind() {
    uint8_t k = (uint8_t)faceRandom(_s.nap ? 3 : 2);
    return k == 0 ? ALIENS_ANIM_SAUCER : k == 1 ? ALIENS_ANIM_BACK : ALIENS_EXTRA_NAP;
  }

  // A scene between changes - the minutes' ones shot and marching in again
  // (the time on show stays right), a saucer, an alien shooting back, a block
  // waking - for the watchface animation, the web app, and now and then. Refused while a scene
  // runs, just after one, and when it could not end before the next change
  // starts: then it is dropped, not kept. One the user asked for must only
  // have ended when the change starts; an extra the clock plays of itself
  // FACE_GUARD_MS before (Peter, 2026-10-22: what the user asks for is
  // granted unless there is a clear reason - and a scene still running when
  // the change has to start would be cut off).
  bool scene(const FaceNow &now, uint8_t kind, const char *why, bool asked) {
    if (_a.running || _a.shown[0] == aliens::NO_DIGIT) { return false; }
    if (now.ms - _sceneEndedAt < FACE_REST_MS) { return false; }
    if (_plannedFor == 0) { return false; }   // the next change is planned on the face's next pass
    if (kind == ALIENS_ANIM_RANDOM) {
      const uint8_t KINDS[3] = { ALIENS_ANIM_REPLAY, ALIENS_ANIM_SAUCER, ALIENS_ANIM_BACK };
      kind = KINDS[faceRandom(3)];
    }
    unsigned long plannedStart = _startAt;
    uint32_t len;
    const char *what;
    if (kind == ALIENS_ANIM_SAUCER) {
      len = _a.planSaucer(faceRandom(2) == 0, (uint8_t)faceRandom(3));
      what = "a saucer";
    } else if (kind == ALIENS_ANIM_BACK) {
      len = planBack();
      what = "an alien shoots back";
    } else if (kind == ALIENS_EXTRA_NAP) {
      len = planNap();
      what = "a block wakes";
    } else {
      uint8_t same[4];
      memcpy(same, _a.shown, 4);
      len = _a.planChange(same, aliens::STYLE_ALL, 1 << 3) ? _a.endMs : 0;
      what = "the minutes' ones again";
    }
    _plannedFor = 0;                          // the change is planned anew afterwards
    if (len == 0 || !faceFits(plannedStart, now.ms, len, asked ? 0 : FACE_GUARD_MS)) { return false; }
    _startAt = now.ms;
    _logStand = false;
    _a.start();
    Console.printf("Aliens: %s (%s)\n", what, why);
    return true;
  }

  // Nothing moving: put a time right that moved on without a change, plan the
  // next change once a minute - to a full hour the wave - and start it when
  // its moment has come. In between, what the web app asks for.
  void idle(const FaceNow &now, const uint8_t d[4]) {
    if (memcmp(_a.shown, d, 4) != 0) {
      if (_a.shown[0] == aliens::NO_DIGIT || d[0] == aliens::NO_DIGIT) {
        _a.show(d);                           // dashes come and go without a fight
        _dirty = true;
      } else if (_a.planChange(d, _s.style)) {
        _startAt = now.ms;
        _logStand = false;
        _a.start();
      }
      _plannedFor = 0;
      return;
    }
    if (d[0] == aliens::NO_DIGIT) { return; }
    time_t next = nextMinute(now);
    if (_plannedFor != next) {
      uint8_t to[4];
      faceDigits(next, now.set, to, aliens::NO_DIGIT);
      _plannedStand = _a.planChange(to, _s.style, 0, clockMinute(next) == 0);
      _plannedFor = next;
    }
    _startAt = nextMinuteAt(now) - _plannedStand;
    // Started at its planned moment even when that has passed (the face was
    // away): from there, everything is where it ought to be by now.
    if (_plannedStand && (long)(now.ms - _startAt) >= 0) {
      _logStand = true;
      _plannedFor = 0;
      _a.start();
      if (_a.scene == aliens::SCENE_WAVE) { Console.println("Aliens: the hour's wave"); }
    }
  }

  uint16_t colour(uint8_t row, const FacePaint &paint) const {
    if (_s.colours == ALIENS_COLOURS_ROWS) {
      return paint.color(ALIEN_ROW_RGB[row][0], ALIEN_ROW_RGB[row][1], ALIEN_ROW_RGB[row][2]);
    }
    return paint.color(235, 235, 235);
  }

  // A sprite's '#' pixels at (x, y); the panel clips what lies off it.
  static void art(Adafruit_GFX &g, const char *const *rows, uint8_t n, int16_t x, int16_t y, uint16_t colour) {
    for (uint8_t r = 0; r < n; r++) {
      for (uint8_t c = 0; rows[r][c]; c++) {
        if (rows[r][c] == '#') { g.drawPixel(x + c, y + r, colour); }
      }
    }
  }

  // A cell at (x, y): an alien in its march frame, or a block.
  static void cell(Adafruit_GFX &g, int16_t x, int16_t y, uint8_t r, uint8_t c, bool alien, uint8_t frame,
                   uint16_t colour) {
    if (alien) { art(g, ALIEN_SMALL[(frame + r + c) & 1], 3, x, y, colour); }
    else       { g.fillRect(x, y, aliens::CELL - 1, aliens::CELL - 1, colour); }
  }

  const AliensSettings &_s;
  aliens::Face _a;
  uint32_t _plannedStand = 0;      // ms from the planned change's start until the new time stands
  bool     _logStand     = false;  // the running scene is the time's change: log when it stands
  uint32_t _sceneMs      = 0;      // ms into the running scene in this pass, 0 between scenes
  bool     _colonOn      = false;
  uint32_t _key          = 0;      // what the picture depends on in this pass
  uint32_t _drawnKey     = 0;      // ... and in the frame drawn last, to repaint only on a change
};

#endif  // FACE_ALIENS_H
