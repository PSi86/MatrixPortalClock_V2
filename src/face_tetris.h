// The Tetris watchface: HH:MM built from falling tetrominoes. A digit that
// changes breaks up - the break-up effect, the exit of the old digit - and the
// new one is built anew from falling blocks, which turn and walk across on the
// way down as a player would move them. The block tables are generated
// (src/tetris_digits.h, scripts/gen_tetris_digits.py): every digit comes in
// several different tilings of its 6x10 cell glyph, each already ordered so
// the pieces can be dropped in from above and coloured so that no two
// touching pieces share a colour class.
//
// Two things happen fresh every time a digit changes: a variant is drawn at
// random, and the colour classes are mapped onto a shuffled palette. So the
// same digit is neither built the same way nor coloured the same way twice.
// The palette is six hues exactly 60 degrees apart. Because the classes are a
// proper colouring and the mapping is injective, touching pieces can never end
// up within 60 degrees of each other - no run-time check needed.
//
// To a full hour all four digits are built anew, with an effect from the
// original game: the rows cleared, the level-up colours or a rocket (the hour
// change, thr; section "The change to a full hour" below).
#ifndef FACE_TETRIS_H
#define FACE_TETRIS_H

#include <Adafruit_GFX.h>
#include "face.h"
#include "tetris_digits.h"

const uint8_t TETRIS_CELL  = 2;   // panel pixels per grid cell -> digit 12x20 px
const uint8_t TETRIS_PITCH = 14;  // x distance between the two digits of a group
const uint8_t TETRIS_DOT   = 4;   // colon dot edge length
const uint8_t TETRIS_SETTLE = 2;  // last cells of the fall, never turning any more

// Drop and turn pace are two separate settings, because they are two separate
// things to watch: how fast the blocks come down, and how busily they turn on
// the way. Both are stored in their own blob (see TetrisSettings below).
const uint8_t  TETRIS_DROP_MIN = 20,  TETRIS_DROP_MAX = 250;   // ms per cell
const uint16_t TETRIS_SPIN_MIN = 80,  TETRIS_SPIN_MAX = 1500;  // ms per quarter turn

// Six hues 60 degrees apart. Blue is lifted off pure 0000FF, which is too dark
// against the others on the panel, without moving its hue.
const uint8_t TETRIS_PALETTE[TETRIS_COLOUR_CLASSES][3] = {
  {255,   0,   0},   // red
  {255, 255,   0},   // yellow
  {  0, 255,   0},   // green
  {  0, 255, 255},   // cyan
  { 48,  48, 255},   // blue
  {255,   0, 255}    // magenta
};

// Settings of this watchface, in their own flash blob. Keeping them out of the
// main Settings struct is not tidiness: that struct is exactly 32 bytes with no
// padding left, and a firmware that stored 36 would be refused wholesale by any
// older build (Preferences::getBytes returns 0 when the blob is larger than the
// buffer), which would reset every setting the user has. A second key costs
// nothing and leaves the old blob untouched.
// Fields added later go on the end and are filled in by revision, so a blob
// written by an earlier firmware keeps everything it had.
#define TETRIS_SETTINGS_MAGIC 0x7E71
#define TETRIS_SETTINGS_REV   4   // 1 = drop and spin, 2 = adds the shake, 3 = on a time change too, 4 = the hour change

// How the digits come apart: the break-up effect.
const uint8_t BREAKUP_COLLAPSE = 0;  // the stack gives way and drops
const uint8_t BREAKUP_SCATTER  = 1;  // the pieces fly off and tumble
const uint8_t BREAKUP_CLEAR    = 2;  // rows flash and vanish, bottom up
const uint8_t BREAKUP_RANDOM   = 3;  // one of the three, drawn each time
const uint8_t BREAKUP_COUNT    = 4;
const uint8_t TETRIS_KNOCK_LEVEL_MAX      = 10;   // the knock's sensitivity, stored here (shk)

// What the change to a full hour plays (Peter, 2026-10-23): all four digits
// built anew, with an effect from the original game.
const uint8_t HOUR_LINE_CLEAR = 0;   // the old time's rows flash and vanish, four at a time, then the build
const uint8_t HOUR_COLOURS    = 1;   // the build, then the blocks run through the colours, as at a level-up
const uint8_t HOUR_ROCKET     = 2;   // the build, then a rocket lifts off between the hours and the minutes
const uint8_t HOUR_RANDOM     = 3;   // one of the three, drawn each hour
const uint8_t HOUR_OFF        = 4;   // the plain change, as every minute
const uint8_t HOUR_COUNT      = 5;

struct TetrisSettings {
  uint16_t magic;
  uint8_t  rev;
  uint8_t  dropMs;      // milliseconds per cell of fall
  uint16_t spinMs;      // average milliseconds between quarter turns
  uint8_t  knockLevel;  // 0 = knocking the clock does nothing, 1..10 sensitivity
  uint8_t  breakUp;  // the break-up effect: how the digits come apart, see BREAKUP_*
  uint8_t  hourAnim;    // what the change to a full hour plays, see HOUR_* (rev 4; until 2026-10-08 the byte
                        // was shakeOnChange, unused since)
};

const TetrisSettings TETRIS_DEFAULTS = {
  TETRIS_SETTINGS_MAGIC, TETRIS_SETTINGS_REV,
  70,    // a block falls one cell every 70 ms
  260,   // and turns a quarter every 260 ms on average
  6,     // reacts to a knock, not to someone walking past
  3,     // a different way of coming apart each time
  HOUR_RANDOM   // a different hour animation each hour
};

// The Tetris settings for the web app and the store (face.h). shk, the knock's
// sensitivity, is kept here, where it has always been stored.
const char *const TETRIS_SHS_LABELS[] = { "Collapse", "Scatter", "Clear rows", "Random each time" };   // by value: BREAKUP_*
static_assert(sizeof(TETRIS_SHS_LABELS) / sizeof(TETRIS_SHS_LABELS[0]) == BREAKUP_COUNT, "a name per break-up effect");
const char *const TETRIS_THR_LABELS[] = { "Line clear", "Level-up colours", "Rocket", "Random each hour", "Off" };   // by value: HOUR_*
static_assert(sizeof(TETRIS_THR_LABELS) / sizeof(TETRIS_THR_LABELS[0]) == HOUR_COUNT, "a name per hour animation");
const SettingParam TETRIS_PARAMS[] = {
  { "tdrop", SETTING_FIELD(TetrisSettings, dropMs),     PARAM_RANGE,  PARAM_LIVE,               TETRIS_DROP_MIN, TETRIS_DROP_MAX,        LOAD_CLAMP,   nullptr },
  { "tspin", SETTING_FIELD(TetrisSettings, spinMs),     PARAM_RANGE,  PARAM_LIVE,               TETRIS_SPIN_MIN, TETRIS_SPIN_MAX,        LOAD_CLAMP,   nullptr },
  { "shk",   SETTING_FIELD(TetrisSettings, knockLevel), PARAM_RANGE,  PARAM_LIVE | PARAM_KNOCK, 0,               TETRIS_KNOCK_LEVEL_MAX, LOAD_CLAMP,   nullptr },
  { "shs",   SETTING_FIELD(TetrisSettings, breakUp),    PARAM_CHOICE, PARAM_LIVE,               0,               BREAKUP_COUNT - 1,      LOAD_DEFAULT, TETRIS_SHS_LABELS },
  { "thr",   SETTING_FIELD(TetrisSettings, hourAnim),   PARAM_CHOICE, PARAM_LIVE | PARAM_PLAYS, 0,               HOUR_COUNT - 1,         LOAD_DEFAULT, TETRIS_THR_LABELS },
};

