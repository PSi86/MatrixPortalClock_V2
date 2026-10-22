// The panel's brightness: the light sensor, the brightness it sets, the
// manual levels and trims, the perceptual fade - and the wall clock's room
// climate, from the sensor beside it (brightness.h).
#include "brightness.h"

#include <math.h>
#include <BH1750FVI.h>
#include "clock_state.h"
#include "web_server.h"
#include "info_band.h"
#include "face.h"        // FADE_GAMMA
#include "gif_player.h"  // gifPlaying()
#include "sensors.h"
#include "settings.h"
#if BOARD_WAVESHARE_WALL
#include "shtc3.h"
#endif

// What only this module calls, ahead of its use.
static uint8_t mapLuxToBrightness(float lux);
static float luxMedian();
static uint8_t levelBrightness(uint8_t level);
static uint8_t trimBrightness(int8_t trim);
#if BOARD_WAVESHARE_WALL
static float saturationHpa(float t);
#endif

// Perceptual brightness fade (Classic clicks: UP held) ----------------------
static const float FADE_PERIOD_MS = 2500.0f; // time for a full 0..1 perceptual sweep (FADE_GAMMA: face.h)
static float       fadePhase = 1.0f;         // perceptual position 0..1 (linear to the human eye)
static int8_t      fadeDir   = -1;
static unsigned long fadeLastMs = 0;

// Ambient light sensor -----------------------------------------------------
// External BH1750 on I2C (default address 0x23). When enabled it drives the
// master brightness via a configurable lux->brightness mapping.
//
// Until 2026-10-21 a 10 s moving average of one reading a second set the
// target: a lamp switched on showed half its change after about 5 s and 90 %
// after 9 s (tools.local/dimming/dim_sim.py on the S3's logged light). Now the
// sensor is read five times a second, and the median of the last LUX_MEDIAN
// readings is filtered in log units: a hand in front of the sensor for up to
// about 1.2 s (a gesture) changes nothing, a real change shows half after
// 1.6 to 2.7 s and 90 % after 2.3 to 4.1 s, the same on and off.
static BH1750FVI     lightMeter(BH1750FVI_DEFAULT_ADDRESS); // I2C address 0x23 (ADDR pin low)
bool          luxOK = false;     // true once the BH1750 was found on I2C
static unsigned long luxLast = 0;       // last light-sensor poll
static unsigned long luxPrintedAt = 0;  // last Lux console line (one a second)
float         lastLux = -1.0f;   // most recent UNFILTERED lux reading (web UI), -1 = none yet
float         luxAvg = -1.0f;    // the filtered light (below), -1 = none yet (the dark room)
static const unsigned long LUX_READ_MS = 200;     // the sensor has a new value every ~120 ms (setContHighRes())
static const uint8_t LUX_MEDIAN = 13;             // readings in the median: 2.6 s
static const float   LUX_TAU_MS = 1000.0f;        // the median's log, filtered with this time constant
static const float   LUX_LN_FLOOR = 0.5f;         // lx: 0 has no log; the sensor's steps are 0.83 lx
static float         luxRecent[LUX_MEDIAN];       // ring buffer of the last readings
static uint8_t       luxRecentIdx = 0;            // next write position
static uint8_t       luxRecentCount = 0;          // valid readings (ramps up to LUX_MEDIAN)
static float         luxLn = 0.0f;                // the filtered log of the light
static unsigned long luxLnAt = 0;                 // millis() of its last reading
// The filtered light sets a brightness TARGET; the master brightness moves
// towards it on every pass in log units, so a fade looks equally fast at
// every level: at most AUTOBRIGHT_RATE, slowing over its last part
// (AUTOBRIGHT_END_MS), and only once the target is AUTOBRIGHT_BAND away, so a
// light that only wavers moves nothing. The panel takes a new value at most
// every AUTOBRIGHT_STEP_MS: on the S3 every brightness whose tables differ
// writes two whole frames (3.65 ms each); the wall clock writes its picture
// again a few rows a pass (PanelCanvas::retintStep(), 42 ms in all).
static uint8_t       autoBrightTarget = 128;        // sensor brightness the fade moves toward
static float         autoBrightLn = 0.0f;           // the brightness now, in log units (sub-unit precision)
static bool          autoBrightMoving = false;      // a fade under way
static bool          autoBrightInit = false;        // false until the fade has been seeded
static unsigned long autoFadeLastMs = 0;            // last fade step time
static unsigned long autoBrightShownAt = 0;         // millis() the panel last took a new value
static const float   AUTOBRIGHT_RATE = 1.0f;        // log units a second at most: a factor of e (2.7)
static const float   AUTOBRIGHT_END_MS = 300.0f;    // the fade's slower end
static const float   AUTOBRIGHT_BAND = 0.04f;       // 4 %: what the target must move to start a fade
static const float   AUTOBRIGHT_SETTLED = 0.004f;   // 0.4 %: a fade is over
static const unsigned long AUTOBRIGHT_STEP_MS = 40; // the panel's new value at most this often
// Brightness actually used for rendering. In manual mode it equals
// settings.brightness (absolute). In auto-brightness mode settings.brightness is
// instead a RELATIVE trim around the sensor value (128 = neutral, 0 = much
// darker, 255 = much brighter) and effectiveBrightness is the result.
uint8_t       effectiveBrightness = 128;

