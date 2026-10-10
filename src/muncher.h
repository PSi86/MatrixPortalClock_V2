/* ----------------------------------------------------------------------
   The Muncher watchface's logic: where everything is and how it moves,
   without any drawing (the sketch draws it, drawMuncherFace()).

   The time is four digits of dots on a seven-segment grid, 5 x 9 dots 2 px
   apart, so a digit is 9 x 17 px. Below them runs a corridor between two
   walls. Between changes nothing moves. For a change of the time the muncher
   comes in along the corridor, goes up into each digit that changes and
   walks its segments: it eats the dots that go, and the ones that come
   appear behind it, while two ghosts follow it along the corridor.

   The way through a digit is the shortest walk over the segments it has to
   pass, from the corner where the muncher comes in and back to it: a
   breadth-first search over (corner, segments passed so far). Every segment
   is four dots long, so the walk with the fewest segments is the shortest.

   Two ways through a digit (STYLE_*): the whole old digit eaten and then
   the new one walked, or only the segments that differ. A knock plays a
   scene of its own: the muncher eats a power pellet of the colon, the
   ghosts turn and flee, and the one it catches runs off as a pair of eyes.

   Everything here counts in steps of one pixel; the sketch says how long a
   step is and asks advanceTo() to catch up with the time.
   ---------------------------------------------------------------------- */
#pragma once

#include <Arduino.h>