// A blob of an older revision, the same idea as the main settings: rev 1 only
// had dropMs and spinMs, and the read left the rest zeroed, so the fields
// added since get an explicit value and what was set stays.
inline void tetrisMigrate(uint8_t *data, uint8_t rev) {
  TetrisSettings &t = *(TetrisSettings *)data;
  if (rev < 2) {
    t.knockLevel = TETRIS_DEFAULTS.knockLevel;
    t.breakUp = TETRIS_DEFAULTS.breakUp;
  }
  if (rev < 4) { t.hourAnim = TETRIS_DEFAULTS.hourAnim; }   // the byte held 0 since rev 3
}

class TetrisFace : public Watchface {
 public:
  explicit TetrisFace(const TetrisSettings &s) : _s(s) {}

  // Chosen: a fresh layout and a fresh drop on the next pass. Shown again or
  // turned, it catches up by itself: a turn is seen on its next pass, and a
  // digit that changed meanwhile breaks up then.
  void enter(const FaceNow &, FaceEnter why) override {
    if (why == ENTER_CHOSEN) { _rotation = 0xFF; }
  }

  // One pass. While blocks are falling it steps at the fall pace; once
  // everything has landed it reports a change only when something it shows has
  // changed, so an idle clock costs almost nothing. Not drawing is safe: the
  // panel keeps the last frame it was given.
  bool update(const FaceNow &now) override {
    // A rotation moves every coordinate a running animation holds, and a rotation
    // rebuilds the digits anyway - so it wins over the animation.
    if (now.turn != _rotation) {
      for (uint8_t i = 0; i < 4; i++) { _breaking[i] = false; }
      layout(now);
    }

    // The change to a full hour, on the pass that began the hour and before
    // pushTime() would start the plain change: all four digits come apart.
    // Only digits that stand can: a face that is just building plays none.
    if (now.hourTick && now.set && faceHourShows(now.local) && _s.hourAnim != HOUR_OFF && !breakingUp() && allStanding()) {
      startHour(now, pickHour());
    }

    // A digit that has finished coming apart is marked unknown, so the next
    // pushTime() starts it again from the current time - which is also what
    // makes the rebuild show the right value if a minute passed on the way.
    bool anyBreakingUp = false;
    bool breakUpEnded = false;   // a digit finished this pass and the panel must be cleared
    bool breakUpDue = (now.ms - _breakUpStepLast >= BREAKUP_STEP_MS);
    for (uint8_t i = 0; i < 4; i++) {
      if (!_breaking[i]) { continue; }
      if (breakUpDue && stepBreakUp(i, now.h)) {
        breakUpEnded = true;
        _breaking[i] = false;
        _digits[i].value = 0xFF;
        // Let the space stand empty for a moment. Without it the first new block
        // is already on its way down while the last of the old ones is still
        // leaving the panel, and the two read as one continuous movement.
        _holdUntil[i] = now.ms + HOLD_MS;
        _breakUpEndedAt = now.ms;   // do not retrigger on the same knock
      }
      if (_breaking[i]) { anyBreakingUp = true; }
    }
    if (breakUpDue) { _breakUpStepLast = now.ms; }

    pushTime(now);

    // Turning runs on its own clock, so a flick between two fall steps has to be
    // painted when it happens - otherwise the drop rate would quietly limit how
    // finely the rotation setting can act.
    uint8_t spinPending = allPendingTurns(now.ms);

    bool stepDue = !_settled && (now.ms - _stepLast >= _s.dropMs);
    if (stepDue) {
      _stepLast = now.ms;
      step(now.ms);
    }
    // The hour change's effect, once its build stands - after the fall step,
    // which may just have finished it: its frames are changes.
    bool effectMoved = stepHour(now);

    // 1 Hz blink, in step with the seconds; off while the rocket flies, which
    // passes through the colon's place (after the effect's step, which may
    // just have started or ended it), and for the rest of the second the
    // rocket left in: the colon comes back as a second begins, never for the
    // last moments of one (Peter, 2026-10-23: a short flash, then a second
    // dark, looked untidy).
    _colonOn = (now.local % 2) == 0 && !rocketFlies() && now.local != _colonDarkIn;

    bool changed = _colonOn != _colonDrawn || spinPending != _spinDrawn || effectMoved;
    // Debris moving is a change in itself, so while anything is coming apart the
    // face repaints on the animation's clock rather than waiting to be asked.
    // breakUpEnded matters just as much: on the pass where the last piece leaves,
    // nothing is moving any more and nothing has "changed", so without it the
    // face would leave the previous frame - debris and all - standing on the
    // panel for the whole pause.
    return stepDue || changed || breakUpEnded || (anyBreakingUp && breakUpDue);
  }

  // The digits, or the ones coming apart, and the colon. The palette follows
  // the brightness, as the classic face's colours do.
  void draw(Adafruit_GFX &g, const FaceNow &now, const FacePaint &paint) override {
    if (paint.bright != _palBright) {   // made anew only for a new brightness (K8 of the face refactor)
      for (uint8_t i = 0; i < TETRIS_COLOUR_CLASSES; i++) {
        _pal[i] = paint.color(TETRIS_PALETTE[i][0], TETRIS_PALETTE[i][1], TETRIS_PALETTE[i][2]);
      }
      _palBright = paint.bright;
    }
    g.fillScreen(0);
    for (uint8_t i = 0; i < 4; i++) {
      if (_breaking[i]) { drawBreakUp(g, i, paint); } else { drawDigit(g, i, originX(i), originY(i), now.ms); }
    }
    if (!now.set) { drawDashes(g, paint); }
    // The colon keeps its own beat and is not touched by the digits' animations.
    // It is the one thing on this face that shows real time passing, so it has to
    // stay on the second, whatever the digits are doing. Hiding it while everything
    // was coming apart, as an earlier version did, made it drop out mid-phase on a
    // knock and reappear off-beat as the new digits arrived. The one exception is
    // the hour's rocket, which flies through its place: the two over each other
    // were not pleasant (Peter, 2026-10-23), so the colon is off for its 2.8 s and
    // comes back on its beat (update()).
    if (_colonOn) { drawColon(g, paint); }
    _colonDrawn = _colonOn;
    if (rocketFlies()) { drawRocket(g, now, paint); }
    // After the step, because a piece may have landed and the next one armed.
    _spinDrawn = allPendingTurns(now.ms);
  }

  // Whether the digits are coming apart, a digit waits in the empty beat to be
  // built, or blocks are still falling - for the time's change or for the
  // animation a knock asked for: what the clock plans itself waits for both.
  // (K7 of the face refactor: until 2026-10-20 the face counted as at rest in
  // the beat, and something could start in it.) The hour change counts until
  // its effect is over.
  bool busy(const FaceNow &now) const override {
    if (breakingUp() || !_settled || _hourPart != PART_NONE) { return true; }
    if (now.set) {
      for (uint8_t i = 0; i < 4; i++) { if (_digits[i].value > 9) { return true; } }
    }
    return false;
  }

  bool offers(FaceTrigger why) const override {
    return why == TRIGGER_ACTION || why == TRIGGER_PREVIEW_CHANGE || why == TRIGGER_PREVIEW_ANIMATION;
  }

