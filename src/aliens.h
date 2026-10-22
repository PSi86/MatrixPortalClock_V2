/* ----------------------------------------------------------------------
   The Aliens watchface (after Space Invaders, with plain names and pixel
   art of our own): HH:MM in blocks on a grid of 3 x 5 cells of 4 x 4 px a
   digit - 12 x 20 px, the Tetris size - and a cannon below. When a digit
   changes, the blocks it loses wake as aliens, the cannon shoots them one
   by one, and the new digit's formation marches down from above and turns
   into blocks, so the new time stands at :00 (concept doc, "Aliens:
   draft"). At the full hour the whole time goes as a wave; now and then a
   saucer crosses, or an alien shoots back.

   A scene is a plan of actions on a timeline, in milliseconds from its
   start. What the face shows at any moment follows from the plan and the
   time alone (at()), so the face (face_aliens.h) plans it backwards from the minute and
   draws it on whatever pass the loop has.
   ---------------------------------------------------------------------- */
#pragma once

#include <Arduino.h>
#include <string.h>

namespace aliens {

const uint8_t COLS = 3, ROWS = 5, CELL = 4, DIGIT_W = COLS * CELL, DIGIT_H = ROWS * CELL;
const uint8_t NO_DIGIT = 0xFF;

// A digit's lit cells, bit r * 3 + c; a row's three cells as bits 0 (left) to 2.
constexpr uint16_t cellRows(uint8_t a, uint8_t b, uint8_t c, uint8_t d, uint8_t e) {
  return (uint16_t)(a | b << 3 | c << 6 | d << 9 | e << 12);
}
//                                 "###"  "#.#" ".#."  "##."  "..#"  "#.."
const uint8_t R_ALL = 7, R_SIDES = 5, R_MID = 2, R_LEFT2 = 3, R_RIGHT = 4, R_LEFT = 1;
constexpr uint16_t DIGIT_CELLS[10] = {
  cellRows(R_ALL, R_SIDES, R_SIDES, R_SIDES, R_ALL),   // 0
  cellRows(R_MID, R_LEFT2, R_MID, R_MID, R_ALL),       // 1
  cellRows(R_ALL, R_RIGHT, R_ALL, R_LEFT, R_ALL),      // 2
  cellRows(R_ALL, R_RIGHT, R_ALL, R_RIGHT, R_ALL),     // 3
  cellRows(R_SIDES, R_SIDES, R_ALL, R_RIGHT, R_RIGHT), // 4
  cellRows(R_ALL, R_LEFT, R_ALL, R_RIGHT, R_ALL),      // 5
  cellRows(R_ALL, R_LEFT, R_ALL, R_SIDES, R_ALL),      // 6
  cellRows(R_ALL, R_RIGHT, R_MID, R_MID, R_MID),       // 7
  cellRows(R_ALL, R_SIDES, R_ALL, R_SIDES, R_ALL),     // 8
  cellRows(R_ALL, R_SIDES, R_ALL, R_RIGHT, R_ALL),     // 9
};
const uint16_t DASH_CELLS = cellRows(0, 0, R_ALL, 0, 0);   // no time yet

inline uint16_t cellsOf(uint8_t d) { return d <= 9 ? DIGIT_CELLS[d] : DASH_CELLS; }

// Where things stand. The cannon is 5 x 3, x its left edge, its muzzle at
// x + 2. The saucer flies in the lane above the digits (y 0-2) and is shot
// through a gap between two digits (gapX: where the shot goes up, clear of
// the colon); it flies slower upright, where the panel is narrow and the
// shot's way long. `bombers`: the digits that can shoot back, with nothing
// below them but the cannon (upright the hours stand above the minutes).
struct Layout {
  int16_t  w, h;
  int16_t  digitX[4], digitY[4];
  int16_t  colonX[2], colonY[2];   // the colon's two dots, 2 x 2
  int16_t  cannonY, cannonHome;
  int16_t  gapX[3];
  uint8_t  gaps, bombers;
  uint16_t saucerMsPx;
};
const Layout LANDSCAPE = { 64, 32, { 3, 17, 35, 49 }, { 3, 3, 3, 3 }, { 31, 31 }, { 9, 15 }, 29, 29,
                           { 15, 29, 47 }, 3, 0x0F, 30 };
const Layout PORTRAIT  = { 32, 64, { 3, 17, 3, 17 }, { 3, 3, 27, 27 }, { 13, 17 }, { 23, 23 }, 61, 13,
                           { 15, 15, 15 }, 1, 0x0C, 60 };

// How a digit that changes goes: all its aliens shot and the new formation
// marching in, or only the ones the new digit does not have; "mixed" takes
// the whole digit when one changes and only the difference when several do,
// as the Muncher's Change setting.
enum Style : uint8_t { STYLE_MIXED, STYLE_ALL, STYLE_DIFF, STYLE_COUNT };

// What a plan plays: a change of the time, the full hour's wave, or one of
// the scenes between changes, which leave the time as it is.
enum Scene : uint8_t { SCENE_CHANGE, SCENE_WAVE, SCENE_SAUCER, SCENE_BACK, SCENE_NAP };

// The pace (ms): the cannon a pixel, the shot a pixel, the burst, the
// formation's step down by a cell, the aliens' march while they are awake -
// a step of their legs and a pixel sideways. WAKE_MS: a transformation is
// given a moment to be seen as one, apart from what moves around it -
// after blocks have woken, before the cannon moves, and after the new
// formation has landed, before it turns into blocks (Peter, 2026-10-11).
const uint16_t MOVE_MS_PX = 25, SHOT_MS_PX = 10, BURST_MS = 240, PAUSE_MS = 120, STEP_MS = 300, MARCH_MS = 400;
const uint16_t WAKE_MS = 1500;
// The full hour's wave is shot row by row from the bottom, the cannon at
// about the arcade's pace, firing on a beat of its own (WAVE_FIRE_MS):
// it drives on as soon as it has fired and stands at least WAVE_AIM_MS
// before the next shot. Once WAVE_ROOM_ROWS rows below the wave are
// empty, the wave steps down a row on the beat of its march, and the
// cannon fires on as it does (Peter, 2026-10-14: it stepped down at once,
// before a shot, and took long; 2026-10-16: the cannon waited for every
// step down).
const uint16_t WAVE_MOVE_MS_PX = 16, WAVE_AIM_MS = 80, WAVE_FIRE_MS = 250;
const uint8_t  WAVE_ROOM_ROWS = 2;
// The saucer: the cannon sees it, then drives to a gap it reaches in time;
// the saucer is hit in its middle, in its row 1 (its lower row has a gap there).
const uint16_t REACT_MS = 400, SAUCER_BURST_MS = 600;
const uint8_t  SAUCER_W = 7, SAUCER_HIT_Y = 1;
// An alien shooting back: its bomb falls once the cannon has driven under
// it, with the alien in the middle of its sway; the hit cannon is a wreck,
// then gone, then a new one blinks in at home.
const uint16_t BOMB_MS_PX = 60;   // from the lowest row the fall is 5 px, still to be seen
const uint16_t WRECK_MS = 900, GONE_MS = 600, BLINK_MS = 900, BLINK_STEP_MS = 150;
// A block woken now and then, for no fight (an option, Peter 2026-10-12):
// awake this long, then a block again.
const uint16_t NAP_MS = 2 * WAKE_MS;
// The sway, in march steps: still, right, still, left - each alien a pixel
// into its own gap or its neighbour's, as the arcade's formation moves.
const int8_t SWAY[4] = { 0, 1, 0, -1 };
const int8_t ARRIVE_FROM = -5;   // the formation starts this many rows above its place

enum Act : uint8_t { ACT_MOVE, ACT_SHOT, ACT_DROP, ACT_DESCEND, ACT_HOME };
const uint8_t AT_SAUCER = 0xFF;  // a shot's digit when it is for the saucer
struct Action {
  uint8_t  act, digit, cell;
  int8_t   drop;       // ACT_DROP: the rows the wave stands lower from then on
  int16_t  x;          // ACT_MOVE, ACT_HOME, ACT_SHOT: where the cannon is
  uint32_t at, len;    // ms from the plan's start; the plan is kept in the order of `at`
  uint16_t fly;        // ACT_SHOT: ms on its way up, then its burst; len ends with the burst, or
                       // with the hit when the cannon has driven on already (the wave)
};
// A move and a shot for every cell of four digits, the wave's steps down
// (upright it has ten rows), the descent, home.
const uint8_t MAX_ACTIONS = 4 * 15 * 2 + 2 * ROWS + 2;

class Face {
 public:
  Layout L = LANDSCAPE;
  uint8_t  shown[4] = { NO_DIGIT, NO_DIGIT, NO_DIGIT, NO_DIGIT };   // the digits standing between scenes
  bool     running = false;
  uint8_t  scene = SCENE_CHANGE;
  // What the face shows, as at() works it out:
  uint16_t cells[4] = { 0, 0, 0, 0 };    // lit cells
  uint16_t awake[4] = { 0, 0, 0, 0 };    // ... of them, woken as aliens
  uint16_t arriving[4] = { 0, 0, 0, 0 }; // the new formation's cells on their way down
  int8_t   arriveRow = 0;                // how many rows above their place they are
  int8_t   drop = 0;                     // rows the awake aliens stand lower (the wave, stepped down)
  int16_t  cannonX = 0;
  bool     cannonOn = true;              // false while it is a wreck or gone, and between its blinks
  bool     wreckOn = false;              // the hit cannon's wreck, at cannonX
  uint8_t  wreckFrame = 0;
  bool     shotOn = false;
  int16_t  shotX = 0, shotY = 0;         // the shot's top pixel; it is 1 x 2
  bool     burstOn = false, burstWide = false;
  int16_t  burstX = 0, burstY = 0;       // a hit's burst, its top left: 3 x 3, or the saucer's width when wide
  bool     saucerOn = false;
  int16_t  saucerX = 0;                  // its left edge; it flies at y 0
  bool     bombOn = false;
  int16_t  bombX = 0, bombY = 0;         // the bomb's top pixel; it is 1 x 2
  uint32_t standMs = 0, endMs = 0;       // the new time stands complete (a change), the scene is over
  // When the scene's parts happen (ms), for the self-test as well:
  uint32_t saucerHitAt = 0, bombAt = 0, hitAt = 0, backAt = 0, steadyAt = 0;

