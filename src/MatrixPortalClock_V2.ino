/* ----------------------------------------------------------------------
Matrix Clock with Animation
Todo Concept:
- Animate all Numbers individually (max 6 active Items)
- Check last digit of HH, MM, SS if it is "9" and generate a trigger
- use this trigger to animate the digit change:
  - boolean array holding animation status for all 6 displayed digits

- if secondTrigger: trigger secondNow[1] animation

- if secondNow+1 quotient 10 = 0: trigger secondNow[0] animation
- if secondNow=59: trigger minuteNow[1] animation

- if minuteNow+1 quotient 10 = 0: trigger minuteNow[0] animation
- if minuteNow=59: trigger hourNow[1] animation

- if hourNow+1 quotient 10 = 0: trigger hourNow[0] animation


------------------------------------------------------------------------- */

#include <SPI.h>
#include <WiFiNINA.h>
//#include <WiFiUdp.h>
#include <TimeLib.h>

#include <Adafruit_Protomatter.h>
//#include <Fonts/FreeSansBold12pt7b.h> // Large friendly font works
//#include <Fonts/FreeMonoBold12pt7b.h> // Large friendly font
//#include <Fonts/FreeSans12pt7b.h> // Large friendly font
#include <Fonts/FreeSansBold12pt7b.h> // Large friendly font
#include <Fonts/FreeSansBold9pt7b.h> // Large friendly font
#include <Fonts/Picopixel.h>

#include <FlashStorage_SAMD.h> // Library Manager: "FlashStorage_SAMD" (Khoi Hoang) - stores settings in flash
#include <math.h>              // powf() for perceptual brightness fade

#include <Wire.h>              // I2C for the onboard accelerometer + light sensor
#include <Adafruit_Sensor.h>  // sensor base class
#include <Adafruit_LIS3DH.h>  // MatrixPortal M4 onboard LIS3DH accelerometer
#include <BH1750.h>            // external BH1750 ambient light sensor (auto-brightness)

/* ----------------------------------------------------------------------
The RGB matrix must be wired to VERY SPECIFIC pins, different for each
microcontroller board. This first section sets that up for a number of
supported boards.
------------------------------------------------------------------------- */

#if defined(_VARIANT_MATRIXPORTAL_M4_) // MatrixPortal M4
  uint8_t rgbPins[]  = {7, 8, 9, 10, 11, 12};
  uint8_t addrPins[] = {17, 18, 19, 20, 21};
  uint8_t clockPin   = 14;
  uint8_t latchPin   = 15;
  uint8_t oePin      = 16;
#endif

Adafruit_Protomatter matrix(
  64,          // Matrix width in pixels
  5,           // Bit depth -- 6 here provides maximum color options // Default here: 5 (produces way less flickering)
  1, rgbPins,  // # of matrix chains, array of 6 RGB pins for each
  4, addrPins, // # of address pins (height is inferred), array of pins
  clockPin, latchPin, oePin, // Other matrix control pins
  true);       // HERE IS THE MAGIC FOR DOUBLE-BUFFERING!

// Persistent settings ------------------------------------------------------
// Configurable at runtime via the WLAN AP config page and the user button.
#define SETTINGS_MAGIC 0xC10E   // bump this to force a reset to defaults after a struct change

struct Settings {
  uint16_t magic;        // validity marker
  int32_t  tzOffset;     // base UTC offset in seconds, WITHOUT daylight saving (CET = 3600)
  uint8_t  dst;          // 1 = daylight saving active (adds +3600s)
  uint8_t  brightness;   // 0..255 master intensity (scales all drawn colors)
  uint8_t  animSpeed;    // frame/loop time in ms (lower = faster animation)
  uint8_t  digitR, digitG, digitB; // color of the time digits
  uint8_t  trailR, trailG, trailB; // color of a digit while it is flying in
  uint8_t  dir[6];       // fly-in direction per digit (0=top,1=right,2=bottom,3=left)
  uint8_t  syncHour;     // hour of daily NTP resync
  uint8_t  syncMinute;   // minute of daily NTP resync
  uint8_t  autoBright;   // 1 = BH1750 light sensor controls the master brightness
  uint16_t luxDark;      // lux at/below which brightness = brightMin
  uint16_t luxBright;    // lux at/above which brightness = brightMax
  uint8_t  brightMin;    // master brightness in darkness
  uint8_t  brightMax;    // master brightness in bright light
};

// Factory defaults reproduce the original hard-coded behaviour.
const Settings DEFAULTS = {
  SETTINGS_MAGIC,
  3600,            // CET base (UTC+1)
  1,               // DST on -> effective UTC+2 like the original adjustTime(7200)
  128,             // neutral: absolute half in manual mode, sensor-as-is trim in auto mode
  12,              // original loopTime
  0, 0, 255,       // blue digits
  255, 255, 255,   // fly-in trail (full color; dimmed relative to brightness at render time)
  {3, 1, 3, 1, 1, 1},
  5, 11,           // original sync time 05:11
  1,               // auto-brightness on (BH1750)
  1, 400,          // lux mapping range: 1 lux (dark) .. 400 lux (bright)
  8, 255           // brightness 8 (dark) .. 255 (bright)
};

Settings settings;

// Persist the settings at a FIXED address in the top 8 KB block of the 512 KB
// flash, OUTSIDE the program image. The upload (bossac --write --offset 0x4000,
// no --erase) only touches the sketch region from 0x4000 up, so this block is
// left untouched and the settings survive a firmware upload. The FlashStorage()
// macro instead reserves a zero-initialised array *inside* the image, which
// every upload overwrites - that was why all settings reset to defaults (e.g.
// brightness back to 255) on each flash.
#define SETTINGS_FLASH_ADDR 0x0007E000UL   // last 8 KB erase block of the 512 KB flash
static_assert(sizeof(Settings) <= 8192, "Settings must fit in one 8 KB flash block");
FlashStorageClass<Settings> clockStore((const void *)SETTINGS_FLASH_ADDR);

// User button / interaction ------------------------------------------------
// MatrixPortal M4: UP button = D2, DOWN button = D3, both active LOW (INPUT_PULLUP).
#define USER_BUTTON_PIN 2
const unsigned long BTN_DEBOUNCE_MS  = 25;   // ignore bounces shorter than this
const unsigned long BTN_LONGPRESS_MS = 600;  // hold longer than this -> brightness fade
const unsigned long BTN_MULTI_GAP_MS = 400;  // window to collect a click sequence
bool          btnPrev = false;
unsigned long btnPressStart = 0, btnLastRelease = 0;
uint8_t       btnClicks = 0;
bool          btnLong = false;

// Perceptual brightness fade (long press) ----------------------------------
const float FADE_PERIOD_MS = 2500.0f; // time for a full 0..1 perceptual sweep
const float FADE_GAMMA     = 2.2f;    // perceptual -> linear-light exponent
float       fadePhase = 1.0f;         // perceptual position 0..1 (linear to the human eye)
int8_t      fadeDir   = -1;
unsigned long fadeLastMs = 0;

// WLAN access point config mode --------------------------------------------
#define AP_SSID "MatrixClock"
#define AP_PASS "clock1234"   // must be >= 8 characters
bool        apActive = false;
bool        apClientConnected = false; // true once a client talks to us (station joined / HTTP hit)
unsigned long apStatusLast = 0;        // last time the AP connection status was polled
unsigned long apClockLast = 0;         // last live-preview clock frame (throttled in AP mode)
WiFiServer  apServer(80);
// Captive portal: a tiny DNS server answers every lookup with the AP IP so the
// phone's connectivity check is hijacked and the config page pops up by itself.
IPAddress      apIP(192, 168, 4, 1); // WiFiNINA default AP address
WiFiUDP        dnsUdp;
const uint16_t DNS_PORT = 53;
byte           dnsBuffer[512];

// Sundry globals used for animation ---------------------------------------

int16_t  textX, // Current text position (X)
         textY,                  // Current text position (Y)
         textMin,                // Text pos. (X) when scrolled off left edge
         hue = 0;
