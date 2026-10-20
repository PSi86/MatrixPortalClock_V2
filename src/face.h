// What the watchfaces share (concept doc, tab "Watchface animations").
//
// The four faces - classic, Tetris, Muncher, Aliens - are being moved behind
// one interface, Watchface below, step by step, with the panel kept as it was
// at every step. The core hands a face the moment (FaceNow), how to colour
// (FacePaint) and the canvas; a face keeps everything about its own picture.
// Also here: the tools that prove the panel stayed as it was - random numbers
// a test build can repeat, and a checksum of a frame.
//
// It is built on every board, the M4 too, so it keeps to C++11.
#ifndef FACE_H
#define FACE_H

#include <Arduino.h>
#include <Adafruit_GFX.h>
#include "clock_time.h"

// Optional things - an animation asked for, an extra, a GIF, info cards - end
// this long before the time's next change starts (concept doc, tab "Panel
// priorities", decided 2026-10-12): the change is seen from its start.
const unsigned long FACE_GUARD_MS = 3000;

// Whether something of `len` ms, starting now, ends FACE_GUARD_MS before a
// change that starts at `startAt` (millis()). The one check every face and the
// core ask; unsigned arithmetic survives the millis() wrap.
inline bool faceFits(unsigned long startAt, unsigned long now, unsigned long len) {
  return (long)(startAt - now) >= (long)(len + FACE_GUARD_MS);
}

// The four digits HH MM of a moment, d[0] the tens of the hours; `none` in
// every place while the clock has no time, so a face shows dashes rather than
// a time counted up from midnight.
inline void faceDigits(time_t t, bool set, uint8_t d[4], uint8_t none) {
  if (!set) {
    for (uint8_t i = 0; i < 4; i++) { d[i] = none; }
    return;
  }
  uint8_t h = (uint8_t)clockHour(t), m = (uint8_t)clockMinute(t);
  d[0] = h / 10; d[1] = h % 10; d[2] = m / 10; d[3] = m % 10;
}

// ---- What the core hands a face ---------------------------------------------
// The moment a face is stepped or drawn at: the core's time as it stands then,
// and the panel as it is turned (faceMoment() in the sketch). The ticks are
// true on the one pass that began a new second, minute or hour.
struct FaceNow {
  unsigned long ms;         // millis() of the pass (a test build's own clock while it runs one)
  unsigned long secondAt;   // when the second on show began, in ms
  time_t local;             // the panel's local time
  bool set;                 // whether the clock has a time; without one the faces show dashes
  uint8_t hour, minute, second;
  bool secondTick, minuteTick, hourTick;
  uint16_t loopHz;          // how often the loop runs per second as the board paces it
  uint8_t turn;             // the panel's rotation, 0..3: 0 and 2 landscape
  int16_t w, h;             // its size as it is turned, 64x32 or 32x64
};

// Perceived brightness to linear light, as the eye sees it: the brightness
// fade and the classic face's trail.
const float FADE_GAMMA = 2.2f;

// How a face colours its picture: every colour scaled to the brightness the
// panel is drawn at. Linear scaling keeps the hue (channel ratios); rounding
// instead of truncating reduces drift when dimming into the low end of the
// panel's few levels per channel. The DMA boards' panel dims itself to that
// brightness, so a colour asked at it (or above) is drawn at full strength,
// and one asked dimmer keeps its share of it: the classic face's trail stays
// the same fraction of its digits.
struct FacePaint {
  uint8_t bright;   // the brightness the panel is drawn at, 0..255

  uint16_t color(uint8_t r, uint8_t g, uint8_t b) const { return color(r, g, b, bright); }
  uint16_t color(uint8_t r, uint8_t g, uint8_t b, uint8_t at) const {
#if PANEL_DRIVER_DMA
    if (at >= bright || bright == 0) { return rgb565(r, g, b); }
    uint16_t panel = bright;
    return rgb565(((uint16_t)r * at + panel / 2) / panel,
                  ((uint16_t)g * at + panel / 2) / panel,
                  ((uint16_t)b * at + panel / 2) / panel);
#else
    return rgb565(((uint16_t)r * at + 127) / 255,
                  ((uint16_t)g * at + 127) / 255,
                  ((uint16_t)b * at + 127) / 255);
#endif
  }
  // A colour that never dims below VISIBLE_MIN, so it cannot quantise to
  // black at a very low brightness: the hotspot's details, the Tetris colon.
  static const uint8_t VISIBLE_MIN = 24;
  uint16_t visible(uint8_t r, uint8_t g, uint8_t b) const {
    uint8_t at = bright;
    if (at < VISIBLE_MIN) { at = VISIBLE_MIN; }
    return color(r, g, b, at);
  }
  // As both panel drivers pack a colour.
  static uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b) {
    return ((uint16_t)(r & 0xF8) << 8) | ((uint16_t)(g & 0xFC) << 3) | (b >> 3);
  }
};

