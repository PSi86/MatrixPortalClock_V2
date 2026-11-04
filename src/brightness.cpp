// The panel's brightness: the light sensor, the brightness it sets, the
// manual levels and trims, the perceptual fade (brightness.h).
#include "brightness.h"

#include <math.h>
#include <BH1750FVI.h>
#include "clock_state.h"
#include "face.h"        // FADE_GAMMA
#include "gif_player.h"  // gifPlaying()
#include "sensors.h"
#include "settings.h"
#include "veml7700.h"

// What only this module calls, ahead of its use.
static bool luxRead(float &lux);
static uint8_t mapLuxToBrightness(float lux);
static float luxMedian();
static uint8_t levelBrightness(uint8_t level);
static uint8_t trimBrightness(int8_t trim);

// Perceptual brightness fade (Classic clicks: UP held) ----------------------
static const float FADE_PERIOD_MS = 2500.0f; // time for a full 0..1 perceptual sweep (FADE_GAMMA: face.h)
static float       fadePhase = 1.0f;         // perceptual position 0..1 (linear to the human eye)
static int8_t      fadeDir   = -1;
static unsigned long fadeLastMs = 0;

// Ambient light sensor -----------------------------------------------------
// An external BH1750 (address 0x23) or VEML7700 (0x10, veml7700.h) on I2C.
// When enabled it drives the master brightness via a configurable
// lux->brightness mapping.
//
// Until 2026-10-21 a 10 s moving average of one reading a second set the
// target: a lamp switched on showed half its change after about 5 s and 90 %
// after 9 s (tools.local/dimming/dim_sim.py on the S3's logged light). Now the
// sensor is read five times a second, and the median of the last LUX_MEDIAN
// readings is filtered in log units: a hand in front of the sensor for up to
// about 1.2 s (a gesture) changes nothing, a real change shows half after
// 1.6 to 2.7 s and 90 % after 2.3 to 4.1 s, the same on and off.
static BH1750FVI     lightMeter(BH1750FVI_DEFAULT_ADDRESS); // I2C address 0x23 (ADDR pin low)
static Veml7700      veml;
bool          luxOK = false;     // true once a light sensor was found on I2C
SensorId      luxSensor = SENSOR_COUNT;   // the one the brightness follows; SENSOR_COUNT: none
static unsigned long luxLast = 0;       // last light-sensor poll
static unsigned long luxPrintedAt = 0;  // last Lux console line (one a second)
float         lastLux = -1.0f;   // most recent UNFILTERED lux reading (web UI), -1 = none yet
float         luxAvg = -1.0f;    // the filtered light (below), -1 = none yet (the dark room)
// The BH1750 has a new value every ~120 ms (setContHighRes()), the VEML7700
// every 100 ms.
static const unsigned long LUX_READ_MS = 200;
static const uint8_t LUX_MEDIAN = 13;             // readings in the median: 2.6 s
static const float   LUX_TAU_MS = 1000.0f;        // the median's log, filtered with this time constant
// lx: 0 has no log. The BH1750's steps are 0.83 lx; the VEML7700's below
// 100 lx are 0.0336 lx, but its light is floored here as well, so the filter
// takes as long out of a dark room with either (a deeper floor would add log
// units for the filter to cover). Unfiltered, the web app shows its own.
static const float   LUX_LN_FLOOR = 0.5f;
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

/* ======================================================================
   Auto-brightness: the ambient light sensor drives the master brightness
   ====================================================================== */

// The external light sensors on the shared I2C bus: a VEML7700 (0x10) and a
// BH1750 (0x23). Both are looked for, so the sensor record knows each one
// fitted. With both found the VEML7700 sets the brightness - its steps are
// finer (0.0336 lx below 100 lx against 0.83 lx) - and the BH1750 is left
// unread. With neither, the brightness stays under manual control.
void lightSensorBegin() {
  sensorTries[SENSOR_LIGHT_VEML] = sensorBegin(SENSOR_LIGHT_VEML, []() { return veml.begin(Wire); });
  sensorTries[SENSOR_LIGHT] = sensorBegin(SENSOR_LIGHT, []() {
    if (!lightMeter.begin()) { return false; }   // true if the sensor answers on I2C
    lightMeter.powerOn();
    lightMeter.setContHighRes();       // 1 lx resolution, a new value every ~120 ms
    return lightMeter.getError() == BH1750FVI_OK;
  });
  luxSensor = sensorTries[SENSOR_LIGHT_VEML] ? SENSOR_LIGHT_VEML
            : sensorTries[SENSOR_LIGHT]      ? SENSOR_LIGHT : SENSOR_COUNT;
  luxOK = luxSensor != SENSOR_COUNT;
  if (sensorTries[SENSOR_LIGHT_VEML]) { Console.printf("VEML7700 found, ID 0x%04X\n", veml.id()); }
  if (sensorTries[SENSOR_LIGHT]) {
    Console.println(luxSensor == SENSOR_LIGHT ? "BH1750 found" : "BH1750 found, not used: the VEML7700 sets the brightness");
  }
  if (!luxOK) { Console.println("No light sensor found - auto-brightness disabled"); }
}

// A reading of the light sensor in use; false when there is none this time
// (a failed I2C read, the VEML7700 changing its range or setting itself up
// again).
bool luxRead(float &lux) {
  if (luxSensor == SENSOR_LIGHT_VEML) {
    uint16_t restored = veml.restored();
    bool ok = veml.read(lux);
    if (veml.restored() != restored && veml.restored() <= 10) {
      Console.println("VEML7700: its setting was lost (power dip?) - set again");
    }
    return ok;
  }
  lux = lightMeter.getLux();
  return lightMeter.getError() == BH1750FVI_OK;
}

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
    float lux;
    if (luxRead(lux)) {                              // skip failed I2C reads
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
        Console.print(" now "); Console.print(effectiveBrightness);
        if (luxSensor == SENSOR_LIGHT_VEML) {
          Console.printf(" (VEML7700 gain %s, %u counts)", veml.gainText(), (unsigned)veml.counts());
        }
        Console.println();
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