  // The input's animation: all four digits come apart and are built anew -
  // unless they are coming apart already, or the last effect ended less than
  // FACE_REST_MS ago. Any build under way may be smashed, the minute
  // change's too, and the animation may run into the change: the change
  // takes it over as a knock takes over a knock's build (pushTime(): a digit
  // still coming apart is built with the new time, one being built that
  // changes comes apart again). Until 2026-10-22 it had to end FACE_GUARD_MS
  // before the next minute, which refused a knock in up to the minute's last
  // 15 s or so at a drop pace of 90 ms, by the build's length (Peter: what
  // the user asks for is granted).
  // The web app's preview of a setting tried there is the same run, now or
  // not at all. The hour change's effect - the rows cleared, the colours, the
  // rocket - is not cut short: a knock then does nothing; one in its build
  // takes the build over as any knock does, and the effect still due is
  // dropped (Peter, 2026-10-23).
  // A new choice of the hour change, tried in the web app, plays it once
  // with the time on show (TRIGGER_PREVIEW_ANIMATION); Off plays nothing.
  FaceOutcome request(const FaceNow &now, FaceTrigger why) override {
    if (why == TRIGGER_PREVIEW_ANIMATION) {
      if (_s.hourAnim == HOUR_OFF) { return OUTCOME_NONE; }
      if (!now.set || hourEffectRuns() || breakingUp() || now.ms - _breakUpEndedAt < FACE_REST_MS) { return OUTCOME_REFUSED; }
      startHour(now, pickHour());
      return OUTCOME_PLAYED;
    }
    if (why != TRIGGER_ACTION && why != TRIGGER_PREVIEW_CHANGE) { return OUTCOME_NONE; }
    if (hourEffectRuns() || breakingUp() || now.ms - _breakUpEndedAt < FACE_REST_MS) { return OUTCOME_REFUSED; }
    _hourPart = PART_NONE;   // a build of the hour change taken over: its effect is not played
    breakUpAll(now.ms);
    return OUTCOME_PLAYED;
  }

  // Its change starts at the minute: the digits that change break up, and are
  // built anew. To a full hour all four, with the hour's effect after them (or
  // the rows cleared before them); with Random each hour the longest of the
  // three, as the one that plays is drawn only then.
  FaceSpan changeSpan(const FaceNow &now, time_t minute) const override {
    FaceSpan s = { 0, 0 };
    if (!now.set) { return s; }
    uint8_t hourAnim = faceHourShows(minute) ? _s.hourAnim : HOUR_OFF;   // a minute, or the hour plainly
    if (minute != _spanFor || _s.dropMs != _spanDrop || _rotation != _spanTurn || hourAnim != _spanHour) {
      uint8_t from[4], to[4];
      faceDigits(minute - 60, true, from, 0xFF);
      faceDigits(minute, true, to, 0xFF);
      if (hourAnim != HOUR_OFF) {
        unsigned long build = (unsigned long)buildSteps(to, nullptr) * _s.dropMs;
        _spanTail = 0;
        for (uint8_t style = 0; style < HOUR_RANDOM; style++) {
          if (hourAnim != HOUR_RANDOM && hourAnim != style) { continue; }
          unsigned long t = hourLength(style, build);
          if (t > _spanTail) { _spanTail = t; }
        }
      } else {
        _spanTail = (unsigned long)BREAKUP_MAX_MS + HOLD_MS + (unsigned long)buildSteps(to, from) * _s.dropMs;
      }
      _spanFor = minute;
      _spanDrop = _s.dropMs;
      _spanTurn = _rotation;
      _spanHour = hourAnim;
    }
    s.tail = _spanTail;
    return s;
  }

#if defined(FACE_FRAME_LOG)
  // Test builds: a scene starts with the face afresh - a new layout and drop,
  // no rest after an effect, no hour change under way.
  void testReset() {
    _rotation = 0xFF;
    _breakUpEndedAt = 0;
    _hourPart = PART_NONE;
    _hueShift = 0;
  }
#endif

 private:
  /* ----------------------------------------------------------------------
     Coming apart: the break-up effect

     The watchface animation throws the digits away and the time is then built up again from
     scratch, which also picks fresh tilings and fresh colours. Three ways of
     coming apart: the stack collapsing, the pieces flying off, and the rows
     being cleared away from the bottom. The first two need pieces that move
     freely, the third needs a cell grid - both are built from the same tables
     when the animation starts, and only the one the style needs is filled in.
     ------------------------------------------------------------------------- */
  static const uint8_t  BREAKUP_STEP_MS     = 30;      // one animation step
  static const int16_t  BREAKUP_GRAVITY     = 10;      // added to the fall speed each step, 1/16 px
  static const uint8_t  ROW_FLASH_STEPS = 3;       // steps a row stays lit before it goes
  static const uint16_t HOLD_MS     = 300;     // beat of empty space before it builds again
  // The longest break-up, measured on the PC (tools.local/refactor/host,
  // 2026-10-20): rows cleared take 0.9 s, the others up to 0.6 s upright.
  static const uint16_t BREAKUP_MAX_MS        = 900;
  // The hour change's line clear: the old time's rows go four at a time from
  // the bottom, as four lines cleared at once in the game - each group lit
  // white three times, then gone, and the rows above drop into the gap. Not a
  // choice of the break-up effect: the hour change's own.
  static const uint8_t  BREAKUP_FOUR_ROWS   = BREAKUP_COUNT;
  static const uint8_t  FOUR_ROWS           = 4;
  static const uint8_t  FOUR_BLINK_STEPS    = 3;   // lit, then its colours, each this many steps
  static const uint8_t  FOUR_GROUP_STEPS    = 6 * FOUR_BLINK_STEPS;   // three times lit, the last half already gone
  static const uint16_t FOUR_ROWS_MS =
      (uint16_t)((TETRIS_GRID_H + FOUR_ROWS - 1) / FOUR_ROWS) * FOUR_GROUP_STEPS * BREAKUP_STEP_MS;
  // All of this is per digit, not per display: the animation takes all four apart
  // at once, but a minute change only replaces the digits that actually
  // changed, and those have to come apart on their own while the rest stand
  // still.
  static const uint8_t  MAX_PIECES  = 13;      // the most any digit is made of

  // State of one of the four digits.
  struct Digit {
    uint8_t value;      // 0..9, or 0xFF when nothing has been placed yet
    uint8_t variant;    // which tiling of that digit is being built
    uint8_t piece;      // index of the piece currently falling; == piece count when settled
    uint8_t fall;       // cells that piece still has to drop
    uint8_t turns;      // quarter turns this piece still owes
    unsigned long turnAt[3];  // millis() at which each of them happens
    int8_t  shiftDx;    // cells the piece still starts to the side of its column
    uint8_t shiftAt;    // rows it falls before it starts walking across
    uint8_t shiftEvery; // rows between one column change and the next
    uint8_t fall0;      // the fall it started with, to work out how far it has come
    uint8_t hue[TETRIS_COLOUR_CLASSES];  // colour class -> palette entry
  };

  // One flying piece. Position and speed are in 1/16 of a pixel so the
  // movement does not step in whole pixels.
  struct Debris {
    int16_t x16, y16;    // top-left corner
    int16_t vx16, vy16;  // speed per step
    uint8_t orient;
    uint8_t colour;      // index into _pal
    uint8_t delay;       // steps to wait before this one starts moving
    uint8_t spin;        // steps between quarter turns, 0 = does not turn
    uint8_t spinTick;    // steps since this one last turned
  };

  static uint16_t pieceAt(uint8_t digit, uint8_t variant, uint8_t piece) {
    return TETRIS_VAR[digit][(uint16_t)variant * TETRIS_PIECES_PER_DIGIT[digit] + piece];
  }