#if BOARD_WAVESHARE_WALL
// The room's temperature and humidity: the board's SHTC3 (shtc3.h), read
// every CLIMATE_READ_MS - room air changes slowly, and the chip sleeps in
// between, so it hardly warms itself - and filtered over CLIMATE_TAU_MS, so
// the band and Home Assistant do not follow the sensor's last digit.
static Shtc3         climate;
bool          climateOK = false;         // found at start-up
bool          roomValid = false;         // a reading came
float         roomTemp = 0.0f;           // degrees Celsius, filtered
float         roomHum = 0.0f;            // percent relative humidity, filtered
static unsigned long roomAt = 0;                // millis() of the last reading
static unsigned long climateStartedAt = 0;      // millis() a measurement was started; 0 = none under way
static unsigned long climateDueAt = 0;          // millis() the last one was due
static uint16_t      climateFailures = 0;       // readings that did not come, since start-up
static const unsigned long CLIMATE_READ_MS = 5000;
static const float   CLIMATE_TAU_MS = 30000.0f;
#endif

/* ======================================================================
   Auto-brightness: BH1750 ambient light sensor drives the master brightness
   ====================================================================== */

// External BH1750 ambient light sensor -> auto-brightness. Shares the I2C bus
// (default address 0x23). If absent, brightness stays under manual control.
void lightSensorBegin() {
  sensorTries[SENSOR_LIGHT] = sensorBegin(SENSOR_LIGHT, []() {
    if (!lightMeter.begin()) { return false; }   // true if the sensor answers on I2C
    lightMeter.powerOn();
    lightMeter.setContHighRes();       // 1 lx resolution, a new value every ~120 ms
    return lightMeter.getError() == BH1750FVI_OK;
  });
  luxOK = sensorTries[SENSOR_LIGHT] != 0;
  if (luxOK) {
    Console.println("BH1750 found");
  } else {
    Console.println("BH1750 not found - auto-brightness disabled");
  }
}

#if BOARD_WAVESHARE_WALL
// The board's SHTC3, the room's temperature and humidity, on its own bus.
void climateBegin() {
  sensorTries[SENSOR_CLIMATE] = sensorBegin(SENSOR_CLIMATE, []() { return climate.begin(Wire1); });
  climateOK = sensorTries[SENSOR_CLIMATE] != 0;
  if (climateOK) { Console.printf("SHTC3 found, ID 0x%04X\n", climate.id()); }
}
#endif

// Map a measured lux value to a master brightness using the configurable
// endpoints: lux <= luxDark -> brightMin, lux >= luxBright -> brightMax, linear
// in between. Tune luxDark/luxBright/brightMin/brightMax on the AP config page.
uint8_t mapLuxToBrightness(float lux) {
  uint16_t dl = settings.luxDark;
  uint16_t bl = settings.luxBright;
  int16_t  bmin = settings.brightMin;
  int16_t  bmax = settings.brightMax;
  if (bl <= dl) { bl = dl + 1; }                   // guard against an empty/inverted range
  if (lux <= dl) { return (uint8_t)bmin; }
  if (lux >= bl) { return (uint8_t)bmax; }
  float t = (lux - (float)dl) / (float)(bl - dl);  // 0..1 across the configured lux range
  int v = bmin + (int)lroundf(t * (bmax - bmin));
  return (uint8_t)constrain(v, 0, 255);
}