char message[10] = "TERMIN";  // Buffer to hold scrolling message text
char timeStr[7], animStr[7]; // 6 digits + null terminator
uint8_t intensityValue, position;
bool animTrigger[6] = {0, 0, 0, 0, 0, 0}; //[0-1] hours digits, [2-3] minutes, [4-5] seconds
bool animShow[6] = {0, 0, 0, 0, 0, 0}; //[0-1] hours digits, [2-3] minutes, [4-5] seconds
int8_t animXPos[6] = {0, 0, 0, 0, 0, 0};
int8_t animYPos[6] = {0, 0, 0, 0, 0, 0};
// Active (runtime) layout - populated by applyOrientation() from the source tables
// below. Initialised to the portrait layout so the very first frames look right
// even before the first accelerometer read.
int8_t timeXPos[6] = {0, 13, 6, 19, 6, 17};
int8_t timeYPos[6] = {16, 16, 35, 35, 50, 50};
int8_t animXTarget[6] = {0, 13, 6, 19, 6, 17}; // Todo check 25 abd 31 value
int8_t animYTarget[6] = {16, 16, 35, 35, 50, 50};
int8_t animDirection[6] = {3, 1, 3, 1, 1, 1}; //0=from the top, 1=from the right, 2=from the bottom, 3=from the left

// Source layouts. The active tables above are copied from one of these whenever
// the orientation changes. Portrait = 32 wide x 64 tall (digits stacked
// HH/MM/SS); landscape = 64 wide x 32 tall (HH/MM large side by side, SS small
// centered below). Landscape positions are starting values, easy to fine-tune.
const int8_t portXTarget[6] = {0, 13, 6, 19, 6, 17};
const int8_t portYTarget[6] = {16, 16, 35, 35, 50, 50};
const int8_t landXTarget[6] = {2, 15, 36, 49, 22, 32}; // HH | MM big, SS small
const int8_t landYTarget[6] = {17, 17, 17, 17, 30, 30}; // HH/MM baseline 17, SS baseline 30

// Orientation handling -----------------------------------------------------
// The MatrixPortal M4 has an onboard LIS3DH accelerometer. We read gravity and
// rotate the display in 90 deg steps so the clock always shows the right way up.
Adafruit_LIS3DH lis = Adafruit_LIS3DH();
bool    accelOK = false;          // true once the LIS3DH was found on I2C
uint8_t curRotation = 3;          // active matrix rotation (3 = portrait, today's default)
bool    isLandscape = false;      // true for rotations 0/2 (64 wide x 32 tall)
unsigned long orientLast = 0;     // last accelerometer poll (throttle)
uint8_t orientCandidate = 3;      // debounce: rotation the sensor currently favours
uint8_t orientStable = 0;         // consecutive polls the candidate has held
const unsigned long ORIENT_POLL_MS = 250; // ~4 Hz orientation polling
const uint8_t ORIENT_DEBOUNCE = 3;        // polls a new orientation must persist

// Ambient light sensor -----------------------------------------------------
// External BH1750 on I2C (default address 0x23). When enabled it drives the
// master brightness once per second via a configurable lux->brightness mapping.
BH1750        lightMeter;        // default I2C address 0x23
bool          luxOK = false;     // true once the BH1750 was found on I2C
unsigned long luxLast = 0;       // last light-sensor poll (1 Hz throttle)
// Moving average of the lux readings so brightness changes are gentle, not
// jumpy. At the 1 Hz sampling rate a 10-sample window equals a 10 s average.
const uint8_t LUX_AVG_SAMPLES = 10;
float         luxHistory[LUX_AVG_SAMPLES]; // ring buffer of recent lux readings
uint8_t       luxHistIdx = 0;              // next write position
uint8_t       luxHistCount = 0;            // valid samples (ramps up to LUX_AVG_SAMPLES)
// The averaged lux sets a brightness TARGET once per second; the actual master
// brightness is eased toward it every frame so the change is a continuous fade
// instead of a once-per-second step.
uint8_t       autoBrightTarget = 255;       // brightness the fade is easing toward
float         autoBrightCurrent = 255.0f;   // smoothly-eased brightness (sub-unit precision)
bool          autoBrightInit = false;       // false until the fade has been seeded
unsigned long autoFadeLastMs = 0;           // last fade step time
const float   AUTOBRIGHT_TAU_MS = 1200.0f;  // fade time constant (~1.2 s to 63% of a step)
// Brightness actually used for rendering. In manual mode it equals
// settings.brightness (absolute). In auto-brightness mode settings.brightness is
// instead a RELATIVE trim around the sensor value (128 = neutral, 0 = much
// darker, 255 = much brighter) and effectiveBrightness is the result.
uint8_t       effectiveBrightness = 128;

int16_t bgBrightness, counter;
uint16_t color, colorBg;
bool directionSwitch;

//Time Related Variables
uint8_t loopTime = 12; // in ms
uint8_t hourNow, minuteNow, secondNow;
long timeOffset;
bool secondTrigger, minuteTrigger, hourTrigger;
uint8_t syncTimeHour = 5, syncTimeMinute = 11;
unsigned long millisNow, deltaT, lastSync, ntpTimeout = 3000; // ms between NTP fetch retries while unsynced

bool ntpRequestActive, ntpSuccess, wifiEnabled;
bool firstSync=true;
time_t sysTime, ntpTime;

// Network Stuff
int netStatus = WL_IDLE_STATUS;
#include "arduino_secrets.h" 
///////please enter your sensitive data in the Secret tab/arduino_secrets.h
char ssid[] = SECRET_SSID;        // your network SSID (name)
char pass[] = SECRET_PASS;    // your network password (use for WPA, or use as key for WEP)
int keyIndex = 0;            // your network key index number (needed only for WEP)

unsigned int localPort = 2390;      // local port to listen for UDP packets

//IPAddress timeServer(129, 6, 15, 28); // time.nist.gov NTP server
IPAddress timeServer(192, 168, 2, 1); // fritz.box NTP server

const int NTP_PACKET_SIZE = 48; // NTP timestamp is in the first 48 bytes of the message
byte packetBuffer[ NTP_PACKET_SIZE]; //buffer to hold incoming and outgoing packets

// A UDP instance to let us send and receive packets over UDP
WiFiUDP Udp;

// SETUP
void setup(void) {
  Serial.begin(115200);

  pinMode(USER_BUTTON_PIN, INPUT_PULLUP);
  loadSettings(); // pulls brightness/colors/animation/tz from flash (or writes defaults)

  // Onboard LIS3DH accelerometer -> automatic screen rotation. If it is not
  // found the clock simply stays in the default portrait orientation.
  Wire.begin();
  accelOK = lis.begin(0x19); // MatrixPortal M4 onboard I2C address
  if (accelOK) {
    lis.setRange(LIS3DH_RANGE_2_G);
    lis.setDataRate(LIS3DH_DATARATE_10_HZ);
    Serial.println("LIS3DH found");
  } else {
    Serial.println("LIS3DH not found - orientation locked to portrait");
  }

  // External BH1750 ambient light sensor -> auto-brightness. Shares the I2C bus
  // (default address 0x23). If absent, brightness stays under manual control.
  luxOK = lightMeter.begin(BH1750::CONTINUOUS_HIGH_RES_MODE);
  if (luxOK) {
    Serial.println("BH1750 found");
  } else {
    Serial.println("BH1750 not found - auto-brightness disabled");
  }

  // Initialize matrix...
  matrix.setRotation(3); //1
  matrix.setTextWrap(false);      // Allow text off edge
  ProtomatterStatus matrixstatus = matrix.begin();
  matrix.setRotation(3); //1
  Serial.print("Protomatter status: ");
  Serial.println((int)matrixstatus);
  if(matrixstatus != PROTOMATTER_OK) {
    // DO NOT CONTINUE if matrix setup encountered an error.
    for(;;);
  }

  //matrix.setFont(&FreeSansBold12pt7b); // Use nice bitmap font
  //matrix.setFont(&FreeMonoBold12pt7b); // Use nice bitmap font
  //matrix.setFont(&FreeSans12pt7b); // Use nice bitmap font
  matrix.setTextWrap(false);           // Allow text off edge
  matrix.setTextColor(0xFFFF);         // White
  matrix.setFont(&Picopixel); // Use nice bitmap font
  matrix.fillScreen(0); // Fill background black
  matrix.setCursor(4, 25);
  matrix.print("CLOCK");
  matrix.show();

  // Recovery / manual entry: hold the user button during boot to open the config AP
  // even when the home WiFi is unavailable.
  if (digitalRead(USER_BUTTON_PIN) == LOW) {
    startAPMode();
  }

  // Initialize Network...... (skipped while the config AP is running)
  if (!apActive) {
  WiFi.status();
 // while (WiFi.status() != WL_NO_MODULE) {
 //   Serial.println("Communication with WiFi module failed!");
    delay(1000);
 // }

  String fv = WiFi.firmwareVersion();
  Serial.print("ESP32 FW: ");
  Serial.println(fv);
  if (fv < WIFI_FIRMWARE_LATEST_VERSION) {
    Serial.println("Please upgrade the firmware");
  }

  // attempt to connect to WiFi network:
  // Connect to WPA/WPA2 network.
  //matrix.fillScreen(0); // Fill background black
  matrix.setCursor(4, 35);
  matrix.print("WLAN?");
  matrix.show();

  WiFi.setTimeout(100);
  
  Serial.print("Attempting to connect to SSID: ");
  Serial.println(ssid);
  
  while (WiFi.status() != WL_CONNECTED) {
    WiFi.begin(ssid, pass); // synchronous call takes several seconds
    delay(7000);
  }
  matrix.setCursor(4, 45);
  matrix.print("WLAN!");
  matrix.show();
  Serial.println("Connected to WiFi");
  printWifiStatus();
  delay(1000);
  } // end if(!apActive)
}