  // Send the next piece on its way: set how far it falls and decide at which
  // heights it flicks round a quarter. The heights are spread over the fall with
  // a random position inside each band, so the turns look tapped out by hand
  // instead of clocked. Nothing turns during the last TETRIS_SETTLE cells, so a
  // piece always arrives in the orientation it will keep.
  void armPiece(uint8_t idx, unsigned long ms) {
    Digit &s = _digits[idx];
    uint16_t rec     = pieceAt(s.value, s.variant, s.piece);
    uint8_t  oFinal  = TETRIS_PIECE_ORIENT(rec);
    uint8_t  hFinal  = TETRIS_SIZE[oFinal] & 0x0F;
    uint8_t  landing = TETRIS_PIECE_TOP(rec);
    uint8_t  targetX = TETRIS_PIECE_X(rec);
    // The fall is led by the piece's bottom edge, so that is what the start
    // height is measured on: it begins _drop* cells above the digit box and
    // travels down to where it lands. Anchoring the TOP instead, as an earlier
    // version did, left tall pieces half inside the panel at the moment they
    // appeared, because a quarter turn swaps the bounding box and the top edge is
    // then up to three cells away from where the fall actually starts.
    uint8_t  drop    = (idx < 2) ? (uint8_t)_dropHour : (uint8_t)_dropMin;
    s.fall  = landing + hFinal - 1 + drop;
    s.fall0 = s.fall;

    // How much room there is to manoeuvre: the rows it can fall while it is still
    // clear of everything already lying in the digit. The game's rule is that a
    // piece may not pass through the stack, and both the turning and the walk
    // sideways are kept inside this window for exactly that reason. A high stack
    // therefore leaves little room for either - as it does in the game.
    int topRow = TETRIS_GRID_H;
    for (uint8_t q = 0; q < s.piece; q++) {
      uint16_t o = pieceAt(s.value, s.variant, q);
      if ((int)TETRIS_PIECE_TOP(o) < topRow) { topRow = TETRIS_PIECE_TOP(o); }
    }
    // After `clear` steps the piece's bottom edge is level with the top of the
    // stack, so only the steps before that are genuinely free.
    int bottom0 = (int)landing + (int)hFinal - 1 - (int)s.fall;
    int clear   = topRow - bottom0;
    if (clear < 0) { clear = 0; }
    if (clear > (int)s.fall) { clear = (int)s.fall; }

    // Quarter turns, all of which have to be over before it reaches the stack.
    // Not every piece turns, and one that does rarely uses every turn it could
    // have, so some come down flat while others tumble.
    uint8_t turns = 0;
    // Every turn has to be over by the last genuinely free step, so that from
    // there on the piece is in the orientation it will land in.
    int turnSteps = clear - 1;
    if (turnSteps > (int)s.fall - TETRIS_SETTLE) { turnSteps = (int)s.fall - TETRIS_SETTLE; }
    unsigned long band  = _s.spinMs;
    unsigned long start = 0;
    if (turnSteps > 0 && band > 0) {
      unsigned long budget = (unsigned long)turnSteps * _s.dropMs;
      uint8_t fits = 0;
      while (fits < 3 && (unsigned long)(fits + 1) * band <= budget) { fits++; }
      turns = (fits == 0) ? 0 : (uint8_t)faceRandom(fits + 1);
      if (turns > 0) {
        // The turns themselves take turns*spinMs; whatever room is left over is
        // where they may begin. A piece with a long way to fall can therefore
        // still be turning well down the panel instead of sorting itself out at
        // the top, which is what a player does too.
        unsigned long slack = budget - (unsigned long)turns * band;
        start = (unsigned long)faceRandom(slack + 1);
      }
    }
    s.turns = turns;
    // One turn per band, at a random point inside it: the gaps come out uneven
    // rather than metronomic, and they always finish inside the free window.
    for (uint8_t i = 0; i < turns; i++) {
      s.turnAt[i] = ms + start + (unsigned long)i * band + 1 + (unsigned long)faceRandom(band);
    }

    // Where it comes in, and how far it has to walk.
    //
    // The game spawns a piece centred over the field, rounded to the left, and the
    // player walks it across. Same here, and the whole walk is over before the
    // piece reaches the stack, because from there on only its own column is clear.
    //
    // The game would also allow one more move as the piece touches down - that is
    // what the lock delay is for. It is deliberately not used here: a piece can
    // only be walked into place at the end if it could descend in the wrong column
    // first, and with tilings where every piece is reached by a straight drop that
    // column is usually blocked. Tried it, and the replay caught pieces sinking
    // straight through the stack while they held their last move back. The move is
    // worth one extra cell of reach in about one case in 250, and costs a piece
    // passing through the stack in far more.
    //
    // When there is less room than distance, the piece comes in closer to its
    // column instead - which is what happens in the game as well: a high stack
    // leaves no time to walk a piece across.
    uint8_t oSpawn = oFinal;
    for (uint8_t i = 0; i < turns; i++) { oSpawn = TETRIS_TURN[oSpawn]; }
    int spawnX   = (TETRIS_GRID_W - (TETRIS_SIZE[oSpawn] >> 4)) / 2;   // rounded left
    int maxShift = clear - 1;      // every move on a step that is still free
    if (maxShift < 0) { maxShift = 0; }
    if (maxShift > (int)s.fall) { maxShift = (int)s.fall; }
    int dx = spawnX - (int)targetX;
    if (dx >  maxShift) { dx =  maxShift; }
    if (dx < -maxShift) { dx = -maxShift; }
    s.shiftDx = (int8_t)dx;
    // When it sets off, and how briskly it walks, are drawn separately and
    // separately from the turning - so one piece darts across the moment it
    // appears while another drifts over a column at a time well down the panel.
    // Both still finish inside the free window.
    int span = (dx < 0) ? -dx : dx;
    s.shiftEvery = 1;
    s.shiftAt    = 0;
    if (span > 0) {
      int strideMax = (span > 1) ? (maxShift - 1) / (span - 1) : maxShift;
      if (strideMax > 3) { strideMax = 3; }        // slower than this reads as drifting
      if (strideMax < 1) { strideMax = 1; }
      s.shiftEvery = (uint8_t)(1 + faceRandom(strideMax));
      int needed = 1 + (span - 1) * (int)s.shiftEvery;
      int slack  = maxShift - needed;
      if (slack < 0) { slack = 0; }
      s.shiftAt = (uint8_t)faceRandom(slack + 1);
    }
  }

  // Quarter turns this digit still owes, which is how far short of its landing
  // orientation the falling piece is drawn.
  uint8_t pendingTurns(uint8_t idx, unsigned long ms) const {
    const Digit &s = _digits[idx];
    uint8_t pending = 0;
    for (uint8_t i = 0; i < s.turns; i++) {
      if ((long)(s.turnAt[i] - ms) > 0) { pending++; }
    }
    return pending;
  }

  uint8_t allPendingTurns(unsigned long ms) const {
    uint8_t n = 0;
    for (uint8_t i = 0; i < 4; i++) { n += pendingTurns(i, ms); }
    return n;
  }

  // Start building a digit: new tiling, new colours, first piece at the top.
  //
  // Do NOT add a randomSeed() anywhere for this: on the S3, random() reads the
  // hardware generator until randomSeed() is called, and calling it switches the
  // core over to a seeded software PRNG (WMath.cpp:44-61). Seeding would make the
  // variety worse, not better.
  void startDigit(uint8_t idx, uint8_t value, unsigned long ms) {
    Digit &s = _digits[idx];
    s.value   = value;
    // Per digit, not TETRIS_VARIANTS: only builds where every piece lands on
    // something are shipped, and the 2, the 5 and the 7 have fewer of those.
    s.variant = (uint8_t)faceRandom(TETRIS_VARIANTS_PER_DIGIT[value]);
    s.piece   = 0;
    armPiece(idx, ms);
    for (uint8_t i = 0; i < TETRIS_COLOUR_CLASSES; i++) { s.hue[i] = i; }
    for (uint8_t i = TETRIS_COLOUR_CLASSES - 1; i > 0; i--) {   // Fisher-Yates
      uint8_t j = (uint8_t)faceRandom(i + 1);
      uint8_t t = s.hue[i]; s.hue[i] = s.hue[j]; s.hue[j] = t;
    }
  }

