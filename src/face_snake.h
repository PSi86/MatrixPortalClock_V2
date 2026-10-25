// The Snake watchface (src/snake.h, concept doc tab "Next game faces"): each
// digit of HH:MM is a snake lying in its digit's shape. At a change the old
// snake crawls out over the panel's nearer side and a new one comes in from
// the same side and eats the dots on the new digit, growing into it; to a
// full hour all four go, as one train out to the left or each to its side
// at once. Planned backwards from the minute (PlannedFace) as the Muncher's
// and the Aliens' changes. Between changes the snakes lie still, but for the
// scenes now and then and those an input asks for.
#ifndef FACE_SNAKE_H
#define FACE_SNAKE_H

#include <Adafruit_GFX.h>
#include <new>
#include "face_planned.h"
#include "snake.h"

// Its settings are in a blob of their own (NVS "snake"), as the other faces' are.
#define SNAKE_SETTINGS_MAGIC 0x5AE1
#define SNAKE_SETTINGS_REV   5   // the first release's; 1-4 were the builds of 2026-10-23 before it, not carried over

// What the watchface animation plays: the input action "trigger watchface
// animation", the knock by default.
const uint8_t SNAKE_ANIM_CHASE  = 0;   // a snake crawls round its own digit once
const uint8_t SNAKE_ANIM_APPLE  = 1;   // a head strikes at an apple in the lane below it
const uint8_t SNAKE_ANIM_REPLAY = 2;   // the minutes' ones crawl away and come anew
const uint8_t SNAKE_ANIM_RANDOM = 3;   // one of the others, drawn each time
const uint8_t SNAKE_ANIM_TONGUE = 4;   // a head flicks its tongue (Peter, 2026-10-24: a choice of its own; after
                                       // random, so that stored values keep their meaning)
const uint8_t SNAKE_ANIM_COUNT  = 5;
// (Now and then, at the pace the faces share, the tongue or the tail chase plays.)

// What the change to a full hour plays (Peter, 2026-10-23: both, to compare).
const uint8_t SNAKE_HOUR_TRAIN   = 0;  // all four out to the left as one train, the new four behind it
const uint8_t SNAKE_HOUR_AT_ONCE = 1;  // all four at once, each to its own side
const uint8_t SNAKE_HOUR_RANDOM  = 2;  // one of the two, drawn each hour
const uint8_t SNAKE_HOUR_COUNT   = 3;

// The snakes' colours (Peter, 2026-10-23). In shades of one green the solid
// look did not show how a snake moves - its coils touch and look alike; a
// pattern of strong contrast that lies on the body moves with it, so its
// stripes run along the coils as the snake crawls. After a round of fixed
// colourings, one way for all of them: a pattern and the colours of one's
// own - colour 1 alone, stripes of colour 1 and 2 as wide as set, or a
// gradient from colour 1 at the head to colour 2 at the tail - and three
// fixed patterns with more colours than two: the rainbow, coral and a run
// from yellow through green and cyan to blue. The head has a colour of its
// own. (Tried and gone: a colour per digit, green and black stripes - black
// is off, the time could not be read.)
const uint8_t SNAKE_PATTERN_ONE      = 0;   // colour 1
const uint8_t SNAKE_PATTERN_STRIPES  = 1;   // colour 1 and colour 2, each as many cells as its width
const uint8_t SNAKE_PATTERN_GRADIENT = 2;   // colour 1 at the head to colour 2 at the tail
const uint8_t SNAKE_PATTERN_RAINBOW  = 3;   // seven colours, each band as wide as width 1
const uint8_t SNAKE_PATTERN_CORAL    = 4;   // red, grey, yellow, grey, each band as wide as width 1
const uint8_t SNAKE_PATTERN_RUN      = 5;   // yellow at the head, then green and cyan, blue at the tail
const uint8_t SNAKE_PATTERN_COUNT    = 6;
const uint8_t SNAKE_WIDTH_MAX        = 8;   // cells a stripe or band is at most

struct SnakeSettings {
  uint16_t magic;
  uint8_t  rev;
  uint8_t  look;        // snake::Look
  uint8_t  animation;   // SNAKE_ANIM_*
  uint8_t  hour;        // SNAKE_HOUR_*
  uint8_t  pattern;     // SNAKE_PATTERN_*
  uint8_t  colourA[3];  // colour 1
  uint8_t  colourB[3];  // colour 2
  uint8_t  widthA;      // cells of colour 1 in the stripes, and of each band of the rainbow and coral
  uint8_t  widthB;      // cells of colour 2 in the stripes
  uint8_t  head[3];     // the head's colour
  uint8_t  digits;      // snake::Digits
  uint8_t  spare;
};
static_assert(sizeof(SnakeSettings) == 20, "the stored blob keeps its size");

