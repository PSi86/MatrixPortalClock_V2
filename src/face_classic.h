// The classic watchface: HH MM SS in large font digits. Each digit that
// changes flies in from its side during the second before, while the one it
// replaces flies out the other way, and lands as the second begins; asked for
// an animation, all six fly in at the next second.
#ifndef FACE_CLASSIC_H
#define FACE_CLASSIC_H

#include <Adafruit_GFX.h>
#include <Fonts/FreeSansBold12pt7b.h>   // the hours and the minutes
#include <Fonts/FreeSansBold9pt7b.h>    // the seconds
#include "face.h"
#include "classic_settings.h"   // its settings, the fly-in time's range

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

  // Up to the time, nothing of before resumes: chosen or shown again, it also
  // gets fresh digit strings, which update() would only make at the next tick,
  // and cuts off an animation another screen or face covered. Every time, every
  // digit stands on its place for the panel as it is turned now.
  void enter(const FaceNow &now, FaceEnter why) override {
    if (why != ENTER_TURNED) {
      if (_anim != ANIM_IDLE) {
        _anim = ANIM_IDLE;
        _animEndedAt = now.ms;
      }
      digits(now);
    }
    _w = now.w;
    _h = now.h;
    bool land = now.w > now.h;
    for (uint8_t i = 0; i < 6; i++) {
      _placeX[i] = land ? CLASSIC_LAND_X[i] : CLASSIC_PORT_X[i];
      _placeY[i] = land ? CLASSIC_LAND_Y[i] : CLASSIC_PORT_Y[i];
      _timeX[i] = _animX[i] = _placeX[i];
      _timeY[i] = _animY[i] = _placeY[i];
      _flying[i] = false;
    }
    _changeFlying = false;   // every digit stands: no change in the air
  }

  // One pass. On a second's tick the digits that flew in stand on their
  // places - also one a stalled loop kept from landing, which would otherwise
  // stay off its place from then on - the strings are made anew, and every
  // digit that changes at the next second starts its flight; a digit under
  // way is placed where its flight is by the time since the second began.
  // Whether the picture changed: a tick, or a digit that moved. (K8 of the face
  // refactor: until 2026-10-20 the flights were placed by the passes counted
  // since the tick, paced by the passes of the second before, and the face was
  // drawn on every pass - about 200 times a second.)
  bool update(const FaceNow &now) override {
    bool starts[6] = {};
    bool changed = false;
    if (now.secondTick) {
      changed = true;
      _flightMs = (unsigned long)_s.flyInTime * 10UL;
      // Which digits change at the next second: the seconds' ones always, each
      // other when the one to its right rolls over.
      starts[5] = true;
      starts[4] = (now.second + 1) % 10 == 0;
      starts[3] = starts[4] && now.second + 1 == 60;
      starts[2] = starts[3] && (now.minute + 1) % 10 == 0;
      starts[1] = starts[2] && now.minute + 1 == 60;
      starts[0] = starts[1] && ((now.hour + 1) % 10 == 0 || now.hour + 1 == 24);
      // The time's change: the minute's digits (and the hour's) fly in during
      // second :59, as all the ones that change at the next second do.
      _changeFlying = starts[3] && now.set;
      // The animation: every digit flies in as if it changed, through the same
      // steps as a real change - from the first tick after the second it was
      // asked in.
      if (_anim == ANIM_ASKED && now.secondAt != _askedIn) {
        _anim = ANIM_FLYING;
        for (uint8_t i = 0; i < 6; i++) { starts[i] = true; }
      }
      digits(now);
    }

    unsigned long since = now.ms - now.secondAt;
    for (uint8_t i = 0; i < 6; i++) {
      if (now.secondTick && _flying[i]) {
        _timeX[i] = _placeX[i];
        _timeY[i] = _placeY[i];
        _flying[i] = false;
      }
      if (starts[i] && now.set) { startFlight(i); }   // dashes do not fly in
      if (_flying[i] && placeFlight(i, since)) { changed = true; }
    }
    // The animation has landed once its flights have: every flight takes
    // _flightMs from the tick it started on.
    if (_anim == ANIM_FLYING && since >= _flightMs) {
      _anim = ANIM_IDLE;
      _animEndedAt = now.ms;
    }
    return changed;
  }

  // The six digits that stand, and each one flying in, in the trail's colour
  // until it has arrived.
  void draw(Adafruit_GFX &g, const FaceNow &, const FacePaint &paint) override {
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

  // Whether the time's change flies - the minute's digits, and the hour's,
  // during second :59 until they have landed - or the animation the user
  // asked for runs: all six digits asked to fly in at the next second, or
  // flying (K1 of the face refactor: until 2026-10-20 a GIF or a card could
  // start in the middle of it). Never the seconds, which fly every second.
  // (Until 2026-10-22 the change was counted in the first fly-in time of
  // second :00, which is the seconds' flight into :01, and not while the
  // minute's digits flew.)
  bool busy(const FaceNow &now) const override {
    if (_anim != ANIM_IDLE) { return true; }
    return _changeFlying && now.ms - now.secondAt < _flightMs;
  }

  // The minute's digits fly in during its second :59 and land a fly-in time
  // after its tick (at most 850 ms), before the minute begins.
  FaceSpan changeSpan(const FaceNow &, time_t) const override {
    FaceSpan s = { 1000UL, 0 };
    return s;
  }
  bool offers(FaceTrigger why) const override { return why == TRIGGER_ACTION; }

  // The input's animation: all six digits fly in at the next second, unless
  // one is still asked for or flying, or landed less than FACE_REST_MS ago.
  // Into the time's change too: it flies the same way, the minute's digits
  // among the six (until 2026-10-22 it had to land FACE_GUARD_MS before
  // second :59, which refused a knock from about :55 on; Peter: what the user
  // asks for is granted). The console gets one line per animation taken,
  // with the requests refused since the one before, not one per request (a
  // stress test of fast knocks wrote over 200 lines in 30 s).
  FaceOutcome request(const FaceNow &now, FaceTrigger why) override {
    if (why != TRIGGER_ACTION) { return OUTCOME_NONE; }
    if (_anim != ANIM_IDLE || now.ms - _animEndedAt < FACE_REST_MS) {
      if (_ignored < 0xFFFF) { _ignored++; }
      return OUTCOME_REFUSED;
    }
    _anim = ANIM_ASKED;
    _askedIn = now.secondAt;
    Console.print("Face animation: the classic digits fly in at the next second (");
    Console.print(_ignored); Console.println(" requests ignored since the last one)");
    _ignored = 0;
    return OUTCOME_PLAYED;
  }

 private:
  // The animation on request: asked for (all six fly in at the next second),
  // flying, or neither. A new one is refused until the last one has landed
  // and FACE_REST_MS have passed. An input can come again before the first
  // has played, and one that came after the second had taken the request
  // asked for a second animation.
  enum Anim : uint8_t { ANIM_IDLE, ANIM_ASKED, ANIM_FLYING };

  // The six characters for now and for one second ahead. Dashes while the
  // clock has no time: it shows that it has none rather than counting up from
  // midnight.
  void digits(const FaceNow &now) {
    if (!now.set) {
      strcpy(_timeStr, "------");
      strcpy(_animStr, "------");
      return;
    }
    sprintf(_timeStr, "%02d%02d%02d", clockHour(now.local),     clockMinute(now.local),     clockSecond(now.local));
    sprintf(_animStr, "%02d%02d%02d", clockHour(now.local + 1), clockMinute(now.local + 1), clockSecond(now.local + 1));
  }

  // How far digit i has to fly when it comes from side dir: so far that the
  // incoming digit starts wholly off the panel and the outgoing one, which
  // moves out the other side by the same distance, ends wholly off it.
  // Measured with the box all ten digits of its font cover, and the panel's
  // size as it is turned now.
  int16_t flightNeed(uint8_t i, uint8_t dir) const {
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
  void startFlight(uint8_t i) {
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

  // Digit i where its flight is `ms` after the tick it started on: the
  // incoming digit the rest of the way out from its place, the outgoing one as
  // far past it on the other side as the flight has come. Every flight covers
  // its way in _flightMs, its pixels spread evenly over it. (Until 2026-10-08
  // the setting was ms per pixel, rounded to whole passes per pixel: with
  // flights up to 62 px and every flight held under a second, it had three
  // steps - 4..7, 8..12 and 13..60 ms looked alike within each.) Whether the
  // digit moved.
  bool placeFlight(uint8_t i, unsigned long ms) {
    static const int8_t SIDE_X[4] = { 0, 1, 0, -1 };   // where each dir comes from
    static const int8_t SIDE_Y[4] = { -1, 0, 1, 0 };
    unsigned long len = _flightMs ? _flightMs : 1;
    uint32_t moved = (uint32_t)((unsigned long)_flightDist[i] * min(ms, len) / len);
    int16_t rest = _flightDist[i] - (int16_t)moved;
    uint8_t d = _flightDir[i];
    int8_t ax = _placeX[i] + SIDE_X[d] * rest, ay = _placeY[i] + SIDE_Y[d] * rest;
    int8_t tx = _placeX[i] - SIDE_X[d] * (int16_t)moved, ty = _placeY[i] - SIDE_Y[d] * (int16_t)moved;
    bool moves = ax != _animX[i] || ay != _animY[i] || tx != _timeX[i] || ty != _timeY[i];
    _animX[i] = ax; _animY[i] = ay;
    _timeX[i] = tx; _timeY[i] = ty;
    return moves;
  }

  // The trail's brightness for the digits' `bright` (TRAIL_BRIGHTNESS_PCT).
  uint8_t trailBrightness(uint8_t bright) const {
    float frac = TRAIL_BRIGHTNESS_PCT / 100.0f;                    // the perceived fraction
    uint16_t t = (uint16_t)lroundf(powf(frac, FADE_GAMMA) * bright);   // in linear light
    if (t < TRAIL_BRIGHTNESS_MIN) { t = TRAIL_BRIGHTNESS_MIN; }
    if (t > bright)               { t = bright; }   // very low brightness: as the digits
    return (uint8_t)t;
  }

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
  // The time every flight of this second takes, in ms, set at its tick: all
  // flights take the same time, so whatever flies at a tick lands together,
  // and every second looks the same whether one digit flies or six.
  unsigned long _flightMs = 400;
  Anim _anim = ANIM_IDLE;
  bool _changeFlying = false;      // the minute's or hour's digits started flying at this second's tick
  unsigned long _askedIn = 0;      // the second the animation was asked in (its secondAt)
  unsigned long _animEndedAt = 0;  // when the last one landed or was cut off
  uint16_t _ignored = 0;           // requests refused since the last one taken, for the console
};

#endif  // FACE_CLASSIC_H
