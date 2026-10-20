// The classic watchface: HH MM SS in large font digits. Each digit that
// changes flies in from its side during the second before, while the one it
// replaces flies out the other way, and lands as the second begins; asked for
// an animation, all six fly in at the next second.
//
// It is built on every board, the M4 too, so it keeps to C++11.
#ifndef FACE_CLASSIC_H
#define FACE_CLASSIC_H

#include <Adafruit_GFX.h>
#include <Fonts/FreeSansBold12pt7b.h>   // the hours and the minutes
#include <Fonts/FreeSansBold9pt7b.h>    // the seconds
#include "face.h"

// The classic face's settings. They sit inside the clock's Settings, in this
// order, where the stored blobs have them.
struct ClassicSettings {
  uint8_t flyInTime;                // how long every fly-in takes, in 10 ms (Settings rev 1: ms per pixel)
  uint8_t digitR, digitG, digitB;   // the time's digits
  uint8_t trailR, trailG, trailB;   // a digit while it flies in
  uint8_t dir[6];                   // the side each digit comes from: 0 top, 1 right, 2 bottom, 3 left
};

// The fly-in time's range: at most 850 ms, so every digit has landed before
// the next second's flights start.
const uint16_t FLY_IN_MIN_MS = 100, FLY_IN_MAX_MS = 850;

// The fly-in time in the 10 ms ClassicSettings::flyInTime keeps, from ms, within its range.
inline uint8_t flyInUnits(long ms) {
  return (uint8_t)constrain((ms + 5) / 10, (long)(FLY_IN_MIN_MS / 10), (long)(FLY_IN_MAX_MS / 10));
}

// Where each digit stands, by the cursor GFX prints from. Portrait = 32 wide
// x 64 tall, HH / MM / SS stacked; landscape = 64 wide x 32 tall, HH MM large
// side by side, SS small centred below.
const int8_t CLASSIC_PORT_X[6] = {0, 13, 6, 19, 6, 17};
const int8_t CLASSIC_PORT_Y[6] = {16, 16, 35, 35, 50, 50};
const int8_t CLASSIC_LAND_X[6] = {2, 15, 36, 49, 22, 32};
const int8_t CLASSIC_LAND_Y[6] = {17, 17, 17, 17, 30, 30};   // HH MM on baseline 17, SS on 30

// The trail is half the digits' perceived brightness: the eye is roughly
// gamma 2.2, so a linear half would hardly look dimmer (50 % linear looks
// like 73 %). It never falls below a floor that keeps the LEDs on, and is
// never brighter than the digits.
const uint8_t TRAIL_BRIGHTNESS_PCT = 50;
const uint8_t TRAIL_BRIGHTNESS_MIN = 24;

class ClassicFace : public Watchface {
 public:
  explicit ClassicFace(const ClassicSettings &s) : _s(s) {}

  void enter(const FaceNow &now, FaceEnter why, int16_t w, int16_t h) override;
  bool update(const FaceNow &now) override;
  void draw(Adafruit_GFX &g, const FaceNow &now, const FacePaint &paint) override;
  bool busy(const FaceNow &now) const override;
  FaceOutcome request(const FaceNow &now, FaceTrigger why) override;

#if defined(FACE_FRAME_LOG)
  // Test builds: the flights' pace as the passes per second, not the one
  // counted before (a test scene's clock makes every pass TEST_STEP_MS).
  void pace(uint16_t passesPerSecond) { _passesPerSec = passesPerSecond; }
#endif

 private:
  // The animation on request: asked for (all six fly in at the next second),
  // flying, or neither. A new one is refused until the last one has landed
  // and REST_MS have passed. An input can come again before the first has
  // played - a knock reports every event the sensor latches, seen 2 to 7 per
  // knock - and one that came after the second had taken the request asked
  // for a second animation.
  enum Anim : uint8_t { ANIM_IDLE, ANIM_ASKED, ANIM_FLYING };
  static const unsigned long REST_MS = 300;

  void digits(const FaceNow &now);
  uint16_t flightPasses(const FaceNow &now) const;
  int16_t flightNeed(uint8_t i, uint8_t dir) const;
  void startFlight(uint8_t i);
  void placeFlight(uint8_t i, uint16_t passes);
  uint8_t trailBrightness(uint8_t bright) const;