  void setLayout(const Layout &l) { L = l; cannonX = L.cannonHome; }

  void show(const uint8_t d[4]) {
    running = false;
    for (uint8_t i = 0; i < 4; i++) {
      shown[i] = d[i];
      cells[i] = cellsOf(d[i]);
      awake[i] = arriving[i] = 0;
    }
    clearSprites();
    drop = 0;
    cannonX = L.cannonHome;
  }

  // Where the cannon has to be to hit cell `cell` of digit i, and the cell's top.
  int16_t aimX(uint8_t i, uint8_t cell) const { return L.digitX[i] + CELL * (cell % COLS) - 1; }
  int16_t cellY(uint8_t i, uint8_t cell) const { return L.digitY[i] + CELL * (cell / COLS); }

  // The panel's rows of cells, counted from the digits' top: upright the
  // minutes' rows come after the hours' and a row between them.
  int8_t panelRow(uint8_t i, uint8_t cell) const {
    int16_t top = min(min(L.digitY[0], L.digitY[1]), min(L.digitY[2], L.digitY[3]));
    return (int8_t)((cellY(i, cell) - top) / CELL);
  }
  int8_t lowestRow() const {
    int8_t low = 0;
    for (uint8_t i = 0; i < 4; i++) { low = max(low, panelRow(i, (ROWS - 1) * COLS)); }
    return low;
  }