namespace muncher {

// --- digits on the segment graph ------------------------------------------
const uint8_t COLS = 5, ROWS = 9;            // dots of a digit, 2 px apart
const uint8_t DIGIT_W = 9, DIGIT_H = 17;     // its size in pixels
const uint8_t NO_DIGIT = 0xFF;               // the clock has no time yet: a dash

// The six corners and the seven segments a..g between them, as on a
// seven-segment display: a top, b upper right, c lower right, d bottom,
// e lower left, f upper left, g middle.
enum Node : uint8_t { CORNER_TL, CORNER_TR, CORNER_ML, CORNER_MR, CORNER_BL, CORNER_BR, NODE_COUNT };
const uint8_t NODE_COL[NODE_COUNT] = { 0, 4, 0, 4, 0, 4 };
const uint8_t NODE_ROW[NODE_COUNT] = { 0, 0, 4, 4, 8, 8 };
const uint8_t SEG_COUNT = 7;
const uint8_t SEG_FROM[SEG_COUNT] = { CORNER_TL, CORNER_TR, CORNER_MR, CORNER_BL, CORNER_ML, CORNER_TL, CORNER_ML };
const uint8_t SEG_TO[SEG_COUNT]   = { CORNER_TR, CORNER_MR, CORNER_BR, CORNER_BR, CORNER_BL, CORNER_ML, CORNER_MR };
const uint8_t SEG_G = 1 << 6;
// The segments of each digit, bit 0 = a ... bit 6 = g; the same shapes as
// the Tetris digits (scripts/gen_tetris_digits.py).
const uint8_t DIGIT_SEGS[10] = { 0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F };

inline uint8_t segsOf(uint8_t digit) { return digit <= 9 ? DIGIT_SEGS[digit] : SEG_G; }

// The dots of a set of segments: bit row * COLS + col.
inline uint64_t dotsOf(uint8_t segs) {
  uint64_t bits = 0;
  for (uint8_t s = 0; s < SEG_COUNT; s++) {
    if (!(segs & (1 << s))) { continue; }
    uint8_t c0 = NODE_COL[SEG_FROM[s]], r0 = NODE_ROW[SEG_FROM[s]];
    uint8_t c1 = NODE_COL[SEG_TO[s]],   r1 = NODE_ROW[SEG_TO[s]];
    for (uint8_t k = 0; k <= 4; k++) {
      uint8_t c = c0 + (c1 - c0) * k / 4, r = r0 + (r1 - r0) * k / 4;
      bits |= 1ULL << (r * COLS + c);
    }
  }
  return bits;
}

// The shortest walk from corner `start` over every segment in `required`,
// ending at corner `end`. Writes the corners passed, both ends included,
// and returns how many; 0 when it does not fit into `max`.
inline uint8_t coverWalk(uint8_t start, uint8_t required, uint8_t end, uint8_t *out, uint8_t max) {
  const uint16_t STATES = NODE_COUNT * 128;   // corner x segments passed
  static int16_t prev[STATES];
  static uint16_t queue[STATES];
  for (uint16_t i = 0; i < STATES; i++) { prev[i] = -2; }   // -2: not reached yet
  uint16_t head = 0, tail = 0;
  prev[start * 128] = -1;                                   // -1: where it starts
  queue[tail++] = start * 128;
  int16_t found = -1;
  while (head < tail) {
    uint16_t s = queue[head++];
    uint8_t node = s / 128, passed = s % 128;
    if (passed == required && node == end) { found = (int16_t)s; break; }
    for (uint8_t seg = 0; seg < SEG_COUNT; seg++) {
      uint8_t other;
      if (SEG_FROM[seg] == node)    { other = SEG_TO[seg]; }
      else if (SEG_TO[seg] == node) { other = SEG_FROM[seg]; }
      else                          { continue; }
      uint16_t t = other * 128 + (passed | (required & (1 << seg)));
      if (prev[t] != -2) { continue; }
      prev[t] = (int16_t)s;
      queue[tail++] = t;
    }
  }
  if (found < 0) { return 0; }
  uint8_t n = 0;
  for (int16_t s = found; s != -1; s = prev[s]) {
    if (n == max) { return 0; }
    out[n++] = (uint8_t)(s / 128);
  }
  for (uint8_t i = 0; i < n / 2; i++) { uint8_t t = out[i]; out[i] = out[n - 1 - i]; out[n - 1 - i] = t; }
  return n;
}

// --- where things are -----------------------------------------------------
struct Layout {
  int16_t w, h;
  int16_t digitX[4], digitY[4];        // top-left of each digit
  int16_t colonX[2], colonY[2];        // top-left of each power pellet, 4 x 4
  int16_t laneY;                       // the corridor's middle row
  int16_t wallTop, wallBottom;         // the corridor's walls
  int16_t hubX[4];                     // where the way up to a digit leaves the corridor
  uint8_t entry[4];                    // the corner of the digit that way reaches
  int16_t knockX;                      // where the way to the power pellet leaves it
  uint8_t knockPellet;                 // the pellet the muncher eats on a knock
  int16_t knockAtX, knockAtY;          // where it stands to eat it
};

// 64 x 32: HH:MM in one line, the corridor below; the way up to a digit is
// under its lower right corner, the one to the pellets between the groups.
const Layout LANDSCAPE = {
  64, 32, { 3, 15, 40, 52 }, { 2, 2, 2, 2 }, { 30, 30 }, { 5, 12 },
  26, 21, 31, { 11, 23, 48, 60 }, { CORNER_BR, CORNER_BR, CORNER_BR, CORNER_BR }, 31, 1, 31, 13
};
// 32 x 64: hours above minutes, the corridor at the bottom. One shaft runs
// up between the two columns of digits; a digit on the left is reached at
// its lower right corner, one on the right at its lower left.
const Layout PORTRAIT = {
  32, 64, { 3, 20, 3, 20 }, { 4, 4, 27, 27 }, { 5, 23 }, { 22, 22 },
  52, 47, 57, { 15, 15, 15, 15 }, { CORNER_BR, CORNER_BL, CORNER_BR, CORNER_BL }, 15, 0, 7, 23
};

inline int16_t nodeX(const Layout &L, uint8_t i, uint8_t node) { return L.digitX[i] + 2 * NODE_COL[node]; }
inline int16_t nodeY(const Layout &L, uint8_t i, uint8_t node) { return L.digitY[i] + 2 * NODE_ROW[node]; }

// Whether x is in an opening of the corridor's upper wall: as wide as the
// muncher, so two openings never leave a single pixel of wall between them.
inline bool wallOpen(const Layout &L, int16_t x) {
  if (x >= L.knockX - 2 && x <= L.knockX + 2) { return true; }
  for (uint8_t i = 0; i < 4; i++) {
    if (x >= L.hubX[i] - 2 && x <= L.hubX[i] + 2) { return true; }
  }
  return false;
}

// --- the walk -------------------------------------------------------------
// What a step does to the dot the muncher reaches.
enum Act : uint8_t { ACT_WALK, ACT_EAT, ACT_LAY, ACT_SET, ACT_PELLET };
// One straight line, to (x, y) from where the one before ended.
struct Leg { int16_t x, y; uint8_t act, digit; };
const uint8_t MAX_LEGS = 112;

enum Style : uint8_t { STYLE_MIXED, STYLE_ALL, STYLE_DIFF, STYLE_COUNT };   // stored values
enum Mode : uint8_t { MODE_IDLE, MODE_CHANGE, MODE_POWER };
enum GhostMode : uint8_t { GHOST_OFF, GHOST_FOLLOW, GHOST_SCARED, GHOST_EYES };
const uint8_t GHOSTS = 2;
const uint8_t GHOST_DELAY[GHOSTS] = { 8, 16 };   // steps behind the muncher's track
const uint8_t GHOST_GAP[GHOSTS]   = { 7, 14 };   // never closer to it than this
const int16_t START_X = -3;                      // just off the left edge

struct Ghost { int16_t x; uint8_t mode; };

class Face {
 public:
  Layout L = LANDSCAPE;
  uint8_t shown[4]  = { NO_DIGIT, NO_DIGIT, NO_DIGIT, NO_DIGIT };   // what the dots stand for
  uint8_t target[4] = { NO_DIGIT, NO_DIGIT, NO_DIGIT, NO_DIGIT };   // what a change leaves
  uint64_t dots[4]  = { 0, 0, 0, 0 };          // the dots on show
  uint32_t lanePellets = 0;                    // corridor pellets, bit k at x = 2 + 4k
  bool pelletEaten = false;                    // the knock's power pellet is gone
  Mode mode = MODE_IDLE;
  int16_t mx = START_X, my = 0;                // the muncher's middle
  int8_t dx = 1, dy = 0;                       // the way it faces
  Ghost ghosts[GHOSTS] = {};
  uint16_t stepsDone = 0;

