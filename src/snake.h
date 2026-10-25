/* ----------------------------------------------------------------------
   The Snake watchface's logic (concept doc, tab "Next game faces"): each
   digit of HH:MM is a snake lying in its digit's cells (snake_paths.h). At
   a change the old snake of a digit crawls out over the panel's nearer side
   and a new one comes in from the same side and eats the dots on the new
   digit, growing into it; to a full hour all four go - as one train out to
   the left with the new four behind it, or each to its own side at once.

   A change is a plan of movers: snakes that each follow a fixed route a
   cell a step. The planner only picks when each one starts - the earliest
   step at which it meets nobody, the movers in a fixed order - so what the
   panel shows at any moment follows from the plan and the time alone, and
   the face (face_snake.h) plans backwards from the minute. The planner was
   first written in Python and run over every change a clock meets
   (tools.local/game_faces/snake_plan.py); the PC test compares the two plan
   for plan.

   The digits come in families, a setting of the face (snake_paths.h): the
   Tetris glyphs the snake coils through, thin strokes for a 2 px snake on
   the same grid of 2 px cells, and the spaced ones on a grid of their own,
   cells of 3 px.

   Everything here counts in grid cells: cell (r, c) is the block at x =
   ox + pitch * c, y = oy + pitch * r of its layout - 2 px cells from (1, 1),
   or 3 px. A cell may lie off the panel (a snake that has crawled out, or
   not yet in).
   ---------------------------------------------------------------------- */
#pragma once

#include <Arduino.h>
#include <string.h>
#include "snake_paths.h"

namespace snake {

const uint8_t NO_DIGIT = 0xFF;     // the clock has no time yet: a dash
const uint8_t DASH = 10;           // the dash's glyph in snake_paths.h
// The pace: a pixel every 40 ms - 25 px/s, the Muncher's; a step, a cell,
// takes as many of them as a cell is wide (80 ms on cells of 2 px).
const uint16_t PX_MS = 40;

struct Cell {
  int8_t r, c;
};
inline bool same(Cell a, Cell b) { return a.r == b.r && a.c == b.c; }

inline uint8_t glyphOf(uint8_t d) { return d <= 9 ? d : DASH; }

// The digits' families: the Tetris glyphs, strokes two cells thick, the
// snake coiling through both; thin strokes a cell apart, so that a 2 px
// snake shows as one long snake (Peter, 2026-10-23: the table clock's snake
// 2 px thick; where it doubles back, its parts may touch) - lines and
// narrower lines; and spaced, digits of 4 x 7 cells on a grid of 3 px cells,
// a 1 px gap everywhere (Peter, 2026-10-24; the default since). Bold
// uprights and springs were tried and left out (2026-10-24).
enum Digits : uint8_t {
  DIGITS_COILS, DIGITS_LINES, DIGITS_NARROW, DIGITS_SPACED, DIGITS_COUNT
};
static_assert(DIGITS_COUNT == SNAKE_FAMILIES, "a family of paths per setting");

inline uint8_t pathLen(uint8_t f, uint8_t d) { return SNAKE_PATH_LEN[f][glyphOf(d)]; }

// Where everything stands. The digits' places (their glyph's top left
// cell), the colon's two cells, and for each place the side its snakes
// leave and come in by and the lane rows they use: below the digits in
// landscape - the places nearer an edge the rows right below them, the
// inner ones the two under those - and upright the two rows between hours
// and minutes, and those below the minutes. The full hour's layouts send
// every snake over the left side. The spaced digits' grid of 3 px cells
// has no column to spare: a free one between the hours and on each side of
// the colon, none outside, so the snakes of a place at its side may cross it
// at any row (`edge`), not only along their lanes.
struct Layout {
  uint8_t gw, gh;          // the panel in cells: 31 x 15 or 15 x 31; 21 x 10 or 10 x 21
  int8_t  placeR[4], placeC[4];
  Cell    colon[2];
  int8_t  side[4];         // -1: over the left side, +1: over the right
  int8_t  outRow[4], inRow[4];
  uint8_t pitch;           // px a cell: 2, or 3
  uint8_t ox, oy;          // the pixel of cell (0, 0)
  uint8_t edge;            // the places at their side whose snakes may cross it at any row (bit k)
  uint8_t appleDepth;      // cells below a head the apple lies: 6 px either way