  // Whether a lit cell of any digit has its top left at (x, y): an alien
  // coming down there is behind it (upright, the minutes' new formation
  // comes down behind the hours).
  bool blockAt(int16_t x, int16_t y) const {
    for (uint8_t i = 0; i < 4; i++) {
      int16_t dx = x - L.digitX[i], dy = y - L.digitY[i];
      if (dx < 0 || dy < 0 || dx >= DIGIT_W || dy >= DIGIT_H || dx % CELL || dy % CELL) { continue; }
      if (cells[i] & (1 << (dy / CELL * COLS + dx / CELL))) { return true; }
    }
    return false;
  }

  // The way from `to` and the style: the cells each changing digit loses and
  // gains. Returns the time the new time stands at (ms), 0 when nothing
  // changes. `force` changes the digits in its bits even when they are the
  // same (the animation "last digit again", a preview). `wave`: the full hour's - every
  // digit wakes whole, the wave is shot away row by row from the bottom,
  // stepping down as it thins out while the cannon fires on, and the new
  // time marches in.
  uint32_t planChange(const uint8_t to[4], uint8_t style, uint8_t force = 0, bool wave = false) {
    begin(wave ? SCENE_WAVE : SCENE_CHANGE);
    if (wave) { style = STYLE_ALL; force = 0x0F; }
    uint8_t changing = 0;
    for (uint8_t i = 0; i < 4; i++) { if (to[i] != shown[i] || (force & (1 << i))) { changing++; } }
    if (changing == 0) { standMs = endMs = 0; return 0; }
    bool whole = style == STYLE_ALL || (style == STYLE_MIXED && changing == 1);
    uint16_t anyLose = 0, anyGain = 0;
    for (uint8_t i = 0; i < 4; i++) {
      target[i] = to[i];
      lose[i] = gain[i] = 0;
      bool change = to[i] != shown[i] || (force & (1 << i));
      if (!change) { continue; }
      uint16_t from = cellsOf(shown[i]), into = cellsOf(to[i]);
      lose[i] = whole ? from : (uint16_t)(from & ~into);
      gain[i] = whole ? into : (uint16_t)(into & ~from);
      // The way down to a new block is shot free: a block that stays above
      // it in its column wakes and is shot as well, and comes back with the
      // new formation - else the new alien would come down hidden behind it
      // (Peter, 2026-10-14).
      uint16_t way = above(gain[i]) & from & into;
      lose[i] |= way;
      gain[i] |= way;
      anyLose |= lose[i];
      anyGain |= gain[i];
    }
    uint32_t t = 0;
    int16_t x = L.cannonHome;
    if (anyLose) {
      t = WAKE_MS;          // before the first shot, a moment to see the aliens woken
      x = wave ? sweep(t) : byColumns(t);
    }
    if (anyGain) {
      uint32_t len = (uint32_t)(-ARRIVE_FROM) * STEP_MS;
      add(ACT_DESCEND, 0, 0, x, t, len, 0);
      t += len;
      t += WAKE_MS;         // landed as aliens: a moment before they turn into blocks
    }
    standMs = t;
    uint32_t home = (uint32_t)abs(L.cannonHome - x) * MOVE_MS_PX;
    add(ACT_HOME, 0, 0, L.cannonHome, t, home, 0);
    endMs = t + home;
    return standMs;
  }

  // A saucer crossing the lane, from the left or the right; the cannon
  // drives to a gap between two digits it reaches in time - `pick` chooses
  // among them - and shoots it in its middle. Returns how long it takes,
  // 0 when no gap can be reached.
  uint32_t planSaucer(bool fromLeft, uint8_t pick) {
    begin(SCENE_SAUCER);
    keep();
    saucerDir = fromLeft ? 1 : -1;
    saucerFrom = fromLeft ? -(int16_t)SAUCER_W : L.w;
    uint16_t fly = (uint16_t)(L.cannonY - 2 - SAUCER_HIT_Y) * SHOT_MS_PX;
    int16_t gx[3];
    uint32_t fireAt[3];
    uint8_t n = 0;
    for (uint8_t g = 0; g < L.gaps; g++) {
      // How far the saucer flies until its middle is over the gap.
      int32_t way = (int32_t)(L.gapX[g] - SAUCER_W / 2 - saucerFrom) * saucerDir;
      uint32_t over = way > 0 ? (uint32_t)way * L.saucerMsPx : 0;
      uint32_t ready = REACT_MS + (uint32_t)abs(L.gapX[g] - 2 - L.cannonHome) * MOVE_MS_PX + PAUSE_MS;
      if (over < (uint32_t)fly + ready) { continue; }
      gx[n] = L.gapX[g];
      fireAt[n] = over - fly;
      n++;
    }
    if (n == 0) { standMs = endMs = 0; return 0; }
    uint8_t p = pick % n;
    int16_t cx = gx[p] - 2;
    if (cx != L.cannonHome) { add(ACT_MOVE, 0, 0, cx, REACT_MS, (uint32_t)abs(cx - L.cannonHome) * MOVE_MS_PX, 0); }
    add(ACT_SHOT, AT_SAUCER, 0, cx, fireAt[p], (uint32_t)fly + SAUCER_BURST_MS, fly);
    saucerHitAt = fireAt[p] + fly;
    uint32_t t = saucerHitAt + SAUCER_BURST_MS + PAUSE_MS;
    add(ACT_HOME, 0, 0, L.cannonHome, t, (uint32_t)abs(L.cannonHome - cx) * MOVE_MS_PX, 0);
    endMs = t + plan[count - 1].len;
    standMs = 0;
    return endMs;
  }