// MAIN
void loop(void) {
  timekeeper(); // Updates Time variables and gives Triggers for second, minute and hour updates
  handleButton(); // single click = DST toggle, 3 clicks = config AP, long press = brightness fade

  if (apActive) {      // config AP running
    handleAP();        // captive-portal DNS + web UI + live settings updates
    updateApDisplay(); // AP-info screen until a client connects, then a live clock preview
    return;
  }

  timeSync_WifiLib();
  updateOrientation();     // rotate the display to match how the panel is held
  updateAutoBrightness();  // light sensor -> master brightness (once per second)
  drawClock();
}

// Leave the AP-info screen and show the live clock (called once a client appears).
void apShowClock() {
  if (apClientConnected) { return; }
  apClientConnected = true;
  applyOrientation(3); // back to portrait (with matching layout) for the clock preview
}

// While the AP is up: show connection info until a client appears, then switch to
// a throttled live clock so brightness/color/speed changes are visible in real time
// without starving the (slow) WiFiNINA web serving.
void updateApDisplay() {
  if (!apClientConnected) {
    if (millisNow - apStatusLast >= 500) { // periodically check whether a station joined
      apStatusLast = millisNow;
      if (WiFi.status() == WL_AP_CONNECTED) { apShowClock(); }
    }
  } else if (millisNow - apClockLast >= 40) { // ~25 fps preview; leaves time for the web server
    apClockLast = millisNow;
    drawClock();
  }
}

/* ======================================================================
   Orientation: read the accelerometer and rotate the display in 90 deg steps
   ====================================================================== */

// Map a portrait fly-in direction to its landscape equivalent so digits enter
// across the short (32 px) edge instead of sweeping the full 64 px width:
// from right(1) <-> from top(0), from left(3) <-> from bottom(2).
uint8_t swapDir(uint8_t d) {
  switch (d) {
    case 0: return 1;
    case 1: return 0;
    case 2: return 3;
    case 3: return 2;
  }
  return d;
}

// Switch the matrix to rotation "rot" and load the matching digit layout into
// the active runtime tables. Rotations 1/3 are portrait (32x64, stacked
// HH/MM/SS); rotations 0/2 are landscape (64x32, HH/MM large + SS small below)
// with the fly-in directions swapped. Digits snap to the new layout so an
// in-flight animation never streaks across the screen during a rotation.
void applyOrientation(uint8_t rot) {
  matrix.setRotation(rot);
  curRotation = rot;
  isLandscape = (rot == 0 || rot == 2);

  for (uint8_t i = 0; i < 6; i++) {
    if (isLandscape) {
      animXTarget[i]   = landXTarget[i];
      animYTarget[i]   = landYTarget[i];
      animDirection[i] = (int8_t)swapDir(settings.dir[i]);
    } else {
      animXTarget[i]   = portXTarget[i];
      animYTarget[i]   = portYTarget[i];
      animDirection[i] = (int8_t)settings.dir[i];
    }
    timeXPos[i] = animXTarget[i];
    timeYPos[i] = animYTarget[i];
    animXPos[i] = animXTarget[i];
    animYPos[i] = animYTarget[i];
    animShow[i] = false;
  }
}

// Poll the LIS3DH (throttled) and rotate the display to match how the panel is
// physically held. Gravity in the panel plane picks one of four quadrants; a
// debounce + diagonal-rejection keeps the orientation from flickering near 45.
void updateOrientation() {
  if (!accelOK) { return; }                                  // no sensor -> stay portrait
  if (millisNow - orientLast < ORIENT_POLL_MS) { return; }   // throttle to ~4 Hz
  orientLast = millisNow;

  lis.read();
  int16_t ax = lis.x, ay = lis.y;
  int16_t axAbs = abs(ax), ayAbs = abs(ay);
  int16_t hi = max(axAbs, ayAbs), lo = min(axAbs, ayAbs);
  // Too flat (panel face up/down) or too close to the diagonal -> undecided.
  // Require the dominant axis to be >25% larger: hi > lo*1.25  <=>  hi*4 > lo*5.
  if (hi < 2000 || (int32_t)hi * 4 <= (int32_t)lo * 5) { return; }

  // Gravity quadrant: 0=+X down, 1=-X down, 2=+Y down, 3=-Y down.
  uint8_t quadrant;
  if (axAbs > ayAbs) { quadrant = (ax > 0) ? 0 : 1; }
  else               { quadrant = (ay > 0) ? 2 : 3; }
  // quadrant -> matrix rotation. Re-order these four values if the panel turns
  // the wrong way during on-device calibration (this is the one line to tweak).
  static const uint8_t ORIENT_MAP[4] = {3, 1, 0, 2};
  uint8_t wanted = ORIENT_MAP[quadrant];

  // Debounce: a new orientation must persist a few polls before we commit.
  if (wanted == orientCandidate) { if (orientStable < 255) { orientStable++; } }
  else { orientCandidate = wanted; orientStable = 1; }

  if (wanted != curRotation && orientStable >= ORIENT_DEBOUNCE) {
    Serial.print("Orientation x="); Serial.print(ax);
    Serial.print(" y="); Serial.print(ay);
    Serial.print(" -> rotation "); Serial.println(wanted);
    applyOrientation(wanted);
  }
}

/* ======================================================================
   Auto-brightness: BH1750 ambient light sensor drives the master brightness
   ====================================================================== */

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

// Poll the light sensor once per second and set the master brightness from a
// 10 s moving average of the readings, so the brightness eases gently instead of
// jumping with every measurement. Skipped when the sensor is absent or
// auto-brightness is off; a read error leaves the current brightness untouched.
// Called from the normal loop path so it is paused during AP config (where the
// brightness slider previews manually).
void updateAutoBrightness() {
  if (!luxOK || !settings.autoBright) { autoBrightInit = false; return; } // manual mode: drawClock uses settings.brightness

  // Once per second: read the sensor, update the 10 s moving average, and derive
  // the sensor brightness from the averaged lux.
  if (millisNow - luxLast >= 1000) {
    luxLast = millisNow;
    float lux = lightMeter.readLightLevel();
    if (lux >= 0) {                                  // ignore -1/-2 not-ready/read errors
      luxHistory[luxHistIdx] = lux;
      luxHistIdx = (luxHistIdx + 1) % LUX_AVG_SAMPLES;
      if (luxHistCount < LUX_AVG_SAMPLES) { luxHistCount++; }
      float sum = 0;
      for (uint8_t i = 0; i < luxHistCount; i++) { sum += luxHistory[i]; }
      float avgLux = sum / luxHistCount;
      autoBrightTarget = mapLuxToBrightness(avgLux);
      Serial.print("Lux "); Serial.print(lux); Serial.print(" avg "); Serial.print(avgLux);
      Serial.print(" -> sensor "); Serial.println(autoBrightTarget);
    }
  }

  // Apply the user's relative trim around the sensor value: settings.brightness
  // 128 = neutral (sensor as-is), 0 = much darker, 255 = ~2x brighter (clamped).
  float relTarget = (float)autoBrightTarget * (float)settings.brightness / 128.0f;
  if (relTarget > 255.0f) { relTarget = 255.0f; }

  // Every frame: ease toward the trimmed target (frame-rate independent
  // exponential approach) so it fades smoothly instead of stepping once per
  // second. Seeded on first activation to avoid a jump.
  if (!autoBrightInit) { autoBrightCurrent = relTarget; autoBrightInit = true; autoFadeLastMs = millisNow; }
  float dt = (float)(millisNow - autoFadeLastMs);
  autoFadeLastMs = millisNow;
  autoBrightCurrent += (relTarget - autoBrightCurrent) * (1.0f - expf(-dt / AUTOBRIGHT_TAU_MS));
  effectiveBrightness = (uint8_t)lroundf(autoBrightCurrent);
}

