// What the watchfaces share (concept doc, tab "Watchface animations").
//
// The four faces - classic, Tetris, Muncher, Aliens - sit behind one
// interface, Watchface below, each in a header of its own (face_classic.h,
// face_tetris.h, face_muncher.h, face_aliens.h; face_planned.h for what the
// Muncher and the Aliens share). The core hands a face the moment (FaceNow),
// how to colour (FacePaint) and the canvas; a face keeps everything about its
// own picture and decides itself what it plays when. Also here: the tools
// that proved the panel stayed as it was while the faces moved in here
// (2026-10-20) - random numbers a test build can repeat, and a checksum of a
// frame.
#ifndef FACE_H
#define FACE_H

#include <Arduino.h>
#include <Adafruit_GFX.h>
#include "clock_time.h"
#include "console.h"          // the faces log their changes and scenes
#include "settings_table.h"

// What the clock plans itself - an extra, a GIF of its own accord, info
// cards - ends this long before the time's next change starts (concept doc,
// tab "Panel priorities", decided 2026-10-12): the change is seen from its
// start. What the user asks for is granted unless there is a clear reason
// against it (Peter, 2026-10-22): a GIF may run into the change, the Tetris
// and classic animations may meet it; only the Muncher's and the Aliens'
// scenes must have ended when their change has to start, or one of the two
// would be cut off.
const unsigned long FACE_GUARD_MS = 3000;

// After an animation of a face has ended, the next one asked for waits this
// long (Peter, 2026-10-20: one rest for every face; it was 0.3 s on the
// classic face, 1.2 s on the Tetris one and 1 s on the Muncher and the
// Aliens). A knock reports itself more than once - the sensor latches 2 to 7
// events per knock - and without a rest one knock could play two.
const unsigned long FACE_REST_MS = 1000;