  // An alien shooting back: the lowest block of column c of digit i wakes;
  // after the moment to see it woken the cannon drives under it, and once
  // it is there the alien drops its bomb, which hits it - timed backwards
  // from the hit, a long fall had the bomb on its way before the cannon
  // moved (Peter, 2026-10-12). The wreck, a moment without a cannon, a new
  // one blinking in at home; then the alien turns back into its block.
  // Returns how long it takes, 0 when that digit cannot shoot back or the
  // column is empty.
  uint32_t planBack(uint8_t i, uint8_t c) {
    begin(SCENE_BACK);
    keep();
    standMs = endMs = 0;
    if (i >= 4 || c >= COLS || !(L.bombers & (1 << i)) || shown[i] > 9) { return 0; }
    uint16_t lit = cellsOf(shown[i]);
    int8_t r = ROWS - 1;
    while (r >= 0 && !(lit & (1 << (r * COLS + c)))) { r--; }
    if (r < 0) { return 0; }
    backDigit = i;
    backCell = (uint8_t)(r * COLS + c);
    int16_t ax = aimX(i, backCell);
    bombColX = ax + 2;                           // the alien's middle, over the muzzle
    bombY0 = cellY(i, backCell) + 3;             // just below the alien
    uint32_t fall = (uint32_t)(L.cannonY - 2 - bombY0) * BOMB_MS_PX;   // until it meets the muzzle
    uint32_t drive = (uint32_t)abs(ax - L.cannonHome) * MOVE_MS_PX;
    if (drive) { add(ACT_MOVE, i, backCell, ax, WAKE_MS, drive, 0); }
    // Arrived, a pause to take aim, and the next step of the march in which
    // the alien stands in its middle (the even steps), so the bomb leaves it there.
    uint32_t t = WAKE_MS + drive + PAUSE_MS;
    if ((t / MARCH_MS) % 2) { t = (t / MARCH_MS + 1) * MARCH_MS; }
    bombAt = t;
    hitAt = bombAt + fall;
    backAt = hitAt + WRECK_MS + GONE_MS;
    add(ACT_HOME, 0, 0, L.cannonHome, backAt, 0, 0);
    steadyAt = backAt + BLINK_MS;
    endMs = steadyAt + WAKE_MS;                  // the alien's moment before it turns back
    return endMs;
  }

  // A block that wakes for a moment between changes and turns back into
  // itself: block k of digit i. Returns how long it takes, 0 when that
  // block is not lit.
  uint32_t planNap(uint8_t i, uint8_t k) {
    begin(SCENE_NAP);
    keep();
    standMs = endMs = 0;
    if (i >= 4 || k >= COLS * ROWS || shown[i] > 9 || !(cellsOf(shown[i]) & (1 << k))) { return 0; }
    backDigit = i;
    backCell = k;
    endMs = NAP_MS;
    return endMs;
  }

  void start() { running = true; at(0); }

  // The picture at `ms` from the start; past endMs the scene is over and
  // the digits stand.
  void at(uint32_t ms) {
    if (!running) { return; }
    if (ms >= endMs) { finish(); return; }
    for (uint8_t i = 0; i < 4; i++) {
      cells[i] = cellsOf(shown[i]);
      awake[i] = lose[i];          // they wake at once and stay awake until shot
      arriving[i] = 0;
    }
    clearSprites();
    arriveRow = ARRIVE_FROM;
    drop = 0;
    int16_t x = startX;
    // Every action begun by now, in the order they begin; in the wave the
    // cannon drives on while its shot flies and the wave steps down.
    for (uint8_t n = 0; n < count; n++) {
      const Action &a = plan[n];
      if (ms < a.at) { break; }
      uint32_t into = ms - a.at;
      bool done = into >= a.len;
      switch (a.act) {
        case ACT_MOVE:
        case ACT_HOME:
          x = done ? a.x : (int16_t)(x + (int32_t)(a.x - x) * (int32_t)into / (int32_t)a.len);
          break;
        case ACT_DROP:
          drop = a.drop;
          break;
        case ACT_SHOT:
          if (into < a.fly) {        // on its way up
            shotOn = true;
            shotX = a.x + 2;
            shotY = (int16_t)(L.cannonY - 2 - (int32_t)into / SHOT_MS_PX);
          } else if (a.digit == AT_SAUCER) {
            if (into < (uint32_t)a.fly + SAUCER_BURST_MS) { burstOn = burstWide = true; burstX = a.x + 2 - SAUCER_W / 2; burstY = 0; }
          } else {                   // a hit: the cell goes, its burst stays a moment where it was hit
            cells[a.digit] &= (uint16_t)~(1 << a.cell);
            awake[a.digit] &= (uint16_t)~(1 << a.cell);
            if (into < (uint32_t)a.fly + BURST_MS) {
              burstOn = true;
              burstX = L.digitX[a.digit] + CELL * (a.cell % COLS);
              burstY = cellY(a.digit, a.cell);   // the wave's rows lower added below
            }
          }
          break;
        case ACT_DESCEND:
          drop = 0;
          if (done) {
            for (uint8_t i = 0; i < 4; i++) {
              cells[i] |= gain[i];
              if (ms < standMs) { awake[i] |= gain[i]; }   // landed, still aliens
            }
          } else {
            arriveRow = (int8_t)(ARRIVE_FROM + (int32_t)into / STEP_MS);
            for (uint8_t i = 0; i < 4; i++) { arriving[i] = gain[i]; }
          }
          break;
      }
    }
    // An alien's burst goes down with the wave when it steps down (it has
    // the place of a shot alien in the formation), so it never stands on
    // one coming down.
    if (burstOn && !burstWide) { burstY += CELL * drop; }
    cannonX = x;
    if (scene == SCENE_SAUCER && ms < saucerHitAt) {
      saucerOn = true;
      saucerX = (int16_t)(saucerFrom + saucerDir * (int32_t)(ms / L.saucerMsPx));
    }
    if (scene == SCENE_NAP) { awake[backDigit] |= (uint16_t)(1 << backCell); }
    if (scene == SCENE_BACK) {
      awake[backDigit] |= (uint16_t)(1 << backCell);
      if (ms >= bombAt && ms < hitAt) {
        bombOn = true;
        bombX = bombColX;
        bombY = (int16_t)(bombY0 + (int32_t)((ms - bombAt) / BOMB_MS_PX));
      }
      if (ms >= hitAt && ms < hitAt + WRECK_MS) {
        wreckOn = true;
        wreckFrame = (uint8_t)(((ms - hitAt) / BLINK_STEP_MS) & 1);
      }
      cannonOn = ms < hitAt || (ms >= backAt && (ms >= steadyAt || ((ms - backAt) / BLINK_STEP_MS) % 2 == 0));
    }
  }