  const ClassicSettings &_s;
  char _timeStr[7] = {}, _animStr[7] = {};   // HHMMSS now and one second ahead (what flies in)
  // Where each digit stands now (the one shown) and where its flight is (the
  // one flying in), both landing on its place.
  int8_t _timeX[6] = {0, 13, 6, 19, 6, 17}, _timeY[6] = {16, 16, 35, 35, 50, 50};
  int8_t _animX[6] = {}, _animY[6] = {};
  int8_t _placeX[6] = {0, 13, 6, 19, 6, 17}, _placeY[6] = {16, 16, 35, 35, 50, 50};
  int16_t _w = 32, _h = 64;   // the panel as it is turned
  // Each digit's flight, set when it starts: the side it comes from
  // (ClassicSettings::dir) and how many pixels it flies.
  bool _flying[6] = {};
  uint8_t _flightDir[6] = {};
  uint8_t _flightDist[6] = {1, 1, 1, 1, 1, 1};
  // The passes every flight takes, set at each second's tick: all flights
  // take the same time, so whatever flies at a tick lands together.
  uint16_t _flightTime = 1;
  // The passes the face had in its last whole second, counted between two
  // ticks (0 = none counted yet). Web requests are served in the same loop
  // and take passes away - measured: with /api/panel asked every 60 ms a
  // flight paced by the panel's refresh rate ran 30 % slow and missed its
  // landing. Pacing by this count keeps the fly-in time real.
  uint16_t _passesPerSec = 0;
  uint16_t _passesSinceTick = 0;   // since the last tick, where flights start
  unsigned long _tickMs = 0;       // when the last tick came; 0 = none since the face came up
  Anim _anim = ANIM_IDLE;
  unsigned long _askedIn = 0;      // the second the animation was asked in (its secondAt)
  unsigned long _animEndedAt = 0;  // when the last one landed or was cut off
  uint16_t _ignored = 0;           // requests refused since the last one taken, for the console
};

// Up to the time, nothing of before resumes: shown again, it also gets fresh
// digit strings, which update() would only make at the next tick, and cuts
// off an animation another screen or face covered. Shown or turned, every
// digit stands on its place for the panel as it is turned now.
inline void ClassicFace::enter(const FaceNow &now, FaceEnter why, int16_t w, int16_t h) {
  if (why == ENTER_SHOWN) {
    _tickMs = 0;   // it was not stepped meanwhile: the next tick starts a new count
    if (_anim != ANIM_IDLE) {
      _anim = ANIM_IDLE;
      _animEndedAt = now.ms;
    }
    digits(now);
  }
  _w = w;
  _h = h;
  bool land = w > h;
  for (uint8_t i = 0; i < 6; i++) {
    _placeX[i] = land ? CLASSIC_LAND_X[i] : CLASSIC_PORT_X[i];
    _placeY[i] = land ? CLASSIC_LAND_Y[i] : CLASSIC_PORT_Y[i];
    _timeX[i] = _animX[i] = _placeX[i];
    _timeY[i] = _animY[i] = _placeY[i];
    _flying[i] = false;
  }
}

// The six characters for now and for one second ahead. Dashes while the
// clock has no time: it shows that it has none rather than counting up from
// midnight.
inline void ClassicFace::digits(const FaceNow &now) {
  if (!now.set) {
    strcpy(_timeStr, "------");
    strcpy(_animStr, "------");
    return;
  }
  sprintf(_timeStr, "%02d%02d%02d", clockHour(now.local),     clockMinute(now.local),     clockSecond(now.local));
  sprintf(_animStr, "%02d%02d%02d", clockHour(now.local + 1), clockMinute(now.local + 1), clockSecond(now.local + 1));
}