  void setLayout(const Layout &layout) { L = layout; show(shown); }

  // Snap to these digits: nothing moves, nothing is planned.
  void show(const uint8_t d[4]) {
    for (uint8_t i = 0; i < 4; i++) { shown[i] = target[i] = d[i]; dots[i] = dotsOf(segsOf(d[i])); }
    mode = MODE_IDLE;
    pelletEaten = false;
    lanePellets = allLanePellets();
    for (uint8_t g = 0; g < GHOSTS; g++) { ghosts[g].mode = GHOST_OFF; }
    legCount = 0;
  }

  // Lay the walk from the digits on show to `to`, visiting the digits that
  // change and those in `force`. Returns the steps up to the moment the
  // last digit is complete, 0 when there is nothing to do.
  uint16_t planChange(const uint8_t to[4], uint8_t style, uint8_t force = 0) {
    legCount = 0;
    steps = 0;
    uint8_t visit[4], count = 0;
    for (uint8_t i = 0; i < 4; i++) {
      if (shown[i] != to[i] || (force & (1 << i))) { visit[count++] = i; }
      target[i] = to[i];
      targetDots[i] = dotsOf(segsOf(to[i]));
    }
    if (count == 0) { return 0; }
    if (style == STYLE_MIXED) { style = (count == 1) ? STYLE_ALL : STYLE_DIFF; }
    // Along the corridor from the left: by where the way up leaves it, and
    // up one shaft the nearest digit first.
    for (uint8_t a = 1; a < count; a++) {
      for (uint8_t b = a; b > 0 && before(visit[b], visit[b - 1]); b--) {
        uint8_t t = visit[b]; visit[b] = visit[b - 1]; visit[b - 1] = t;
      }
    }
    px = START_X; py = L.laneY;
    uint16_t done = 0;
    for (uint8_t k = 0; k < count; k++) {
      uint8_t i = visit[k];
      uint8_t e = L.entry[i];
      int16_t ex = nodeX(L, i, e), ey = nodeY(L, i, e);
      if (py == L.laneY) { leg(L.hubX[i], L.laneY, ACT_WALK, i); }   // along the corridor
      leg(L.hubX[i], ey, ACT_WALK, i);                               // up, or on up the shaft
      leg(ex, ey, ACT_WALK, i);                                      // into the corner
      if (!walkDigit(i, shown[i], to[i], style)) { legCount = 0; return 0; }
      done = steps;
      leg(L.hubX[i], ey, ACT_WALK, i);                               // back out of the corner
      bool sameShaft = (k + 1 < count) && L.hubX[visit[k + 1]] == L.hubX[i];
      if (!sameShaft) { leg(L.hubX[i], L.laneY, ACT_WALK, i); }      // down to the corridor
    }
    // Out to the right, far enough for the ghosts to leave as well.
    leg(L.w + 3 + GHOST_DELAY[GHOSTS - 1] + 2, L.laneY, ACT_WALK, 0);
    pathSteps = steps;
    return done;
  }