  // Place the digit groups for the panel's rotation and rebuild all four
  // digits. A group is TETRIS_GRID_W x TETRIS_GRID_H cells, so 12 x 20 px, and
  // the two digits of a group sit TETRIS_PITCH apart; the coordinates below are
  // the top-left corner of each group.
  void layout(const FaceNow &now) {
    _rotation  = now.turn;
    _landscape = now.turn == 0 || now.turn == 2;
    if (_landscape) {
      // 64x32: one line. Hours x 2..27, minutes x 36..61, both rows 6..25.
      _xHour = 2;  _xMin = 36;
      _yHour = 6;  _yMin = 6;
      // A piece starts with its BOTTOM edge one row above the panel, so the whole
      // of it is off screen however tall it happens to be at that moment, and it
      // slides in over the edge. Measured in cells above the digit box.
      _dropHour = _dropMin = _yHour / TETRIS_CELL + 1;
    } else {
      // 32x64: hours above minutes, x 3..28, rows 8..27 and 36..55.
      _xHour = 3;  _xMin = 3;
      _yHour = 8;  _yMin = 36;
      _dropHour = _yHour / TETRIS_CELL + 1;   // off the top of the panel
      // The minutes have the hours above them, not the panel edge, so they start
      // in the eight-pixel gap between the two rows: bottom edge on its lowest
      // row, which leaves room for a piece of any height without reaching up into
      // the hours.
      _dropMin = 1;
    }
    for (uint8_t i = 0; i < 4; i++) {
      _digits[i].value = 0xFF;
      _holdUntil[i] = 0;   // a rotation rebuilds at once, it does not wait
    }
    _settled = false;
    _hourPart = PART_NONE;   // a turn rebuilds: an hour change under way is over
    _hueShift = 0;
  }

  // Hand the current HH:MM to the four digits. Only a digit whose value really
  // changed is rebuilt - the others keep the blocks they have already dropped.
  void pushTime(const FaceNow &now) {
    if (!now.set) { return; }   // nothing to build yet; draw() shows dashes
    uint8_t d[4];
    faceDigits(now.local, true, d, 0xFF);
    uint8_t changeStyle = 0xFF;   // drawn once, so digits changing together match
    for (uint8_t i = 0; i < 4; i++) {
      if (_breaking[i]) { continue; }   // still coming apart, it will be rebuilt after
      // The pause after it came apart; 0 = none, and cleared once over, so an
      // old one never reads as ahead again. Signed, so it survives the
      // millis() wrap (the plain < left a digit blank for 49.7 days when a
      // pause spanned the wrap; code review of 2026-10-14).
      if (_holdUntil[i] != 0) {
        if ((long)(now.ms - _holdUntil[i]) < 0) { continue; }
        _holdUntil[i] = 0;
      }
      if (d[i] == _digits[i].value) { continue; }
      // The outgoing digit breaks up rather than vanishing at once (decided by
      // Peter, 2026-10-08: the effect always counts at a time change). Only one
      // that is actually standing there can break up, so a digit still being
      // built, or not yet drawn at all, is just swapped.
      if (_digits[i].value <= 9 && _digits[i].piece > 0) {
        if (changeStyle == 0xFF) { changeStyle = pickBreakUp(); }
        breakUpDigit(i, changeStyle);
        continue;
      }
      startDigit(i, d[i], now.ms);
      _settled = false;
    }
  }

  // Move every digit on by one fall step. A piece drops one cell per step; when
  // it has arrived the next one starts at the top of the digit box.
  void step(unsigned long ms) {
    bool allSettled = true;
    for (uint8_t i = 0; i < 4; i++) {
      Digit &s = _digits[i];
      if (s.value > 9 || _breaking[i]) { continue; }
      uint8_t total = TETRIS_PIECES_PER_DIGIT[s.value];
      if (s.piece >= total) { continue; }        // this digit is complete
      allSettled = false;
      if (s.fall > 0) { s.fall--; continue; }
      s.piece++;
      if (s.piece < total) { armPiece(i, ms); }
    }
    _settled = allSettled;
  }

  // Draw one digit: every piece already dropped, plus the one still falling.
  //
  // The falling piece spins into place. A quarter turn changes the piece's
  // bounding box, so a rotated state can be wider than where it will land - the
  // library this replaced simply let those pixels run off the panel (measured: 21
  // of its 229 fall states left the six-cell digit box, by up to 6 px, which on
  // the rightmost digit meant drawing to x=67 on a 64 px panel). Here the spinning
  // piece is kept inside its own digit box instead, the way a real game kicks a
  // piece off the wall when you rotate against it. Vertically the piece hangs from
  // its landing edge, so its bottom travels smoothly however it is turned.
  void drawDigit(Adafruit_GFX &g, uint8_t idx, int originX, int originY, unsigned long ms) const {
    const Digit &s = _digits[idx];
    if (s.value > 9) { return; }
    uint8_t total = TETRIS_PIECES_PER_DIGIT[s.value];
    uint8_t last  = (s.piece < total) ? s.piece : (uint8_t)(total - 1);
    for (uint8_t p = 0; p <= last; p++) {
      uint16_t rec    = pieceAt(s.value, s.variant, p);
      uint8_t  orient = TETRIS_PIECE_ORIENT(rec);
      int      lift   = (p == s.piece) ? s.fall : 0;   // only the current one is airborne
      // One cell closer to its column for every row it has fallen; the last of
      // those moves may be the one it makes as it lands.
      int      cx     = TETRIS_PIECE_X(rec);
      if (lift > 0 && s.shiftDx != 0 && p == s.piece) {
        int total = (s.shiftDx < 0) ? -s.shiftDx : s.shiftDx;
        // Column changes happen every shiftEvery rows, starting shiftAt rows down.
        int rows = (int)s.fall0 - lift - (int)s.shiftAt - 1;
        int done = (rows < 0) ? 0 : (rows / (int)s.shiftEvery) + 1;
        if (done > total) { done = total; }
        int left = total - done;
        if (left > 0) { cx += (s.shiftDx < 0) ? -left : left; }
      }
      int      bottom = (int)TETRIS_PIECE_TOP(rec) + (TETRIS_SIZE[orient] & 0x0F) - 1 - lift;

      if (lift > 0) {
        // Turns still to come: the piece is shown that many quarters short of its
        // landing orientation and unwinds one flick at a time on the way down.
        uint8_t turns = pendingTurns(idx, ms);
        while (turns--) { orient = TETRIS_TURN[orient]; }
        int w = TETRIS_SIZE[orient] >> 4;
        if (cx + w > TETRIS_GRID_W) { cx = TETRIS_GRID_W - w; }   // kick off the wall
        if (cx < 0) { cx = 0; }
      }

      // The hour's level-up colours move every piece on through the palette
      // together: the colouring stays proper, touching pieces apart.
      uint16_t col = _pal[(s.hue[TETRIS_PIECE_CLASS(rec)] + _hueShift) % TETRIS_COLOUR_CLASSES];
      int      bx  = originX + cx * TETRIS_CELL;
      int      by  = originY + (bottom - (TETRIS_SIZE[orient] & 0x0F) + 1) * TETRIS_CELL;
      for (uint8_t c = 0; c < 4; c++) {
        uint8_t cell = TETRIS_SHAPE[orient][c];
        g.fillRect(bx + (cell >> 4) * TETRIS_CELL, by + (cell & 0x0F) * TETRIS_CELL,
                   TETRIS_CELL, TETRIS_CELL, col);
      }
    }
  }

  // The separator between hours and minutes: vertically stacked between the two
  // groups in landscape, a horizontal pair in the gap between the rows in portrait.
  void drawColon(Adafruit_GFX &g, const FacePaint &paint) const {
    uint16_t c = paint.visible(255, 255, 255);
    if (_landscape) {
      int x = _xHour + TETRIS_PITCH * 2;   // x 30..33, between the two groups
      g.fillRect(x, _yHour + 4,  TETRIS_DOT, TETRIS_DOT, c);  // y 10..13
      g.fillRect(x, _yHour + 12, TETRIS_DOT, TETRIS_DOT, c);  // y 18..21
    } else {
      int y = _yHour + 22;                 // y 30..33, in the 8 px gap between rows
      g.fillRect(_xHour + 5,  y, TETRIS_DOT, TETRIS_DOT, c);  // x 8..11
      g.fillRect(_xHour + 17, y, TETRIS_DOT, TETRIS_DOT, c);  // x 20..23
    }
  }