// The median of the last readings (luxRecent).
float luxMedian() {
  float v[LUX_MEDIAN];
  memcpy(v, luxRecent, sizeof(v));
  for (uint8_t i = 1; i < luxRecentCount; i++) {   // insertion sort, at most 13
    float x = v[i];
    int8_t j = (int8_t)i - 1;
    while (j >= 0 && v[j] > x) { v[j + 1] = v[j]; j--; }
    v[j + 1] = x;
  }
  return v[luxRecentCount / 2];
}

// Resolve the brightness used for ALL rendering (clock, boot, AP info, banner)
// with one shared rule, so every screen shows the same brightness as the clock.
// Manual mode / no sensor: the absolute setting. Auto-brightness: the sensor's
// filtered light (above), mapped, trimmed by settings.brightness (128 =
// neutral) and clamped to >= Min, then faded to. Call once per frame in every
// path.
void updateBrightness() {
  // Sample the raw ambient light whenever a sensor is present, in BOTH modes, so
  // the config UI can always show the live lux value. The first call is forced
  // (luxLast == 0) so the very first frame, e.g. boot, has a real reading. In
  // auto mode the same reading also feeds the filter and the mapping.
  if (luxOK && (luxLast == 0 || millisNow - luxLast >= LUX_READ_MS)) {
    luxLast = millisNow;
    float lux = lightMeter.getLux();
    if (lightMeter.getError() == BH1750FVI_OK) {     // skip failed I2C reads
      lastLux = lux;                                 // unfiltered reading shown in the web UI
      luxRecent[luxRecentIdx] = lux;
      luxRecentIdx = (luxRecentIdx + 1) % LUX_MEDIAN;
      if (luxRecentCount < LUX_MEDIAN) { luxRecentCount++; }
      float ln = logf(max(luxMedian(), LUX_LN_FLOOR));
      if (luxAvg < 0) {
        luxLn = ln;                                  // the first reading: no fade from nothing
      } else {
        luxLn += (ln - luxLn) * (1.0f - expf(-(float)(millisNow - luxLnAt) / LUX_TAU_MS));
      }
      luxLnAt = millisNow;
      luxAvg = expf(luxLn);
      autoBrightTarget = mapLuxToBrightness(luxAvg);
      if (consoleLevel >= 2 && millisNow - luxPrintedAt >= 1000) {   // a running figure (console.h)
        luxPrintedAt = millisNow;
        Console.print("Lux "); Console.print(lux); Console.print(" avg "); Console.print(luxAvg);
        Console.print(" -> sensor "); Console.print(autoBrightTarget);
        Console.print(" now "); Console.println(effectiveBrightness);
      }
    }
  }

  uint8_t before = effectiveBrightness;
  if (!brightnessIsTrim()) {            // manual mode (or no sensor)
    effectiveBrightness = settings.brightness;
    autoBrightInit = false;             // re-seed the fade when auto resumes
  } else {
    // Relative trim around the sensor value: settings.brightness 128 = neutral,
    // 0 = much darker, 255 = ~2x brighter. Min brightness is a hard floor.
    float relTarget = (float)autoBrightTarget * (float)settings.brightness / 128.0f;
    if (relTarget > 255.0f)              { relTarget = 255.0f; }
    if (relTarget < settings.brightMin)  { relTarget = settings.brightMin; }
    float target = logf(max(relTarget, 1.0f));

    // Seeded to the target on the first call to avoid a jump.
    if (!autoBrightInit) {
      autoBrightLn = target;
      autoBrightMoving = false;
      autoBrightInit = true;
      autoFadeLastMs = millisNow;
      autoBrightShownAt = millisNow - AUTOBRIGHT_STEP_MS;
    }
    float dt = (float)(millisNow - autoFadeLastMs);
    autoFadeLastMs = millisNow;
    float off = target - autoBrightLn;
    if (!autoBrightMoving && fabsf(off) > AUTOBRIGHT_BAND) { autoBrightMoving = true; }
    if (autoBrightMoving) {
      float step = min(AUTOBRIGHT_RATE * dt / 1000.0f, fabsf(off) * (1.0f - expf(-dt / AUTOBRIGHT_END_MS)));
      autoBrightLn += off > 0 ? step : -step;
      if (fabsf(target - autoBrightLn) < AUTOBRIGHT_SETTLED) { autoBrightMoving = false; }
    }
    if (millisNow - autoBrightShownAt >= AUTOBRIGHT_STEP_MS) {
      autoBrightShownAt = millisNow;
      long b = lroundf(expf(autoBrightLn));
      // A Min of 0 lets the sensor switch the panel dark: at the fade's end.
      if (relTarget < 0.5f && b <= 1) { b = 0; }
      effectiveBrightness = (uint8_t)constrain(b, 0L, 255L);
    }
  }
  if (effectiveBrightness != before) {
    matrix.setBrightness(panelBrightness());     // the panel dims as a whole
    // Colours are scaled with it (on the S3 only those asked dimmer than the
    // panel), so the screens that only repaint on a change have to repaint.
    panelDirty = true;
  }
#if BOARD_WAVESHARE_WALL
  matrix.retintStep();   // a new brightness reaches the wall's rows a few at a time
#endif
}