  // The aliens' march frame while a scene runs.
  static uint8_t marchFrame(uint32_t ms) { return (uint8_t)((ms / MARCH_MS) & 1); }

  // How far the awake alien in cell k of digit i stands to the side at `ms`
  // (after at()): the formation's sway, but not towards a block that stays.
  // The awake aliens of a row sway together, so an alien has room when the
  // row has an empty cell or the digit's edge that way before such a block;
  // one between two of them stands still (Peter, 2026-10-11).
  int8_t sway(uint8_t i, uint8_t k, uint32_t ms) const {
    int8_t want = SWAY[(ms / MARCH_MS) % 4];
    if (want == 0) { return 0; }
    uint8_t row = k - k % COLS;
    for (int8_t c = (int8_t)(k % COLS) + want; c >= 0 && c < (int8_t)COLS; c += want) {
      uint16_t bit = (uint16_t)1 << (row + c);
      if (!(cells[i] & bit)) { return want; }   // an empty cell
      if (!(awake[i] & bit)) { return 0; }      // a block that stays
    }
    return want;                                // the digit's edge
  }

 private:
  Action   plan[MAX_ACTIONS];
  uint8_t  count = 0;
  uint8_t  target[4] = { NO_DIGIT, NO_DIGIT, NO_DIGIT, NO_DIGIT };
  uint16_t lose[4] = { 0, 0, 0, 0 }, gain[4] = { 0, 0, 0, 0 };
  int16_t  startX = 0;
  int16_t  saucerFrom = 0;
  int8_t   saucerDir = 1;
  uint8_t  backDigit = 0, backCell = 0;
  int16_t  bombColX = 0, bombY0 = 0;

  void begin(uint8_t s) {
    scene = s;
    count = 0;
    startX = L.cannonHome;
    saucerHitAt = bombAt = hitAt = backAt = steadyAt = 0;
  }

  // A scene between changes: the digits stay.
  void keep() {
    for (uint8_t i = 0; i < 4; i++) { target[i] = shown[i]; lose[i] = gain[i] = 0; }
  }

  void clearSprites() {
    shotOn = burstOn = burstWide = saucerOn = bombOn = wreckOn = false;
    cannonOn = true;
  }

  // The cells above the lowest of `cells` in each column.
  static uint16_t above(uint16_t cells) {
    uint16_t out = 0;
    for (uint8_t c = 0; c < COLS; c++) {
      uint16_t higher = 0;
      for (uint8_t r = 0; r < ROWS; r++) {
        uint16_t bit = (uint16_t)(1 << (r * COLS + c));
        if (cells & bit) { out |= higher; }
        higher |= bit;
      }
    }
    return out;
  }

  // How long a shot flies from the muzzle up to cell k of digit i, standing `down` rows lower.
  uint16_t flyMs(uint8_t i, uint8_t k, int8_t down) const {
    int16_t travel = (L.cannonY - 2) - (cellY(i, k) + CELL * down + 2);
    return (uint16_t)max<int16_t>(travel, 1) * SHOT_MS_PX;
  }

  // The cannon drives from x to ax from t on, msPx a pixel. Returns ax.
  int16_t moveTo(int16_t ax, int16_t x, uint32_t &t, uint16_t msPx) {
    if (ax != x) {
      uint32_t len = (uint32_t)abs(ax - x) * msPx;
      add(ACT_MOVE, 0, 0, ax, t, len, 0);
      t += len;
    }
    return ax;
  }

  // When a shot fired at `fire` meets the alien in cell k of digit i, with
  // the wave stepping down a row at each of the `steps` times in `stepAt`:
  // it meets the alien where it stands then, or the alien comes down onto
  // it.
  uint32_t meet(uint32_t fire, uint8_t i, uint8_t k, const uint32_t *stepAt, uint8_t steps) const {
    uint8_t j = 0;                   // the rows the wave stands lower
    while (j < steps && stepAt[j] <= fire) { j++; }
    uint32_t from = fire;
    for (;; j++) {
      uint32_t h = max(from, fire + flyMs(i, k, (int8_t)j));
      if (j >= steps || h < stepAt[j]) { return h; }
      from = stepAt[j];
    }
  }

  // A change's fight from t on: digit by digit, column by column from the
  // left, each from the bottom up, the cannon waiting for each burst.
  // Returns where the cannon ends.
  int16_t byColumns(uint32_t &t) {
    int16_t x = L.cannonHome;
    for (uint8_t i = 0; i < 4; i++) {
      for (uint8_t c = 0; c < COLS; c++) {
        for (int8_t r = ROWS - 1; r >= 0; r--) {
          uint8_t k = (uint8_t)(r * COLS + c);
          if (!(lose[i] & (1 << k))) { continue; }
          x = moveTo(aimX(i, k), x, t, MOVE_MS_PX);
          uint16_t fly = flyMs(i, k, 0);
          add(ACT_SHOT, i, k, x, t, (uint32_t)fly + BURST_MS, fly);
          t += plan[count - 1].len + PAUSE_MS;
        }
      }
    }
    return x;
  }