  bool on(Cell a) const { return a.r >= 0 && a.r < gh && a.c >= 0 && a.c < gw; }
  uint16_t at(Cell a) const { return (uint16_t)(a.r * gw + a.c); }
  int16_t pxX(Cell a) const { return (int16_t)(ox + pitch * a.c); }
  int16_t pxY(Cell a) const { return (int16_t)(oy + pitch * a.r); }
  uint16_t stepMs() const { return (uint16_t)(pitch * PX_MS); }
  // Cell i of the path of digit d of family f at place k.
  Cell digit(uint8_t f, uint8_t k, uint8_t d, uint8_t i) const {
    uint8_t p = SNAKE_PATH[f][glyphOf(d)][i];
    return Cell{ (int8_t)(placeR[k] + (p >> 4)), (int8_t)(placeC[k] + (p & 0x0F)) };
  }
};
const Layout LANDSCAPE      = { 31, 15, { 1, 1, 1, 1 }, { 1, 8, 17, 24 }, { { 4, 15 }, { 7, 15 } },
                                { -1, -1, 1, 1 }, { 11, 13, 13, 11 }, { 12, 14, 14, 12 }, 2, 1, 1, 0, 3 };
const Layout PORTRAIT       = { 15, 31, { 1, 1, 13, 13 }, { 1, 8, 1, 8 }, { { 11, 6 }, { 11, 8 } },
                                { -1, 1, -1, 1 }, { 11, 11, 23, 23 }, { 12, 12, 24, 24 }, 2, 1, 1, 0, 3 };
const Layout HOUR_LANDSCAPE = { 31, 15, { 1, 1, 1, 1 }, { 1, 8, 17, 24 }, { { 4, 15 }, { 7, 15 } },
                                { -1, -1, -1, -1 }, { 14, 14, 14, 14 }, { 12, 12, 12, 12 }, 2, 1, 1, 0, 3 };
const Layout HOUR_PORTRAIT  = { 15, 31, { 1, 1, 13, 13 }, { 1, 8, 1, 8 }, { { 11, 6 }, { 11, 8 } },
                                { -1, -1, -1, -1 }, { 11, 11, 23, 25 }, { 12, 12, 24, 26 }, 2, 1, 1, 0, 3 };
// The spaced digits: 4 x 7 cells of 3 px. Landscape 21 x 10 cells from (1, 1),
// the digits in rows 0-6 at columns 0, 5, 12, 17, the colon in column 10,
// lanes rows 7-9; upright 10 x 21 cells from (2, 1), hours in rows 0-6, the
// colon in row 7, minutes in rows 9-15, their lanes below. The full hour
// upright: the colon stands in row 7, so the new hours come in along row 8.
// Lanes and edges as tools.local/game_faces/snake_thick_proto.py found them.
const Layout SPACED_LANDSCAPE      = { 21, 10, { 0, 0, 0, 0 }, { 0, 5, 12, 17 }, { { 2, 10 }, { 4, 10 } },
                                       { -1, -1, 1, 1 }, { 7, 8, 8, 7 }, { 7, 9, 9, 7 }, 3, 1, 1, 0x9, 2 };
const Layout SPACED_PORTRAIT       = { 10, 21, { 0, 0, 9, 9 }, { 0, 5, 0, 5 }, { { 7, 3 }, { 7, 5 } },
                                       { -1, 1, -1, 1 }, { 7, 7, 16, 16 }, { 7, 7, 16, 16 }, 3, 2, 1, 0xF, 2 };
const Layout SPACED_HOUR_LANDSCAPE = { 21, 10, { 0, 0, 0, 0 }, { 0, 5, 12, 17 }, { { 2, 10 }, { 4, 10 } },
                                       { -1, -1, -1, -1 }, { 9, 9, 9, 9 }, { 7, 7, 7, 7 }, 3, 1, 1, 0x1, 2 };
const Layout SPACED_HOUR_PORTRAIT  = { 10, 21, { 0, 0, 9, 9 }, { 0, 5, 0, 5 }, { { 7, 3 }, { 7, 5 } },
                                       { -1, -1, -1, -1 }, { 7, 7, 16, 18 }, { 8, 8, 17, 19 }, 3, 2, 1, 0x5, 2 };
const uint16_t GRID_CELLS = 31 * 15;   // both orientations, the largest grid

// A set of the panel's cells.
struct CellSet {
  uint8_t bits[(GRID_CELLS + 7) / 8];
  void clear() { memset(bits, 0, sizeof(bits)); }
  void add(const Layout &L, Cell a) { if (L.on(a)) { uint16_t i = L.at(a); bits[i >> 3] |= (uint8_t)(1 << (i & 7)); } }
  bool has(const Layout &L, Cell a) const { if (!L.on(a)) { return false; } uint16_t i = L.at(a); return bits[i >> 3] & (1 << (i & 7)); }
};

// ---- movers ------------------------------------------------------------------
const uint8_t MAX_MOVERS = 8;      // four places, an old and a new snake each
const uint8_t MAX_CELLS = 128;     // the cells of one mover kept: its body and its route on the panel
const int16_t NEVER = 32000;
const int16_t LATEST_START = 600;  // in steps; a mover that would start later fails the plan
// A new snake's dots: one on every DOT_EVERY-th cell of its digit, the last
// DOT_GROW cells before the digit's end; each one eaten makes it DOT_GROW
// cells longer at once - its tail stays for that many steps, as in the game.
const uint8_t DOT_EVERY = 4;
const uint8_t DOT_GROW = 2;

// Whether cell j of a digit of n cells holds a dot, how many dots it has,
// and how long its new snake is as it comes in: by its dots short of it.
inline bool hasDot(uint8_t n, uint8_t j) {
  return j + DOT_GROW < n && (n - 1 - DOT_GROW - j) % DOT_EVERY == 0;
}
inline uint8_t dotsOf(uint8_t n) { return n > DOT_GROW ? (uint8_t)((n - 1 - DOT_GROW) / DOT_EVERY + 1) : 0; }
inline uint8_t comesInAs(uint8_t n) { return (uint8_t)(n - DOT_GROW * dotsOf(n)); }

// A snake that follows its route a cell a step from `start` on. Its cells,
// tail first, are its body and then its route. An old snake keeps its body
// (its digit) and its length, and its route on the panel; the route goes on
// beyond the side for as many cells as it takes to be gone, which are not
// kept. A new snake comes in about half as long as its digit, beyond the
// side, not kept; its route comes in, the first cell beyond the side too,
// and ends with its digit, whose dots it eats, growing with each, so that
// its body becomes the digit (Peter, 2026-10-23: the digit should come of
// the snake's eating, and the growth be seen - its tail moves, and stops
// after each dot).
struct Mover {
  uint8_t  place;
  bool     fresh;      // a new snake
  int8_t   side;
  uint8_t  len;        // its length in the end: its digit's cells
  uint8_t  startLen;   // ... and at the start (an old snake: the same)
  int16_t  firstEat;   // a new snake: the moves after which its head is on its first dot
  uint8_t  count;      // the cells kept
  uint16_t route;      // the steps its head takes, those beyond the side included
  int16_t  start;      // the step it starts at
  Cell     cells[MAX_CELLS];