// Spaced digits and solid snakes by default on both clocks (Peter, 2026-10-24).
const SnakeSettings SNAKE_DEFAULTS = {
  SNAKE_SETTINGS_MAGIC, SNAKE_SETTINGS_REV, snake::LOOK_SOLID, SNAKE_ANIM_RANDOM, SNAKE_HOUR_RANDOM, SNAKE_PATTERN_ONE,
  { 40, 220, 60 }, { 255, 220, 0 }, 2, 2, { 255, 255, 255 }, snake::DIGITS_SPACED, 0
};

// The Snake settings for the web app and the store (face.h).
const char *const SNAKE_SLK_LABELS[] = { "Solid", "Line" };                                             // snake::Look
const char *const SNAKE_SKN_LABELS[] = { "Tail chase", "Apple", "Last digit again", "Random each time", "Tongue" };   // SNAKE_ANIM_*
const char *const SNAKE_SHR_LABELS[] = { "Train", "All four at once", "Random each hour" };              // SNAKE_HOUR_*
const char *const SNAKE_SCOL_LABELS[] = { "One colour", "Stripes", "Gradient", "Rainbow", "Coral", "Head to tail" };   // SNAKE_PATTERN_*
const char *const SNAKE_SDG_LABELS[] = { "Coils", "Lines", "Narrow lines", "Spaced" };   // snake::Digits
static_assert(sizeof(SNAKE_SLK_LABELS) / sizeof(SNAKE_SLK_LABELS[0]) == snake::LOOK_COUNT, "a name per look");
static_assert(sizeof(SNAKE_SKN_LABELS) / sizeof(SNAKE_SKN_LABELS[0]) == SNAKE_ANIM_COUNT, "a name per scene");
static_assert(sizeof(SNAKE_SHR_LABELS) / sizeof(SNAKE_SHR_LABELS[0]) == SNAKE_HOUR_COUNT, "a name per hour change");
static_assert(sizeof(SNAKE_SCOL_LABELS) / sizeof(SNAKE_SCOL_LABELS[0]) == SNAKE_PATTERN_COUNT, "a name per pattern");
static_assert(sizeof(SNAKE_SDG_LABELS) / sizeof(SNAKE_SDG_LABELS[0]) == snake::DIGITS_COUNT, "a name per family of digits");
const SettingParam SNAKE_PARAMS[] = {
  { "slk",  SETTING_FIELD(SnakeSettings, look),      PARAM_CHOICE, PARAM_LIVE | PARAM_REDRAW, 0, snake::LOOK_COUNT - 1,   LOAD_DEFAULT, SNAKE_SLK_LABELS },
  { "sdg",  SETTING_FIELD(SnakeSettings, digits),    PARAM_CHOICE, PARAM_LIVE | PARAM_REDRAW, 0, snake::DIGITS_COUNT - 1, LOAD_DEFAULT, SNAKE_SDG_LABELS },
  { "scol", SETTING_FIELD(SnakeSettings, pattern),   PARAM_CHOICE, PARAM_LIVE | PARAM_REDRAW, 0, SNAKE_PATTERN_COUNT - 1, LOAD_DEFAULT, SNAKE_SCOL_LABELS },
  { "sca",  SETTING_FIELD(SnakeSettings, colourA),   PARAM_COLOUR, PARAM_LIVE | PARAM_REDRAW, 0, 0,                       LOAD_KEEP,    nullptr },
  { "scb",  SETTING_FIELD(SnakeSettings, colourB),   PARAM_COLOUR, PARAM_LIVE | PARAM_REDRAW, 0, 0,                       LOAD_KEEP,    nullptr },
  { "scw",  SETTING_FIELD(SnakeSettings, widthA),    PARAM_RANGE,  PARAM_LIVE | PARAM_REDRAW, 1, SNAKE_WIDTH_MAX,         LOAD_CLAMP,   nullptr },
  { "scv",  SETTING_FIELD(SnakeSettings, widthB),    PARAM_RANGE,  PARAM_LIVE | PARAM_REDRAW, 1, SNAKE_WIDTH_MAX,         LOAD_CLAMP,   nullptr },
  { "sch",  SETTING_FIELD(SnakeSettings, head),      PARAM_COLOUR, PARAM_LIVE | PARAM_REDRAW, 0, 0,                       LOAD_KEEP,    nullptr },
  { "skn",  SETTING_FIELD(SnakeSettings, animation), PARAM_CHOICE, PARAM_LIVE | PARAM_PLAYS,  0, SNAKE_ANIM_COUNT - 1,    LOAD_DEFAULT, SNAKE_SKN_LABELS },
  { "shr",  SETTING_FIELD(SnakeSettings, hour),      PARAM_CHOICE, PARAM_LIVE | PARAM_PLAYS,  0, SNAKE_HOUR_COUNT - 1,    LOAD_DEFAULT, SNAKE_SHR_LABELS },
};