  // The knock's scene: up to the power pellet and back, then after the
  // ghosts. Returns the steps up to the pellet.
  uint16_t planPower() {
    legCount = 0;
    steps = 0;
    for (uint8_t i = 0; i < 4; i++) { target[i] = shown[i]; targetDots[i] = dotsOf(segsOf(shown[i])); }
    px = START_X; py = L.laneY;
    leg(L.knockX, L.laneY, ACT_WALK, 0);
    leg(L.knockX, L.knockAtY, ACT_WALK, 0);
    leg(L.knockAtX, L.knockAtY, ACT_WALK, 0);
    // The pellet goes with whichever leg reaches it: in landscape it stands
    // right above the opening, and the leg across to it has no length.
    legs[legCount - 1].act = ACT_PELLET;
    uint16_t pellet = steps;
    leg(L.knockX, L.knockAtY, ACT_WALK, 0);
    leg(L.knockX, L.laneY, ACT_WALK, 0);
    leg(START_X, L.laneY, ACT_WALK, 0);
    pathSteps = steps;
    return pellet;
  }

  // Start what was planned: a change, or the knock's scene.
  void start(Mode m) {
    if (legCount == 0) { return; }
    mode = m;
    stepsDone = 0;
    legAt = 0;
    mx = START_X; my = L.laneY; dx = 1; dy = 0;
    laneX = START_X;
    for (uint8_t g = 0; g < GHOSTS; g++) { ghosts[g].mode = GHOST_FOLLOW; ghosts[g].x = START_X - GHOST_GAP[g]; }
    for (uint8_t k = 0; k < TRACK; k++) { track[k] = START_X; }
  }

  // Catch up to `n` steps since the start. True when anything moved; the
  // mode is back to MODE_IDLE once the scene is over.
  bool advanceTo(uint16_t n) {
    if (mode == MODE_IDLE) { return false; }
    bool moved = false;
    while (stepsDone < n && mode != MODE_IDLE) { stepOnce(); moved = true; }
    return moved;
  }

  bool mouthOpen() const { return (stepsDone / 3) % 2 == 0; }
  bool muncherOn() const { return mode != MODE_IDLE && legAt < legCount; }
  uint8_t legsPlanned() const { return legCount; }

 private:
  Leg legs[MAX_LEGS];
  uint8_t legCount = 0, legAt = 0;
  uint64_t targetDots[4] = { 0, 0, 0, 0 };   // the dots the walk leaves
  int16_t px = START_X, py = 0;      // where the walk being planned stands
  uint16_t steps = 0;                // its length so far
  uint16_t pathSteps = 0;            // the whole walk's length
  // However the ghosts are placed, a scene is over this many steps after the
  // muncher's walk: a scene that never ended would hold up every change of
  // the time after it.
  static const uint16_t AFTER_WALK_MAX = 150;
  int16_t laneX = START_X;           // the muncher's place along the corridor
  static const uint8_t TRACK = 32;   // its last places there, for the ghosts
  int16_t track[TRACK] = {};

  uint32_t allLanePellets() const {
    uint32_t bits = 0;
    for (uint8_t k = 0; 2 + 4 * k < L.w && k < 32; k++) { bits |= 1UL << k; }
    return bits;
  }

  bool before(uint8_t a, uint8_t b) const {
    if (L.hubX[a] != L.hubX[b]) { return L.hubX[a] < L.hubX[b]; }
    return nodeY(L, a, L.entry[a]) > nodeY(L, b, L.entry[b]);
  }

  void leg(int16_t x, int16_t y, uint8_t act, uint8_t digit) {
    if (x == px && y == py) { return; }
    if (legCount >= MAX_LEGS) { return; }
    legs[legCount++] = { x, y, act, digit };
    steps += (uint16_t)(abs(x - px) + abs(y - py));
    px = x; py = y;
  }