// Whether something of `len` ms, starting now, ends `guard` ms before a
// change that starts at `startAt` (millis()): FACE_GUARD_MS for what the
// clock plans itself, 0 for a scene the user asked for. The one check every
// face and the core ask; unsigned arithmetic survives the millis() wrap.
inline bool faceFits(unsigned long startAt, unsigned long now, unsigned long len,
                     unsigned long guard = FACE_GUARD_MS) {
  return (long)(startAt - now) >= (long)(len + guard);
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
    if (at >= bright || bright == 0) { return rgb565(r, g, b); }
    uint16_t panel = bright;
    return rgb565(((uint16_t)r * at + panel / 2) / panel,
                  ((uint16_t)g * at + panel / 2) / panel,
                  ((uint16_t)b * at + panel / 2) / panel);
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

// A face's change of the time to a minute, about: how long before the minute
// begins it starts, and how long after it goes on (Watchface::changeSpan()).
struct FaceSpan {
  unsigned long lead, tail;   // ms
};

// Why a face comes onto the panel (Watchface::enter()).
enum FaceEnter : uint8_t {
  ENTER_CHOSEN,   // chosen as the clock's face: everything afresh
  ENTER_SHOWN,    // uncovered again by a screen on top: it was not stepped meanwhile
  ENTER_TURNED,   // the panel turned
};

// Who asks a face for an animation (Watchface::request()).
enum FaceTrigger : uint8_t {
  TRIGGER_ACTION,              // the input action "trigger watchface animation": a knock, a button, a gesture
  TRIGGER_NOW_AND_THEN,        // the shared beat "How often"
  TRIGGER_PREVIEW_CHANGE,      // a setting of the change was tried in the web app: show a change
  TRIGGER_PREVIEW_ANIMATION,   // a new choice of the watchface animation: play what it picks
};
// Now or not at all: nothing asked for is kept for later (Peter, 2026-10-12
// for the inputs, 2026-10-20 for the web app's previews).
enum FaceOutcome : uint8_t {
  OUTCOME_NONE,       // the face offers nothing for this trigger
  OUTCOME_PLAYED,     // it plays now (the classic face's from the next second)
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
  // Whether the time's change is under way, or another animation of the
  // face - one the user asked for, or an extra: what the clock plans itself
  // waits for all of them (Peter, 2026-10-22). Never what moves all the
  // time, as the classic face's seconds.
  virtual bool busy(const FaceNow &now) const = 0;
  // The span of its change to the minute that begins at `minute` (local
  // time): as it has planned it, or as it would plan it - the hour's longer
  // ones included. The rules for the panel keep it free.
  virtual FaceSpan changeSpan(const FaceNow &now, time_t minute) const = 0;
  // Whether it has anything for a trigger: an extra now and then, a preview.
  virtual bool offers(FaceTrigger why) const = 0;
  // An animation asked for.
  virtual FaceOutcome request(const FaceNow &now, FaceTrigger why) = 0;

 protected:
  ~Watchface() {}   // the faces live for good: never deleted through this, so no virtual destructor
};

// ---- A face's settings --------------------------------------------------------
// Each face keeps its settings in a blob of its own - NVS key, magic,
// revision, then its fields - so no other blob ever changes for it. Their
// table (settings_table.h) lets one piece of code load, check, take from the
// web app and report them all, where each face had its own copy of each. The
// keys, the blobs and the values are the ones stored all along; every face
// setting is tried live, a range is moved into its range when loaded, a
// choice out of its range takes its default, a flag is 0 or 1.
struct FaceBlob {
  const char *key;            // the NVS key
  uint16_t magic;
  uint8_t rev;
  uint8_t face;               // the watchface these are the settings of
  uint8_t size;
  SettingsTable table;        // the face's settings struct (magic: 2 bytes, rev, its fields), its defaults and its table
  void (*migrate)(uint8_t *data, uint8_t storedRev);   // an older blob: fill in what came since
};

// Read a face's blob through `store` (the board's SettingsBlobStore). A blob
// that is not this face's (no magic: none stored yet) gives the defaults,
// written with the next save; an older revision is migrated in RAM, the same.
// Then every value is checked.
template <typename Store>
void faceBlobLoad(FaceBlob &b, Store &store) {
  uint8_t *data = b.table.data;
  store.begin(b.key);
  store.read(data, b.size);
  if ((uint16_t)(data[0] | data[1] << 8) != b.magic) {
    memcpy(data, b.table.defaults, b.size);
  } else if (data[2] < b.rev && b.migrate) {
    b.migrate(data, data[2]);
  }
  settingsCheck(b.table);
}

// Whether it is stored.
template <typename Store>
bool faceBlobSave(FaceBlob &b, Store &store) {
  b.table.data[0] = (uint8_t)b.magic;
  b.table.data[1] = (uint8_t)(b.magic >> 8);
  b.table.data[2] = b.rev;
  return store.write(b.table.data, b.size);
}

// Random numbers for the faces. A release draws them from random(), which on
// the S3 reads the hardware generator (no randomSeed() anywhere: see
// tetrisStartDigit()). A test build (FACE_FRAME_LOG) draws them from a
// generator of its own, set anew from the minute at every minute's start, so
// two runs over the same minutes draw the same numbers and show the same
// frames - also when the code that draws them has moved.
#if defined(FACE_FRAME_LOG)
struct FaceRandom {
  static inline uint32_t state = 0x9E3779B9u;   // one for every file that includes this
  static void seed(uint32_t s) { state = s ? s : 0x9E3779B9u; }
  static uint32_t next() {   // xorshift32
    uint32_t x = state;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    return state = x;
  }
};
inline long faceRandom(long howbig) { return howbig <= 0 ? 0 : (long)(FaceRandom::next() % (uint32_t)howbig); }
inline long faceRandom(long howsmall, long howbig) {
  return howsmall >= howbig ? howsmall : howsmall + faceRandom(howbig - howsmall);
}
#else
inline long faceRandom(long howbig) { return random(howbig); }
inline long faceRandom(long howsmall, long howbig) { return random(howsmall, howbig); }
#endif

// CRC-32 (the zip polynomial) with a table of 16, a nibble at a time: about
// 0.15 ms for a 64x32 frame.
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