// The colours of the fixed patterns, a swallowed apple's bulge, the apple
// and its stem, the tongue.
const uint8_t SNAKE_RGB_WHITE[3] = { 255, 255, 255 };
const uint8_t SNAKE_RGB_APPLE[3] = { 255, 30, 40 };
const uint8_t SNAKE_RGB_STEM[3]  = { 40, 200, 40 };
const uint8_t SNAKE_RGB_TONGUE[3] = { 255, 40, 90 };
const uint8_t SNAKE_RGB_DOT[3]    = { 255, 40, 40 };   // a new digit's cells, before its snake eats them
// coral's black as a dim grey: black bands left holes, and at rest the digits could not be read
const uint8_t SNAKE_RGB_CORAL[4][3]   = { { 255, 25, 20 }, { 50, 50, 60 }, { 255, 210, 0 }, { 50, 50, 60 } };
const uint8_t SNAKE_RGB_RAINBOW[7][3] = { { 255, 0, 0 }, { 255, 110, 0 }, { 255, 220, 0 }, { 0, 220, 0 },
                                          { 0, 200, 230 }, { 30, 60, 255 }, { 200, 0, 255 } };
const uint8_t SNAKE_RGB_RUN[4][3]     = { { 255, 220, 0 }, { 0, 220, 40 }, { 0, 180, 255 }, { 40, 40, 255 } };

// The colour along stops spread evenly from the head (h 0) to the tail.
inline void snakeRun(const uint8_t (*stops)[3], uint8_t count, uint16_t h, uint16_t n, uint8_t rgb[3]) {
  uint8_t spans = (uint8_t)(count - 1);
  uint32_t at = n > 1 ? (uint32_t)h * spans * 256 / (n - 1) : 0;   // 0 .. spans * 256
  uint8_t k = at >= (uint32_t)spans * 256 ? (uint8_t)(spans - 1) : (uint8_t)(at / 256);
  uint8_t f = at >= (uint32_t)spans * 256 ? 255 : (uint8_t)(at % 256);
  for (uint8_t j = 0; j < 3; j++) {
    rgb[j] = (uint8_t)(stops[k][j] + ((int16_t)stops[k + 1][j] - stops[k][j]) * f / 255);
  }
}

// The colour of a snake's cell i of n (the tail 0) in the pattern set. The
// pattern is counted from the head, so it moves with the body.
inline void snakeColour(const SnakeSettings &s, uint16_t i, uint16_t n, uint8_t rgb[3]) {
  uint16_t h = (uint16_t)(n - 1 - i);   // cells from the head
  uint8_t wa = s.widthA < 1 ? 1 : s.widthA, wb = s.widthB < 1 ? 1 : s.widthB;
  const uint8_t *c = s.colourA;
  switch (s.pattern) {
    case SNAKE_PATTERN_STRIPES: c = h % (wa + wb) < wa ? s.colourA : s.colourB; break;
    case SNAKE_PATTERN_GRADIENT: {
      const uint8_t stops[2][3] = { { s.colourA[0], s.colourA[1], s.colourA[2] }, { s.colourB[0], s.colourB[1], s.colourB[2] } };
      snakeRun(stops, 2, h, n, rgb);
      return;
    }
    case SNAKE_PATTERN_RAINBOW: c = SNAKE_RGB_RAINBOW[(h / wa) % 7]; break;
    case SNAKE_PATTERN_CORAL:   c = SNAKE_RGB_CORAL[(h / wa) % 4]; break;
    case SNAKE_PATTERN_RUN:     snakeRun(SNAKE_RGB_RUN, 4, h, n, rgb); return;
    default: break;
  }
  rgb[0] = c[0]; rgb[1] = c[1]; rgb[2] = c[2];
}

class SnakeFace : public PlannedFace {
 public:
  explicit SnakeFace(const SnakeSettings &s) : _s(s) {}

  // One pass: it reports a change only when what it shows has changed - a
  // pixel of a snake's way, a step of a scene, the colon's beat.
  bool update(const FaceNow &now) override {
    uint8_t d[4];
    faceDigits(now.local, now.set, d, snake::NO_DIGIT);
    bool turn = turned(now);
    if (turn) { _upright = now.turn == 1 || now.turn == 3; }
    uint8_t digits = _s.digits < snake::DIGITS_COUNT ? _s.digits : snake::DIGITS_COILS;
    if (turn || digits != _digits) {
      _digits = digits;
      _running = false;                   // a scene of the other orientation or digits ends; the time shows at once
      memcpy(_shown, d, 4);
      _plannedFor = 0;
      _dirty = true;
    }
    if (_running) {
      long since = (long)(now.ms - _startAt);
      _ms = since < 0 ? 0 : (uint32_t)since;
      if (_scene == SCENE_CHANGE && _logStand && _ms >= (uint32_t)_plan.stands * _plan.L->stepMs()) {
        logStands("Snake", now);
        _logStand = false;
      }
      if (_ms >= _sceneLen) {
        if (_scene == SCENE_CHANGE) { memcpy(_shown, _plan.to, 4); }
        _running = false;
        _sceneEndedAt = now.ms;
        _dirty = true;
      }
    }
    if (!_running) {
      _ms = 0;
      idle(now, d);
    }
    _colonOn = (now.local % 2) == 0;
    _seenHour = _s.hour;
    // What the picture depends on, folded into one number (FNV-1a).
    uint32_t key = 2166136261u;
    auto fold = [&key](uint32_t v) { for (uint8_t b = 0; b < 4; b++) { key = (key ^ (uint8_t)(v >> (8 * b))) * 16777619u; } };
    fold(_running ? _scene : 0xFF);
    fold(_running ? _ms / (_scene == SCENE_APPLE ? 20 : _scene == SCENE_TONGUE ? snake::TONGUE_MS : snake::PX_MS) : 0);
    fold(_shown[0] | _shown[1] << 8 | _shown[2] << 16 | (uint32_t)_shown[3] << 24);
    fold(_colonOn); fold(_s.look); fold(_s.pattern); fold(_upright); fold(_digits);
    fold(_s.colourA[0] | _s.colourA[1] << 8 | (uint32_t)_s.colourA[2] << 16 | (uint32_t)_s.widthA << 24);
    fold(_s.colourB[0] | _s.colourB[1] << 8 | (uint32_t)_s.colourB[2] << 16 | (uint32_t)_s.widthB << 24);
    fold(_s.head[0] | _s.head[1] << 8 | (uint32_t)_s.head[2] << 16);
    _key = key;
    return key != _drawnKey || _dirty;
  }