// Renders one frame of the animated clock using the current settings.
void drawClock(void) {
/*
  int16_t  x1, y1;
  uint16_t w, h;
  matrix.getTextBounds(message, 0, 0, &x1, &y1, &w, &h); // How big is it?
  textX = (matrix.width()-w)/2-1;
  textY = matrix.height() / 2 - (y1 + h / 2); // Center text vertically
*/

  // When auto-brightness is not actively driving the display (manual mode, no
  // sensor, or AP config preview), the rendered brightness is just the absolute
  // setting. In active auto mode updateAutoBrightness() already set it this frame.
  if (apActive || !luxOK || !settings.autoBright) { effectiveBrightness = settings.brightness; }

  if (secondTrigger) {
    sprintf(timeStr, "%02d%02d%02d", hour(sysTime), minute(sysTime), second(sysTime));
    sprintf(animStr, "%02d%02d%02d", hour(sysTime+1), minute(sysTime+1), second(sysTime+1));
      //sprintf(animStr, "%02d%02d%02d", hourNow+1, minuteNow+1, secondNow+1 );
    //animShow[4]=false; //right number [5] will be set true on the secondTrigger everytime so resetting it is not necessary
  }
 // if (minuteTrigger) { animShow[2]=false; animShow[3]=false; }
 // if (hourTrigger) { animShow[0]=false; animShow[1]=false; }

  matrix.fillScreen(0); // Fill background black

  // Concept: Iterate through the digits of the time display. If the animTrigger[i] is true, then create an location offset for the digit according to the fly-in direction that is configured for that digit.
  for (uint i = 0; i < 6; i++) { // 6 displayed digits (HH MM SS); timeStr[6] is the null terminator
    // check each second if animation digit has reached its target location
    if (animXPos[i] == animXTarget[i] && animYPos[i] == animYTarget[i] && animShow[i] == true && secondTrigger) { 
      // reset time position as animation digit is now in the exact place where current time digit is when not animating
      timeXPos[i]=animXTarget[i]; 
      timeYPos[i]=animYTarget[i]; 
      animShow[i] = false;
    }    
    if(animTrigger[i] == true) {
      animShow[i] = true;
      // Fly-in start offset = the off-screen edge the digit comes from. The
      // vertical offset shrinks in landscape (short edge is 32 px) so the
      // swapped top/bottom entries don't sit far off-screen for many frames.
      int8_t vOff = isLandscape ? 32 : 50;
      int8_t hOff = 32;
      //animDirection: 0=from the top, 1=from the right, 2=from the bottom, 3=from the left
      if(animDirection[i]==0)       { animXPos[i] = animXTarget[i];
                                      animYPos[i] = animYTarget[i]-vOff; }
      else if(animDirection[i]==1)  { animXPos[i] = animXTarget[i]+hOff;
                                      animYPos[i] = animYTarget[i]; }
      else if(animDirection[i]==2)  { animXPos[i] = animXTarget[i];
                                      animYPos[i] = animYTarget[i]+vOff; }
      else if(animDirection[i]==3)  { animXPos[i] = animXTarget[i]-hOff;
                                      animYPos[i] = animYTarget[i]; }
    }
    else if (animShow[i]) { 
      // as long as animShow is true, we need to update the position / do the animation of the corresponding digit (i)
      // 
      //Serial.println(i);
      if      (animYPos[i] < animYTarget[i]) { animYPos[i]++; timeYPos[i]++; } // move animation digit and current time digit at the same time in the same direction
      else if (animYPos[i] > animYTarget[i]) { animYPos[i]--; timeYPos[i]--; }
      //else if (animYPos[i] == animYTarget[i]) { timeYPos[i]=animYTarget[i];   }      
      
      if      (animXPos[i] < animXTarget[i]) { animXPos[i]++; timeXPos[i]++; }
      else if (animXPos[i] > animXTarget[i]) { animXPos[i]--; timeXPos[i]--; }
      //else if (animXPos[i] == animXTarget[i]) { timeXPos[i]=animXTarget[i];   }
    }
    
    // Animate future digits BEGIN
    if(i>3) { matrix.setFont(&FreeSansBold9pt7b); }  // Smaller Font for displaying Seconds
    else    { matrix.setFont(&FreeSansBold12pt7b); } // Bigger Font for displaying Minutes and Hours
    
    if(animShow[i] == true) {
      //Serial.println(animXPos[i]);
      if (animXPos[i] == animXTarget[i] && animYPos[i] == animYTarget[i]) { matrix.setTextColor(scaledColor(settings.digitR, settings.digitG, settings.digitB)); } // digit has arrived
      else { matrix.setTextColor(scaledColorB(settings.trailR, settings.trailG, settings.trailB, trailBrightness())); } // dim trail (relative to brightness, never black)
      matrix.setCursor(animXPos[i], animYPos[i]);
      matrix.print(animStr[i]);
    }
    // Animate future digits END

    // Time now digits are drawn here
    matrix.setTextColor(scaledColor(settings.digitR, settings.digitG, settings.digitB));
    matrix.setCursor(timeXPos[i], timeYPos[i]);
    matrix.print(timeStr[i]);

  }

  // NTP sync status pixel (green = synced, red = not), dimmed with the master brightness but kept visible
  uint8_t statusInt = effectiveBrightness / 6;
  if (statusInt < 3) { statusInt = 3; }
  if (ntpSuccess) { color=matrix.color565(0, statusInt, 0); }
  else { color=matrix.color565(statusInt, 0, 0); }
  matrix.drawPixel(0, 63, color); // NTP sync status Pixel

  /*
  matrix.setFont(&Picopixel);
  matrix.setCursor(25, 62);
  matrix.print(timeOffset);  
  */

  matrix.show();  // AFTER DRAWING, A show() CALL IS REQUIRED TO UPDATE THE MATRIX!
  if(secondTrigger) { Serial.println(deltaT); } // Debugging //timeOffset //deltaT //animTrigger[4] //animShow[i]
}

/* Updates millisNow, sysTime. Keeps track of the looptime using delay(). Updates the animTrigger[] array. */
void timekeeper(void) { 
  /*Call this always in the beginning of an iteration in loop()
    for scheduling of short actions use millisNow; for long-term schedules use hourNow, minuteNow, secondNow or plain sysTime with TimeLib functions eg: day(sysTime)
    Triggers are only active for one iteration: hourTrigger -> when the hour has changed
    animTrigger[]: for each individual clock digit (6) the corresponding bool goes high if this digit will change in one second. This gives enough time for the entry animation of the digit.

  - if secondTrigger: trigger secondNow[1] animation
  - if secondNow+1 quotient 10 = 0: trigger secondNow[0] animation
  - if secondNow=59: trigger minuteNow[1] animation
  - if minuteNow+1 quotient 10 = 0: trigger minuteNow[0] animation
  - if minuteNow=59: trigger hourNow[1] animation
  - if hourNow+1 quotient 10 = 0: trigger hourNow[0] animation
*/
  deltaT=millisNow;
  millisNow=millis();
  deltaT=millisNow-deltaT; // duration of last iteration in ms
  if(deltaT<loopTime) { delay(loopTime-deltaT); } //delay start of execution until we have the right iteration interval
  
  millisNow=millis();
  sysTime=now();
 
  if(secondNow != second(sysTime)) { secondNow=second(sysTime); secondTrigger=true; }
  else { secondTrigger=false; }
 
  if(secondTrigger && minuteNow != minute(sysTime)) { minuteNow=minute(sysTime); minuteTrigger=true; }
  else { minuteTrigger=false; }

  if(minuteTrigger && hourNow != hour(sysTime)) { hourNow=hour(sysTime); hourTrigger=true; }
  else { hourTrigger=false; }

  if(secondTrigger) { 
    animTrigger[5]=true; 
    if ((secondNow+1) % 10 == 0) { animTrigger[4]=true; }
      else { animTrigger[4]=false; }
    if (animTrigger[4]==true && secondNow+1 == 60) { animTrigger[3]=true; }
      else { animTrigger[3]=false; }
    if (animTrigger[3]==true && (minuteNow+1) % 10 == 0) { animTrigger[2]=true; }
      else { animTrigger[2]=false; }
    if (animTrigger[2]==true && minuteNow+1 == 60) { animTrigger[1]=true; }
      else { animTrigger[1]=false; }
    if (animTrigger[1]==true && ((hourNow+1) % 10 == 0 || hourNow+1 == 24)) { animTrigger[0]=true; }
      else { animTrigger[0]=false; }
      // TODO: maybe there is another check necessary for the hours: 23 to 00 change
  }
  else {
    for (uint i = 0; i < sizeof(animTrigger); i++) {
      animTrigger[i] = false;
    }
  }
}