// One pass. On a second's tick the digits that flew in stand on their
// places - also one a stalled loop kept from landing, which would otherwise
// stay off its place from then on - the strings are made anew, and every
// digit that changes at the next second starts its flight; a digit under
// way is placed where its flight is. The flights are placed by the passes,
// so the face is drawn on every pass.
inline bool ClassicFace::update(const FaceNow &now) {
  bool starts[6] = {};
  if (now.secondTick) {
    // The passes of the second that just ended (the one the last tick
    // started, counted with it) pace the flights of the next.
    uint32_t span = now.ms - _tickMs;
    if (_tickMs != 0 && span >= 900 && span <= 1100) {
      _passesPerSec = (uint16_t)min(((uint32_t)_passesSinceTick + 1) * 1000UL / span, (uint32_t)0xFFFF);
    }
    _tickMs = now.ms;
    _passesSinceTick = 0;
    _flightTime = flightPasses(now);
    // Which digits change at the next second: the seconds' ones always, each
    // other when the one to its right rolls over.
    starts[5] = true;
    starts[4] = (now.second + 1) % 10 == 0;
    starts[3] = starts[4] && now.second + 1 == 60;
    starts[2] = starts[3] && (now.minute + 1) % 10 == 0;
    starts[1] = starts[2] && now.minute + 1 == 60;
    starts[0] = starts[1] && ((now.hour + 1) % 10 == 0 || now.hour + 1 == 24);
    // The animation: every digit flies in as if it changed, through the same
    // steps as a real change - from the first tick after the second it was
    // asked in.
    if (_anim == ANIM_ASKED && now.secondAt != _askedIn) {
      _anim = ANIM_FLYING;
      for (uint8_t i = 0; i < 6; i++) { starts[i] = true; }
    }
    digits(now);
  }
  else if (_passesSinceTick < 0xFFFF) { _passesSinceTick++; }

  for (uint8_t i = 0; i < 6; i++) {
    if (now.secondTick && _flying[i]) {
      _timeX[i] = _placeX[i];
      _timeY[i] = _placeY[i];
      _flying[i] = false;
    }
    if (starts[i] && now.set) { startFlight(i); }   // dashes do not fly in
    if (_flying[i]) { placeFlight(i, _passesSinceTick); }
  }
  // The animation has landed once its flights have: every flight takes
  // _flightTime passes from the tick it started on.
  if (_anim == ANIM_FLYING && _passesSinceTick >= _flightTime) {
    _anim = ANIM_IDLE;
    _animEndedAt = now.ms;
  }
  return true;
}

// The passes every flight takes: the fly-in time at the rate the face's loop
// really has. All flights take this time, each covering its own way in it,
// so whatever flies at a tick lands together, and every second looks the same
// whether one digit flies or six. (Until 2026-10-08 the setting was ms per
// pixel, rounded to whole passes per pixel: with flights up to 62 px and
// every flight held under a second, it had three steps - 4..7, 8..12 and
// 13..60 ms looked alike within each.)
inline uint16_t ClassicFace::flightPasses(const FaceNow &now) const {
  uint32_t ms = (uint32_t)_s.flyInTime * 10;
  uint32_t hz = _passesPerSec ? _passesPerSec : now.loopHz;
  uint32_t passes = (ms * hz + 500) / 1000;
  return (uint16_t)max(passes, (uint32_t)1);
}

// How far digit i has to fly when it comes from side dir: so far that the
// incoming digit starts wholly off the panel and the outgoing one, which
// moves out the other side by the same distance, ends wholly off it.
// Measured with the box all ten digits of its font cover, and the panel's
// size as it is turned now.
inline int16_t ClassicFace::flightNeed(uint8_t i, uint8_t dir) const {
  const GFXfont *f = (i > 3) ? &FreeSansBold9pt7b : &FreeSansBold12pt7b;   // as draw() prints them
  bool across = (dir == 1 || dir == 3);
  int16_t lo = 127, hi = -128;   // the digits' box along the flight, from the cursor (hi exclusive)
  for (char c = '0'; c <= '9'; c++) {
    const GFXglyph &g = f->glyph[c - f->first];
    int16_t a = across ? g.xOffset : g.yOffset;
    int16_t b = a + (across ? g.width : g.height);
    if (a < lo) { lo = a; }
    if (b > hi) { hi = b; }
  }
  int16_t pos  = across ? _placeX[i] : _placeY[i];
  int16_t size = across ? _w : _h;
  return max((int16_t)(size - pos - lo), (int16_t)(pos + hi));
}

// Digit i starts its flight from the side its setting names, in the time all
// flights take. Every digit that comes from the same side flies the same
// distance, the longest any of them needs, so they move at one speed as one
// block and keep their spacing - a pair set to one side flies in as it
// stands, and with every digit set to one side the whole face slides. (Each
// flying only as far as it needs, the hours' and the minutes' two digits drew
// apart or together on the way.)
inline void ClassicFace::startFlight(uint8_t i) {
  _flightDir[i] = _s.dir[i] & 3;
  int16_t d = 1;
  for (uint8_t j = 0; j < 6; j++) {
    if ((_s.dir[j] & 3) == _flightDir[i]) { d = max(d, flightNeed(j, _flightDir[i])); }
  }
  bool across = (_flightDir[i] == 1 || _flightDir[i] == 3);
  int16_t pos = across ? _placeX[i] : _placeY[i];
  if (d > 127 - pos) { d = 127 - pos; }   // the positions are int8_t
  _flightDist[i] = (uint8_t)max(d, (int16_t)1);
  _flying[i] = true;
}