  // Stand-ins for the four digits while the clock has no time yet: a bar of
  // blocks across the middle of each digit box, in the colon's colour.
  void drawDashes(Adafruit_GFX &g, const FacePaint &paint) const {
    uint16_t c = paint.visible(255, 255, 255);
    for (uint8_t i = 0; i < 4; i++) {
      g.fillRect(originX(i) + TETRIS_CELL, originY(i) + 4 * TETRIS_CELL,
                 4 * TETRIS_CELL, 2 * TETRIS_CELL, c);
    }
  }

  // The fall steps the slowest of the digits `to` takes to be built - each
  // with the longest of its tilings - of all four, or of those that differ
  // from `from`.
  uint16_t buildSteps(const uint8_t to[4], const uint8_t *from) const {
    uint16_t most = 0;
    for (uint8_t i = 0; i < 4; i++) {
      uint8_t v = to[i];
      if (v > 9 || (from && from[i] == v)) { continue; }
      int drop = (i < 2) ? _dropHour : _dropMin;
      for (uint8_t var = 0; var < TETRIS_VARIANTS_PER_DIGIT[v]; var++) {
        uint16_t steps = 0;
        for (uint8_t p = 0; p < TETRIS_PIECES_PER_DIGIT[v]; p++) {
          uint16_t rec = pieceAt(v, var, p);
          steps += TETRIS_PIECE_TOP(rec) + (TETRIS_SIZE[TETRIS_PIECE_ORIENT(rec)] & 0x0F) + drop;
        }
        if (steps > most) { most = steps; }
      }
    }
    return most;
  }

  int originX(uint8_t idx) const { return ((idx < 2) ? _xHour : _xMin) + ((idx & 1) ? TETRIS_PITCH : 0); }
  int originY(uint8_t idx) const { return (idx < 2) ? _yHour : _yMin; }

  // The style is chosen per EVENT and passed in, never per digit: with "random"
  // selected, picking it per digit would give one animation four different effects at
  // once.
  uint8_t pickBreakUp() const {
    uint8_t style = _s.breakUp;
    if (style >= BREAKUP_RANDOM) { style = (uint8_t)faceRandom(BREAKUP_RANDOM); }
    return style;
  }

  // Take one digit apart. Only the pieces that have actually landed are thrown;
  // one still in the air simply goes with the rest of the build.
  void breakUpDigit(uint8_t d, uint8_t style) {
    Digit &s = _digits[d];
    bool rows = (style == BREAKUP_CLEAR || style == BREAKUP_FOUR_ROWS);   // these two take rows away, on a grid
    _breakUpOf[d]    = style;
    _debrisCount[d]      = 0;
    _breakUpSteps[d]      = 0;
    _rowTick[d]  = 0;
    _rowsLeft[d] = rows ? TETRIS_GRID_H : 0;
    if (rows) { memset(_clearGrid[d], 0, TETRIS_GRID_W * TETRIS_GRID_H); }
    if (s.value > 9) { return; }   // nothing standing there, so nothing to throw

    uint8_t total   = TETRIS_PIECES_PER_DIGIT[s.value];
    uint8_t settled = (s.piece < total) ? s.piece : total;
    // Marked busy only when there is something to show, or an empty digit would
    // hold the whole display in the animation for nothing.
    _breaking[d] = (settled > 0);
    if (!_breaking[d]) { return; }
    for (uint8_t p = 0; p < settled; p++) {
      uint16_t rec    = pieceAt(s.value, s.variant, p);
      uint8_t  orient = TETRIS_PIECE_ORIENT(rec);
      uint8_t  colour = s.hue[TETRIS_PIECE_CLASS(rec)];
      if (rows) {
        for (uint8_t c = 0; c < 4; c++) {
          uint8_t cell = TETRIS_SHAPE[orient][c];
          uint8_t gx = TETRIS_PIECE_X(rec) + (cell >> 4);
          uint8_t gy = TETRIS_PIECE_TOP(rec) + (cell & 0x0F);
          _clearGrid[d][gy * TETRIS_GRID_W + gx] = colour + 1;
        }
      } else if (_debrisCount[d] < MAX_PIECES) {
        Debris &f = _debris[d][_debrisCount[d]++];
        f.orient = orient;
        f.colour = colour;
        f.x16 = (int16_t)((originX(d) + TETRIS_PIECE_X(rec) * TETRIS_CELL) * 16);
        f.y16 = (int16_t)((originY(d) + TETRIS_PIECE_TOP(rec) * TETRIS_CELL) * 16);
        if (style == BREAKUP_COLLAPSE) {
          // The bottom gives way first and the rest sinks after it.
          f.vx16 = 0;
          f.vy16 = 0;
          f.spin = 0;
          f.spinTick = 0;
          f.delay = (uint8_t)((TETRIS_GRID_H - 1 - TETRIS_PIECE_TOP(rec)) / 2);
        } else {
          f.vx16  = (int16_t)faceRandom(-40, 41);
          f.vy16  = (int16_t)faceRandom(-48, -8);   // a kick upwards first
          f.spin  = (uint8_t)faceRandom(2, 6);
          f.spinTick = (uint8_t)faceRandom(f.spin);   // start them out of step
          f.delay = 0;
        }
      }
    }
  }

  bool breakingUp() const {
    for (uint8_t d = 0; d < 4; d++) { if (_breaking[d]) { return true; } }
    return false;
  }

  // Take all four apart at once - the watchface animation.
  void breakUpAll(unsigned long ms) {
    _breakUpStepLast = ms;
    uint8_t style = pickBreakUp();   // one effect for the whole animation
    for (uint8_t d = 0; d < 4; d++) { breakUpDigit(d, style); }
  }

  // Advance one digit by one step. Returns true once it has nothing left to show.
  bool stepBreakUp(uint8_t d, int16_t panelH) {
    if (++_breakUpSteps[d] > 150) { return true; }   // never let it hang

    if (_breakUpOf[d] == BREAKUP_FOUR_ROWS) {
      if (++_rowTick[d] < FOUR_GROUP_STEPS) { return false; }
      _rowTick[d] = 0;
      uint8_t n = (_rowsLeft[d] < FOUR_ROWS) ? _rowsLeft[d] : FOUR_ROWS;
      for (int r = TETRIS_GRID_H - 1; r >= (int)n; r--) {   // the rows above drop into the gap
        memcpy(&_clearGrid[d][r * TETRIS_GRID_W], &_clearGrid[d][(r - n) * TETRIS_GRID_W], TETRIS_GRID_W);
      }
      memset(&_clearGrid[d][0], 0, (size_t)n * TETRIS_GRID_W);
      _rowsLeft[d] -= n;
      return _rowsLeft[d] == 0;
    }

    if (_breakUpOf[d] == BREAKUP_CLEAR) {
      if (++_rowTick[d] < ROW_FLASH_STEPS) { return false; }
      _rowTick[d] = 0;
      for (uint8_t r = TETRIS_GRID_H - 1; r > 0; r--) {   // everything drops into the gap
        memcpy(&_clearGrid[d][r * TETRIS_GRID_W],
               &_clearGrid[d][(r - 1) * TETRIS_GRID_W], TETRIS_GRID_W);
      }
      memset(&_clearGrid[d][0], 0, TETRIS_GRID_W);
      return (_rowsLeft[d] == 0) || (--_rowsLeft[d] == 0);
    }

    bool allGone = true;
    for (uint8_t i = 0; i < _debrisCount[d]; i++) {
      Debris &f = _debris[d][i];
      if (f.delay) { f.delay--; allGone = false; continue; }
      f.vy16 += BREAKUP_GRAVITY;
      f.x16  += f.vx16;
      f.y16  += f.vy16;
      // A quarter turn swaps the piece's bounding box, so shift it by half the
      // difference and its middle stays put; anchored at the top-left instead, a
      // long piece would jump three cells sideways on every turn. The tick is per
      // piece, so pieces sharing a rate do not all flick on the same frame.
      if (f.spin && ++f.spinTick >= f.spin) {
        f.spinTick = 0;
        uint8_t o = f.orient, n = TETRIS_TURN[o];
        int dw = (int)(TETRIS_SIZE[o] >> 4)   - (int)(TETRIS_SIZE[n] >> 4);
        int dh = (int)(TETRIS_SIZE[o] & 0x0F) - (int)(TETRIS_SIZE[n] & 0x0F);
        f.x16 = (int16_t)(f.x16 + dw * TETRIS_CELL * 8);   // 8 = half of the sixteenths
        f.y16 = (int16_t)(f.y16 + dh * TETRIS_CELL * 8);
        f.orient = n;
      }
      if ((f.y16 >> 4) < panelH) { allGone = false; }
    }
    return allGone;
  }