// Updates sysTime using NTP of the WiFi library. Uses hardcoded timeserver (eg: time.nist.gov). Enables/Disables WiFi when necessary.
void timeSync_WifiLib() {
  if (!ntpSuccess && !ntpRequestActive) {
    ntpTime=WiFi.getTime();
    lastSync=millisNow;
    if(ntpTime != 0) {
      if(timeStatus()==timeSet) { timeOffset=ntpTime+tzTotalOffset()-sysTime; } // timeOffset will be positive if acutal time is ahead of sysTime (=sysTime/ system clock is slow) and negative if acutal time is behind sysTime (=sysTime/ system clock is fast)
      else { timeOffset=0; }
      setTime(ntpTime);
      adjustTime(tzTotalOffset()); // configurable timezone + daylight saving offset
      Serial.println("NTP success");
      Serial.print("NTP offset: ");
      Serial.println(timeOffset);
      ntpSuccess = true;
      ntpRequestActive = false;
      WiFi.end();
      wifiEnabled = false;
      Serial.println("Disabled Wifi");
    }
    else {
      Serial.println("NTP failed"); // print the second
      ntpSuccess = false;
      ntpRequestActive = true; // First wait for timout period before new try
    }
  }
  if(minuteTrigger) {
    /* This code should to a smooth transition between shown time and NTP time but adjustTime works on seconds as smallest increment. Need to find way to make the clock work not on systemtime directly or adjus system time in another way
    if(timeOffset != 0) {
      if(abs(timeOffset)<100 || firstSync) {
        adjustTime(timeOffset);
        timeOffset=0;
        firstSync=false;
      }
      else if(timeOffset>0) {
        timeOffset-=100;
        adjustTime(100);
      }
      else if(timeOffset<0) {
        timeOffset+=100;
        adjustTime(-100);
      }
    }
    */
    if(!wifiEnabled && hourNow == syncTimeHour && minuteNow == syncTimeMinute-1) { // 1 minute before next Sync
      netStatus = WiFi.begin(ssid, pass); // Connect to wifi
      wifiEnabled = true;
      Serial.println("Enabled Wifi");
    }
    if(ntpSuccess && hourNow == syncTimeHour && minuteNow == syncTimeMinute) {
      ntpSuccess=false; // Trigger Renewal of NTP sync
      printWifiStatus();
      Serial.println("Renew NTP sync");
    }
  }

  // Retry a failed NTP fetch promptly, independent of the minute change above.
  // WiFi.getTime() returns 0 until the NINA's SNTP completes (a few seconds after
  // associating); gating this retry behind minuteTrigger left the clock stuck at
  // 1970 (00:00:00) for up to a minute after boot even though WiFi was connected.
  if(ntpRequestActive && millisNow-lastSync > ntpTimeout) { // time to try getTime() again
    ntpRequestActive=false; // allow a fresh getTime() on the next iteration
    Serial.println("NTP Retry");
  }
}

/*
// Updates sysTime using UDP Packet based low level NTP. NOT maintained! Nice because it can contact a given server rather than a hardcoded one.
// Server: 0.europe.pool.ntp.org
// Server: ptbtime2.ptb.de
// https://www.pool.ntp.org/zone/europe
void timeSync_LowLevelUdp() {
  if (!ntpSuccess && !ntpRequestActive) {
    Serial.println(WiFi.status());
    //Serial.println(WiFi.getTime());
    Udp.begin(localPort);
    Udp.flush();
    sendNTPpacket(timeServer); // send an NTP packet to a time server
    Serial.println("NTP request sent"); // print the second
    lastSync=millisNow;
    ntpRequestActive = true;
    ntpSuccess = false;
  }
  else if (Udp.parsePacket()) { // check for new packets
    Serial.println("NTP packet received");
    // We've received a packet, read the data from it
    Udp.read(packetBuffer, NTP_PACKET_SIZE); // read the packet into the buffer

    //the timestamp starts at byte 40 of the received packet and is four bytes,
    // or two words, long. First, extract the two words:

    unsigned long highWord = word(packetBuffer[40], packetBuffer[41]);
    unsigned long lowWord = word(packetBuffer[42], packetBuffer[43]);
    // combine the four bytes (two words) into a long integer
    // this is NTP time (seconds since Jan 1 1900):
    unsigned long secsSince1900 = highWord << 16 | lowWord;

    //ntpTime = secsSince1900;
    //setTime(secsSince1900);
    setTime(WiFi.getTime());
    adjustTime(7200); // UTC+2 (60sec*60min*2h)

    hourNow=hour(); 
    minuteNow=minute();

    Udp.stop();
    ntpRequestActive = false;
    ntpSuccess = true;
  }
  else if(ntpRequestActive && millisNow-lastSync > ntpTimeout) { // NTP Request Timed out
    ntpRequestActive=false; // asume the request has timed out
    Serial.println("NTP Timeout"); // print the second
    Udp.stop();
    // WiFi.disconnect(); // TEST
  }
  else if(ntpSuccess && millisNow-lastSync > syncInterval) {
    ntpSuccess=false; // asume the request has timed out
    Serial.println("Renew NTP sync"); // print the second
  }
}

// send an NTP request to the time server at the given address
void sendNTPpacket(IPAddress& address) {
  //Serial.println("1");
  // set all bytes in the buffer to 0
  memset(packetBuffer, 0, NTP_PACKET_SIZE);
  // Initialize values needed to form NTP request
  // (see URL above for details on the packets)
  //Serial.println("2");
  packetBuffer[0] = 0b11100011;   // LI, Version, Mode
  packetBuffer[1] = 0;     // Stratum, or type of clock
  packetBuffer[2] = 6;     // Polling Interval
  packetBuffer[3] = 0xEC;  // Peer Clock Precision
  // 8 bytes of zero for Root Delay & Root Dispersion
  packetBuffer[12]  = 49;
  packetBuffer[13]  = 0x4E;
  packetBuffer[14]  = 49;
  packetBuffer[15]  = 52;

  //Serial.println("3");

  // all NTP fields have been given values, now
  // you can send a packet requesting a timestamp:
  Udp.beginPacket(address, 123); //NTP requests are to port 123
  //Serial.println("4");
  Udp.write(packetBuffer, NTP_PACKET_SIZE);
  //Serial.println("5");
  Udp.endPacket();
  //Serial.println("6");
}
*/

// Prints Wifi connection status, SSID, IP and RSSI to console
void printWifiStatus() {
  /*
    typedef enum {
      WL_NO_SHIELD = 255,
            WL_NO_MODULE = WL_NO_SHIELD,
            WL_IDLE_STATUS = 0,
            WL_NO_SSID_AVAIL,
            WL_SCAN_COMPLETED,
            WL_CONNECTED,
            WL_CONNECT_FAILED,
            WL_CONNECTION_LOST,
            WL_DISCONNECTED,
            WL_AP_LISTENING,
            WL_AP_CONNECTED,
            WL_AP_FAILED
    } wl_status_t;
  */
  Serial.print("WiFi Status: ");
  Serial.println(WiFi.status());

  // print the SSID of the network you're attached to:
  Serial.print("SSID: ");
  Serial.println(WiFi.SSID());

  // print your board's IP address:
  IPAddress ip = WiFi.localIP();
  Serial.print("IP Address: ");
  Serial.println(ip);

  // print the received signal strength:
  long rssi = WiFi.RSSI();
  Serial.print("signal strength (dBm): ");
  Serial.println(rssi);
}

/* ======================================================================
   Settings persistence
   ====================================================================== */

// Effective timezone offset in seconds, including daylight saving.
long tzTotalOffset() {
  return (long)settings.tzOffset + (settings.dst ? 3600L : 0L);
}

// Load settings from flash; fall back to factory defaults on first run / struct change.
void loadSettings() {
  clockStore.read(settings);
  if (settings.magic != SETTINGS_MAGIC) {
    settings = DEFAULTS;
    saveSettings();
    Serial.println("Settings: defaults written to flash");
  }
  applySettings();
}

// Write current settings to flash.
void saveSettings() {
  settings.magic = SETTINGS_MAGIC;
  clockStore.write(settings);
}

// Push settings into the runtime globals that drive the clock.
void applySettings() {
  loopTime = settings.animSpeed;
  for (uint8_t i = 0; i < 6; i++) { animDirection[i] = (int8_t)settings.dir[i]; }
  syncTimeHour   = settings.syncHour;
  syncTimeMinute = settings.syncMinute;
  // Enforce full colors (the trail is dimmed at render time, not by the swatch).
  normalizeColorFull(settings.digitR, settings.digitG, settings.digitB);
  normalizeColorFull(settings.trailR, settings.trailG, settings.trailB);
}