// Why a face comes onto the panel (Watchface::enter()).
enum FaceEnter : uint8_t {
  ENTER_CHOSEN,   // chosen as the clock's face: everything afresh
  ENTER_SHOWN,    // uncovered again by a screen on top: it was not stepped meanwhile
  ENTER_TURNED,   // the panel turned
};

// Who asks a face for an animation (Watchface::request()).
enum FaceTrigger : uint8_t {
  TRIGGER_ACTION,         // the input action "trigger watchface animation": a knock, a button, a gesture
  TRIGGER_NOW_AND_THEN,   // the shared beat "How often"
  TRIGGER_PREVIEW,        // a setting changed in the web app
};
enum FaceOutcome : uint8_t {
  OUTCOME_NONE,       // the face offers nothing for this trigger
  OUTCOME_PLAYED,     // it plays, or will at the moment it plays such things
  OUTCOME_REFUSED,    // not now: something runs, or it would run into the time's change
};

// What every watchface does for the core. The core keeps the time, the
// screens and the rules for who takes the panel; it steps the face on every
// pass and has it drawn when its picture changed, then puts its marks over it
// and shows it (faceShow() in the sketch).
class Watchface {
 public:
  // It comes onto the panel (why): it shows the time as it stands, with what
  // it keeps of before as it decides.
  virtual void enter(const FaceNow &now, FaceEnter why) = 0;
  // One pass: it steps what moves. Whether its picture changed.
  virtual bool update(const FaceNow &now) = 0;
  // Its picture, the whole canvas, without the core's marks.
  virtual void draw(Adafruit_GFX &g, const FaceNow &now, const FacePaint &paint) = 0;
  // Whether the time's change is under way: the picture moves for it.
  virtual bool busy(const FaceNow &now) const = 0;
  // An animation asked for.
  virtual FaceOutcome request(const FaceNow &now, FaceTrigger why) = 0;

 protected:
  ~Watchface() {}   // the faces live for good: never deleted through this, so no virtual destructor
};

// ---- A face's settings --------------------------------------------------------
// Each face keeps its settings in a blob of its own - NVS key, magic,
// revision, then its fields - so no other blob ever changes for it. A table of
// its settings - the web app's key, where it sits in the blob, what it may be -
// lets one piece of code load, check, take from the web app and report them
// all, where each face had its own copy of each. The keys, the blobs and the
// values are the ones stored all along.
enum FaceParamKind : uint8_t {
  PARAM_RANGE,    // a number from lo to hi; a stored one outside is moved into it
  PARAM_CHOICE,   // one of lo..hi, by value; a stored one outside is the default
  PARAM_FLAG,     // 0 or 1; the web app's anything-but-0 is 1
};
const uint8_t PARAM_REDRAW = 0x01;   // a new value: the face redraws at once
const uint8_t PARAM_PLAYS  = 0x02;   // a new value tried live: the face plays what it picks
const uint8_t PARAM_KNOCK  = 0x04;   // the knock's sensitivity: to the sensor whenever it comes
struct FaceParam {
  const char *key;            // in the web app and in /api/state
  uint8_t offset, size;       // where in the blob: 1 or 2 bytes, little-endian
  FaceParamKind kind;
  uint8_t flags;              // PARAM_REDRAW, PARAM_PLAYS, PARAM_KNOCK
  uint16_t lo, hi;
  const char *const *labels;  // a choice's and a flag's names, by value; nullptr for a range
};
struct FaceBlob {
  const char *key;            // the NVS key
  uint16_t magic;
  uint8_t rev;
  uint8_t face;               // the watchface these are the settings of
  uint8_t *data;              // the face's settings struct: magic (2 bytes), rev, its fields
  uint8_t size;
  const uint8_t *defaults;    // the same struct with the defaults; a choice out of range takes its value
  const FaceParam *params;
  uint8_t paramCount;
  void (*migrate)(uint8_t *data, uint8_t storedRev);   // an older blob: fill in what came since
};

inline uint16_t faceParamGet(const uint8_t *data, const FaceParam &p) {
  return p.size == 2 ? (uint16_t)(data[p.offset] | data[p.offset + 1] << 8) : data[p.offset];
}
inline void faceParamSet(uint8_t *data, const FaceParam &p, uint16_t v) {
  data[p.offset] = (uint8_t)v;
  if (p.size == 2) { data[p.offset + 1] = (uint8_t)(v >> 8); }
}