  // The wave's fight from t on: the panel's rows from the bottom up -
  // upright the minutes' first - each from its end nearer the cannon. The
  // cannon keeps its own beat: it drives on to its next alien as soon as it
  // has fired, and fires on the first beat at which it stands there aimed
  // and its last shot has hit - a long way costs it a beat, the wave never
  // does. The wave keeps the beat of its march: once WAVE_ROOM_ROWS rows
  // below it are empty it steps down a row on the march's next beat, a row
  // a beat, so it comes nearer as it thins out, but never further than a
  // row above where the digits' lowest row stood. A step down is in the
  // plan only while an alien is left to see it. Returns where the cannon
  // ends.
  int16_t sweep(uint32_t &t) {
    int16_t x = L.cannonHome;
    int8_t bottom = lowestRow(), drop = 0;
    uint32_t stepAt[2 * ROWS];
    uint8_t steps = 0, planned = 0;   // steps down due, and of them in the plan
    uint32_t drive = t, fired = 0, hit = t;
    bool first = true;
    for (int8_t row = bottom; row >= 0; row--) {
      uint8_t n = 0, di[4 * COLS], dk[4 * COLS];
      for (uint8_t i = 0; i < 4; i++) {
        for (uint8_t k = 0; k < COLS * ROWS; k++) {
          if ((lose[i] & (1 << k)) && panelRow(i, k) == row) { di[n] = i; dk[n] = k; n++; }
        }
      }
      if (n == 0) { continue; }
      while (row + drop + WAVE_ROOM_ROWS <= bottom && steps < 2 * ROWS) {
        // After the hit that made room, a beat after the step before.
        uint32_t after = steps ? max(hit, stepAt[steps - 1] + MARCH_MS) : hit;
        stepAt[steps++] = (after + MARCH_MS - 1) / MARCH_MS * MARCH_MS;
        drop++;
      }
      for (uint8_t a = 1; a < n; a++) {   // by where the cannon aims, from the left
        for (uint8_t b = a; b > 0 && aimX(di[b], dk[b]) < aimX(di[b - 1], dk[b - 1]); b--) {
          uint8_t si = di[b], sk = dk[b];
          di[b] = di[b - 1]; dk[b] = dk[b - 1];
          di[b - 1] = si; dk[b - 1] = sk;
        }
      }
      bool fromRight = abs(aimX(di[n - 1], dk[n - 1]) - x) < abs(aimX(di[0], dk[0]) - x);
      for (uint8_t m = 0; m < n; m++) {
        uint8_t o = fromRight ? n - 1 - m : m;
        uint32_t ready = drive;
        x = moveTo(aimX(di[o], dk[o]), x, ready, WAVE_MOVE_MS_PX);
        ready = max(ready + WAVE_AIM_MS, hit);
        uint32_t fire = first ? ready : fired + (ready - fired + WAVE_FIRE_MS - 1) / WAVE_FIRE_MS * WAVE_FIRE_MS;
        first = false;
        hit = meet(fire, di[o], dk[o], stepAt, steps);
        for (; planned < steps && stepAt[planned] <= hit; planned++) {   // this alien is there to step down
          add(ACT_DROP, 0, 0, 0, stepAt[planned], 0, 0, (int8_t)(planned + 1));
        }
        add(ACT_SHOT, di[o], dk[o], x, fire, hit - fire, (uint16_t)(hit - fire));
        fired = drive = fire;
      }
    }
    t = hit + BURST_MS + PAUSE_MS;   // the last burst seen, before the new formation comes
    return x;
  }

  void finish() {
    for (uint8_t i = 0; i < 4; i++) {
      shown[i] = target[i];
      cells[i] = cellsOf(target[i]);
      awake[i] = arriving[i] = 0;
    }
    clearSprites();
    drop = 0;
    cannonX = L.cannonHome;
    running = false;
  }