#if BOARD_WAVESHARE_WALL
// On every pass: every CLIMATE_READ_MS a measurement started, and read
// Shtc3::MEASURE_MS later, so the loop never waits for it.
void climateUpdate() {
  if (!climateOK) { return; }
  if (climateStartedAt != 0) {
    if (millisNow - climateStartedAt < Shtc3::MEASURE_MS) { return; }
    climateStartedAt = 0;
    float t, h;
    if (!climate.read(t, h)) {
      if (++climateFailures <= 10) { Console.println("SHTC3: no reading"); }
      return;
    }
    if (!roomValid) {
      roomTemp = t;
      roomHum = h;
      roomValid = true;
      Console.printf("SHTC3: %.1f C, %.0f %% humidity\n", t, h);
    } else {
      float a = 1.0f - expf(-(float)(millisNow - roomAt) / CLIMATE_TAU_MS);
      roomTemp += (t - roomTemp) * a;
      roomHum += (h - roomHum) * a;
    }
    roomAt = millisNow;
    if (consoleLevel >= 2) {   // a running figure (console.h)
      Console.printf("Room %.2f C %.1f %% -> %.2f C %.1f %%\n", t, h, roomTemp, roomHum);
    }
    return;
  }
  if (climateDueAt != 0 && millisNow - climateDueAt < CLIMATE_READ_MS) { return; }
  climateDueAt = millisNow ? millisNow : 1;
  if (climate.start()) {
    climateStartedAt = millisNow ? millisNow : 1;
  } else if (++climateFailures <= 10) {
    Console.println("SHTC3: did not start a measurement");
  }
}

// The web app's Sensors section: the SHTC3's warmth (toff, degrees with one
// decimal, 0 to 20); absent keeps it. Says whether it is stored, or nothing
// changed.
bool applySensorParams(const String &q) {
  uint16_t was = sensorWarmth;
  settingsApply(sensorTable, [&q](const char *key) { return getParam(q, key); }, false);   // SENSOR_PARAMS
  if (sensorWarmth == was) { return true; }
  bandLocalDone = false;   // the band's room anew
  Console.printf("SHTC3: %u.%u C warmer than the room\n", sensorWarmth / 10, sensorWarmth % 10);
  return saveSensorRecord();
}

// Water's saturation vapour pressure at t degrees Celsius, in hPa (Magnus,
// over water).
float saturationHpa(float t) {
  return 6.112f * expf(17.62f * t / (243.12f + t));
}

// The room's temperature: the SHTC3's, filtered, less the board's warmth
// (sensorWarmth).
float roomTempNow() {
  return roomTemp - sensorWarmth / 10.0f;
}

// The room's humidity. The air at the sensor holds as much water as the
// room's, but it is warmer there, so its relative humidity is lower: worked
// out for the room's temperature through the saturation vapour pressure - a
// fixed share off would be wrong. The conversion also scales the sensor's
// own error (about x1.8 from 34 to 24 C). At most 100 %.
float roomHumNow() {
  if (sensorWarmth == 0) { return roomHum; }
  float h = roomHum * saturationHpa(roomTemp) / saturationHpa(roomTempNow());
  return h > 100.0f ? 100.0f : h;
}
#endif