  // Draw the one digit that is coming apart.
  void drawBreakUp(Adafruit_GFX &g, uint8_t d, const FacePaint &paint) const {
    if (_breakUpOf[d] == BREAKUP_FOUR_ROWS) {
      // The group going: the bottom rows, lit white, in their colours, lit,
      // ... and in the last blink already gone.
      uint8_t n = (_rowsLeft[d] < FOUR_ROWS) ? _rowsLeft[d] : FOUR_ROWS;
      uint8_t blink = _rowTick[d] / FOUR_BLINK_STEPS;   // 0..5
      uint16_t white = paint.visible(255, 255, 255);
      int ox = originX(d), oy = originY(d);
      for (uint8_t gy = 0; gy < TETRIS_GRID_H; gy++) {
        bool going = gy >= TETRIS_GRID_H - n;
        if (going && blink == 5) { continue; }
        for (uint8_t gx = 0; gx < TETRIS_GRID_W; gx++) {
          uint8_t v = _clearGrid[d][gy * TETRIS_GRID_W + gx];
          if (!v) { continue; }
          uint16_t col = (going && (blink & 1) == 0) ? white : _pal[v - 1];
          g.fillRect(ox + gx * TETRIS_CELL, oy + gy * TETRIS_CELL, TETRIS_CELL, TETRIS_CELL, col);
        }
      }
      return;
    }
    if (_breakUpOf[d] == BREAKUP_CLEAR) {
      bool flash = (_rowTick[d] == ROW_FLASH_STEPS - 1);
      uint16_t white = paint.visible(255, 255, 255);
      int ox = originX(d), oy = originY(d);
      for (uint8_t gy = 0; gy < TETRIS_GRID_H; gy++) {
        for (uint8_t gx = 0; gx < TETRIS_GRID_W; gx++) {
          uint8_t v = _clearGrid[d][gy * TETRIS_GRID_W + gx];
          if (!v) { continue; }
          uint16_t col = (flash && gy == TETRIS_GRID_H - 1) ? white : _pal[v - 1];
          g.fillRect(ox + gx * TETRIS_CELL, oy + gy * TETRIS_CELL, TETRIS_CELL, TETRIS_CELL, col);
        }
      }
      return;
    }
    for (uint8_t i = 0; i < _debrisCount[d]; i++) {
      const Debris &f = _debris[d][i];
      int bx = f.x16 >> 4, by = f.y16 >> 4;
      uint16_t col = _pal[f.colour];
      for (uint8_t c = 0; c < 4; c++) {
        uint8_t cell = TETRIS_SHAPE[f.orient][c];
        g.fillRect(bx + (cell >> 4) * TETRIS_CELL, by + (cell & 0x0F) * TETRIS_CELL,
                   TETRIS_CELL, TETRIS_CELL, col);
      }
    }
  }

  /* ----------------------------------------------------------------------
     The change to a full hour (Peter, 2026-10-23)

     All four digits come apart - the line clear takes the old time's rows
     away four at a time, the others use the break-up effect - and are built
     anew; then, for the colours and the rocket, the effect plays over the
     time that stands. In parts: PART_REBUILD from the start until every
     digit stands again, PART_EFFECT until the effect is over.
     ------------------------------------------------------------------------- */
  static const uint8_t  PART_NONE = 0, PART_REBUILD = 1, PART_EFFECT = 2;
  // Level-up colours: every piece's colour moves on a hue per step, twice
  // round the six, and the last step is its own colours again.
  static const uint16_t COLOUR_STEP_MS = 200;
  static const uint8_t  COLOUR_STEPS   = 2 * TETRIS_COLOUR_CLASSES;
  static const uint16_t COLOURS_MS     = COLOUR_STEPS * COLOUR_STEP_MS;
  // The rocket: it stands ROCKET_STAND_MS on its flame with smoke spreading,
  // then lifts off, faster and faster, and has left the panel by ROCKET_MS.
  static const uint16_t ROCKET_STEP_MS  = 40;
  static const uint16_t ROCKET_STAND_MS = 900;
  static const uint16_t ROCKET_MS       = 2800;
  static const uint8_t  ROCKET_W = 6, ROCKET_H = 9, ROCKET_FLAME = 3;
  static const uint8_t  SMOKE_PUFFS = 4;          // to each side
  static const uint16_t SMOKE_LIFE_MS = 1400;
  // Own pixel art after the rocket Game Boy Tetris launches: nose and fins
  // red (R), the body light grey (W), a blue window (B).
  static constexpr const char *ROCKET_ROWS[ROCKET_H] = {
    "..RR..",
    ".RRRR.",
    ".WWWW.",
    ".WBBW.",
    ".WBBW.",
    ".WWWW.",
    ".WWWW.",
    "RWWWWR",
    "RR..RR",
  };

  // Every digit has blocks standing: the hour change has something to take apart.
  bool allStanding() const {
    for (uint8_t i = 0; i < 4; i++) { if (_digits[i].value > 9 || _digits[i].piece == 0) { return false; } }
    return true;
  }

  // Every digit built to its last piece.
  bool allBuilt() const {
    for (uint8_t i = 0; i < 4; i++) {
      const Digit &s = _digits[i];
      if (s.value > 9 || s.piece < TETRIS_PIECES_PER_DIGIT[s.value]) { return false; }
    }
    return true;
  }

  uint8_t pickHour() const {
    uint8_t style = _s.hourAnim;
    if (style >= HOUR_RANDOM) { style = (uint8_t)faceRandom(HOUR_RANDOM); }
    return style;
  }

  bool hourEffectRuns() const { return _hourPart == PART_EFFECT; }
  bool rocketFlies() const { return _hourPart == PART_EFFECT && _hourStyle == HOUR_ROCKET; }

  // The hour change begins: all four come apart together.
  void startHour(const FaceNow &now, uint8_t style) {
    _hourStyle = style;
    _hourPart  = PART_REBUILD;
    _hueShift  = 0;
    _breakUpStepLast = now.ms;
    uint8_t apart = (style == HOUR_LINE_CLEAR) ? BREAKUP_FOUR_ROWS : pickBreakUp();
    for (uint8_t d = 0; d < 4; d++) { breakUpDigit(d, apart); }
    Console.printf("Tetris: the hour change, %s\n", TETRIS_THR_LABELS[style]);
  }

  // On every pass: once the rebuild stands, the effect begins (the line clear
  // has none); the effect steps on its own clock. Whether its picture moved.
  bool stepHour(const FaceNow &now) {
    if (_hourPart == PART_REBUILD) {
      if (breakingUp() || !_settled || !allBuilt()) { return false; }
      if (_hourStyle == HOUR_LINE_CLEAR) { endHour(now); return false; }
      _hourPart    = PART_EFFECT;
      _effectAt    = now.ms;
      _effectFrame = 0xFFFF;
      _rocketSeed  = (uint8_t)faceRandom(256);
    }
    if (_hourPart != PART_EFFECT) { return false; }
    unsigned long t = now.ms - _effectAt;
    bool colours = (_hourStyle == HOUR_COLOURS);
    if (t >= (colours ? COLOURS_MS : ROCKET_MS)) { endHour(now); return true; }
    uint16_t frame = (uint16_t)(t / (colours ? COLOUR_STEP_MS : ROCKET_STEP_MS));
    if (frame == _effectFrame) { return false; }
    _effectFrame = frame;
    if (colours) { _hueShift = (uint8_t)((frame + 1) % TETRIS_COLOUR_CLASSES); }
    return true;
  }