  void draw(Adafruit_GFX &g, const FaceNow &, const FacePaint &paint) override {
    const snake::Layout &L = snake::layoutFor(_upright, _digits);
    _paint = &paint;
    snake::Colours col = { paint.color(_s.head[0], _s.head[1], _s.head[2]),
                           paint.color(SNAKE_RGB_WHITE[0], SNAKE_RGB_WHITE[1], SNAKE_RGB_WHITE[2]),
                           &SnakeFace::shadeOf, this };
    uint8_t look = this->look();
    g.fillScreen(0);
    // The snakes lying still: every place but those a scene moves.
    for (uint8_t k = 0; k < 4; k++) {
      if (_running && _scene == SCENE_CHANGE && (_plan.moving & (1 << k))) { continue; }
      if (_running && k == _scenePlace && (_scene == SCENE_CHASE || _scene == SCENE_APPLE)) { continue; }
      uint8_t dk = _shown[k], f = _digits;
      snake::drawSnake(g, L, [&L, f, k, dk](uint16_t i) { return L.digit(f, k, dk, (uint8_t)i); },
                       snake::pathLen(f, dk), 0, snake::Cell{ 0, 0 }, look, col);
    }
    if (_colonOn) {
      uint16_t white = paint.color(235, 235, 235);
      for (uint8_t p = 0; p < 2; p++) { g.fillRect(L.pxX(L.colon[p]), L.pxY(L.colon[p]), 2, 2, white); }
    }
    if (_running) {
      if (_scene == SCENE_CHANGE) { drawChange(g, look, col); }
      else if (_scene == SCENE_CHASE) { drawChase(g, L, look, col); }
      else if (_scene == SCENE_APPLE) { drawApple(g, L, look, col, paint); }
      else if (_scene == SCENE_TONGUE) { drawTongue(g, L, look, paint); }
    }
    _drawnKey = _key;
    _dirty = false;
  }

  bool busy(const FaceNow &) const override { return _running; }

  // The input's scene; the extra now and then, measured by its own length
  // against the next change; what the web app asks to see - a change (the
  // minutes' ones crawl away and come anew), the scene a new choice of the
  // watchface animation plays, or the full hour a new choice of the hour
  // change plays, with the time on show. Each now or not at all.
  FaceOutcome request(const FaceNow &now, FaceTrigger why) override {
    bool played;
    switch (why) {
      case TRIGGER_ACTION:         played = scene(now, _s.animation, "a knock", true); break;
      case TRIGGER_NOW_AND_THEN: {
        // the tongue or a tail chase, each where it can play: the chase not without a 0 or 1 (thin digits), the
        // tongue not where no head has room
        uint8_t kind = faceRandom(2) ? SNAKE_ANIM_TONGUE : SNAKE_ANIM_CHASE;
        if (kind == SNAKE_ANIM_CHASE && !chasePlaces()) { kind = SNAKE_ANIM_TONGUE; }
        else if (kind == SNAKE_ANIM_TONGUE && chasePlaces() && !tonguePlaces(snake::layoutFor(_upright, _digits))) {
          kind = SNAKE_ANIM_CHASE;
        }
        played = scene(now, kind, "now and then", false);
        break;
      }
      case TRIGGER_PREVIEW_CHANGE: played = scene(now, SNAKE_ANIM_REPLAY, "the web app's preview", true); break;
      case TRIGGER_PREVIEW_ANIMATION:
        // Two settings play a preview: the hour change if that is the one that changed.
        if (_s.hour != _seenHour) {
          _seenHour = _s.hour;
          played = hourPreview(now);
        } else {
          played = scene(now, _s.animation, "the web app's choice", true);
        }
        break;
      default: return OUTCOME_NONE;
    }
    return played ? OUTCOME_PLAYED : OUTCOME_REFUSED;
  }