// The brightness the panel runs at: the clock's, and while a GIF plays the
// higher of the GIFs' own lowest (gifSettings.minBright) and a multiple of the
// clock's (gifSettings.brightTimes tenths) - a GIF needs more light than the
// face, the more so the lighter the room - unless the clock has switched the
// panel dark (0).
uint8_t panelBrightness() {
  if (gifPlaying() && effectiveBrightness > 0) {
    uint32_t times = (uint32_t)effectiveBrightness * gifSettings.brightTimes / 10;
    uint32_t b = max((uint32_t)gifSettings.minBright, times);
    return (uint8_t)min(max(b, (uint32_t)effectiveBrightness), (uint32_t)255);
  }
  return effectiveBrightness;
}

// Whether settings.brightness is a trim around the light sensor (auto
// brightness on and a sensor found) rather than the brightness itself.
bool brightnessIsTrim() {
  return luxOK && settings.autoBright;
}

// Manual brightness level 0..BRIGHT_LEVELS-1 nearest to a brightness.
uint8_t brightnessLevel(uint8_t b) {
  float phase = powf((float)b / 255.0f, 1.0f / FADE_GAMMA);
  long level = lroundf((phase - BRIGHT_PHASE_MIN) / (1.0f - BRIGHT_PHASE_MIN) * (BRIGHT_LEVELS - 1));
  return (uint8_t)constrain(level, 0L, (long)(BRIGHT_LEVELS - 1));
}

// The brightness of a manual level.
uint8_t levelBrightness(uint8_t level) {
  float phase = BRIGHT_PHASE_MIN + (1.0f - BRIGHT_PHASE_MIN) * level / (BRIGHT_LEVELS - 1);
  return (uint8_t)lroundf(255.0f * powf(phase, FADE_GAMMA));
}

// Auto-brightness trim -BRIGHT_TRIM_MAX..BRIGHT_TRIM_MAX nearest to a brightness.
int8_t brightnessTrim(uint8_t b) {
  if (b == 0) { return -BRIGHT_TRIM_MAX; }
  long trim = lroundf(BRIGHT_TRIM_MAX * log2f((float)b / 128.0f));
  return (int8_t)constrain(trim, (long)-BRIGHT_TRIM_MAX, (long)BRIGHT_TRIM_MAX);
}

// The brightness of a trim: 128 times 2^(trim/8), at most 255.
uint8_t trimBrightness(int8_t trim) {
  long b = lroundf(128.0f * exp2f((float)trim / BRIGHT_TRIM_MAX));
  return (uint8_t)constrain(b, 1L, 255L);
}

// Move the brightness by whole steps of its current meaning: levels in manual
// mode, trim steps with auto brightness.
void brightnessStep(int delta) {
  if (brightnessIsTrim()) {
    int trim = constrain(brightnessTrim(settings.brightness) + delta, -(int)BRIGHT_TRIM_MAX, (int)BRIGHT_TRIM_MAX);
    settings.brightness = trimBrightness((int8_t)trim);
  } else {
    int level = constrain(brightnessLevel(settings.brightness) + delta, 0, (int)BRIGHT_LEVELS - 1);
    settings.brightness = levelBrightness((uint8_t)level);
  }
  panelDirty = true;
}

/* ======================================================================
   Perceptually linear brightness fade
   The human eye perceives brightness roughly as a power law, so we move a
   "perceptual" phase linearly and raise it to FADE_GAMMA to get the actual
   PWM/intensity. That makes the fade look linear to the eye.
   ====================================================================== */
void fadeStart() {
  fadePhase = powf((float)settings.brightness / 255.0f, 1.0f / FADE_GAMMA); // perceptual position of current brightness
  fadeDir   = (fadePhase >= 0.99f) ? -1 : +1; // if already near max, start dimming
  fadeLastMs = millisNow;
}

void fadeStep() {
  float dt = (float)(millisNow - fadeLastMs);
  fadeLastMs = millisNow;
  fadePhase += (float)fadeDir * (dt / FADE_PERIOD_MS);
  if (fadePhase >= 1.0f)  { fadePhase = 1.0f;  fadeDir = -1; }
  if (fadePhase <= 0.04f) { fadePhase = 0.04f; fadeDir = +1; } // keep a visible minimum
  settings.brightness = (uint8_t)lroundf(255.0f * powf(fadePhase, FADE_GAMMA));
}