// Digit i where its flight is after `passes`: the incoming digit the rest of
// the way out from its place, the outgoing one as far past it on the other
// side as the flight has come. Every flight covers its way in _flightTime
// passes, its pixels spread evenly over them.
inline void ClassicFace::placeFlight(uint8_t i, uint16_t passes) {
  static const int8_t SIDE_X[4] = { 0, 1, 0, -1 };   // where each dir comes from
  static const int8_t SIDE_Y[4] = { -1, 0, 1, 0 };
  uint32_t moved = (uint32_t)_flightDist[i] * min(passes, _flightTime) / _flightTime;
  int16_t rest = _flightDist[i] - (int16_t)moved;
  uint8_t d = _flightDir[i];
  _animX[i] = _placeX[i] + SIDE_X[d] * rest;
  _animY[i] = _placeY[i] + SIDE_Y[d] * rest;
  _timeX[i] = _placeX[i] - SIDE_X[d] * (int16_t)moved;
  _timeY[i] = _placeY[i] - SIDE_Y[d] * (int16_t)moved;
}

// The trail's brightness for the digits' `bright` (TRAIL_BRIGHTNESS_PCT).
inline uint8_t ClassicFace::trailBrightness(uint8_t bright) const {
  float frac = TRAIL_BRIGHTNESS_PCT / 100.0f;                    // the perceived fraction
  uint16_t t = (uint16_t)lroundf(powf(frac, FADE_GAMMA) * bright);   // in linear light
  if (t < TRAIL_BRIGHTNESS_MIN) { t = TRAIL_BRIGHTNESS_MIN; }
  if (t > bright)               { t = bright; }   // very low brightness: as the digits
  return (uint8_t)t;
}

// The six digits that stand, and each one flying in, in the trail's colour
// until it has arrived.
inline void ClassicFace::draw(Adafruit_GFX &g, const FaceNow &, const FacePaint &paint) {
  uint16_t digit = paint.color(_s.digitR, _s.digitG, _s.digitB);
  uint16_t trail = paint.color(_s.trailR, _s.trailG, _s.trailB, trailBrightness(paint.bright));
  g.fillScreen(0);
  for (uint8_t i = 0; i < 6; i++) {
    g.setFont(i > 3 ? &FreeSansBold9pt7b : &FreeSansBold12pt7b);   // the seconds smaller
    if (_flying[i]) {
      bool arrived = _animX[i] == _placeX[i] && _animY[i] == _placeY[i];
      g.setTextColor(arrived ? digit : trail);
      g.setCursor(_animX[i], _animY[i]);
      g.print(_animStr[i]);
    }
    g.setTextColor(digit);
    g.setCursor(_timeX[i], _timeY[i]);
    g.print(_timeStr[i]);
  }
}

// Whether the new minute is still flying in: the first fly-in time of its
// first second.
inline bool ClassicFace::busy(const FaceNow &now) const {
  return now.second == 0 && now.ms - now.secondAt < (unsigned long)_s.flyInTime * 10UL;
}

// The input's animation: all six digits fly in at the next second, unless
// one is still asked for or flying, or landed less than REST_MS ago. The
// console gets one line per animation taken, with the requests refused since
// the one before, not one per request (a stress test of fast knocks wrote
// over 200 lines in 30 s).
inline FaceOutcome ClassicFace::request(const FaceNow &now, FaceTrigger why) {
  if (why != TRIGGER_ACTION) { return OUTCOME_NONE; }
  if (_anim != ANIM_IDLE || now.ms - _animEndedAt < REST_MS) {
    if (_ignored < 0xFFFF) { _ignored++; }
    return OUTCOME_REFUSED;
  }
  _anim = ANIM_ASKED;
  _askedIn = now.secondAt;
  Serial.print("Face animation: the classic digits fly in at the next second (");
  Serial.print(_ignored); Serial.println(" requests ignored since the last one)");
  _ignored = 0;
  return OUTCOME_PLAYED;
}

#endif  // FACE_CLASSIC_H