  void endHour(const FaceNow &now) {
    if (rocketFlies()) { _colonDarkIn = now.local; }   // the colon waits for the next second
    _hourPart = PART_NONE;
    _hueShift = 0;
    _breakUpEndedAt = now.ms;   // the rest after an animation, as after a break-up
  }

  // How long the hour change takes with this style, its build `build` ms.
  unsigned long hourLength(uint8_t style, unsigned long build) const {
    unsigned long apart  = (style == HOUR_LINE_CLEAR) ? FOUR_ROWS_MS : BREAKUP_MAX_MS;
    unsigned long effect = (style == HOUR_COLOURS) ? COLOURS_MS : (style == HOUR_ROCKET) ? ROCKET_MS : 0;
    return apart + HOLD_MS + build + effect;
  }

  // The rocket, in the middle of the panel: in landscape in the gap between
  // the hours and the minutes (x 29..34), upright over the digits. Smoke
  // puffs spread from its foot while it stands and fade; then it lifts off,
  // faster and faster, a short trail below its flame.
  void drawRocket(Adafruit_GFX &g, const FaceNow &now, const FacePaint &paint) const {
    unsigned long t = now.ms - _effectAt;
    if (t >= ROCKET_MS) { return; }
    int x0 = now.w / 2 - ROCKET_W / 2;
    int y0 = now.h - ROCKET_FLAME - ROCKET_H;   // its top while it stands, the flame below it on the panel
    int lift = 0;
    bool flying = t > ROCKET_STAND_MS;
    if (flying) {
      unsigned long f = t - ROCKET_STAND_MS, fly = ROCKET_MS - ROCKET_STAND_MS;
      unsigned long dist = (unsigned long)(y0 + ROCKET_H + ROCKET_FLAME + 1);   // until the flame has left the top
      lift = (int)(dist * f / fly * f / fly);
    }
    int top = y0 - lift;

    // Smoke, behind the rocket: a puff to each side every 170 ms while it
    // stands, drifting out a pixel per 100 ms and fading over SMOKE_LIFE_MS.
    for (uint8_t i = 0; i < SMOKE_PUFFS; i++) {
      unsigned long born = 150UL + i * 170UL;
      if (t < born) { break; }
      unsigned long age = t - born;
      if (age >= SMOKE_LIFE_MS) { continue; }
      uint8_t grey = (uint8_t)(150UL * (SMOKE_LIFE_MS - age) / SMOKE_LIFE_MS);
      uint16_t c = paint.color(grey, grey, grey);
      int dx = 2 + (int)(age / 100);
      int y = now.h - 2 - ((_rocketSeed >> i) & 1);
      g.fillRect(x0 - dx, y, 2, 2, c);
      g.fillRect(x0 + ROCKET_W - 2 + dx, y, 2, 2, c);
    }

    // The flame under its middle: one row as it lights, then two or three,
    // flickering; in flight a trail of two fading pixels below it.
    uint16_t frame = (uint16_t)(t / ROCKET_STEP_MS);
    uint8_t flick = (uint8_t)((frame * 7U + _rocketSeed) % 3U);
    uint8_t len = (t < 300) ? 1 : (flick != 0 ? 3 : 2);
    for (uint8_t r = 0; r < len; r++) {
      uint16_t c = (r == 0) ? paint.color(255, 230, 0) : paint.color(255, r == 1 ? 120 : 60, 0);
      g.fillRect(x0 + 2, top + ROCKET_H + r, 2, 1, c);
    }
    if (flying) {
      g.fillRect(x0 + 2, top + ROCKET_H + len, 2, 1, paint.color(90, 90, 90));
      g.fillRect(x0 + 2, top + ROCKET_H + len + 1, 2, 1, paint.color(45, 45, 45));
    }

    // The rocket itself.
    for (uint8_t r = 0; r < ROCKET_H; r++) {
      for (uint8_t c = 0; c < ROCKET_W; c++) {
        char k = ROCKET_ROWS[r][c];
        if (k == '.') { continue; }
        uint16_t col = (k == 'R') ? paint.color(255, 40, 30) : (k == 'B') ? paint.color(40, 120, 255) : paint.color(200, 200, 210);
        g.drawPixel(x0 + c, top + r, col);
      }
    }
  }

  const TetrisSettings &_s;
  Digit         _digits[4];
  uint16_t      _pal[TETRIS_COLOUR_CLASSES];  // palette at the current brightness
  uint16_t      _palBright = 0x100;           // the brightness it was made for; 0x100 = none yet
  uint8_t       _rotation   = 0xFF;   // rotation the layout was built for; 0xFF = build anew
  bool          _landscape  = false;  // orientation the layout was built for
  bool          _settled    = false;  // true once every block has landed
  unsigned long _stepLast   = 0;      // millis() of the last fall step
  bool          _colonOn    = false;  // the colon in this pass: on every even second
  bool          _colonDrawn = false;  // colon state of the frame on the panel
  uint8_t       _spinDrawn  = 0;      // turns still owed when that frame was drawn
  int           _xHour = 0, _xMin = 0;        // left edge of each digit group
  int           _yHour = 0, _yMin = 0;        // top edge of each digit group
  int           _dropHour = 4, _dropMin = 4;  // cells above the box where a piece's bottom edge starts

  bool          _breaking[4]    = {false, false, false, false};
  uint8_t       _breakUpOf[4] = {0, 0, 0, 0};   // the style that digit is using
  uint8_t       _debrisCount[4]   = {0, 0, 0, 0};   // pieces flying, per digit
  uint16_t      _breakUpSteps[4]   = {0, 0, 0, 0};   // steps since that one started
  Debris    _debris[4][MAX_PIECES];
  // Cell grid for the row-clearing style: colour + 1 per cell, 0 = empty. Rows
  // are always taken off the bottom of the digit and everything above drops
  // into the gap, just like a line clear in the game.
  uint8_t       _clearGrid[4][TETRIS_GRID_W * TETRIS_GRID_H];
  uint8_t       _rowsLeft[4] = {0, 0, 0, 0};   // rows still to take away
  uint8_t       _rowTick[4]  = {0, 0, 0, 0};   // steps spent on the row going
  unsigned long _breakUpStepLast = 0;                   // one clock for all of them, they step together
  unsigned long _holdUntil[4] = {0, 0, 0, 0};    // empty space before the rebuild starts
  unsigned long _breakUpEndedAt = 0;   // when the last effect had run its course
  // The hour change (startHour()).
  uint8_t       _hourPart    = PART_NONE;        // none, its rebuild, its effect
  uint8_t       _hourStyle   = HOUR_LINE_CLEAR;  // the one playing: HOUR_LINE_CLEAR, HOUR_COLOURS or HOUR_ROCKET
  uint8_t       _hueShift    = 0;                // the level-up colours' shift; 0 = the pieces' own colours
  unsigned long _effectAt    = 0;                // millis() the effect began
  uint16_t      _effectFrame = 0xFFFF;           // its frame last stepped
  uint8_t       _rocketSeed  = 0;                // the flame's flicker and the smoke, drawn when it begins
  time_t        _colonDarkIn = -1;               // the second the rocket ended in: the colon stays off in it
  // The tail of the last span asked for, kept: the rules for the panel ask on
  // every pass, and it changes only with the minute, the pace, the turn or
  // the hour change chosen.
  mutable time_t        _spanFor  = 0;
  mutable uint8_t       _spanDrop = 0, _spanTurn = 0xFF, _spanHour = 0xFF;
  mutable unsigned long _spanTail = 0;
};

#endif  // FACE_TETRIS_H