  // Into the plan in the order of `at`, after those that begin at the same time.
  void add(uint8_t act, uint8_t digit, uint8_t cell, int16_t x, uint32_t at, uint32_t len, uint16_t fly, int8_t drop = 0) {
    if (count >= MAX_ACTIONS) { return; }
    uint8_t n = count++;
    for (; n > 0 && plan[n - 1].at > at; n--) { plan[n] = plan[n - 1]; }
    plan[n] = { act, digit, cell, drop, x, at, len, fly };
  }
};

// While woken aliens wait (between `from` and `to` ms): none sways into a
// block that stays, and a lit cell it sways towards holds an alien swaying
// the same way.
inline bool swayFits(Face &f, uint8_t i, uint32_t from, uint32_t to) {
  for (uint32_t ms = from; ms < to; ms += MARCH_MS) {
    f.at(ms);
    for (uint8_t k = 0; k < COLS * ROWS; k++) {
      if (!(f.awake[i] & (1 << k))) { continue; }
      int8_t s = f.sway(i, k, ms), c = (int8_t)(k % COLS) + s;
      if (s == 0 || c < 0 || c >= (int8_t)COLS) { continue; }
      uint8_t n = (uint8_t)(k - k % COLS + c);
      if ((f.cells[i] & (1 << n)) && !((f.awake[i] & (1 << n)) && f.sway(i, n, ms) == s)) { return false; }
    }
  }
  return true;
}

// Whether every alien coming down is in sight: none behind a block of its
// own digit (upright, the minutes' formation still comes down behind the
// hours above them).
inline bool arrivalSeen(const Face &f) {
  for (uint8_t i = 0; i < 4; i++) {
    for (uint8_t k = 0; k < COLS * ROWS; k++) {
      if (!(f.arriving[i] & (1 << k))) { continue; }
      int8_t row = (int8_t)(k / COLS) + f.arriveRow;
      if (row >= 0 && (f.cells[i] & (1 << (row * COLS + k % COLS)))) { return false; }
    }
  }
  return true;
}

// Whether a shot on its way up (after at(ms)) is clear of every lit cell,
// awake or a block, where it is drawn: it hits the lowest alien of its
// column and flies through nothing.
inline bool shotClear(const Face &f, uint32_t ms) {
  if (!f.shotOn) { return true; }
  const int16_t S = CELL - 1;   // a cell's lit size
  for (uint8_t i = 0; i < 4; i++) {
    for (uint8_t k = 0; k < COLS * ROWS; k++) {
      if (!(f.cells[i] & (1 << k))) { continue; }
      bool awake = f.awake[i] & (1 << k);
      int16_t x = f.L.digitX[i] + CELL * (k % COLS) + (awake ? f.sway(i, k, ms) : 0);
      int16_t y = f.cellY(i, k) + (awake ? CELL * f.drop : 0);
      if (f.shotX >= x && f.shotX < x + S && f.shotY < y + S && f.shotY + 2 > y) { return false; }
    }
  }
  return true;
}

// The debug build's check, on both layouts. Every change of every digit,
// both ways: its woken aliens sway rightly, no new alien comes down hidden
// behind a block, a whole digit's shots fly through nothing, the new
// formation stands a moment as aliens and then as the new digit, the
// cannon home, within the minute. The full hour's wave: shot row by row
// from the bottom, through nothing, stepping down only with rows empty
// below it. The saucer from either side to every gap it can be shot at,
// and every alien that can shoot back: the shot meets the saucer, the bomb
// meets the cannon, the time stays; every block that wakes for a moment
// sways rightly and is a block again. Returns false and the first case
// that went wrong (from * 10 + to, + 1000 for the difference; 3000 + the
// wave's case, 4000 + the saucer's, 5000 + digit * 100 + digit's value *
// 10 + column for shooting back, 7000 + value * 15 + cell for a block
// waking; + 10000 upright); `longest` is the longest change to stand,
// `longestScene` the longest scene between changes.
inline bool selfTest(Face &f, uint32_t &longest, uint32_t &longestScene, uint16_t &failed) {
  longest = longestScene = 0;
  const Layout *layouts[2] = { &LANDSCAPE, &PORTRAIT };
  for (uint8_t l = 0; l < 2; l++) {
    f.setLayout(*layouts[l]);
    uint16_t up = (uint16_t)(l * 10000);
    for (uint8_t style = STYLE_ALL; style <= STYLE_DIFF; style++) {
      for (uint8_t a = 0; a < 10; a++) {
        for (uint8_t b = 0; b < 10; b++) {
          uint8_t from[4] = { 1, 2, 3, a }, to[4] = { 1, 2, 3, b };
          // At least these land as aliens; the difference also the blocks
          // that stood in their way.
          uint16_t into = cellsOf(b), gain = style == STYLE_ALL ? into : (uint16_t)(into & ~cellsOf(a));
          f.show(from);
          uint32_t stand = f.planChange(to, style, 1 << 3);
          f.start();     // at(0): woken, waiting
          bool ok = swayFits(f, 3, 0, WAKE_MS);
          for (uint32_t ms = 0; ms < stand && ok; ms += 2 * SHOT_MS_PX) {
            f.at(ms);
            ok = arrivalSeen(f) && (style != STYLE_ALL || shotClear(f, ms));
          }
          if (gain) {    // landed: aliens for a moment, swaying rightly
            f.at(stand - 1);
            ok = ok && f.cells[3] == into && (f.awake[3] & gain) == gain && !(f.awake[3] & ~into) && f.arriving[3] == 0;
            ok = ok && swayFits(f, 3, stand - WAKE_MS, stand);
          }
          f.at(stand);   // the new time stands
          ok = ok && f.cells[3] == into && f.arriving[3] == 0 && f.awake[3] == 0;
          f.at(f.endMs);
          ok = ok && !f.running && f.shown[3] == b && f.cannonX == f.L.cannonHome && f.endMs < 60000;
          if (stand > longest) { longest = stand; }
          if (!ok) { failed = (uint16_t)(a * 10 + b + (style == STYLE_DIFF ? 1000 : 0) + up); return false; }
        }
      }
    }
    // The longest change: all four digits at midnight, the whole of each.
    uint8_t late[4] = { 2, 3, 5, 9 }, midnight[4] = { 0, 0, 0, 0 };
    f.show(late);
    uint32_t stand = f.planChange(midnight, STYLE_ALL);
    if (stand > longest) { longest = stand; }
    if (f.endMs >= 60000) { failed = (uint16_t)(9999 - l); return false; }
    // Every full hour's wave, from hh-1:59 to hh:00: all of it woken, then
    // shot row by row from the bottom, every shot through nothing and
    // meeting its alien where it stands - just above the shot, or come down
    // onto it; the cannon fires on its beat, one or two beats after its last
    // shot, whenever the wave steps down; the wave steps down a row only once
    // WAVE_ROOM_ROWS rows below it are empty, and does step down; the new
    // time standing.
    int8_t bottom = f.lowestRow();
    const uint32_t STEP = 2 * SHOT_MS_PX;
    for (uint8_t w = 0; w < 24; w++) {
      uint8_t was = (uint8_t)((w + 23) % 24);
      const uint8_t from[4] = { (uint8_t)(was / 10), (uint8_t)(was % 10), 5, 9 }, to[4] = { (uint8_t)(w / 10), (uint8_t)(w % 10), 0, 0 };
      f.show(from);
      uint32_t s = f.planChange(to, STYLE_MIXED, 0, true);
      f.start();
      f.at(WAKE_MS - 1);
      bool ok = f.drop == 0 && !f.shotOn;
      for (uint8_t i = 0; i < 4; i++) { ok = ok && f.awake[i] == cellsOf(from[i]); }
      int8_t deepest = 0, dropWas = 0;
      uint16_t awakeWas[4];
      memcpy(awakeWas, f.awake, sizeof awakeWas);
      bool shotWas = false;
      int16_t shotXWas = 0, shotYWas = 0;
      uint32_t fired = 0;
      uint16_t shots = 0;
      for (uint32_t ms = WAKE_MS; ms < s && ok; ms += STEP) {
        f.at(ms);
        ok = shotClear(f, ms) && arrivalSeen(f);
        for (uint8_t i = 0; i < 4; i++) {
          uint16_t hit = awakeWas[i] & (uint16_t)~f.awake[i];
          for (uint8_t k = 0; k < COLS * ROWS; k++) {
            if (!(hit & (1 << k))) { continue; }
            int16_t foot = f.cellY(i, k) + CELL * dropWas + CELL - 2;   // the alien's lowest pixel, a step ago
            ok = ok && shotWas && shotXWas == f.aimX(i, k) + 2 && shotYWas > foot &&
                 shotYWas <= foot + (int16_t)(STEP / SHOT_MS_PX) + CELL;
          }
        }
        if (f.shotOn) {    // when it left the muzzle, to within SHOT_MS_PX
          uint32_t fire = ms - (uint32_t)(f.L.cannonY - 2 - f.shotY) * SHOT_MS_PX;
          if (!shotWas || fire >= fired + WAVE_FIRE_MS / 2) {
            if (shots++) {
              uint32_t gap = fire - fired, beats = (gap + WAVE_FIRE_MS / 2) / WAVE_FIRE_MS;
              ok = ok && beats >= 1 && beats <= 2 && abs((int32_t)gap - (int32_t)(beats * WAVE_FIRE_MS)) <= SHOT_MS_PX;
            }
            fired = fire;
          }
        }
        int8_t low = -1;   // the lowest awake alien's row, where it stood
        for (uint8_t i = 0; i < 4; i++) {
          for (uint8_t k = 0; k < COLS * ROWS; k++) { if (f.awake[i] & (1 << k)) { low = max(low, f.panelRow(i, k)); } }
        }
        if (low >= 0 && f.drop > 0) { ok = ok && low + f.drop - 1 + WAVE_ROOM_ROWS <= bottom; }
        deepest = max(deepest, f.drop);
        memcpy(awakeWas, f.awake, sizeof awakeWas);
        dropWas = f.drop;
        shotWas = f.shotOn;
        shotXWas = f.shotX;
        shotYWas = f.shotY;
      }
      ok = ok && deepest > 0;
      f.at(s - 1);
      ok = ok && f.drop == 0;
      for (uint8_t i = 0; i < 4; i++) { ok = ok && f.awake[i] == cellsOf(to[i]); }
      f.at(s);
      for (uint8_t i = 0; i < 4; i++) { ok = ok && f.cells[i] == cellsOf(to[i]) && f.awake[i] == 0; }
      f.at(f.endMs);
      ok = ok && !f.running && f.cannonX == f.L.cannonHome && f.endMs < 60000;
      if (s > longest) { longest = s; }
      if (!ok) { failed = (uint16_t)(3000 + w + up); return false; }
    }
    // The saucer, from either side, to each gap it can be shot at.
    uint8_t now[4] = { 1, 2, 3, 4 };
    for (uint8_t side = 0; side < 2; side++) {
      for (uint8_t pick = 0; pick < 3; pick++) {
        f.show(now);
        uint32_t len = f.planSaucer(side == 0, pick);
        f.start();
        f.at(f.saucerHitAt - 1);     // the shot just below the saucer's middle (a pixel off: it is still flying)
        bool ok = len > 0 && f.shotOn && f.saucerOn && abs(f.shotX - (f.saucerX + SAUCER_W / 2)) <= 1 && f.shotY <= SAUCER_HIT_Y + 1;
        f.at(f.saucerHitAt);
        ok = ok && !f.saucerOn && f.burstOn && f.burstWide && !f.shotOn;
        f.at(f.endMs);
        for (uint8_t i = 0; i < 4; i++) { ok = ok && f.cells[i] == cellsOf(now[i]); }
        ok = ok && !f.running && f.cannonX == f.L.cannonHome;
        if (len > longestScene) { longestScene = len; }
        if (!ok) { failed = (uint16_t)(4000 + side * 10 + pick + up); return false; }
      }
    }
    // Every alien that can shoot back, of every digit.
    for (uint8_t i = 0; i < 4; i++) {
      for (uint8_t d = 0; d < 10; d++) {
        for (uint8_t c = 0; c < COLS; c++) {
          uint8_t these[4] = { 1, 2, 3, 4 };
          these[i] = d;
          f.show(these);
          uint32_t len = f.planBack(i, c);
          bool can = (f.L.bombers & (1 << i)) && (cellsOf(d) & ((1 << c) | (1 << (3 + c)) | (1 << (6 + c)) | (1 << (9 + c)) | (1 << (12 + c))));
          bool ok = (len > 0) == can;
          if (len) {
            f.start();
            f.at(f.bombAt);          // the bomb leaves the alien only once the cannon is under it (Peter, 2026-10-12)
            ok = ok && f.bombOn && f.cannonOn && f.bombX == f.cannonX + 2;
            f.at(f.hitAt - 1);       // the bomb just above the muzzle, the cannon under it
            ok = ok && f.bombOn && f.cannonOn && abs(f.bombX - (f.cannonX + 2)) <= 1 && f.bombY + 2 >= f.L.cannonY - 1;
            f.at(f.hitAt);
            ok = ok && f.wreckOn && !f.cannonOn && !f.bombOn;
            f.at(f.endMs - 1);
            ok = ok && f.cannonOn && f.cannonX == f.L.cannonHome && f.awake[i] != 0;
            f.at(f.endMs);
            ok = ok && !f.running && f.cells[i] == cellsOf(d) && f.awake[i] == 0;
            if (len > longestScene) { longestScene = len; }
          }
          if (!ok) { failed = (uint16_t)(5000 + i * 100 + d * 10 + c + up); return false; }
        }
      }
    }
    // Every block of every digit waking for a moment.
    for (uint8_t d = 0; d < 10; d++) {
      for (uint8_t k = 0; k < COLS * ROWS; k++) {
        uint8_t these[4] = { 1, 2, 3, d };
        f.show(these);
        uint32_t len = f.planNap(3, k);
        bool lit = cellsOf(d) & (1 << k);
        bool ok = (len > 0) == lit;
        if (len) {
          f.start();
          ok = ok && f.awake[3] == (1 << k) && swayFits(f, 3, 0, len);
          f.at(f.endMs);
          ok = ok && !f.running && f.cells[3] == cellsOf(d) && f.awake[3] == 0;
          if (len > longestScene) { longestScene = len; }
        }
        if (!ok) { failed = (uint16_t)(7000 + d * 15 + k + up); return false; }
      }
    }
  }
  return true;
}

}  // namespace aliens