  bool walkDigit(uint8_t i, uint8_t from, uint8_t to, uint8_t style) {
    uint8_t e = L.entry[i];
    uint8_t a[24], b[24], na = 0, nb = 0;
    if (style == STYLE_ALL) {
      // Eat the old digit up to some corner, then walk the new one from there:
      // the corner that makes the two together shortest.
      uint8_t best = 0xFF;
      for (uint8_t mid = 0; mid < NODE_COUNT; mid++) {
        uint8_t ta[24], tb[24];
        uint8_t n1 = coverWalk(e, segsOf(from), mid, ta, 24);
        uint8_t n2 = coverWalk(mid, segsOf(to), e, tb, 24);
        if (!n1 || !n2 || n1 + n2 >= best) { continue; }
        best = n1 + n2;
        memcpy(a, ta, n1); na = n1;
        memcpy(b, tb, n2); nb = n2;
      }
      if (best == 0xFF) { return false; }
      for (uint8_t k = 1; k < na; k++) { leg(nodeX(L, i, a[k]), nodeY(L, i, a[k]), ACT_EAT, i); }
      for (uint8_t k = 1; k < nb; k++) { leg(nodeX(L, i, b[k]), nodeY(L, i, b[k]), ACT_LAY, i); }
    } else {
      na = coverWalk(e, segsOf(from) ^ segsOf(to), e, a, 24);
      if (!na) { return false; }
      for (uint8_t k = 1; k < na; k++) { leg(nodeX(L, i, a[k]), nodeY(L, i, a[k]), ACT_SET, i); }
    }
    return true;
  }

  // What the muncher does to the dot it stands on.
  void touch(const Leg &g) {
    uint8_t i = g.digit;
    int16_t cx = mx - L.digitX[i], cy = my - L.digitY[i];
    if (g.act == ACT_WALK || g.act == ACT_PELLET) { return; }
    if (cx < 0 || cy < 0 || cx > DIGIT_W - 1 || cy > DIGIT_H - 1 || (cx & 1) || (cy & 1)) { return; }
    uint64_t bit = 1ULL << ((cy / 2) * COLS + cx / 2);
    bool wanted = targetDots[i] & bit;
    if (g.act == ACT_EAT)                 { dots[i] &= ~bit; }
    else if (g.act == ACT_LAY && wanted)  { dots[i] |= bit; }
    else if (g.act == ACT_SET)            { dots[i] = wanted ? (dots[i] | bit) : (dots[i] & ~bit); }
  }

  void stepOnce() {
    stepsDone++;
    if (legAt < legCount) {
      const Leg &g = legs[legAt];
      int8_t sx = (g.x > mx) - (g.x < mx), sy = (g.y > my) - (g.y < my);
      mx += sx; my += sy;
      if (sx || sy) { dx = sx; dy = sy; }
      touch(g);
      if (mx == g.x && my == g.y) {
        if (g.act == ACT_PELLET) { powerPellet(); }
        legAt++;
        // A walk that eats, lays or sets starts on a corner the muncher only
        // walked to: that corner counts too. Otherwise the corner a walk
        // starts from would keep its old dot whenever no later leg comes
        // back to it - an 8 becoming a 4, entered at its lower left, say.
        if (legAt < legCount && legs[legAt].act != g.act) { touch(legs[legAt]); }
      }
    }
    if (my == L.laneY) {
      laneX = mx;
      if (mx >= 2 && (mx - 2) % 4 == 0 && (mx - 2) / 4 < 32) { lanePellets &= ~(1UL << ((mx - 2) / 4)); }
    }
    track[stepsDone % TRACK] = laneX;
    moveGhosts();
    if (legAt >= legCount && (ghostsGone() || stepsDone > pathSteps + AFTER_WALK_MAX)) { finish(); }
  }

  void powerPellet() {
    pelletEaten = true;
    for (uint8_t g = 0; g < GHOSTS; g++) {
      if (ghosts[g].mode == GHOST_FOLLOW) { ghosts[g].mode = GHOST_SCARED; }
    }
  }