// Scale a base color by an explicit brightness (0..255). Linear scaling keeps the
// hue (channel ratios); rounding (+127) instead of truncating reduces drift when
// dimming into the low end of the panel's 5-bit range.
uint16_t scaledColorB(uint8_t r, uint8_t g, uint8_t b, uint8_t bright) {
  return matrix.color565(((uint16_t)r * bright + 127) / 255,
                         ((uint16_t)g * bright + 127) / 255,
                         ((uint16_t)b * bright + 127) / 255);
}

// Scale a base color by the brightness actually used for rendering.
uint16_t scaledColor(uint8_t r, uint8_t g, uint8_t b) {
  return scaledColorB(r, g, b, effectiveBrightness);
}

// Effective brightness of the fly-in (trail) color. The eye is roughly gamma
// 2.2, so a *linear* fraction of the master brightness hardly looks dimmer
// (50% linear ~= 73% perceived). We dim perceptually instead - using the same
// FADE_GAMMA as the brightness fade - so the trail actually appears at
// TRAIL_BRIGHTNESS_PCT of the digit's perceived brightness. Floored so it never
// quantises to black, capped so it is never brighter than the digit.
const uint8_t TRAIL_BRIGHTNESS_PCT = 50; // trail = ~50% of the digit's PERCEIVED brightness
const uint8_t TRAIL_BRIGHTNESS_MIN = 24; // absolute floor that keeps the LEDs on
uint8_t trailBrightness() {
  float frac = TRAIL_BRIGHTNESS_PCT / 100.0f;                 // desired perceived fraction
  uint16_t t = (uint16_t)lroundf(powf(frac, FADE_GAMMA) * effectiveBrightness); // -> linear light
  if (t < TRAIL_BRIGHTNESS_MIN) { t = TRAIL_BRIGHTNESS_MIN; }
  if (t > effectiveBrightness)  { t = effectiveBrightness; } // very low brightness -> match the digit
  return (uint8_t)t;
}

// Snap a color to a "full" color (largest channel = 255) while preserving hue.
// Digit and fly-in colors are always full colors now; the trail's dimness comes
// from trailBrightness(), not from a pre-dimmed swatch. This also migrates any
// previously-stored dark/grey color so old settings never render near-black.
void normalizeColorFull(uint8_t &r, uint8_t &g, uint8_t &b) {
  uint8_t m = max(r, max(g, b));
  if (m == 0)   { r = g = b = 255; return; } // black -> white
  if (m == 255) { return; }                  // already a full color
  r = (uint16_t)r * 255 / m;
  g = (uint16_t)g * 255 / m;
  b = (uint16_t)b * 255 / m;
}

/* ======================================================================
   User button: single click = DST, triple click = AP, long press = fade
   ====================================================================== */
void handleButton() {
  if (apActive) { return; } // button disabled while configuring; exit AP via the web page

  bool pressed = (digitalRead(USER_BUTTON_PIN) == LOW);
  unsigned long t = millisNow;

  if (pressed && !btnPrev) {           // press starts
    btnPressStart = t;
    btnLong = false;
  }

  if (pressed && !btnLong && (t - btnPressStart >= BTN_LONGPRESS_MS)) { // becomes a long press
    btnLong = true;
    fadeStart();
  }
  if (pressed && btnLong) {            // hold -> keep fading the brightness
    fadeStep();
  }

  if (!pressed && btnPrev) {           // release
    unsigned long dur = t - btnPressStart;
    if (btnLong) {                     // long press just ended -> keep the faded brightness
      saveSettings();
      btnLong = false;
      btnClicks = 0;
      Serial.print("Brightness set to "); Serial.println(settings.brightness);
    } else if (dur >= BTN_DEBOUNCE_MS) { // a valid short click
      btnClicks++;
      btnLastRelease = t;
    }
  }

  // Evaluate the click sequence once no further click arrived within the window.
  if (!pressed && btnClicks > 0 && (t - btnLastRelease >= BTN_MULTI_GAP_MS)) {
    if (btnClicks >= 3)      { startAPMode(); }
    else if (btnClicks == 2) { toggleAutoBright(); }
    else if (btnClicks == 1) { toggleDST(); }
    btnClicks = 0;
  }

  btnPrev = pressed;
}

// Toggle daylight saving and shift the running clock by +/- 1 hour immediately.
void toggleDST() {
  if (settings.dst) { adjustTime(-3600); settings.dst = 0; }
  else              { adjustTime( 3600); settings.dst = 1; }
  saveSettings();
  Serial.print("DST toggled -> "); Serial.println(settings.dst);
}