  int16_t end() const { return start + (int16_t)route; }
  // Index i of body + route: kept, or worked out beyond the side.
  Cell cellAt(uint16_t i) const {
    if (!fresh) {
      if (i < count) { return cells[i]; }
      Cell last = cells[count - 1];
      return Cell{ last.r, (int8_t)(last.c + side * (int16_t)(i - count + 1)) };
    }
    if (i >= startLen) { return cells[i - startLen]; }
    return Cell{ cells[0].r, (int8_t)(cells[0].c + side * (int16_t)(startLen - i)) };
  }
  // The index in body + route of kept cell j.
  uint16_t indexOf(uint8_t j) const { return fresh ? (uint16_t)(startLen + j) : j; }
  // How many steps it has taken after step t.
  uint16_t moved(int16_t t) const {
    int16_t m = t - start;
    return m <= 0 ? 0 : m >= (int16_t)route ? route : (uint16_t)m;
  }
  // How much it has grown after m moves: from its first dot on, of every
  // DOT_EVERY moves the first DOT_GROW are ones its tail stays at.
  uint16_t grownBy(uint16_t m) const {
    if (!fresh || (int16_t)m <= firstEat) { return 0; }
    uint16_t y = (uint16_t)(m - firstEat), r = y % DOT_EVERY;
    return (uint16_t)(DOT_GROW * (y / DOT_EVERY) + (r < DOT_GROW ? r : DOT_GROW));
  }
  // Its head's index after step t, and how long it is then.
  uint16_t headAt(int16_t t) const { return (uint16_t)(startLen - 1 + moved(t)); }
  uint16_t lengthAt(int16_t t) const { return (uint16_t)(startLen + grownBy(moved(t))); }
  // Whether the step after t is one it grows at: its tail stays.
  bool growsAt(int16_t t) const {
    int16_t m = t - start;
    if (!fresh || m < 0 || m >= (int16_t)route || m + 1 <= firstEat) { return false; }
    return (m - firstEat) % DOT_EVERY < DOT_GROW;
  }
  // When it holds the cell at index i: from the step its head enters it
  // (-NEVER: from the start) to the last step it is there (NEVER: it stays):
  // the step before its tail has moved i + 1 times - after its first dot,
  // the tail moves at the last DOT_EVERY - DOT_GROW of every DOT_EVERY moves.
  void span(uint16_t i, int16_t &lo, int16_t &hi) const {
    lo = i < startLen ? -NEVER : (int16_t)(start + (int16_t)i - startLen + 1);
    int16_t k = (int16_t)i + 1, m = k;
    if (fresh && k > firstEat) {
      int16_t over = k - firstEat - 1;
      const int16_t moves = DOT_EVERY - DOT_GROW;
      m = (int16_t)(firstEat + (over / moves) * DOT_EVERY + DOT_GROW + over % moves + 1);
    }
    hi = m > (int16_t)route ? NEVER : (int16_t)(start + m - 1);
  }
};

// Two spans of one cell clash when a head enters it while the other holds
// it at that step or the step before: the cell must have been empty for a
// whole step, else the two would touch at the half step.
inline bool clash(int16_t lo1, int16_t hi1, int16_t lo2, int16_t hi2) {
  if (lo1 > -NEVER && lo2 <= lo1 && hi2 >= lo1 - 1) { return true; }
  if (lo2 > -NEVER && lo1 <= lo2 && hi1 >= lo2 - 1) { return true; }
  return false;
}

// Whether the row runs clear from col over the side.
inline bool clearRun(const Layout &L, int8_t row, int8_t col, int8_t side, const CellSet &blocked) {
  for (int8_t c = col; c >= 0 && c < (int8_t)L.gw; c += side) {
    if (blocked.has(L, Cell{ row, c })) { return false; }
  }
  return true;
}

// The shortest way from `from` (not included) through cells not blocked to a
// cell of `row` - of any row when `anyRow` - from which the row runs clear
// over the side - down first on a tie - then along the row to the panel's
// edge; from `from` straight out when it lies at its side. Writes the cells
// into out; returns how many, or -1 when there is none or it does not fit.
inline int16_t wayOut(const Layout &L, Cell from, int8_t row, bool anyRow, int8_t side, const CellSet &blocked,
                      Cell *out, uint8_t room) {
  static int16_t prev[GRID_CELLS];
  static uint16_t queue[GRID_CELLS];
  if ((anyRow || from.r == row) && from.c + side == (side < 0 ? -1 : (int8_t)L.gw)) { return 0; }
  for (uint16_t i = 0; i < GRID_CELLS; i++) { prev[i] = -2; }   // -2: not reached
  const int8_t dr[4] = { 1, 0, 0, -1 }, dc[4] = { 0, side, (int8_t)-side, 0 };
  uint16_t head = 0, tail = 0;
  prev[L.at(from)] = -1;
  queue[tail++] = L.at(from);
  int16_t goal = -1;
  while (head < tail) {
    uint16_t cur = queue[head++];
    Cell c = { (int8_t)(cur / L.gw), (int8_t)(cur % L.gw) };
    if (cur != L.at(from) && (anyRow || c.r == row) && clearRun(L, c.r, c.c, side, blocked)) {
      goal = (int16_t)cur;
      break;
    }
    for (uint8_t n = 0; n < 4; n++) {
      Cell x = { (int8_t)(c.r + dr[n]), (int8_t)(c.c + dc[n]) };
      if (!L.on(x) || prev[L.at(x)] != -2 || blocked.has(L, x)) { continue; }
      prev[L.at(x)] = (int16_t)cur;
      queue[tail++] = L.at(x);
    }
  }
  if (goal < 0) { return -1; }
  uint8_t n = 0;
  for (int16_t i = goal; i != (int16_t)L.at(from); i = prev[i]) { n++; }
  Cell g = { (int8_t)(goal / L.gw), (int8_t)(goal % L.gw) };
  int16_t run = side < 0 ? g.c : (int16_t)L.gw - 1 - g.c;
  if (n + run > room) { return -1; }
  uint8_t k = n;
  for (int16_t i = goal; i != (int16_t)L.at(from); i = prev[i]) {
    out[--k] = Cell{ (int8_t)(i / L.gw), (int8_t)(i % L.gw) };
  }
  for (int16_t s = 1; s <= run; s++) { out[n++] = Cell{ g.r, (int8_t)(g.c + side * s) }; }
  return n;
}

// ---- a plan of movers -----------------------------------------------------------
struct Plan {
  const Layout *L = &LANDSCAPE;
  uint8_t  count = 0;          // movers
  uint8_t  moving = 0;         // the places whose snakes move (bit k)
  uint8_t  from[4] = { 0 }, to[4] = { 0 };
  uint16_t stands = 0;         // the step at which the new time stands
  uint16_t gone = 0;           // ... and the one after which every mover is done
  uint8_t  digits = DIGITS_COILS;   // the family its digits are of
  Mover    m[MAX_MOVERS];