  void moveGhosts() {
    for (uint8_t g = 0; g < GHOSTS; g++) {
      Ghost &h = ghosts[g];
      switch (h.mode) {
        case GHOST_FOLLOW: {
          // On the muncher's track, some steps behind it, and never closer to
          // it than its gap: while it is up in a digit the ghosts wait below.
          int16_t behind = (stepsDone >= GHOST_DELAY[g]) ? track[(stepsDone - GHOST_DELAY[g]) % TRACK]
                                                         : (int16_t)(START_X - (GHOST_DELAY[g] - stepsDone));
          int16_t x = min(behind, (int16_t)(laneX - GHOST_GAP[g]));
          if (x > h.x) { h.x = x; }
          break;
        }
        case GHOST_SCARED:
          if (stepsDone % 2 == 0) { h.x--; }               // fleeing, at half speed
          if (my == L.laneY && abs(mx - h.x) <= 3) { h.mode = GHOST_EYES; }
          if (h.x < -3) { h.mode = GHOST_OFF; }
          break;
        case GHOST_EYES:
          h.x += 2;                                        // home, quickly
          if (h.x > L.w + 3) { h.mode = GHOST_OFF; }
          break;
        default:
          break;
      }
    }
  }

  bool ghostsGone() const {
    for (uint8_t g = 0; g < GHOSTS; g++) {
      const Ghost &h = ghosts[g];
      if (h.mode == GHOST_FOLLOW && h.x - 2 < L.w) { return false; }
      if (h.mode == GHOST_SCARED || h.mode == GHOST_EYES) { return false; }
    }
    return true;
  }

  void finish() {
    for (uint8_t i = 0; i < 4; i++) { shown[i] = target[i]; dots[i] = dotsOf(segsOf(target[i])); }
    mode = MODE_IDLE;
    pelletEaten = false;
    lanePellets = allLanePellets();
    for (uint8_t g = 0; g < GHOSTS; g++) { ghosts[g].mode = GHOST_OFF; }
    legCount = 0;
  }
};

// Every change of a digit, both ways, walked through on both layouts and
// from both kinds of corner a digit is entered at (portrait: lower right on
// the left, lower left on the right): true when each leaves exactly the new
// digit's dots at the moment planChange() said. `failed` names the first
// that did not, as layout * 10000 + style * 1000 + position * 100 + from * 10
// + to (layout 0 landscape, 1 portrait; style STYLE_*).
inline bool selfTest(Face &f, uint16_t &longest, uint16_t &failed) {
  longest = 0;
  failed = 0;
  const Layout *layouts[2] = { &LANDSCAPE, &PORTRAIT };
  for (uint8_t l = 0; l < 2; l++) {
    // The knock's scene: the pellet is eaten when the muncher reaches it,
    // the ghosts turn, and the scene ends. failed = 50000 + layout.
    uint8_t d[4] = { 1, 2, 3, 4 };
    f.setLayout(*layouts[l]);
    f.show(d);
    uint16_t pellet = f.planPower();
    f.start(MODE_POWER);
    f.advanceTo(pellet);
    bool scared = f.pelletEaten && f.ghosts[0].mode == GHOST_SCARED;
    f.advanceTo(3000);
    if (!scared || f.mode != MODE_IDLE) { failed = 50000 + l; return false; }
    for (uint8_t pos = 2; pos <= 3; pos++) {
      for (uint8_t style = STYLE_ALL; style <= STYLE_DIFF; style++) {
        for (uint8_t from = 0; from <= 9; from++) {
          for (uint8_t to = 0; to <= 9; to++) {
            uint8_t a[4] = { 1, 2, 3, 4 }, b[4] = { 1, 2, 3, 4 };
            a[pos] = from; b[pos] = to;
            f.setLayout(*layouts[l]);
            f.show(a);
            uint16_t done = f.planChange(b, style, (from == to) ? (1 << pos) : 0);
            if (done > longest) { longest = done; }
            f.start(MODE_CHANGE);
            // Stop at the step the digit is due to be complete, before the
            // scene's end sets the dots anyway.
            f.advanceTo(done);
            bool ok = done > 0 && f.dots[pos] == dotsOf(segsOf(to));
            f.advanceTo(3000);
            if (!ok || f.mode != MODE_IDLE) {
              failed = l * 10000 + style * 1000 + pos * 100 + from * 10 + to;
              return false;
            }
          }
        }
      }
    }
  }
  return true;
}

}  // namespace muncher