// Double click: toggle the BH1750 auto-brightness on/off (lets you fall back to
// the manual brightness / long-press fade without opening the config page).
void toggleAutoBright() {
  settings.autoBright = settings.autoBright ? 0 : 1;
  saveSettings();
  Serial.print("Auto-brightness -> "); Serial.println(settings.autoBright);
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

/* ======================================================================
   WLAN access point configuration mode
   ====================================================================== */
void startAPMode() {
  if (apActive) { return; }
  Serial.println("Starting config AP...");
  WiFi.end();
  delay(100);
  WiFi.beginAP(AP_SSID, AP_PASS);
  unsigned long t0 = millis();
  while (WiFi.status() != WL_AP_LISTENING && millis() - t0 < 6000) { delay(100); }
  apServer.begin();
  dnsUdp.begin(DNS_PORT); // captive portal DNS
  apActive = true;
  drawAPScreen();
  Serial.print("AP status: "); Serial.println(WiFi.status());
}

// Show AP connection info on the matrix. Switches to landscape (64x32) because
// the portrait width is too narrow for the SSID/password text.
// Use setRotation(2) instead of (0) if the text appears upside down.
void drawAPScreen() {
  matrix.setRotation(0); // landscape, 64 wide x 32 tall
  matrix.fillScreen(0);
  matrix.setFont(&Picopixel);
  matrix.setTextColor(matrix.color565(0, 0, 200));
  matrix.setCursor(0, 8);  matrix.print("SSID "); matrix.print(AP_SSID);
  matrix.setTextColor(matrix.color565(0, 140, 0));
  matrix.setCursor(0, 18); matrix.print("PW "); matrix.print(AP_PASS);
  matrix.setTextColor(matrix.color565(150, 70, 0));
  matrix.setCursor(0, 28); matrix.print("IP "); matrix.print(apIP);
  matrix.show();
}

// Minimal DNS server: answer every query with the AP IP so any hostname the
// phone looks up (e.g. its connectivity-check host) resolves to us. Drains the
// whole receive queue each call so a burst of queries is answered promptly.
void handleDNS() {
  for (uint8_t guard = 0; guard < 10; guard++) {
    int pktLen = dnsUdp.parsePacket();
    if (pktLen <= 0) { break; }
    int n = dnsUdp.read(dnsBuffer, sizeof(dnsBuffer));
    if (n < 12) { continue; } // smaller than a DNS header -> ignore

    // Walk the question's QNAME to find where the answer can be appended.
    int qpos = 12;
    while (qpos < n && dnsBuffer[qpos] != 0) { qpos += dnsBuffer[qpos] + 1; }
    qpos += 1 + 4; // null label + QTYPE(2) + QCLASS(2)
    if (qpos > n || qpos + 16 > (int)sizeof(dnsBuffer)) { continue; }

    // Turn the request into a response in place.
    dnsBuffer[2] = 0x81; dnsBuffer[3] = 0x80; // QR=1, RD copied, RA=1
    dnsBuffer[6] = 0x00; dnsBuffer[7] = 0x01; // ANCOUNT = 1
    dnsBuffer[8] = 0x00; dnsBuffer[9] = 0x00; // NSCOUNT = 0
    dnsBuffer[10] = 0x00; dnsBuffer[11] = 0x00; // ARCOUNT = 0 (drop any EDNS/extra records)

    int p = qpos;                                  // append the answer right after the question
    dnsBuffer[p++] = 0xC0; dnsBuffer[p++] = 0x0C;  // NAME -> pointer to QNAME at offset 12
    dnsBuffer[p++] = 0x00; dnsBuffer[p++] = 0x01;  // TYPE  A
    dnsBuffer[p++] = 0x00; dnsBuffer[p++] = 0x01;  // CLASS IN
    dnsBuffer[p++] = 0x00; dnsBuffer[p++] = 0x00; dnsBuffer[p++] = 0x00; dnsBuffer[p++] = 0x3C; // TTL 60s
    dnsBuffer[p++] = 0x00; dnsBuffer[p++] = 0x04;  // RDLENGTH 4
    dnsBuffer[p++] = apIP[0]; dnsBuffer[p++] = apIP[1]; dnsBuffer[p++] = apIP[2]; dnsBuffer[p++] = apIP[3];

    dnsUdp.beginPacket(dnsUdp.remoteIP(), dnsUdp.remotePort());
    dnsUdp.write(dnsBuffer, p);
    dnsUdp.endPacket();
  }
}

// Tiny 204 response for the /live AJAX preview requests (no body needed).
void sendNoContent(WiFiClient &c) {
  c.println("HTTP/1.1 204 No Content");
  c.println("Connection: close");
  c.println();
}

// Lightweight 302 to the portal. Used for captive-portal probe URLs so the OS
// shows the "sign in to network" prompt without us shipping the whole form for
// every probe (which floods the few NINA sockets).
void sendCaptiveRedirect(WiFiClient &c) {
  c.println("HTTP/1.1 302 Found");
  c.print("Location: http://"); c.print(apIP); c.println("/");
  c.println("Content-Length: 0");
  c.println("Connection: close");
  c.println();
}

// Serve one web client per call (non-blocking between clients).
void handleAP() {
  handleDNS(); // keep the captive-portal DNS responsive

  WiFiClient client = apServer.available();
  if (!client) { return; }

  apShowClock(); // a client is talking to us -> reliably switch to the live clock

  String reqLine = client.readStringUntil('\n'); // "GET /path?query HTTP/1.1\r"
  // discard the remaining request headers
  while (client.connected()) {
    String h = client.readStringUntil('\n');
    if (h.length() == 0 || h == "\r") { break; }
  }

  // Parse the request target out of "METHOD <target> HTTP/1.1"
  int sp1 = reqLine.indexOf(' ');
  int sp2 = reqLine.indexOf(' ', sp1 + 1);
  String target = (sp1 >= 0 && sp2 > sp1) ? reqLine.substring(sp1 + 1, sp2) : String("/");
  String path = target, query = "";
  int q = target.indexOf('?');
  if (q >= 0) { path = target.substring(0, q); query = target.substring(q + 1); }

  if (path.startsWith("/save")) {
    applyParams(query);
    saveSettings();
    sendSavedPage(client);
    client.stop();
    delay(300);
    NVIC_SystemReset(); // reboot so the new timezone/WiFi settings take full effect
  } else if (path.startsWith("/live")) {
    applyLiveParams(query); // live preview: apply to RAM only, no save, no reboot
    sendNoContent(client);
    client.stop();
  } else if (path == "/" || path.startsWith("/index")) {
    sendFormPage(client); // the actual config UI (only served on explicit navigation)
    client.stop();
  } else {
    // Every captive-portal probe (/generate_204, /hotspot-detect.html, /ncsi.txt,
    // ...) gets a small redirect to "/", which triggers the OS "sign in" prompt.
    sendCaptiveRedirect(client);
    client.stop();
  }
}

/* ---- HTTP helpers -------------------------------------------------------- */

uint8_t hexVal(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return 0;
}

String urldecode(const String &s) {
  String out;
  for (unsigned int i = 0; i < s.length(); i++) {
    char c = s.charAt(i);
    if (c == '+') { out += ' '; }
    else if (c == '%' && i + 2 < s.length()) {
      out += (char)((hexVal(s.charAt(i + 1)) << 4) | hexVal(s.charAt(i + 2)));
      i += 2;
    } else { out += c; }
  }
  return out;
}

// Extract a query parameter value (returns "" if absent).
String getParam(const String &query, const String &key) {
  String k = key + "=";
  int idx = query.indexOf(k);
  while (idx >= 0) {
    if (idx == 0 || query.charAt(idx - 1) == '&') { // make sure we matched a whole key
      int start = idx + k.length();
      int end = query.indexOf('&', start);
      if (end < 0) { end = query.length(); }
      return urldecode(query.substring(start, end));
    }
    idx = query.indexOf(k, idx + 1);
  }
  return String();
}

void parseHexColor(const String &s, uint8_t &r, uint8_t &g, uint8_t &b) {
  String t = s;
  if (t.startsWith("#")) { t = t.substring(1); }
  if (t.length() >= 6) {
    r = (hexVal(t[0]) << 4) | hexVal(t[1]);
    g = (hexVal(t[2]) << 4) | hexVal(t[3]);
    b = (hexVal(t[4]) << 4) | hexVal(t[5]);
  }
}

String toHex(uint8_t r, uint8_t g, uint8_t b) {
  char buf[8];
  sprintf(buf, "#%02x%02x%02x", r, g, b);
  return String(buf);
}

// Apply submitted form values to the settings struct (with validation).
void applyParams(const String &q) {
  String v;
  v = getParam(q, "tz");     if (v.length()) { settings.tzOffset = (int32_t)v.toInt(); } // seconds, from the dropdown
  settings.dst = (getParam(q, "dst") == "on") ? 1 : 0; // checkbox: absent when unchecked
  v = getParam(q, "bright"); if (v.length()) { settings.brightness = constrain(v.toInt(), 0, 255); }
  v = getParam(q, "speed");  if (v.length()) { settings.animSpeed  = constrain(v.toInt(), 4, 60); }
  v = getParam(q, "digit");  if (v.length()) { parseHexColor(v, settings.digitR, settings.digitG, settings.digitB); }
  v = getParam(q, "trail");  if (v.length()) { parseHexColor(v, settings.trailR, settings.trailG, settings.trailB); }
  for (int i = 0; i < 6; i++) {
    v = getParam(q, "dir" + String(i));
    if (v.length()) { settings.dir[i] = constrain(v.toInt(), 0, 3); }
  }
  v = getParam(q, "synch");  if (v.length()) { settings.syncHour   = constrain(v.toInt(), 0, 23); }
  v = getParam(q, "syncm");  if (v.length()) { settings.syncMinute = constrain(v.toInt(), 0, 59); }
  settings.autoBright = (getParam(q, "autob") == "on") ? 1 : 0; // checkbox: absent when unchecked
  v = getParam(q, "luxd");   if (v.length()) { settings.luxDark   = (uint16_t)constrain(v.toInt(), 0, 65535); }
  v = getParam(q, "luxb");   if (v.length()) { settings.luxBright = (uint16_t)constrain(v.toInt(), 1, 65535); }
  v = getParam(q, "brmin");  if (v.length()) { settings.brightMin = constrain(v.toInt(), 0, 255); }
  v = getParam(q, "brmax");  if (v.length()) { settings.brightMax = constrain(v.toInt(), 0, 255); }
}

// Live preview (/live): apply only the visual settings to RAM, no flash write,
// no reboot. The clock is being rendered every frame, so changes show instantly.
void applyLiveParams(const String &q) {
  String v;
  v = getParam(q, "bright"); if (v.length()) { settings.brightness = constrain(v.toInt(), 0, 255); }
  v = getParam(q, "speed");  if (v.length()) { settings.animSpeed = constrain(v.toInt(), 4, 60); loopTime = settings.animSpeed; }
  v = getParam(q, "digit");  if (v.length()) { parseHexColor(v, settings.digitR, settings.digitG, settings.digitB); }
  v = getParam(q, "trail");  if (v.length()) { parseHexColor(v, settings.trailR, settings.trailG, settings.trailB); }
}

void sendHttpHeader(WiFiClient &c) {
  c.println("HTTP/1.1 200 OK");
  c.println("Content-Type: text/html; charset=utf-8");
  c.println("Connection: close");
  c.println();
}

// Helper: print one <option> with the right "selected" attribute.
void printDirOption(WiFiClient &c, uint8_t cur, uint8_t val, const char *label) {
  c.print("<option value=\""); c.print(val); c.print("\"");
  if (cur == val) { c.print(" selected"); }
  c.print(">"); c.print(label); c.println("</option>");
}

// Common timezone choices. value = base UTC offset in seconds (DST is a separate
// checkbox). The current setting is pre-selected.
struct TzOption { int32_t off; const char *label; };
const TzOption TZONES[] = {
  {-39600, "(UTC-11:00) Midway"},
  {-36000, "(UTC-10:00) Hawaii"},
  {-32400, "(UTC-09:00) Alaska"},
  {-28800, "(UTC-08:00) Pacific (LA)"},
  {-25200, "(UTC-07:00) Mountain (Denver)"},
  {-21600, "(UTC-06:00) Central (Chicago)"},
  {-18000, "(UTC-05:00) Eastern (New York)"},
  {-14400, "(UTC-04:00) Atlantic"},
  {-10800, "(UTC-03:00) Buenos Aires"},
  {  -3600, "(UTC-01:00) Azores"},
  {      0, "(UTC+00:00) London, Lisbon"},
  {   3600, "(UTC+01:00) Berlin, Paris"},
  {   7200, "(UTC+02:00) Athens, Cairo"},
  {  10800, "(UTC+03:00) Moscow, Istanbul"},
  {  12600, "(UTC+03:30) Tehran"},
  {  14400, "(UTC+04:00) Dubai"},
  {  18000, "(UTC+05:00) Karachi"},
  {  19800, "(UTC+05:30) India"},
  {  21600, "(UTC+06:00) Dhaka"},
  {  25200, "(UTC+07:00) Bangkok"},
  {  28800, "(UTC+08:00) Beijing, Singapore"},
  {  32400, "(UTC+09:00) Tokyo, Seoul"},
  {  34200, "(UTC+09:30) Adelaide"},
  {  36000, "(UTC+10:00) Sydney"},
  {  39600, "(UTC+11:00) Solomon Is."},
  {  43200, "(UTC+12:00) Auckland"},
};

void printTzOptions(WiFiClient &c) {
  for (unsigned int i = 0; i < sizeof(TZONES) / sizeof(TZONES[0]); i++) {
    c.print("<option value="); c.print(TZONES[i].off);
    if (TZONES[i].off == settings.tzOffset) { c.print(" selected"); }
    c.print(">"); c.print(TZONES[i].label); c.println("</option>");
  }
}

void sendFormPage(WiFiClient &c) {
  sendHttpHeader(c);
  c.println("<!DOCTYPE html><html><head><meta charset=utf-8>");
  c.println("<meta name=viewport content=\"width=device-width,initial-scale=1\">");
  c.println("<title>Matrix Clock</title><style>");
  c.println("body{font-family:sans-serif;background:#111;color:#eee;margin:0;padding:16px}");
  c.println("h1{font-size:20px}label{display:block;margin:12px 0 4px}");
  c.println("input,select{width:100%;max-width:320px;padding:6px;font-size:16px;box-sizing:border-box}");
  c.println("input[type=checkbox]{width:auto}.row{display:flex;gap:8px;max-width:320px}");
  c.println(".row>div{flex:1}button{margin-top:18px;padding:10px 18px;font-size:16px;background:#06c;color:#fff;border:0;border-radius:4px}");
  c.println(".swbox{max-width:320px}.sw{display:inline-block;width:30px;height:30px;margin:3px;border-radius:5px;border:2px solid #333;cursor:pointer;vertical-align:middle}.sw.sel{border-color:#fff;box-shadow:0 0 0 2px #06c}");
  c.println("</style></head><body><h1>Matrix Clock Settings</h1>");
  c.println("<p style=\"color:#8c8;font-size:13px;margin:0 0 8px\">Brightness, colors and speed preview live on the clock.</p>");
  c.println("<form action=\"/save\" method=get>");

  // Timezone + DST
  c.print("<label>Timezone</label><select name=tz>");
  printTzOptions(c);
  c.println("</select>");
  c.print("<label><input type=checkbox name=dst ");
  if (settings.dst) { c.print("checked"); }
  c.println("> Daylight saving (+1h)</label>");

  // Brightness (live). Manual mode: absolute brightness. Auto mode: relative trim
  // around the sensor value (128 = neutral, lower = darker, higher = brighter).
  c.print("<label>Brightness (0-255; auto mode: 128 = neutral)</label>");
  c.print("<input type=range min=0 max=255 name=bright value="); c.print(settings.brightness);
  c.println(" oninput=\"this.nextElementSibling.value=this.value;live()\">");
  c.print("<output>"); c.print(settings.brightness); c.println("</output>");

  // Auto-brightness (BH1750 light sensor). Maps lux -> brightness; when on, the
  // sensor overrides the manual brightness above once per second.
  c.print("<label><input type=checkbox name=autob ");
  if (settings.autoBright) { c.print("checked"); }
  c.println("> Auto brightness (light sensor)</label>");
  c.println("<div class=row>");
  c.print("<div><label>Dark lux</label><input type=number min=0 max=65535 name=luxd value="); c.print(settings.luxDark); c.println("></div>");
  c.print("<div><label>Bright lux</label><input type=number min=1 max=65535 name=luxb value="); c.print(settings.luxBright); c.println("></div>");
  c.println("</div><div class=row>");
  c.print("<div><label>Min brightness</label><input type=number min=0 max=255 name=brmin value="); c.print(settings.brightMin); c.println("></div>");
  c.print("<div><label>Max brightness</label><input type=number min=0 max=255 name=brmax value="); c.print(settings.brightMax); c.println("></div>");
  c.println("</div>");

  // Animation speed (live)
  c.print("<label>Animation speed (frame time ms, small=fast)</label>");
  c.print("<input type=number min=4 max=60 name=speed value="); c.print(settings.animSpeed);
  c.println(" oninput=live() onchange=live()>");

  // Colors (live) - palette swatches instead of a free color picker (5-bit panel)
  c.print("<label>Digit color</label><div class=swbox id=swDigit></div><input type=hidden name=digit value=");
  c.print(toHex(settings.digitR, settings.digitG, settings.digitB)); c.println(">");
  c.print("<label>Fly-in color (auto-dimmed)</label><div class=swbox id=swTrail></div><input type=hidden name=trail value=");
  c.print(toHex(settings.trailR, settings.trailG, settings.trailB)); c.println(">");

  // Animation directions
  const char *names[6] = {"Hours tens", "Hours ones", "Min tens", "Min ones", "Sec tens", "Sec ones"};
  c.println("<label>Fly-in direction per digit</label>");
  for (int i = 0; i < 6; i++) {
    c.print("<div style=\"margin-bottom:6px\">"); c.print(names[i]);
    c.print(" <select name=dir"); c.print(i); c.println(">");
    printDirOption(c, settings.dir[i], 0, "from top");
    printDirOption(c, settings.dir[i], 1, "from right");
    printDirOption(c, settings.dir[i], 2, "from bottom");
    printDirOption(c, settings.dir[i], 3, "from left");
    c.println("</select></div>");
  }

  // NTP sync time
  c.println("<label>NTP sync time</label><div class=row>");
  c.print("<div><input type=number min=0 max=23 name=synch value="); c.print(settings.syncHour); c.println("></div>");
  c.print("<div><input type=number min=0 max=59 name=syncm value="); c.print(settings.syncMinute); c.println("></div></div>");

  c.println("<button type=submit>Save &amp; Restart</button>");
  c.println("</form>");

  // Live preview: throttle the flood of slider events, but always send a trailing
  // update so the final value lands. Colors are URL-encoded (# -> %23).
  c.println("<script>");
  c.println("var _t=0,_p=null;");
  c.println("function _send(){var g=function(n){return document.getElementsByName(n)[0].value;};");
  c.println("fetch('/live?bright='+g('bright')+'&speed='+g('speed')+'&digit='+encodeURIComponent(g('digit'))+'&trail='+encodeURIComponent(g('trail'))).catch(function(){});}");
  c.println("function live(){clearTimeout(_p);var n=Date.now();if(n-_t>=120){_t=n;_send();}else{_p=setTimeout(function(){_t=Date.now();_send();},120);}}");
  // Palette swatches: full colors only. The pre-dimmed greys are gone; the fly-in
  // color now dims itself relative to the master brightness so it never goes black.
  c.println("var PAL=['#ffffff','#ff0000','#ff8000','#ffff00','#80ff00','#00ff00','#00ff80','#00ffff','#00c0ff','#0000ff','#8000ff','#ff00ff','#ff0080','#ff80c0'];");
  c.println("function mkSw(boxId,name){var box=document.getElementById(boxId),cur=document.getElementsByName(name)[0].value.toLowerCase();");
  c.println("PAL.forEach(function(col){var s=document.createElement('span');s.className='sw'+(col===cur?' sel':'');s.style.background=col;");
  c.println("s.onclick=function(){document.getElementsByName(name)[0].value=col;box.querySelectorAll('.sw').forEach(function(e){e.className='sw';});s.className='sw sel';live();};");
  c.println("box.appendChild(s);});}");
  c.println("mkSw('swDigit','digit');mkSw('swTrail','trail');");
  c.println("</script>");
  c.println("</body></html>");
}

void sendSavedPage(WiFiClient &c) {
  sendHttpHeader(c);
  c.println("<!DOCTYPE html><html><head><meta charset=utf-8>");
  c.println("<meta name=viewport content=\"width=device-width,initial-scale=1\">");
  c.println("<title>Saved</title><style>body{font-family:sans-serif;background:#111;color:#eee;padding:24px}</style>");
  c.println("</head><body><h1>Saved</h1>");
  c.println("<p>Settings have been saved. The clock restarts and reconnects to the WiFi.</p>");
  c.println("</body></html>");
}