  // The cells that stay put: the colon and the digits that do not move.
  void statics(CellSet &s) const {
    s.clear();
    s.add(*L, L->colon[0]);
    s.add(*L, L->colon[1]);
    for (uint8_t k = 0; k < 4; k++) {
      if (moving & (1 << k)) { continue; }
      for (uint8_t i = 0; i < pathLen(digits, from[k]); i++) { s.add(*L, L->digit(digits, k, from[k], i)); }
    }
  }
};

// Where each mover's kept cells lie, for the planner: the kept index + 1 by
// cell, 0 where it does not pass.
struct CellIndex {
  uint8_t at[MAX_MOVERS][GRID_CELLS];
};

// Whether mover i, at its start, meets none of the movers 0 .. upTo - 1
// (itself left out): those before it while they are planned, all of them
// after.
inline bool fitsAmong(const Plan &p, const CellIndex &x, uint8_t i, uint8_t upTo) {
  const Layout &L = *p.L;
  const Mover &a = p.m[i];
  for (uint8_t j = 0; j < a.count; j++) {
    Cell c = a.cells[j];
    if (!L.on(c)) { continue; }
    int16_t lo1, hi1;
    a.span(a.indexOf(j), lo1, hi1);
    for (uint8_t o = 0; o < upTo; o++) {
      if (o == i) { continue; }
      uint8_t k = x.at[o][L.at(c)];
      if (!k) { continue; }
      int16_t lo2, hi2;
      p.m[o].span(p.m[o].indexOf(k - 1), lo2, hi2);
      if (clash(lo1, hi1, lo2, hi2)) { return false; }
    }
  }
  return true;
}

// The movers of a swap: for every place in `places`, its old snake out over
// its side and its new one in. Old ones first, then new ones, each by place.
// Then each gets the earliest start at which it meets none before it.
// Returns false when a route cannot be found or does not fit, or a mover
// cannot start in time - the face then shows the new time at once.
inline bool planSwap(Plan &p, const Layout &L, const uint8_t from[4], const uint8_t to[4], uint8_t places,
                     uint8_t digits) {
  static CellIndex x;
  p.L = &L;
  p.digits = digits;
  p.moving = places;
  memcpy(p.from, from, 4);
  memcpy(p.to, to, 4);
  p.count = 0;
  CellSet stat, blocked;
  p.statics(stat);
  for (uint8_t fresh = 0; fresh < 2; fresh++) {
    for (uint8_t k = 0; k < 4; k++) {
      if (!(places & (1 << k))) { continue; }
      Mover &m = p.m[p.count];
      m.place = k;
      m.fresh = fresh;
      m.side = L.side[k];
      m.start = 0;
      uint8_t d = fresh ? to[k] : from[k];
      m.len = pathLen(digits, d);
      m.startLen = fresh ? comesInAs(m.len) : m.len;
      m.firstEat = NEVER;
      blocked = stat;
      if (!fresh) {
        // its body, then from its head down into its row and out over the side
        for (uint8_t i = 0; i < m.len; i++) { m.cells[i] = L.digit(digits, k, d, i); }
        for (uint8_t i = 0; i + 1 < m.len; i++) { blocked.add(L, m.cells[i]); }
        int16_t n = wayOut(L, m.cells[m.len - 1], L.outRow[k], (L.edge >> k) & 1, m.side, blocked, m.cells + m.len,
                           MAX_CELLS - m.len);
        if (n < 0) { return false; }
        m.count = (uint8_t)(m.len + n);
        m.route = (uint16_t)(n + m.len + 2);   // and on beyond the side until its tail is 2 cells past it
      } else {
        // the way out from its digit's first cell, walked the other way in
        Cell back[MAX_CELLS];
        for (uint8_t i = 1; i < m.len; i++) { blocked.add(L, L.digit(digits, k, d, i)); }
        Cell first = L.digit(digits, k, d, 0);
        int16_t n = wayOut(L, first, L.inRow[k], (L.edge >> k) & 1, m.side, blocked, back, MAX_CELLS);
        if (n < 0 || 1 + n + m.len > MAX_CELLS) { return false; }
        uint8_t c = 0;
        Cell edge = { n > 0 ? back[n - 1].r : first.r, (int8_t)(m.side < 0 ? -1 : L.gw) };
        m.cells[c++] = edge;
        for (int16_t i = n - 1; i >= 0; i--) { m.cells[c++] = back[i]; }
        for (uint8_t i = 0; i < m.len; i++) { m.cells[c++] = L.digit(digits, k, d, i); }
        m.count = c;
        m.route = c;
        // its head is on digit cell j after route - len + 1 + j moves
        m.firstEat = (int16_t)(m.route - m.len + 1 + (m.len - 1 - DOT_GROW) % DOT_EVERY);
      }
      // its cells on the panel: each once, and none of the route on a cell that stays
      for (uint8_t j = 0; j < m.count; j++) {
        Cell a = m.cells[j];
        if (!L.on(a)) { continue; }
        if (m.indexOf(j) >= m.startLen && stat.has(L, a)) { return false; }
        for (uint8_t i = 0; i < j; i++) { if (same(m.cells[i], a)) { return false; } }
      }
      memset(x.at[p.count], 0, GRID_CELLS);
      for (uint8_t j = 0; j < m.count; j++) {
        if (L.on(m.cells[j])) { x.at[p.count][L.at(m.cells[j])] = (uint8_t)(j + 1); }
      }
      p.count++;
    }
  }
  p.stands = p.gone = 0;
  int16_t oldsEnd = 0;
  uint8_t last = 0;   // the new snake that stands last
  for (uint8_t i = 0; i < p.count; i++) {
    Mover &m = p.m[i];
    for (m.start = 0; !fitsAmong(p, x, i, i); m.start++) {
      if (m.start >= LATEST_START) { return false; }
    }
    if (m.fresh && m.end() > p.stands) { p.stands = (uint16_t)m.end(); last = i; }
    if (!m.fresh && m.end() > oldsEnd) { oldsEnd = m.end(); }
  }
  // An old snake still on its way out when the new time stands (the hour's
  // train, as new snakes come in shorter than their digits): the new snake
  // that stands last starts later, the least that meets nobody, so that it
  // ends no sooner than the last old one - as the minute begins, the panel
  // holds the new time and nothing else.
  if ((int16_t)p.stands < oldsEnd) {
    Mover &m = p.m[last];
    for (m.start += (int16_t)(oldsEnd - (int16_t)p.stands); !fitsAmong(p, x, last, p.count); m.start++) {
      if (m.start >= LATEST_START) { return false; }
    }
    p.stands = (uint16_t)m.end();
  }
  p.gone = (uint16_t)((int16_t)p.stands > oldsEnd ? (int16_t)p.stands : oldsEnd);
  return true;
}

// What a change uses. HOUR_TRAIN: all four out to the left as one train, the
// new four in behind it; HOUR_AT_ONCE: all four, each to its own side.
enum HourMode : uint8_t { HOUR_TRAIN, HOUR_AT_ONCE };

// The layouts of a family of digits: the spaced ones on their grid of 3 px cells.
inline const Layout &layoutFor(bool upright, uint8_t digits) {
  if (digits == DIGITS_SPACED) { return upright ? SPACED_PORTRAIT : SPACED_LANDSCAPE; }
  return upright ? PORTRAIT : LANDSCAPE;
}
inline const Layout &hourLayoutFor(bool upright, uint8_t digits) {
  if (digits == DIGITS_SPACED) { return upright ? SPACED_HOUR_PORTRAIT : SPACED_HOUR_LANDSCAPE; }
  return upright ? HOUR_PORTRAIT : HOUR_LANDSCAPE;
}

// The change from `from` to `to` in the digits of family `digits`: the
// places that differ; to a full hour all four, by `hour`'s way. Dashes count
// as digits.
inline bool planChange(Plan &p, bool upright, const uint8_t from[4], const uint8_t to[4], bool hour, HourMode mode,
                       uint8_t digits) {
  if (hour) {
    if (mode == HOUR_TRAIN) { return planSwap(p, hourLayoutFor(upright, digits), from, to, 0x0F, digits); }
    return planSwap(p, layoutFor(upright, digits), from, to, 0x0F, digits);
  }
  uint8_t places = 0;
  for (uint8_t k = 0; k < 4; k++) { if (glyphOf(from[k]) != glyphOf(to[k])) { places |= (uint8_t)(1 << k); } }
  if (!places) { p.L = &layoutFor(upright, digits); p.count = 0; p.moving = 0; memcpy(p.from, from, 4);
                 memcpy(p.to, to, 4); p.stands = p.gone = 0; p.digits = digits; return true; }
  return planSwap(p, layoutFor(upright, digits), from, to, places, digits);
}

// Check a plan from scratch, as the Python one does: no two movers clash,
// and when all are done the panel holds exactly the new digits.
inline bool planRight(const Plan &p) {
  const Layout &L = *p.L;
  for (uint8_t i = 0; i < p.count; i++) {
    for (uint8_t o = 0; o < p.count; o++) {
      if (o == i) { continue; }
      for (uint8_t j = 0; j < p.m[i].count; j++) {
        if (!L.on(p.m[i].cells[j])) { continue; }
        for (uint8_t k = 0; k < p.m[o].count; k++) {
          if (!same(p.m[i].cells[j], p.m[o].cells[k])) { continue; }
          int16_t a, b, c, d;
          p.m[i].span(p.m[i].indexOf(j), a, b);
          p.m[o].span(p.m[o].indexOf(k), c, d);
          if (clash(a, b, c, d)) { return false; }
        }
      }
    }
  }
  CellSet want, have;
  want.clear();
  for (uint8_t k = 0; k < 4; k++) {
    for (uint8_t i = 0; i < pathLen(p.digits, p.to[k]); i++) { want.add(L, L.digit(p.digits, k, p.to[k], i)); }
  }
  have.clear();
  for (uint8_t k = 0; k < 4; k++) {
    if (p.moving & (1 << k)) { continue; }
    for (uint8_t i = 0; i < pathLen(p.digits, p.from[k]); i++) { have.add(L, L.digit(p.digits, k, p.from[k], i)); }
  }
  for (uint8_t i = 0; i < p.count; i++) {
    const Mover &m = p.m[i];
    uint16_t head = m.headAt((int16_t)p.gone), n = m.lengthAt((int16_t)p.gone);
    for (uint16_t b = 0; b < n; b++) { have.add(L, m.cellAt((uint16_t)(head - n + 1 + b))); }
  }
  return memcmp(want.bits, have.bits, sizeof(want.bits)) == 0;
}

// ---- drawing -----------------------------------------------------------------------
// The looks: solid, 2 px thick, the head with an eye, or the body as a line
// of single LEDs. (Darker towards the tail and dark rings were looks of
// their own at first, 2026-10-23; they are colours now: a gradient, stripes.)
enum Look : uint8_t { LOOK_SOLID, LOOK_LINE, LOOK_COUNT };

// A snake's colours: `shade(i, n)` the body's cell i of n (tail 0), `head`,
// `bulge` (a swallowed apple), and black for the eye.
struct Colours {
  uint16_t head, bulge;
  uint16_t (*shade)(void *ctx, uint8_t look, uint16_t i, uint16_t n);
  void *ctx;
};

// One snake: its cells from index `first` on, `n` of them (tail first),
// `sub` pixels on its way to cell `next`. The line runs a pixel at a time:
// each cell's corner and the pixels between two cells (one on cells of 2 px,
// two on cells of 3 px); each pixel on the way adds one at the head towards
// its next cell and drops one at the tail - unless `tailStays`: the snake
// grows at this step. G is anything with drawPixel(x, y, colour) that clips.
template <class G, class CellOf>
void drawSnake(G &g, const Layout &L, CellOf cellOf, uint16_t n, uint8_t sub, Cell next, uint8_t look,
               const Colours &col, int16_t bulge = -1, bool tailStays = false) {
  if (n == 0) { return; }
  const uint8_t thick = look == LOOK_LINE ? 1 : 2;
  const uint8_t p = L.pitch;
  // the positions along the line: p (n - 1) + 1, sub more on the way, the first sub dropped then
  uint16_t total = (uint16_t)(p * (n - 1) + 1 + sub);
  uint16_t firstPos = tailStays ? 0 : sub;
  uint16_t headLen = look == LOOK_LINE ? 2 : 1;
  uint16_t headFrom = total > headLen ? total - headLen : 0;   // a snake of a cell: all head
  int16_t hy = 0, hx = 0, py = 0, px = 0;
  for (uint16_t q = firstPos; q < total; q++) {
    int16_t y, x;
    uint16_t cell = q / p;
    uint8_t k = (uint8_t)(q % p);
    Cell a = cellOf(cell);
    y = L.pxY(a); x = L.pxX(a);
    if (k) {
      Cell b = cell + 1 < n ? cellOf(cell + 1) : next;
      y = (int16_t)(y + (L.pxY(b) - y) * k / p);
      x = (int16_t)(x + (L.pxX(b) - x) * k / p);
    }
    uint16_t c;
    if (q >= headFrom) { c = col.head; }
    else if (bulge >= 0 && (int16_t)cell == bulge) { c = col.bulge; }   // as thick as the snake, its colour shows it
    else { c = col.shade(col.ctx, look, cell, n); }
    for (uint8_t dy = 0; dy < thick; dy++) {
      for (uint8_t dx = 0; dx < thick; dx++) { g.drawPixel(x + dx, y + dy, c); }
    }
    py = hy; px = hx;
    hy = y; hx = x;
  }
  if (look != LOOK_LINE && total - firstPos > 1) {
    // the eye: in the head's 2 x 2, the pixel ahead on its left
    int16_t dy = hy - py, dx = hx - px;
    int16_t ey = dx == 0 ? hy + (dy > 0 ? 1 : 0) : hy;
    int16_t ex = dy == 0 ? hx + (dx > 0 ? 1 : 0) : hx + (dy > 0 ? 1 : 0);
    g.drawPixel(ex, ey, 0);
  }
}

// ---- scenes between changes -------------------------------------------------------
// The tail chase: where head and tail lie side by side, a snake can crawl
// round its own digit; its head goes round once, the digit stays whole. The
// coils all can; of the thin strokes only the 0 and the 1.
inline bool chaseable(uint8_t f, uint8_t d) {
  uint8_t n = pathLen(f, d), a = SNAKE_PATH[f][glyphOf(d)][0], b = SNAKE_PATH[f][glyphOf(d)][n - 1];
  int8_t dr = (int8_t)((a >> 4) - (b >> 4)), dc = (int8_t)((a & 0x0F) - (b & 0x0F));
  return (dr == 0 && (dc == 1 || dc == -1)) || (dc == 0 && (dr == 1 || dr == -1));
}
inline uint32_t chaseMs(const Layout &L, uint8_t f, uint8_t d) { return (uint32_t)pathLen(f, d) * L.stepMs(); }
// The apple: it rolls in along the lane from beyond the right side to below
// a head and hops, the snake waits a moment, its head strikes down at it,
// holds it, pulls back, and the swallowed apple runs down the body to the
// tail as a bulge. Slow enough to be followed (Peter, 2026-10-23: the first
// one, 1.9 s and a 2 x 2 px apple dropping 5 px, was over before it was seen).
// It lies its layout's appleDepth cells below the head: 6 px.
const uint16_t APPLE_FALL_MS = 700;      // rolling in, slower and slower
const uint16_t APPLE_BOUNCE_MS = 240;    // a hop of 2 px
const uint16_t APPLE_WAIT_MS = 400;      // lying there before the strike
const uint16_t STRIKE_DOWN_MS = 120;     // a cell down
const uint16_t STRIKE_HOLD_MS = 300;     // the head on the apple
const uint16_t STRIKE_BACK_MS = 160;     // a cell back
const uint16_t BULGE_MS = 40;            // a cell down the body
const uint32_t APPLE_STRIKE_AT = (uint32_t)APPLE_FALL_MS + APPLE_BOUNCE_MS + APPLE_WAIT_MS;
inline uint32_t appleBulgeAt(const Layout &L) {
  return APPLE_STRIKE_AT + (uint32_t)L.appleDepth * (STRIKE_DOWN_MS + STRIKE_BACK_MS) + STRIKE_HOLD_MS;
}
inline uint32_t appleMs(const Layout &L, uint8_t f, uint8_t d) {
  return appleBulgeAt(L) + (uint32_t)pathLen(f, d) * BULGE_MS;
}
// The tongue: a head flicks it three times, each time out a pixel at a
// time to its fork and in again (Peter, 2026-10-24: the first one, 2 px
// shown twice, read as a red dot). A stage of a flick: the pixels it is
// out, 3 the fork - a pixel on either side of the 2 px stem's tip; 0 in.
const uint16_t TONGUE_MS = 50;           // a stage
const uint8_t TONGUE_STAGES[] = { 1, 2, 3, 3, 2, 1, 0, 0 };
const uint8_t TONGUE_FLICKS = 3;
const uint32_t TONGUE_SCENE_MS = (uint32_t)TONGUE_MS * (TONGUE_FLICKS * sizeof(TONGUE_STAGES) - 2);   // 1.1 s, the last pause left out
inline uint8_t tongueOut(uint32_t ms) {
  return ms >= TONGUE_SCENE_MS ? 0 : TONGUE_STAGES[ms / TONGUE_MS % sizeof(TONGUE_STAGES)];
}

// The panel in pixels: 64 x 32, upright 32 x 64 (the wall shows the same canvas).
inline int16_t panelW(const Layout &L) { return L.gw > L.gh ? 64 : 32; }
inline int16_t panelH(const Layout &L) { return L.gw > L.gh ? 32 : 64; }

struct Pixel {
  int16_t x, y;
};

// A set of the panel's pixels: what the snakes at rest light, for the tongue's
// room. Anything with drawPixel for drawSnake(); a pixel drawn black (the
// eye) is dark.
struct PixelSet {
  uint8_t bits[64 * 64 / 8];   // either orientation, a row of 64 a line
  void clear() { memset(bits, 0, sizeof(bits)); }
  void drawPixel(int16_t x, int16_t y, uint16_t c) {
    if (x < 0 || x >= 64 || y < 0 || y >= 64) { return; }
    uint16_t i = (uint16_t)(y * 64 + x);
    if (c) { bits[i >> 3] |= (uint8_t)(1 << (i & 7)); } else { bits[i >> 3] &= (uint8_t)~(1 << (i & 7)); }
  }
  bool has(int16_t x, int16_t y) const {
    if (x < 0 || x >= 64 || y < 0 || y >= 64) { return false; }
    uint16_t i = (uint16_t)(y * 64 + x);
    return bits[i >> 3] & (1 << (i & 7));
  }
};

// The snake of digit d at place k lying at rest, as the face draws it in the
// look set, added to a set.
inline void snakePixels(const Layout &L, uint8_t f, uint8_t k, uint8_t d, uint8_t look, PixelSet &set) {
  const Colours col = { 1, 1, [](void *, uint8_t, uint16_t, uint16_t) -> uint16_t { return 1; }, nullptr };
  drawSnake(set, L, [&L, f, k, d](uint16_t i) { return L.digit(f, k, d, (uint8_t)i); }, pathLen(f, d), 0, Cell{ 0, 0 },
            look, col);
}

// The tongue of the head of digit d at place k, `out` stages out (1, 2, 3 the
// fork): straight ahead of the head's last move (Peter, 2026-10-24: a tongue at
// a right angle to the way a head looks is wrong), from its mouth - in the
// solid look the front pixel beside the eye, in the line look the head. Writes
// its pixels into px (1, 2 or 4); returns how many.
inline uint8_t tonguePixels(const Layout &L, uint8_t f, uint8_t k, uint8_t d, uint8_t look, uint8_t out, Pixel px[4]) {
  uint8_t n = pathLen(f, d);
  Cell h = L.digit(f, k, d, (uint8_t)(n - 1)), b = L.digit(f, k, d, (uint8_t)(n - 2));
  int16_t dx = h.c - b.c, dy = h.r - b.r;   // a step: one of them +-1
  int16_t mx = L.pxX(h), my = L.pxY(h);
  if (look != LOOK_LINE) {
    // drawSnake's eye: the head's 2 x 2, the pixel ahead on its left
    if (dy == 0) { mx += dx > 0 ? 1 : 0; my += 1; }
    else { mx += dy < 0 ? 1 : 0; my += dy > 0 ? 1 : 0; }
  }
  uint8_t c = 0;
  if (out >= 1) { px[c++] = Pixel{ (int16_t)(mx + dx), (int16_t)(my + dy) }; }
  if (out >= 2) { px[c++] = Pixel{ (int16_t)(mx + 2 * dx), (int16_t)(my + 2 * dy) }; }
  if (out >= 3) {
    px[c++] = Pixel{ (int16_t)(mx + 3 * dx + dy), (int16_t)(my + 3 * dy + dx) };
    px[c++] = Pixel{ (int16_t)(mx + 3 * dx - dy), (int16_t)(my + 3 * dy - dx) };
  }
  return c;
}

// Whether the head at place k can flick its tongue straight ahead over the
// digits on show: every pixel of it on the panel and dark, none beside a
// pixel of its own body (its head aside), none beside or corner to corner
// with another snake or the colon - it would seem to lick it. In the coils
// the next turn of the snake's own coil lies corner to corner with the
// tongue's root, which looks right. Where it cannot, it is not flicked
// (Peter, 2026-10-24).
// What a scene of place k may light besides the snakes at rest is checked
// pixel by pixel against them: on the panel, dark, beside no part of the
// place's own body (its head aside), and beside or corner to corner with no
// other snake or the colon. Set up once per check (`own`: the place's snake,
// `others`: the rest and the colon), then asked of each pixel.
struct SceneRoom {
  PixelSet own, others;
  const Layout *L;
  int16_t hx, hy, size;   // the head's block
  void setUp(const Layout &lay, uint8_t f, const uint8_t digits[4], uint8_t k, uint8_t look) {
    L = &lay;
    own.clear();
    others.clear();
    for (uint8_t j = 0; j < 4; j++) { snakePixels(lay, f, j, digits[j], look, j == k ? own : others); }
    for (uint8_t i = 0; i < 2; i++) {
      for (uint8_t p = 0; p < 4; p++) { others.drawPixel(lay.pxX(lay.colon[i]) + (p & 1), lay.pxY(lay.colon[i]) + (p >> 1), 1); }
    }
    Cell h = lay.digit(f, k, digits[k], (uint8_t)(pathLen(f, digits[k]) - 1));
    hx = lay.pxX(h);
    hy = lay.pxY(h);
    size = look == LOOK_LINE ? 1 : 2;
  }
  bool free(int16_t x, int16_t y) const {
    if (x < 0 || x >= panelW(*L) || y < 0 || y >= panelH(*L) || own.has(x, y) || others.has(x, y)) { return false; }
    for (int8_t dy = -1; dy <= 1; dy++) {
      for (int8_t dx = -1; dx <= 1; dx++) {
        if (!dx && !dy) { continue; }
        if (others.has(x + dx, y + dy)) { return false; }
        bool head = x + dx >= hx && x + dx < hx + size && y + dy >= hy && y + dy < hy + size;
        if ((!dx || !dy) && !head && own.has(x + dx, y + dy)) { return false; }
      }
    }
    return true;
  }
};

// One for both checks: 1 KB, the face is asked from the loop alone.
inline SceneRoom &sceneRoom() {
  static SceneRoom room;
  return room;
}

inline bool tongueRoom(const Layout &L, uint8_t f, const uint8_t digits[4], uint8_t k, uint8_t look) {
  SceneRoom &room = sceneRoom();
  room.setUp(L, f, digits, k, look);
  Pixel px[4];
  uint8_t n = tonguePixels(L, f, k, digits[k], look, 3, px);
  for (uint8_t i = 0; i < n; i++) { if (!room.free(px[i].x, px[i].y)) { return false; } }
  return true;
}

// Whether the cells straight below a place's head are free for the apple:
// the head on its digit's lowest row (a thin digit's head may end higher up,
// its own strokes below it), the cells in the lane, not on the colon
// (upright the hours' lane holds it). The apple asks for one cell more than
// its depth: it is 3 px tall below its stem.
inline bool roomBelow(const Layout &L, uint8_t f, uint8_t k, uint8_t d, uint8_t depth) {
  uint8_t n = pathLen(f, d), low = 0;
  for (uint8_t i = 0; i < n; i++) {
    uint8_t r = (uint8_t)(SNAKE_PATH[f][glyphOf(d)][i] >> 4);
    if (r > low) { low = r; }
  }
  if ((SNAKE_PATH[f][glyphOf(d)][n - 1] >> 4) != low) { return false; }
  Cell h = L.digit(f, k, d, n - 1);
  for (uint8_t i = 1; i <= depth; i++) {
    Cell a = { (int8_t)(h.r + i), h.c };
    if (!L.on(a) || same(a, L.colon[0]) || same(a, L.colon[1])) { return false; }
  }
  return true;
}

// Whether the head at place k can strike at the apple (Peter, 2026-10-24: at
// any digit, not only the minutes' ones): room below it (roomBelow), and
// every pixel the scene lights besides the snakes at rest free (SceneRoom) -
// the head's way down, the apple (3 x 3) with its stem and its hop of 2 px,
// and the lane it rolls in along from beyond the right side.
inline bool appleRoom(const Layout &L, uint8_t f, const uint8_t digits[4], uint8_t k, uint8_t look) {
  if (!roomBelow(L, f, k, digits[k], L.appleDepth + 1)) { return false; }
  SceneRoom &room = sceneRoom();
  room.setUp(L, f, digits, k, look);
  Cell h = L.digit(f, k, digits[k], (uint8_t)(pathLen(f, digits[k]) - 1));
  int16_t hx = room.hx, hy = room.hy, t = room.size;
  int16_t ax = L.pxX(h), ay = (int16_t)(L.pxY(h) + L.appleDepth * L.pitch);   // drawApple()'s apple
  for (int16_t y = hy + t; y < hy + L.appleDepth * L.pitch + t; y++) {      // the strike
    for (int16_t x = hx; x < hx + t; x++) { if (!room.free(x, y)) { return false; } }
  }
  for (int16_t y = ay - 2; y <= ay + 2; y++) {                              // the apple and its hop
    for (int16_t x = ax - 1; x <= ax + 1; x++) { if (!room.free(x, y)) { return false; } }
  }
  for (int16_t y = ay - 3; y < ay; y++) { if (!room.free(ax, y)) { return false; } }   // the stem
  for (int16_t y = ay - 1; y <= ay + 2; y++) {                              // the lane it rolls in along
    for (int16_t x = ax - 1; x < panelW(L); x++) { if (!room.free(x, y)) { return false; } }
  }
  return true;
}

}  // namespace snake