// Read a face's blob through `store` (the board's SettingsBlobStore). A blob
// that is not this face's (no magic: none stored yet) gives the defaults,
// written with the next save; an older revision is migrated in RAM, the same.
// Then every value is checked, as each face did by hand.
template <typename Store>
void faceBlobLoad(FaceBlob &b, Store &store) {
  store.begin(b.key);
  store.read(b.data, b.size);
  if ((uint16_t)(b.data[0] | b.data[1] << 8) != b.magic) {
    memcpy(b.data, b.defaults, b.size);
  } else if (b.data[2] < b.rev && b.migrate) {
    b.migrate(b.data, b.data[2]);
  }
  for (uint8_t i = 0; i < b.paramCount; i++) {
    const FaceParam &p = b.params[i];
    uint16_t v = faceParamGet(b.data, p);
    if (p.kind == PARAM_RANGE)       { v = v < p.lo ? p.lo : v > p.hi ? p.hi : v; }
    else if (p.kind == PARAM_CHOICE) { if (v < p.lo || v > p.hi) { v = faceParamGet(b.defaults, p); } }
    else if (v > 1)                  { v = 1; }
    faceParamSet(b.data, p, v);
  }
}

template <typename Store>
void faceBlobSave(FaceBlob &b, Store &store) {
  b.data[0] = (uint8_t)b.magic;
  b.data[1] = (uint8_t)(b.magic >> 8);
  b.data[2] = b.rev;
  store.write(b.data, b.size);
}

// The web app's values for a face's settings, live or to be saved: each key
// that `get(key)` finds is taken, kept within its range. Returns what came:
// PARAM_REDRAW or PARAM_PLAYS for such a setting whose value changed,
// PARAM_KNOCK whenever the sensitivity was given.
template <typename Get>
uint8_t faceBlobApply(FaceBlob &b, Get get) {
  uint8_t came = 0;
  for (uint8_t i = 0; i < b.paramCount; i++) {
    const FaceParam &p = b.params[i];
    String v = get(p.key);
    if (!v.length()) { continue; }
    long x = v.toInt();
    uint16_t now = p.kind == PARAM_FLAG ? (x ? 1 : 0) : (uint16_t)constrain(x, (long)p.lo, (long)p.hi);
    if (now != faceParamGet(b.data, p)) { came |= p.flags & (PARAM_REDRAW | PARAM_PLAYS); }
    came |= p.flags & PARAM_KNOCK;
    faceParamSet(b.data, p, now);
  }
  return came;
}

// The settings into /api/state's settings object `o`, by key.
template <typename Obj>
void faceBlobJson(const FaceBlob &b, Obj o) {
  for (uint8_t i = 0; i < b.paramCount; i++) { o[b.params[i].key] = faceParamGet(b.data, b.params[i]); }
}

// The names of a face's choices, for /api/state's lists: `add(key, labels,
// count)` once per setting that has names.
template <typename Add>
void faceBlobLists(const FaceBlob &b, Add add) {
  for (uint8_t i = 0; i < b.paramCount; i++) {
    const FaceParam &p = b.params[i];
    if (p.labels) { add(p.key, p.labels, (uint8_t)(p.hi - p.lo + 1)); }
  }
}

// Random numbers for the faces. A release draws them from random(), which on
// the S3 reads the hardware generator (no randomSeed() anywhere: see
// tetrisStartDigit()). A test build (FACE_FRAME_LOG) draws them from a
// generator of its own, set anew from the minute at every minute's start, so
// two runs over the same minutes draw the same numbers and show the same
// frames - also when the code that draws them has moved.
#if defined(FACE_FRAME_LOG)
struct FaceRandom {
  static uint32_t state;
  static void seed(uint32_t s) { state = s ? s : 0x9E3779B9u; }
  static uint32_t next() {   // xorshift32
    uint32_t x = state;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    return state = x;
  }
};
uint32_t FaceRandom::state = 0x9E3779B9u;
inline long faceRandom(long howbig) { return howbig <= 0 ? 0 : (long)(FaceRandom::next() % (uint32_t)howbig); }
inline long faceRandom(long howsmall, long howbig) {
  return howsmall >= howbig ? howsmall : howsmall + faceRandom(howbig - howsmall);
}
#else
inline long faceRandom(long howbig) { return random(howbig); }
inline long faceRandom(long howsmall, long howbig) { return random(howsmall, howbig); }
#endif

// CRC-32 (the zip polynomial) with a table of 16, a nibble at a time: small
// enough for the M4, and about 0.15 ms for a 64x32 frame on the S3.
inline uint32_t faceCrc32(const uint8_t *p, size_t n) {
  static const uint32_t T[16] = {
    0x00000000, 0x1DB71064, 0x3B6E20C8, 0x26D930AC, 0x76DC4190, 0x6B6B51F4, 0x4DB26158, 0x5005713C,
    0xEDB88320, 0xF00F9344, 0xD6D6A3E8, 0xCB61B38C, 0x9B64C2B0, 0x86D3D2D4, 0xA00AE278, 0xBDBDF21C,
  };
  uint32_t c = 0xFFFFFFFFu;
  while (n--) {
    c ^= *p++;
    c = (c >> 4) ^ T[c & 15];
    c = (c >> 4) ^ T[c & 15];
  }
  return ~c;
}

#endif  // FACE_H