  // The span of its change to `minute`: as planned for the next minute, or
  // as it would be planned, on a plan of its own on the heap (2.2 KB, only
  // while it is used); a full hour drawn at random counts as the train, the
  // longer. Without the memory, the longest the self-test finds in any
  // family of digits (a minute 7.8 s, the hour 19.9 s, both in the coils).
  // The new time stands as the minute begins.
  FaceSpan changeSpan(const FaceNow &now, time_t minute) const override {
    FaceSpan s = { 0, 0 };
    if (!now.set) { return s; }
    bool hour = faceHourShows(minute);
    if (_plannedFor == minute && _plannedStand) {
      s.lead = _plannedStand;
      return s;
    }
    snake::Plan *p = new (std::nothrow) snake::Plan();
    if (!p) {
      s.lead = hour ? 20000UL : 8000UL;
      return s;
    }
    uint8_t from[4], to[4];
    faceDigits(minute - 60, true, from, snake::NO_DIGIT);
    faceDigits(minute, true, to, snake::NO_DIGIT);
    snake::HourMode mode = _s.hour == SNAKE_HOUR_AT_ONCE ? snake::HOUR_AT_ONCE : snake::HOUR_TRAIN;
    if (snake::planChange(*p, _upright, from, to, hour, mode, _digits)) {
      s.lead = (unsigned long)p->stands * p->L->stepMs();
    }
    delete p;
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
  enum Scene : uint8_t { SCENE_CHANGE, SCENE_CHASE, SCENE_APPLE, SCENE_TONGUE };

  // The body's shade for the looks that have one, through the face's paint.
  static uint16_t shadeOf(void *ctx, uint8_t look, uint16_t i, uint16_t n) {
    const SnakeFace &f = *(const SnakeFace *)ctx;
    uint8_t rgb[3];
    snakeColour(f._s, i, n, rgb);
    return f._paint->color(rgb[0], rgb[1], rgb[2]);
  }

  // The places whose digit on show can chase its tail (bit k): the coils all
  // can; of the thin strokes only the 0 and the 1.
  uint8_t chasePlaces() const {
    uint8_t places = 0;
    for (uint8_t k = 0; k < 4; k++) { if (snake::chaseable(_digits, _shown[k])) { places |= (uint8_t)(1 << k); } }
    return places;
  }
  // A place of `can` (bit k) for the tail chase, the apple or the tongue,
  // drawn evenly - but not the one the last of them had while another can
  // (Peter, 2026-10-24: the first pick, the first that can from a random
  // place on, was even enough over a day, yet the 1 on the left, which nearly
  // always can, came again and again in an afternoon); NOWHERE without one.
  static const uint8_t NOWHERE = 0xFF;
  uint8_t pickPlace(uint8_t can) const {
    if (_lastPlace < 4 && (can & ~(1 << _lastPlace))) { can &= (uint8_t)~(1 << _lastPlace); }
    uint8_t count = 0;
    for (uint8_t k = 0; k < 4; k++) { count += (can >> k) & 1; }
    if (!count) { return NOWHERE; }
    uint8_t n = (uint8_t)faceRandom(count);
    for (uint8_t k = 0; k < 4; k++) {
      if (((can >> k) & 1) && n-- == 0) { return k; }
    }
    return NOWHERE;
  }

  // The look as set, a stored value out of range as the line.
  uint8_t look() const { return _s.look < snake::LOOK_COUNT ? _s.look : snake::LOOK_LINE; }

  // The places whose head can flick its tongue straight ahead over the digits
  // on show (bit k).
  uint8_t tonguePlaces(const snake::Layout &L) const {
    uint8_t places = 0;
    for (uint8_t k = 0; k < 4; k++) {
      if (snake::tongueRoom(L, _digits, _shown, k, look())) { places |= (uint8_t)(1 << k); }
    }
    return places;
  }
  // ... and those whose head can strike at the apple.
  uint8_t applePlaces(const snake::Layout &L) const {
    uint8_t places = 0;
    for (uint8_t k = 0; k < 4; k++) {
      if (snake::appleRoom(L, _digits, _shown, k, look())) { places |= (uint8_t)(1 << k); }
    }
    return places;
  }

  snake::HourMode hourMode() const {
    if (_s.hour == SNAKE_HOUR_TRAIN) { return snake::HOUR_TRAIN; }
    if (_s.hour == SNAKE_HOUR_AT_ONCE) { return snake::HOUR_AT_ONCE; }
    return faceRandom(2) ? snake::HOUR_AT_ONCE : snake::HOUR_TRAIN;
  }

  void startScene(const FaceNow &now, Scene scene, uint32_t len) {
    _scene = scene;
    _sceneLen = len;
    _startAt = now.ms;
    _ms = 0;
    _running = true;
    _logStand = false;
  }

  // A scene between changes - a tail chase, the apple, the minutes' ones
  // anew (the time on show stays right), the tongue - for the watchface
  // animation, the web app, and now and then. Refused while a scene runs,
  // just after one, and when it could not end before the next change
  // starts: then it is dropped, not kept. One the user asked for must only
  // have ended when the change starts; one the clock plays of itself
  // FACE_GUARD_MS before (Peter, 2026-10-22).
  bool scene(const FaceNow &now, uint8_t kind, const char *why, bool asked) {
    if (_running || _shown[0] == snake::NO_DIGIT) { return false; }
    if (now.ms - _sceneEndedAt < FACE_REST_MS) { return false; }
    if (_plannedFor == 0) { return false; }   // the next change is planned on the face's next pass
    const snake::Layout &L = snake::layoutFor(_upright, _digits);
    if (kind == SNAKE_ANIM_RANDOM) {
      // of those the digits on show allow: a thin digit may not chase its tail
      // nor have room for the apple or the tongue
      uint8_t kinds[4], count = 0;
      if (chasePlaces()) { kinds[count++] = SNAKE_ANIM_CHASE; }
      if (applePlaces(L)) { kinds[count++] = SNAKE_ANIM_APPLE; }
      kinds[count++] = SNAKE_ANIM_REPLAY;
      if (tonguePlaces(L)) { kinds[count++] = SNAKE_ANIM_TONGUE; }
      kind = kinds[faceRandom(count)];
    }
    unsigned long plannedStart = _startAt;
    uint32_t len = 0;
    uint8_t place = 3;
    Scene sc = SCENE_CHANGE;
    const char *what;
    if (kind == SNAKE_ANIM_CHASE) {
      place = pickPlace(chasePlaces());
      if (place != NOWHERE) { len = snake::chaseMs(L, _digits, _shown[place]); }
      sc = SCENE_CHASE;
      what = "a tail chase";
    } else if (kind == SNAKE_ANIM_APPLE) {
      place = pickPlace(applePlaces(L));
      if (place != NOWHERE) { len = snake::appleMs(L, _digits, _shown[place]); }
      sc = SCENE_APPLE;
      what = "an apple";
    } else if (kind == SNAKE_ANIM_TONGUE) {
      place = pickPlace(tonguePlaces(L));
      if (place != NOWHERE) { len = snake::TONGUE_SCENE_MS; }
      sc = SCENE_TONGUE;
      what = "the tongue";
    } else {
      if (snake::planSwap(_plan, L, _shown, _shown, 1 << 3, _digits)) { len = (uint32_t)_plan.gone * L.stepMs(); }
      what = "the minutes' ones again";
    }
    _plannedFor = 0;                          // the change is planned anew afterwards
    if (len == 0 || !faceFits(plannedStart, now.ms, len, asked ? 0 : FACE_GUARD_MS)) { return false; }
    _scenePlace = place;
    if (sc != SCENE_CHANGE) { _lastPlace = place; }
    startScene(now, sc, len);
    static const char *const PLACE_NAMES[4] = { "hours' tens", "hours' ones", "minutes' tens", "minutes' ones" };
    if (sc == SCENE_CHANGE) { Console.printf("Snake: %s (%s)\n", what, why); }
    else { Console.printf("Snake: %s, %s (%s)\n", what, PLACE_NAMES[place], why); }
    return true;
  }

  // A new choice of the hour change in the web app: the full hour plays once
  // with the time on show - all four snakes go and come again.
  bool hourPreview(const FaceNow &now) {
    if (_running || _shown[0] == snake::NO_DIGIT) { return false; }
    if (now.ms - _sceneEndedAt < FACE_REST_MS || _plannedFor == 0) { return false; }
    unsigned long plannedStart = _startAt;
    snake::HourMode mode = hourMode();
    bool ok = snake::planChange(_plan, _upright, _shown, _shown, true, mode, _digits);
    _plannedFor = 0;
    uint32_t len = ok ? (uint32_t)_plan.gone * _plan.L->stepMs() : 0;
    if (len == 0 || !faceFits(plannedStart, now.ms, len, 0)) { return false; }
    startScene(now, SCENE_CHANGE, len);
    Console.printf("Snake: the hour change, %s (the web app's choice)\n", mode == snake::HOUR_TRAIN ? "train" : "all at once");
    return true;
  }

  // Nothing moving: put a time right that moved on without a change - the
  // digits that differ swap at once; the first time comes in from the dashes
  // - plan the next change once a minute, and start it when its moment has
  // come.
  void idle(const FaceNow &now, const uint8_t d[4]) {
    if (memcmp(_shown, d, 4) != 0) {
      bool lost = d[0] == snake::NO_DIGIT && _shown[0] != snake::NO_DIGIT;
      if (!lost && snake::planChange(_plan, _upright, _shown, d, false, snake::HOUR_TRAIN, _digits) && _plan.gone) {
        startScene(now, SCENE_CHANGE, (uint32_t)_plan.gone * _plan.L->stepMs());
      } else {
        memcpy(_shown, d, 4);                 // the time lost: dashes at once
        _dirty = true;
      }
      _plannedFor = 0;
      return;
    }
    if (d[0] == snake::NO_DIGIT) { return; }
    time_t next = nextMinute(now);
    if (_plannedFor != next) {
      uint8_t to[4];
      faceDigits(next, now.set, to, snake::NO_DIGIT);
      bool hour = faceHourShows(next);   // in Calm, as an option, the places that change, as at a minute
      _plannedMode = hourMode();
      _plannedStand = snake::planChange(_plan, _upright, _shown, to, hour, _plannedMode, _digits)
                    ? (uint32_t)_plan.stands * _plan.L->stepMs() : 0;
      _plannedHour = hour;
      _plannedFor = next;
    }
    _startAt = nextMinuteAt(now) - _plannedStand;
    // Started at its planned moment even when that has passed (the face was
    // away): from there, everything is where it ought to be by now.
    if (_plannedStand && (long)(now.ms - _startAt) >= 0) {
      startScene(now, SCENE_CHANGE, (uint32_t)_plan.gone * _plan.L->stepMs());
      _startAt = nextMinuteAt(now) - _plannedStand;
      _logStand = true;
      _plannedFor = 0;
      if (_plannedHour) {
        Console.printf("Snake: the hour change, %s\n", _plannedMode == snake::HOUR_TRAIN ? "train" : "all at once");
      }
    }
  }

  // The movers of the running change at the pass's moment, a pixel at a
  // time. Under them, the dots on the new digits, as thick as the snakes: a
  // new snake eats them and grows with each, so its body becomes the digit
  // (Peter, 2026-10-23).
  template <class G>
  void drawChange(G &g, uint8_t look, const snake::Colours &col) const {
    const snake::Layout &L = *_plan.L;
    uint16_t step = L.stepMs();
    int16_t t = (int16_t)(_ms / step);
    uint8_t sub = (uint8_t)(_ms % step / snake::PX_MS);   // pixels on the way to the next cell
    uint16_t dot = _paint->color(SNAKE_RGB_DOT[0], SNAKE_RGB_DOT[1], SNAKE_RGB_DOT[2]);
    uint8_t size = look == snake::LOOK_LINE ? 1 : 2;
    for (uint8_t i = 0; i < _plan.count; i++) {
      const snake::Mover &m = _plan.m[i];
      if (!m.fresh) { continue; }
      uint16_t first = (uint16_t)(m.startLen + m.route - m.len);   // its digit's first cell's index
      for (uint8_t j = 0; j < m.len; j++) {
        if (!snake::hasDot(m.len, j) || first + j <= m.headAt(t)) { continue; }   // none, or eaten
        snake::Cell a = m.cellAt((uint16_t)(first + j));
        for (uint8_t dy = 0; dy < size; dy++) {
          for (uint8_t dx = 0; dx < size; dx++) { g.drawPixel(L.pxX(a) + dx, L.pxY(a) + dy, dot); }
        }
      }
    }
    for (uint8_t i = 0; i < _plan.count; i++) {
      const snake::Mover &m = _plan.m[i];
      uint16_t head = m.headAt(t), n = m.lengthAt(t);
      int16_t ahead = t - m.start;
      bool moving = ahead >= 0 && ahead < (int16_t)m.route;
      snake::Cell next = moving ? m.cellAt((uint16_t)(head + 1)) : snake::Cell{ 0, 0 };
      uint16_t tail = (uint16_t)(head - n + 1);
      snake::drawSnake(g, L, [&m, tail](uint16_t b) { return m.cellAt(tail + b); }, n, moving ? sub : 0, next, look,
                       col, -1, m.growsAt(t));
    }
  }

  // The tail chase: the snake's head goes round its digit once.
  template <class G>
  void drawChase(G &g, const snake::Layout &L, uint8_t look, const snake::Colours &col) const {
    uint8_t k = _scenePlace, dk = _shown[k], f = _digits, n = snake::pathLen(f, dk);
    uint16_t step = L.stepMs(), t = (uint16_t)(_ms / step);
    if (t > n) { t = n; }
    uint8_t sub = t < n ? (uint8_t)(_ms % step / snake::PX_MS) : 0;
    snake::drawSnake(g, L, [&L, f, k, dk, n, t](uint16_t b) { return L.digit(f, k, dk, (uint8_t)((t + b) % n)); }, n,
                     sub, L.digit(f, k, dk, (uint8_t)(t % n)), look, col);
  }

  // The apple (3 x 3 px and a green stem) rolls in along the lane from beyond
  // the right side to below the head of its place and hops; it lies there a
  // moment; the head strikes down to it a cell at a time, holds it - the apple
  // gone - and pulls back; a white bulge runs down the body to the tail.
  template <class G>
  void drawApple(G &g, const snake::Layout &L, uint8_t look, const snake::Colours &col, const FacePaint &paint) const {
    uint8_t k = _scenePlace, dk = _shown[k], f = _digits, n = snake::pathLen(f, dk);
    snake::Cell head = L.digit(f, k, dk, n - 1);
    const uint8_t depth = L.appleDepth;
    const uint32_t bulgeAt = snake::appleBulgeAt(L);
    snake::Cell apple = { (int8_t)(head.r + depth), head.c };
    uint16_t red = paint.color(SNAKE_RGB_APPLE[0], SNAKE_RGB_APPLE[1], SNAKE_RGB_APPLE[2]);
    uint16_t stem = paint.color(SNAKE_RGB_STEM[0], SNAKE_RGB_STEM[1], SNAKE_RGB_STEM[2]);
    const uint32_t down = (uint32_t)depth * snake::STRIKE_DOWN_MS;
    uint8_t s = 0;            // cells the head is down in the lane
    int16_t bulge = -1;
    int16_t appleY = L.pxY(apple), appleX = L.pxX(apple);   // its top row's middle; the stem above
    bool appleOn = true;
    if (_ms < snake::APPLE_FALL_MS) {
      // from beyond the right side, slower and slower
      int32_t left = (int32_t)(snake::APPLE_FALL_MS - _ms), from = L.ox + L.pitch * L.gw + 2;
      appleX = (int16_t)(appleX + (from - appleX) * left * left / ((int32_t)snake::APPLE_FALL_MS * snake::APPLE_FALL_MS));
    } else if (_ms < snake::APPLE_FALL_MS + snake::APPLE_BOUNCE_MS) {
      appleY -= (_ms - snake::APPLE_FALL_MS) < snake::APPLE_BOUNCE_MS / 2 ? 2 : 1;
    } else if (_ms >= snake::APPLE_STRIKE_AT && _ms < bulgeAt) {
      uint32_t u = _ms - snake::APPLE_STRIKE_AT;
      if (u < down) {
        s = (uint8_t)(u / snake::STRIKE_DOWN_MS + 1);
      } else if (u < down + snake::STRIKE_HOLD_MS) {
        s = depth;
      } else {
        uint8_t back = (uint8_t)((u - down - snake::STRIKE_HOLD_MS) / snake::STRIKE_BACK_MS + 1);
        s = back >= depth ? 0 : (uint8_t)(depth - back);
      }
      appleOn = u < down - snake::STRIKE_DOWN_MS;   // gone once the head is there
    } else if (_ms >= bulgeAt) {
      appleOn = false;
      int32_t b = (int32_t)n - 1 - (int32_t)((_ms - bulgeAt) / snake::BULGE_MS);
      bulge = b < 0 ? -1 : (int16_t)b;
    }
    if (appleOn) {
      g.fillRect(appleX - 1, appleY, 3, 3, red);
      g.drawPixel(appleX, appleY - 1, stem);
    }
    // the body: the digit's path and then s cells straight down, its last n
    snake::drawSnake(g, L, [&L, f, k, dk, n, s, head](uint16_t b) {
      uint16_t i = b + s;
      if (i < n) { return L.digit(f, k, dk, (uint8_t)i); }
      return snake::Cell{ (int8_t)(head.r + (i - n + 1)), head.c };
    }, n, 0, snake::Cell{ 0, 0 }, look, col, bulge);
  }

  // The tongue: three flicks straight ahead of a head, each out to its fork
  // and in again - where snake::tongueRoom() found room.
  template <class G>
  void drawTongue(G &g, const snake::Layout &L, uint8_t look, const FacePaint &paint) const {
    uint8_t out = snake::tongueOut(_ms);
    if (!out) { return; }
    snake::Pixel px[4];
    uint8_t n = snake::tonguePixels(L, _digits, _scenePlace, _shown[_scenePlace], look, out, px);
    uint16_t c = paint.color(SNAKE_RGB_TONGUE[0], SNAKE_RGB_TONGUE[1], SNAKE_RGB_TONGUE[2]);
    for (uint8_t i = 0; i < n; i++) { g.drawPixel(px[i].x, px[i].y, c); }
  }

  const SnakeSettings &_s;
  snake::Plan _plan;
  const FacePaint *_paint = nullptr;    // while drawing, for the shades
  uint8_t  _shown[4] = { snake::NO_DIGIT, snake::NO_DIGIT, snake::NO_DIGIT, snake::NO_DIGIT };
  bool     _upright = false;
  uint8_t  _digits = snake::DIGITS_COILS;   // the family the face shows, as set
  bool     _running = false;
  Scene    _scene = SCENE_CHANGE;
  uint8_t  _scenePlace = 3;
  uint8_t  _lastPlace = NOWHERE;        // the place the last tail chase, apple or tongue had
  uint32_t _sceneLen = 0;               // ms
  uint32_t _ms = 0;                     // ms into the running scene in this pass
  uint32_t _plannedStand = 0;           // ms from the planned change's start until the new time stands
  bool     _plannedHour = false;
  snake::HourMode _plannedMode = snake::HOUR_TRAIN;
  bool     _logStand = false;           // the running scene is the time's change: log when it stands
  uint8_t  _seenHour = 0xFF;            // the hour change setting at the last pass
  bool     _colonOn = false;
  uint32_t _key = 0;                    // what the picture depends on in this pass
  uint32_t _drawnKey = 0;               // ... and in the frame drawn last
};

#endif  // FACE_SNAKE_H
