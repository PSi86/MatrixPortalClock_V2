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

#include "board_hal.h"      // board detection: pins, WiFi/NTP, settings storage, reset
#include "clock_time.h"     // software clock: seconds since 1970 (local time), C library only

#include <Adafruit_Protomatter.h>
//#include <Fonts/FreeSansBold12pt7b.h> // Large friendly font works
//#include <Fonts/FreeMonoBold12pt7b.h> // Large friendly font
//#include <Fonts/FreeSans12pt7b.h> // Large friendly font
#include <Fonts/FreeSansBold12pt7b.h> // Large friendly font
#include <Fonts/FreeSansBold9pt7b.h> // Large friendly font
#include <Fonts/Picopixel.h>

#if WATCHFACE_TETRIS
#include "tetris_digits.h"     // generated block tables for the Tetris watchface
#endif

#include <math.h>              // powf() for perceptual brightness fade

#include <Wire.h>              // I2C for the onboard accelerometer + light sensor
#include <Adafruit_Sensor.h>  // sensor base class
#include <Adafruit_LIS3DH.h>  // onboard LIS3DH accelerometer (both boards)
#include <BH1750FVI.h>         // external BH1750 ambient light sensor (auto-brightness)

/* ----------------------------------------------------------------------
The RGB matrix must be wired to VERY SPECIFIC pins, different for each
microcontroller board. board_hal.h picks the set that belongs to the board
being built for (MatrixPortal M4 or MatrixPortal S3).
------------------------------------------------------------------------- */

uint8_t rgbPins[]  = MATRIX_RGB_PINS;
uint8_t addrPins[] = MATRIX_ADDR_PINS;
uint8_t clockPin   = MATRIX_CLOCK_PIN;
uint8_t latchPin   = MATRIX_LATCH_PIN;
uint8_t oePin      = MATRIX_OE_PIN;

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
// Layout revision inside a valid blob. Fields added later go into spare bytes and
// are migrated in loadSettings() by revision, so the magic can stay the same and
// an older firmware keeps reading (and round-tripping) the settings it knows.
#define SETTINGS_REV   1        // 0 = before the watchface byte existed

struct Settings {
  uint16_t magic;        // validity marker
  uint8_t  settingsRev;  // layout revision, see SETTINGS_REV (was padding in rev 0)
  uint8_t  watchface;    // WATCHFACE_CLASSIC or WATCHFACE_TETRIS_ID
  int32_t  tzOffset;     // base UTC offset in seconds, WITHOUT daylight saving (CET = 3600)
  uint8_t  dst;          // daylight saving mode: DST_WINTER, DST_SUMMER (fixed) or DST_AUTO
  uint8_t  brightness;   // 0..255 master intensity (scales all drawn colors)
  uint8_t  animSpeed;    // ms per animation pixel (lower = faster animation)
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

// settingsRev and watchface went into the two padding bytes that sat between
// magic and tzOffset, so the struct keeps the size the stored blobs have. That
// matters: SettingsStore::read() on the S3 accepts an NVS entry only at exactly
// sizeof(Settings), and a mismatch would silently reset EVERY setting.
static_assert(sizeof(Settings) == 32,
              "Settings must stay 32 bytes - stored blobs are read back by exact size");

// Watchfaces (Settings::watchface).
const uint8_t WATCHFACE_CLASSIC   = 0;  // six flying GFX-font digits, HH MM SS
const uint8_t WATCHFACE_TETRIS_ID = 1;  // HH:MM built from falling tetromino blocks
const uint8_t WATCHFACE_COUNT     = 2;

// Daylight saving modes (Settings::dst). 0 and 1 keep the meaning the byte had
// before the automatic mode existed, so stored settings stay valid.
const uint8_t DST_WINTER = 0;  // standard time, fixed
const uint8_t DST_SUMMER = 1;  // daylight saving time (+1 h), fixed
const uint8_t DST_AUTO   = 2;  // follow the daylight-saving rule of the selected timezone

// Factory defaults reproduce the original hard-coded behaviour.
const Settings DEFAULTS = {
  SETTINGS_MAGIC,
  SETTINGS_REV,
  WATCHFACE_CLASSIC,
  3600,            // CET base (UTC+1)
  DST_AUTO,        // CET/CEST switched automatically (the original was a fixed UTC+2)
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

// The settings are stored OUTSIDE the program image so they survive a firmware
// upload, not just a restart: a fixed flash block on the M4, NVS on the S3.
// See board_hal.h for why each board needs its own backend.
SettingsStore<Settings> clockStore;

// User button / interaction ------------------------------------------------
// USER_BUTTON_PIN comes from board_hal.h (M4: D2, S3: GPIO6) - the S3 drives the
// matrix clock on D2. Active LOW with the internal pull-up on both boards.
const unsigned long BTN_DEBOUNCE_MS  = 25;   // ignore bounces shorter than this
const unsigned long BTN_LONGPRESS_MS = 600;  // hold longer than this -> brightness fade
const unsigned long BTN_MULTI_GAP_MS = 400;  // window to collect a click sequence
bool          btnPrev = false;
unsigned long btnPressStart = 0, btnLastRelease = 0;
uint8_t       btnClicks = 0;
bool          btnLong = false;

// Button feedback ----------------------------------------------------------
// The red board LED (FEEDBACK_LED_PIN) - and, if enabled, a small square in the
// bottom-right corner of the matrix - is lit while the button is held. Once a
// click sequence has triggered its function it blinks once per click, so 1x /
// 2x / 3x can be told apart. A sequence that triggers nothing (1x/2x while the
// config AP is up) gets no confirmation.
#define BUTTON_FEEDBACK_ON_MATRIX 1          // 0 = board LED only
// Dark pause before the first confirmation blink, on top of BTN_MULTI_GAP_MS
// (the function itself still triggers after the gap). Separates the blinks
// clearly from the last button press so they are easy to count.
const unsigned long FB_LEAD_IN_MS   = 250;
const unsigned long FB_BLINK_ON_MS  = 150;
const unsigned long FB_BLINK_OFF_MS = 250;
uint8_t       fbBlinkCount = 0;              // confirmation blinks of the running sequence
unsigned long fbBlinkStart = 0;              // millis() when that sequence started
bool          fbLit = false;                 // feedback state of this loop (LED and matrix)
bool          apScreenFbLit = false;         // feedback state the static AP info screen shows

// Perceptual brightness fade (long press) ----------------------------------
const float FADE_PERIOD_MS = 2500.0f; // time for a full 0..1 perceptual sweep
const float FADE_GAMMA     = 2.2f;    // perceptual -> linear-light exponent
float       fadePhase = 1.0f;         // perceptual position 0..1 (linear to the human eye)
int8_t      fadeDir   = -1;
unsigned long fadeLastMs = 0;
unsigned long dstMsgUntil = 0;        // show the daylight-saving banner until this millis()
// Set by every screen that puts something other than the Tetris watchface on the
// panel (boot messages, the daylight-saving banner, the AP info screen, the
// classic watchface). The Tetris watchface skips redrawing a picture that has
// not changed, so it needs to know when someone else has overwritten it.
bool tetrisPanelStale = true;
// Boot breadcrumb stages, written to flash while starting (see board_hal.h).
const uint8_t BOOT_STAGE_CLEAR = 0;   // the last run was healthy
const uint8_t BOOT_STAGE_START = 1;   // setup() entered
const uint8_t BOOT_STAGE_PANEL = 2;   // panel and sensors up
const uint8_t BOOT_STAGE_WIFI  = 3;   // joining the WiFi
const uint8_t BOOT_STAGE_RUN   = 4;   // WiFi joined, clock running
uint8_t prevBootStage = BOOT_STAGE_CLEAR;
bool bootStageCleared = false;        // breadcrumb cleared after the first healthy seconds
bool dstAutoActive = false;           // DST_AUTO: whether summer time is in effect now (derived from UTC, not stored)

// WLAN access point config mode --------------------------------------------
#define AP_SSID "MatrixClock"
#define AP_PASS "clock1234"   // must be >= 8 characters
bool        apActive = false;
bool        apClientConnected = false; // true once a client talks to us (station joined / HTTP hit)
unsigned long apStatusLast = 0;        // last time the AP connection status was polled
unsigned long apClockLast = 0;         // last live-preview clock frame (throttled in AP mode)
// While a client is connected serving the web UI has priority, so the clock
// preview is throttled to the rate the board's radio can spare (board_hal.h:
// 5 fps on the M4's SPI-attached NINA, not throttled on the S3).
const unsigned long AP_PREVIEW_MS = AP_PREVIEW_INTERVAL_MS;
WiFiServer  apServer(80);
// Captive portal: a tiny DNS server answers every lookup with the AP IP so the
// phone's connectivity check is hijacked and the config page pops up by itself.
IPAddress      apIP(AP_IP_ADDR);    // 4.3.2.1 on both boards, see board_hal.h; re-read once the radio is up
WiFiUDP        dnsUdp;
const uint16_t DNS_PORT = 53;
byte           dnsBuffer[512];

// Compile-time switch: buffer each HTTP response in RAM and push it out in a few
// big client.write() chunks instead of ~100 tiny print() calls. On the M4 every
// write() is a full SPI round trip to the NINA co-processor (further slowed by
// the matrix refresh interrupt), so unbuffered serving takes many seconds per
// page load; on the S3 it still saves one lwIP send per line.
// Set to 0 to fall back to direct, unbuffered writes.
#define AP_BUFFERED_SEND 1

// Compile-time switch: watchdog that re-creates the AP when the radio status
// says it is gone. This only ever mattered on the M4, whose NINA co-processor
// can crash and reboot under captive-portal probe load; the S3 hosts the AP on
// the main MCU, so there is no separate module to lose.
// DISABLED: on nina-fw 3.3.0 / WiFiNINA 2.0.1 the status reads are so
// unreliable in AP mode (SPI timeouts return 255 while the module is busy)
// that the watchdog tears down a healthy AP over and over - the phone gets
// kicked before the captive-portal check ever completes. Set to 1 to re-enable.
#define AP_WATCHDOG_ENABLE 0

// CAUTION: on the M4 the radio status is unreliable as the sole signal - the
// driver returns 255 whenever the SPI reply times out, which happens
// sporadically while the module is busy serving a client. So a restart
// additionally requires several bad reads in a row AND no recent DNS/HTTP
// traffic (traffic = proof of life).
const unsigned long AP_WATCHDOG_MS       = 2000; // status poll interval
const unsigned long AP_ACTIVITY_GRACE_MS = 8000; // recent traffic vetoes a restart
const uint8_t       AP_BAD_STATUS_LIMIT  = 3;    // consecutive bad polls before restart
unsigned long apWatchdogLast = 0;
unsigned long apLastActivity = 0; // last DNS packet or HTTP client seen
uint8_t       apBadStatus    = 0; // consecutive unhealthy status reads

#if AP_BUFFERED_SEND
// Print adapter that collects the many small print() calls of one HTTP response
// and forwards them to the client in large chunks (one SPI transfer each).
class BufferedWriter : public Print {
 public:
  explicit BufferedWriter(WiFiClient &c) : _c(c), _len(0) {}
  size_t write(uint8_t b) override {
    if (_len >= sizeof(_buf)) { flushBuf(); }
    _buf[_len++] = b;
    return 1;
  }
  size_t write(const uint8_t *data, size_t size) override {
    size_t left = size;
    while (left > 0) {
      if (_len >= sizeof(_buf)) { flushBuf(); }
      size_t n = sizeof(_buf) - _len;
      if (n > left) { n = left; }
      memcpy(_buf + _len, data, n);
      _len += n; data += n; left -= n;
    }
    return size;
  }
  void flushBuf() { // send the buffered bytes, honoring partial writes
    size_t off = 0;
    while (off < _len && _c.connected()) {
      size_t sent = _c.write(_buf + off, _len - off);
      if (sent == 0) { break; }
      off += sent;
    }
    _len = 0;
  }
 private:
  WiFiClient &_c;
  size_t _len;
  static uint8_t _buf[2000]; // fits one NINA SPI transfer; only one response is built at a time
};
uint8_t BufferedWriter::_buf[2000];
#endif

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
BH1750FVI     lightMeter(BH1750FVI_DEFAULT_ADDRESS); // I2C address 0x23 (ADDR pin low)
bool          luxOK = false;     // true once the BH1750 was found on I2C
unsigned long luxLast = 0;       // last light-sensor poll (1 Hz throttle)
float         lastLux = -1.0f;   // most recent UNFILTERED lux reading (web UI), -1 = none yet
// Moving average of the lux readings so brightness changes are gentle, not
// jumpy. At the 1 Hz sampling rate a 10-sample window equals a 10 s average.
const uint8_t LUX_AVG_SAMPLES = 10;
float         luxHistory[LUX_AVG_SAMPLES]; // ring buffer of recent lux readings
uint8_t       luxHistIdx = 0;              // next write position
uint8_t       luxHistCount = 0;            // valid samples (ramps up to LUX_AVG_SAMPLES)
// The averaged lux sets a brightness TARGET once per second; the actual master
// brightness is eased toward it every frame so the change is a continuous fade
// instead of a once-per-second step.
uint8_t       autoBrightTarget = 128;       // sensor brightness the fade is easing toward
float         autoBrightCurrent = 128.0f;   // smoothly-eased brightness (sub-unit precision)
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
uint8_t loopTime = 12; // ms per animation pixel (PANEL_PACED_LOOP: rounded to whole panel refreshes)
uint16_t panelHz = 0;            // measured panel refresh rate, 0 until the first measurement
unsigned long panelRateLast = 0; // start of the running refresh-rate measurement
uint8_t hourNow, minuteNow, secondNow;
long timeOffset;
bool secondTrigger, minuteTrigger, hourTrigger;
uint8_t syncTimeHour = 5, syncTimeMinute = 11;
unsigned long millisNow, deltaT, lastSync, ntpTimeout = 3000; // ms between NTP fetch retries while unsynced

bool ntpRequestActive, ntpSuccess, wifiEnabled;
bool firstSync=true;
time_t sysTime, ntpTime;

// Network Stuff
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

/* ======================================================================
   Timezones
   ====================================================================== */

// Common timezone choices. value = base UTC offset in seconds, which is what the
// settings store, so every offset appears only once. rule = the zone's POSIX TZ
// string from the IANA tz database (tzdata 2026.4) for zones with daylight saving,
// used by DST_AUTO; nullptr = no daylight saving. The current setting is pre-selected.
struct TzOption { int32_t off; const char *label; const char *rule; };
const TzOption TZONES[] = {
  {-39600, "(UTC-11:00) Midway",              nullptr},
  {-36000, "(UTC-10:00) Hawaii",              nullptr},
  {-32400, "(UTC-09:00) Alaska",              "AKST9AKDT,M3.2.0,M11.1.0"},
  {-28800, "(UTC-08:00) Pacific (LA)",        "PST8PDT,M3.2.0,M11.1.0"},
  {-25200, "(UTC-07:00) Mountain (Denver)",   "MST7MDT,M3.2.0,M11.1.0"},
  {-21600, "(UTC-06:00) Central (Chicago)",   "CST6CDT,M3.2.0,M11.1.0"},
  {-18000, "(UTC-05:00) Eastern (New York)",  "EST5EDT,M3.2.0,M11.1.0"},
  {-14400, "(UTC-04:00) Atlantic (Halifax)",  "AST4ADT,M3.2.0,M11.1.0"},
  {-10800, "(UTC-03:00) Buenos Aires",        nullptr},
  {  -3600, "(UTC-01:00) Azores",             "<-01>1<+00>,M3.5.0/0,M10.5.0/1"},
  {      0, "(UTC+00:00) London, Lisbon",     "GMT0BST,M3.5.0/1,M10.5.0"},
  {   3600, "(UTC+01:00) Berlin, Paris",      "CET-1CEST,M3.5.0,M10.5.0/3"},
  {   7200, "(UTC+02:00) Athens, Helsinki",   "EET-2EEST,M3.5.0/3,M10.5.0/4"},
  {  10800, "(UTC+03:00) Moscow, Istanbul",   nullptr},
  {  12600, "(UTC+03:30) Tehran",             nullptr},
  {  14400, "(UTC+04:00) Dubai",              nullptr},
  {  18000, "(UTC+05:00) Karachi",            nullptr},
  {  19800, "(UTC+05:30) India",              nullptr},
  {  21600, "(UTC+06:00) Dhaka",              nullptr},
  {  25200, "(UTC+07:00) Bangkok",            nullptr},
  {  28800, "(UTC+08:00) Beijing, Singapore", nullptr},
  {  32400, "(UTC+09:00) Tokyo, Seoul",       nullptr},
  {  34200, "(UTC+09:30) Adelaide",           "ACST-9:30ACDT,M10.1.0,M4.1.0/3"},
  {  36000, "(UTC+10:00) Sydney",             "AEST-10AEDT,M10.1.0,M4.1.0/3"},
  {  39600, "(UTC+11:00) Solomon Is.",        nullptr},
  {  43200, "(UTC+12:00) Auckland",           "NZST-12NZDT,M9.5.0,M4.1.0/3"},
};

/* ======================================================================
   Daylight saving
   ====================================================================== */

// POSIX TZ rule of the timezone row with this base offset, or nullptr when the
// zone has no daylight saving (or the offset matches no row).
const char *tzRule(int32_t offset) {
  for (unsigned int i = 0; i < sizeof(TZONES) / sizeof(TZONES[0]); i++) {
    if (TZONES[i].off == offset) { return TZONES[i].rule; }
  }
  return nullptr;
}

// Whether daylight saving is in effect at this UTC instant under a POSIX TZ rule,
// decided by the C library. TZ is set on every call because Arduino's configTime()
// (SNTP start on the S3) resets it to UTC.
bool dstActiveAt(const char *rule, time_t utc) {
  if (!rule) { return false; }
  setenv("TZ", rule, 1);
  tzset();
  struct tm local;
  localtime_r(&utc, &local);
  return local.tm_isdst > 0;
}

bool dstInEffect() {
  if (settings.dst == DST_AUTO) { return dstAutoActive; }
  return settings.dst == DST_SUMMER;
}

// Switch the daylight-saving mode and shift the running clock by the change in
// the effective offset (none, or one hour).
void setDstMode(uint8_t mode) {
  long before = tzTotalOffset();
  time_t utc = clockNow() - before;
  settings.dst = mode;
  if (mode == DST_AUTO) { dstAutoActive = clockIsSet() && dstActiveAt(tzRule(settings.tzOffset), utc); }
  long after = tzTotalOffset();
  if (after != before) { clockAdjust(after - before); }
}

// DST_AUTO, once a minute: follow the rule's changes by shifting the running clock
// (EU: 01:00 UTC on the last Sunday of March and October). Decided on UTC, so the
// hour repeated in autumn does not switch back again.
void updateAutoDst() {
  if (settings.dst != DST_AUTO || !clockIsSet()) { return; }
  bool active = dstActiveAt(tzRule(settings.tzOffset), clockNow() - tzTotalOffset());
  if (active == dstAutoActive) { return; }
  dstAutoActive = active;
  clockAdjust(active ? 3600 : -3600);
  time_t now = clockNow();
  Serial.print(active ? "DST auto -> summer, clock " : "DST auto -> winter, clock ");
  Serial.print(clockHour(now)); Serial.print(':'); Serial.println(clockMinute(now));
}

#if defined(CLOCK_DEBUG)
// Debug build only: check dstActiveAt() against the 2026 changes from the IANA tz
// database (tzdata 2026.4), one second before and at each change, and print the
// result once, 10 s after boot.
void dstSelfTest() {
  struct Check { int32_t off; uint32_t utc; bool dst; };
  static const Check checks[] = {
    // Berlin, London, Azores and Athens all change at 01:00 UTC
    {  3600, 1774745999UL, false }, {  3600, 1774746000UL, true  },
    {  3600, 1792889999UL, true  }, {  3600, 1792890000UL, false },
    {     0, 1774745999UL, false }, {     0, 1774746000UL, true  },
    {     0, 1792889999UL, true  }, {     0, 1792890000UL, false },
    { -3600, 1774745999UL, false }, { -3600, 1774746000UL, true  },
    { -3600, 1792889999UL, true  }, { -3600, 1792890000UL, false },
    {  7200, 1774745999UL, false }, {  7200, 1774746000UL, true  },
    {  7200, 1792889999UL, true  }, {  7200, 1792890000UL, false },
    // New York, Los Angeles: 02:00 local
    {-18000, 1772953199UL, false }, {-18000, 1772953200UL, true  },
    {-18000, 1793512799UL, true  }, {-18000, 1793512800UL, false },
    {-28800, 1772963999UL, false }, {-28800, 1772964000UL, true  },
    {-28800, 1793523599UL, true  }, {-28800, 1793523600UL, false },
    // Sydney, Adelaide, Auckland: southern hemisphere, summer time over New Year
    { 36000, 1775318399UL, true  }, { 36000, 1775318400UL, false },
    { 36000, 1791043199UL, false }, { 36000, 1791043200UL, true  },
    { 34200, 1775320199UL, true  }, { 34200, 1775320200UL, false },
    { 34200, 1791044999UL, false }, { 34200, 1791045000UL, true  },
    { 43200, 1775311199UL, true  }, { 43200, 1775311200UL, false },
    { 43200, 1790431199UL, false }, { 43200, 1790431200UL, true  },
    // Tokyo: no daylight saving
    { 32400, 1774746000UL, false }, { 32400, 1792890000UL, false },
  };
  unsigned int ok = 0, total = 0;
  for (const Check &c : checks) {
    total++;
    if (dstActiveAt(tzRule(c.off), (time_t)c.utc) == c.dst) { ok++; continue; }
    Serial.print("DST self-test FAIL offset "); Serial.print(c.off);
    Serial.print(" utc "); Serial.println(c.utc);
  }
  Serial.print("DST self-test: "); Serial.print(ok); Serial.print('/'); Serial.print(total); Serial.println(" ok");
}
#endif

// SETUP
void setup(void) {
  boardSerialBegin(115200);

  // Boot breadcrumb: what the previous run reached before it ended (see
  // board_hal.h). Read it first, then mark this run as "started".
  prevBootStage = boardBootStageRead();
  boardBootStageWrite(BOOT_STAGE_START);

  pinMode(USER_BUTTON_PIN, INPUT_PULLUP);
  pinMode(FEEDBACK_LED_PIN, OUTPUT);
  digitalWrite(FEEDBACK_LED_PIN, LOW);
  loadSettings(); // pulls brightness/colors/animation/tz from flash (or writes defaults)

  // Onboard LIS3DH accelerometer -> automatic screen rotation. If it is not
  // found the clock simply stays in the default portrait orientation.
  Wire.begin();
  accelOK = lis.begin(ACCEL_I2C_ADDR); // onboard, non-standard address on both boards
  if (accelOK) {
    lis.setRange(LIS3DH_RANGE_2_G);
    lis.setDataRate(LIS3DH_DATARATE_10_HZ);
    Serial.println("LIS3DH found");
  } else {
    Serial.println("LIS3DH not found - orientation locked to portrait");
  }

  // External BH1750 ambient light sensor -> auto-brightness. Shares the I2C bus
  // (default address 0x23). If absent, brightness stays under manual control.
  luxOK = lightMeter.begin();          // true if the sensor answers on I2C
  if (luxOK) {
    lightMeter.powerOn();
    lightMeter.setContHighRes();       // 1 lx resolution, a new value every ~120 ms
    luxOK = (lightMeter.getError() == BH1750FVI_OK);
  }
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
    // DO NOT CONTINUE if matrix setup encountered an error. The delay keeps the
    // ESP32's task watchdog fed so the board reports the error instead of
    // rebooting in a loop.
    for(;;) { delay(1000); }
  }

  matrix.setTextWrap(false);           // Allow text off edge

  // Why did the previous run end? Shown on the panel for 2 s when it was not a
  // normal power-up or reset button, so a crash is visible without a console
  // (e.g. while the clock runs on a power supply).
  Serial.print("Reset reason: "); Serial.println(boardResetReasonText());
  if (!boardResetWasNormal()) {
    bootStatus(boardResetReasonText());
    delay(2000);
  }
  // The previous run did not get far enough to clear its breadcrumb.
  if (prevBootStage != BOOT_STAGE_CLEAR) {
    Serial.print("Previous boot died in stage "); Serial.println(prevBootStage);
    char msg[8];
    snprintf(msg, sizeof(msg), "DIED%u", (unsigned)prevBootStage);
    bootStatus(msg);
    delay(2000);
  }
  boardBootStageWrite(BOOT_STAGE_PANEL);

  bootStatus("CLOCK");

  // Recovery / manual entry: hold the user button during boot to open the config AP
  // even when the home WiFi is unavailable.
  if (digitalRead(USER_BUTTON_PIN) == LOW) {
    startAPMode();
  }

  // Initialize Network...... (skipped while the config AP is running)
  if (!apActive) {
    netRadioInit();
    netPrintRadioInfo();

    // attempt to connect to WiFi network: Connect to WPA/WPA2 network.
    boardBootStageWrite(BOOT_STAGE_WIFI);
    bootStatus("WLAN?");
    Serial.print("Attempting to connect to SSID: ");
    Serial.println(ssid);

    netStaBegin(ssid, pass);
    uint8_t waited = 0;
    while (!netStaConnected()) {
      delay(500);
      if (++waited >= 14) { waited = 0; netStaBegin(ssid, pass); } // re-issue the join every 7 s
    }
    boardBootStageWrite(BOOT_STAGE_RUN);
    bootStatus("WLAN!");
    Serial.println("Connected to WiFi");
    printWifiStatus();
    delay(1000);
  } // end if(!apActive)
}

// MAIN
void loop(void) {
  timekeeper(); // Updates Time variables and gives Triggers for second, minute and hour updates
  updatePanelRate();
  // Ran fine for 15 s: clear the breadcrumb, so only a real early death leaves one.
  if (!bootStageCleared && millisNow > 15000) { bootStageCleared = true; boardBootStageWrite(BOOT_STAGE_CLEAR); }
#if defined(CLOCK_DEBUG)
  static bool dstTested = false;
  if (!dstTested && millisNow > 10000) { dstTested = true; dstSelfTest(); }
#endif
  handleButton(); // single click = daylight-saving mode, 3 clicks = config AP on/off, long press = brightness fade

  if (apActive) {      // config AP running
    apWatchdog();      // re-create the AP if the ESP32 silently rebooted
    handleAP();        // captive-portal DNS + web UI + live settings updates
    updateBrightness();// same brightness logic during AP (info screen + clock preview)
    updateApDisplay(); // AP-info screen until a client connects, then a live clock preview
    return;
  }

  timeSync_WifiLib();
  if (minuteTrigger) { updateAutoDst(); } // follow the timezone's summer/winter time changes
  updateOrientation();  // rotate the display to match how the panel is held
  updateBrightness();   // resolve the brightness for every screen (manual or auto)
  if (millisNow < dstMsgUntil) { drawDstMessage(); } // brief banner after a daylight-saving mode change
  else                         { drawClock(); }
}

// Leave the AP-info screen and show the live clock (called once a client appears).
void apShowClock() {
  if (apClientConnected) { return; }
  apClientConnected = true;
  applyOrientation(detectRotation()); // start the clock preview in the current orientation
}

// While the AP is up: show connection info until a client appears, then switch to
// a throttled live clock so brightness/color/speed changes are visible in real time
// without starving the (slow) WiFiNINA web serving.
void updateApDisplay() {
  if (!apClientConnected) {
    if (millisNow - apStatusLast >= 500) { // periodically check whether a station joined
      apStatusLast = millisNow;
      if (netApHasStation()) { apShowClock(); return; }
      if (apInfoRotation() != curRotation) { drawAPScreen(); } // re-orient the info if turned
    }
#if BUTTON_FEEDBACK_ON_MATRIX
    // The info screen is static, so redraw it whenever the button indicator flips.
    if (fbLit != apScreenFbLit) { drawAPScreen(); }
#endif
    return;
  }
  // A client is connected. Keep the animation advancing every loop so it runs at
  // real time (stepClockAnim is cheap, no panel I/O), but only push the latest
  // state to the panel at the board's preview rate (5 fps on the M4): skipped
  // frames are computed, not slowed down. This leaves the radio free for the
  // slow WiFiNINA web server; the S3 draws every frame.
  updateOrientation(); // clock preview follows the accelerometer (all four rotations)
#if WATCHFACE_TETRIS
  // The Tetris watchface throttles itself (it only repaints on an animation step
  // or a visible change), so it needs no preview rate on top of that.
  if (activeWatchface() == WATCHFACE_TETRIS_ID) { drawTetrisFace(); return; }
#else
  activeWatchface();
#endif
  stepClockAnim();
  if (millisNow - apClockLast >= AP_PREVIEW_MS) {
    apClockLast = millisNow;
    renderClock();
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
// One-shot orientation read: returns the desired rotation (0..3) from gravity, or
// the current rotation when the reading is undecided (panel flat / near the 45 deg
// diagonal) or no sensor is present. Shared by the live updateOrientation() and by
// the boot / AP screens so every screen aligns to the panel the same way.
uint8_t detectRotation() {
  if (!accelOK) { return curRotation; }
  lis.read();
  int16_t ax = lis.x, ay = lis.y;
  int16_t axAbs = abs(ax), ayAbs = abs(ay);
  int16_t hi = max(axAbs, ayAbs), lo = min(axAbs, ayAbs);
  // Too flat (panel face up/down) or too close to the diagonal -> keep current.
  // Require the dominant axis to be >25% larger: hi > lo*1.25  <=>  hi*4 > lo*5.
  if (hi < 2000 || (int32_t)hi * 4 <= (int32_t)lo * 5) { return curRotation; }
  // Gravity quadrant: 0=+X down, 1=-X down, 2=+Y down, 3=-Y down.
  uint8_t quadrant;
  if (axAbs > ayAbs) { quadrant = (ax > 0) ? 0 : 1; }
  else               { quadrant = (ay > 0) ? 2 : 3; }
  // quadrant -> matrix rotation. Re-order these four values if the panel turns
  // the wrong way during on-device calibration (this is the one line to tweak).
  static const uint8_t ORIENT_MAP[4] = {3, 1, 0, 2};
  return ORIENT_MAP[quadrant];
}

// Poll the LIS3DH (throttled) and rotate the clock to match how the panel is held,
// with a debounce so it doesn't flicker near 45 deg.
void updateOrientation() {
  if (!accelOK) { return; }
  if (millisNow - orientLast < ORIENT_POLL_MS) { return; }   // throttle to ~4 Hz
  orientLast = millisNow;
  uint8_t wanted = detectRotation();
  // Debounce: a new orientation must persist a few polls before we commit.
  if (wanted == orientCandidate) { if (orientStable < 255) { orientStable++; } }
  else { orientCandidate = wanted; orientStable = 1; }
  if (wanted != curRotation && orientStable >= ORIENT_DEBOUNCE) {
    Serial.print("Orientation -> rotation "); Serial.println(wanted);
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

// Resolve the brightness used for ALL rendering (clock, boot, AP info, banner)
// with one shared rule, so every screen shows the same brightness as the clock.
// Manual mode / no sensor: the absolute setting. Auto-brightness: the sensor's
// 10 s moving average, mapped, trimmed by settings.brightness (128 = neutral) and
// clamped to >= Min, then eased smoothly. Call once per frame in every path.
void updateBrightness() {
  // Sample the raw ambient light at ~1 Hz whenever a sensor is present, in BOTH
  // modes, so the config UI can always show the live lux value. The first call is
  // forced (luxLast == 0) so the very first frame, e.g. boot, has a real reading.
  // In auto mode the same reading also feeds the 10 s moving average + mapping.
  if (luxOK && (luxLast == 0 || millisNow - luxLast >= 1000)) {
    luxLast = millisNow;
    float lux = lightMeter.getLux();
    if (lightMeter.getError() == BH1750FVI_OK) {     // skip failed I2C reads
      lastLux = lux;                                 // unfiltered reading shown in the web UI
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

  if (!luxOK || !settings.autoBright) { // manual mode (or no sensor)
    effectiveBrightness = settings.brightness;
    autoBrightInit = false;             // re-seed the ease when auto resumes
    return;
  }

  // Relative trim around the sensor value: settings.brightness 128 = neutral,
  // 0 = much darker, 255 = ~2x brighter. Min brightness is a hard floor.
  float relTarget = (float)autoBrightTarget * (float)settings.brightness / 128.0f;
  if (relTarget > 255.0f)              { relTarget = 255.0f; }
  if (relTarget < settings.brightMin)  { relTarget = settings.brightMin; }

  // Ease toward the trimmed target (frame-rate independent) for a smooth fade;
  // seeded to the target on the first call to avoid a jump.
  if (!autoBrightInit) { autoBrightCurrent = relTarget; autoBrightInit = true; autoFadeLastMs = millisNow; }
  float dt = (float)(millisNow - autoFadeLastMs);
  autoFadeLastMs = millisNow;
  autoBrightCurrent += (relTarget - autoBrightCurrent) * (1.0f - expf(-dt / AUTOBRIGHT_TAU_MS));
  effectiveBrightness = (uint8_t)lroundf(autoBrightCurrent);
}

// Draw a short text centred in the current rotation's canvas. Used by the boot
// status screens and the summer/winter banner so they align like the clock.
void drawCenteredText(const char *msg, uint16_t color) {
  matrix.setFont(&Picopixel);
  int16_t x1, y1; uint16_t w, h;
  matrix.getTextBounds(msg, 0, 0, &x1, &y1, &w, &h);
  matrix.fillScreen(0);
  matrix.setTextColor(color);
  matrix.setCursor((matrix.width() - (int)w) / 2 - x1, (matrix.height() - (int)h) / 2 - y1);
  matrix.print(msg);
  drawFeedbackIndicator();
  matrix.show();
  tetrisPanelStale = true;
}

// Boot status line: oriented to the panel and dimmed to the live clock brightness
// (sensor-driven when auto-brightness is on), exactly like every other screen.
void bootStatus(const char *msg) {
  millisNow = millis();                // setup() runs before timekeeper(), so refresh the clock
  applyOrientation(detectRotation());  // follow the accelerometer
  updateBrightness();                  // same brightness logic as the clock
  drawCenteredText(msg, scaledColor(255, 255, 255));
}

// Brief banner shown for ~3 s after the daylight-saving mode changed: "auto",
// "summer" or "winter", in orange while summer time is in effect and in ice-blue
// otherwise, aligned to the accelerometer.
void drawDstMessage() {
  const char *mode = (settings.dst == DST_AUTO)   ? "auto"
                   : (settings.dst == DST_SUMMER) ? "summer" : "winter";
  if (dstInEffect()) { drawCenteredText(mode, scaledColor(255, 165, 0)); }
  else               { drawCenteredText(mode, scaledColor(120, 200, 255)); }
}

// Measure the panel refresh rate once per second (Protomatter counts refreshes).
void updatePanelRate() {
  if (millisNow - panelRateLast < 1000) { return; }
  uint32_t refreshes = matrix.getFrameCount();
  if (panelRateLast != 0) { panelHz = refreshes * 1000UL / (millisNow - panelRateLast); }
  panelRateLast = millisNow;
}

// Loop iterations per animation pixel. With PANEL_PACED_LOOP every iteration is
// one panel refresh, so loopTime (ms per pixel) is rounded to whole refreshes and
// every step stays on the panel equally long. Otherwise the loop itself runs
// every loopTime ms and moves one pixel each time.
uint8_t framesPerStep() {
#if PANEL_PACED_LOOP
  uint32_t hz = panelHz ? panelHz : 166;   // before the first measurement: typical S3 rate
  uint32_t frames = ((uint32_t)loopTime * hz + 500) / 1000;
  return frames ? frames : 1;
#else
  return 1;
#endif
}

// Advance the clock animation state by one step. This is the cheap part - no
// matrix I/O at all - so it can run on EVERY loop iteration to keep the animation
// at real time regardless of how often the panel is actually redrawn. On a second
// tick it rebuilds the digit strings; per digit it starts a fly-in (animTrigger),
// moves an in-flight digit one pixel toward its target every framesPerStep()
// calls, or retires an arrived one.
void stepClockAnim(void) {
  static uint8_t framesInStep = 0;
  if (secondTrigger) { framesInStep = 0; } // fly-ins start on the second tick: step from there
  bool moveNow = (++framesInStep >= framesPerStep());
  if (moveNow) { framesInStep = 0; }

  if (secondTrigger) {
    sprintf(timeStr, "%02d%02d%02d", clockHour(sysTime), clockMinute(sysTime), clockSecond(sysTime));
    sprintf(animStr, "%02d%02d%02d", clockHour(sysTime+1), clockMinute(sysTime+1), clockSecond(sysTime+1));
      //sprintf(animStr, "%02d%02d%02d", hourNow+1, minuteNow+1, secondNow+1 );
    //animShow[4]=false; //right number [5] will be set true on the secondTrigger everytime so resetting it is not necessary
  }
 // if (minuteTrigger) { animShow[2]=false; animShow[3]=false; }
 // if (hourTrigger) { animShow[0]=false; animShow[1]=false; }

  // Concept: Iterate through the digits of the time display. If the animTrigger[i] is true, then create an location offset for the digit according to the fly-in direction that is configured for that digit.
  for (uint8_t i = 0; i < 6; i++) { // 6 displayed digits (HH MM SS); timeStr[6] is the null terminator
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
    else if (animShow[i] && moveNow) {
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
  }
  if(secondTrigger) { Serial.println(deltaT); } // Debugging //timeOffset //deltaT //animTrigger[4] //animShow[i]
}

#if defined(CLOCK_DEBUG)
// Debug build only: frame timing, printed once per second. "frames" is how often
// the clock was rendered, "panel" the refresh rate Protomatter actually reached
// (updatePanelRate()), "draw" the time to build a frame and "show" the time
// show() spent handing it over (with double buffering it waits for the next
// panel refresh).
struct FrameStats { uint32_t frames, drawSum, drawMax, showSum, showMax, since; };
FrameStats frameStats = {};

void frameStatsAdd(uint32_t drawUs, uint32_t showUs) {
  FrameStats &f = frameStats;
  f.frames++;
  f.drawSum += drawUs; if (drawUs > f.drawMax) { f.drawMax = drawUs; }
  f.showSum += showUs; if (showUs > f.showMax) { f.showMax = showUs; }
  if (millis() - f.since < 1000) { return; }
  Serial.printf("frames %lu/s panel %lu Hz draw avg %lu max %lu us show avg %lu max %lu us\n",
                (unsigned long)f.frames, (unsigned long)panelHz,
                (unsigned long)(f.drawSum / f.frames), (unsigned long)f.drawMax,
                (unsigned long)(f.showSum / f.frames), (unsigned long)f.showMax);
  f = FrameStats{};
  f.since = millis();
}
#endif

// NTP sync status pixel (green = synced, red = not), dimmed with the master
// brightness but kept visible. It sits in the bottom-left corner of the current
// rotation: (0,63) in portrait, (0,31) in landscape. A fixed (0,63) lies outside
// the 32 px tall landscape canvas and was silently clipped, so landscape never
// showed the sync status. Every watchface draws it the same way.
void drawStatusPixel() {
  uint8_t statusInt = effectiveBrightness / 6;
  if (statusInt < 3) { statusInt = 3; }
  matrix.drawPixel(0, matrix.height() - 1,
                   ntpSuccess ? matrix.color565(0, statusInt, 0)
                              : matrix.color565(statusInt, 0, 0));
}

/* ======================================================================
   Tetris watchface

   HH:MM built from falling tetromino blocks. The block tables live in
   src/tetris_digits.h and are produced by scripts/gen_tetris_digits.py: every
   digit comes in TETRIS_VARIANTS different tilings of its 6x10 glyph, each
   already ordered so the pieces can be dropped in from above and coloured so
   that no two touching pieces share a colour class.

   Two things happen fresh every time a digit changes: a variant is drawn at
   random, and the colour classes are mapped onto a shuffled palette. So the
   same digit is neither built the same way nor coloured the same way twice.

   The palette is six hues exactly 60 degrees apart. Because the classes are a
   proper colouring and the mapping is injective, touching pieces can never end
   up within 60 degrees of each other - no run-time check needed.
   ====================================================================== */
#if WATCHFACE_TETRIS

const uint8_t TETRIS_CELL  = 2;   // panel pixels per grid cell -> digit 12x20 px
const uint8_t TETRIS_PITCH = 14;  // x distance between the two digits of a group
const uint8_t TETRIS_DOT   = 4;   // colon dot edge length
const uint8_t TETRIS_DROP  = 4;   // cells a piece starts above its own digit box
const uint8_t TETRIS_SETTLE = 2;  // last cells of the fall, never turning any more

// Drop and turn pace are two separate settings, because they are two separate
// things to watch: how fast the blocks come down, and how busily they turn on
// the way. Both are stored in their own blob (see TetrisSettings below).
const uint8_t  TETRIS_DROP_MIN = 20,  TETRIS_DROP_MAX = 250;   // ms per cell
const uint16_t TETRIS_SPIN_MIN = 80,  TETRIS_SPIN_MAX = 1500;  // ms per quarter turn
// One fall step every animSpeed * TETRIS_STEP_MULT ms, so the existing speed
// setting covers 16..240 ms and the default (12) lands at 48 ms. A digit needs
// between 40 and 140 steps, so even the slowest setting finishes a digit well
// inside the minute before it has to change again.
const uint8_t TETRIS_STEP_MULT = 4;

// Six hues 60 degrees apart. Blue is lifted off pure 0000FF, which is too dark
// against the others on the panel, without moving its hue.
const uint8_t TETRIS_PALETTE[TETRIS_COLOUR_CLASSES][3] = {
  {255,   0,   0},   // red
  {255, 255,   0},   // yellow
  {  0, 255,   0},   // green
  {  0, 255, 255},   // cyan
  { 48,  48, 255},   // blue
  {255,   0, 255}    // magenta
};

// Settings of this watchface, in their own flash blob. Keeping them out of the
// main Settings struct is not tidiness: that struct is exactly 32 bytes with no
// padding left, and a firmware that stored 36 would be refused wholesale by any
// older build (Preferences::getBytes returns 0 when the blob is larger than the
// buffer), which would reset every setting the user has. A second key costs
// nothing and leaves the old blob untouched.
#define TETRIS_SETTINGS_MAGIC 0x7E71
#define TETRIS_SETTINGS_REV   1

struct TetrisSettings {
  uint16_t magic;
  uint8_t  rev;
  uint8_t  dropMs;   // milliseconds per cell of fall
  uint16_t spinMs;   // average milliseconds between quarter turns
};

const TetrisSettings TETRIS_DEFAULTS = {
  TETRIS_SETTINGS_MAGIC, TETRIS_SETTINGS_REV,
  70,    // a block falls one cell every 70 ms
  260    // and turns a quarter every 260 ms on average
};

TetrisSettings tetrisSettings;
SettingsStore<TetrisSettings> tetrisStore;

// State of one of the four digits.
struct TetrisDigit {
  uint8_t value;      // 0..9, or 0xFF when nothing has been placed yet
  uint8_t variant;    // which tiling of that digit is being built
  uint8_t piece;      // index of the piece currently falling; == piece count when settled
  uint8_t fall;       // cells that piece still has to drop
  uint8_t turns;      // quarter turns this piece still owes
  unsigned long turnAt[3];  // millis() at which each of them happens
  uint8_t hue[TETRIS_COLOUR_CLASSES];  // colour class -> palette entry
};

TetrisDigit   tetrisDigits[4];
uint16_t      tetrisPal[TETRIS_COLOUR_CLASSES]; // palette at the current brightness
uint8_t       tetrisRotation    = 0xFF;   // rotation the layout was built for
bool          tetrisLandscape   = false;  // orientation the layout was built for
bool          tetrisSettled     = false;  // true once every block has landed
unsigned long tetrisStepLast    = 0;      // millis() of the last animation step
uint8_t       tetrisBrightDrawn = 0xFF;   // effectiveBrightness of the frame on the panel
bool          tetrisColonDrawn  = false;  // colon state of the frame on the panel
bool          tetrisFbDrawn     = false;  // button indicator state of the frame on the panel
uint8_t       tetrisSpinDrawn   = 0;      // turns still owed when that frame was drawn
int           tetrisXHour = 0, tetrisXMin = 0;  // left edge of each digit group
int           tetrisYHour = 0, tetrisYMin = 0;  // top edge of each digit group

// The packed piece record of one piece of one variant.
uint16_t tetrisPieceAt(uint8_t digit, uint8_t variant, uint8_t piece) {
  return TETRIS_VAR[digit][(uint16_t)variant * TETRIS_PIECES_PER_DIGIT[digit] + piece];
}

// Send the next piece on its way: set how far it falls and decide at which
// heights it flicks round a quarter. The heights are spread over the fall with
// a random position inside each band, so the turns look tapped out by hand
// instead of clocked. Nothing turns during the last TETRIS_SETTLE cells, so a
// piece always arrives in the orientation it will keep.
//
// Takes the digit index rather than a reference: the .ino preprocessor injects
// a prototype for every function ahead of the file, so a parameter of a type
// declared here would not be known yet.
void tetrisArmPiece(uint8_t idx) {
  TetrisDigit &s = tetrisDigits[idx];
  s.fall  = TETRIS_PIECE_TOP(tetrisPieceAt(s.value, s.variant, s.piece)) + TETRIS_DROP;
  s.turns = 0;
  if (s.fall <= TETRIS_SETTLE) { return; }

  // How long the piece is in the air before it has to stop turning.
  unsigned long budget = (unsigned long)(s.fall - TETRIS_SETTLE) * tetrisSettings.dropMs;
  unsigned long spin   = tetrisSettings.spinMs;
  unsigned long at     = millisNow;
  unsigned long slots[3];
  uint8_t fits = 0;
  while (fits < 3) {
    // Each gap is half to one and a half times the set interval, so successive
    // flicks are noticeably unevenly spaced instead of metronomic.
    unsigned long gap = spin / 2 + (unsigned long)random(spin);
    if (at + gap - millisNow > budget) { break; }
    at += gap;
    slots[fits++] = at;
  }
  // Not every piece turns, and one that does rarely uses every turn it could
  // have - so some come down flat while others tumble.
  s.turns = (fits == 0) ? 0 : (uint8_t)random(fits + 1);
  for (uint8_t i = 0; i < s.turns; i++) { s.turnAt[i] = slots[i]; }
}

// Quarter turns this digit still owes, which is how far short of its landing
// orientation the falling piece is drawn.
uint8_t tetrisPendingTurns(uint8_t idx) {
  TetrisDigit &s = tetrisDigits[idx];
  uint8_t pending = 0;
  for (uint8_t i = 0; i < s.turns; i++) {
    if ((long)(s.turnAt[i] - millisNow) > 0) { pending++; }
  }
  return pending;
}

// Start building a digit: new tiling, new colours, first piece at the top.
//
// Do NOT add a randomSeed() anywhere for this: on the S3, random() reads the
// hardware generator until randomSeed() is called, and calling it switches the
// core over to a seeded software PRNG (WMath.cpp:44-61). Seeding would make the
// variety worse, not better.
void tetrisStartDigit(uint8_t idx, uint8_t value) {
  TetrisDigit &s = tetrisDigits[idx];
  s.value   = value;
  s.variant = (uint8_t)random(TETRIS_VARIANTS);
  s.piece   = 0;
  tetrisArmPiece(idx);
  for (uint8_t i = 0; i < TETRIS_COLOUR_CLASSES; i++) { s.hue[i] = i; }
  for (uint8_t i = TETRIS_COLOUR_CLASSES - 1; i > 0; i--) {   // Fisher-Yates
    uint8_t j = (uint8_t)random(i + 1);
    uint8_t t = s.hue[i]; s.hue[i] = s.hue[j]; s.hue[j] = t;
  }
}

// Place the digit groups for the current rotation and rebuild all four digits.
// A group is TETRIS_GRID_W x TETRIS_GRID_H cells, so 12 x 20 px, and the two
// digits of a group sit TETRIS_PITCH apart; the coordinates below are the
// top-left corner of each group.
//
// The orientation is taken from curRotation, not from the global isLandscape:
// drawAPScreen() rotates the panel and updates curRotation without going
// through applyOrientation(), so isLandscape can be one step behind.
void tetrisLayout() {
  tetrisRotation  = curRotation;
  tetrisLandscape = (curRotation == 0 || curRotation == 2);
  if (tetrisLandscape) {
    // 64x32: one line. Hours x 2..27, minutes x 36..61, both rows 6..25.
    tetrisXHour = 2;  tetrisXMin = 36;
    tetrisYHour = 6;  tetrisYMin = 6;
  } else {
    // 32x64: hours above minutes, x 3..28, rows 8..27 and 36..55. The minutes
    // start TETRIS_DROP cells above their own box, which is row 28 - exactly
    // the gap below the hours, so the two groups never draw over each other.
    tetrisXHour = 3;  tetrisXMin = 3;
    tetrisYHour = 8;  tetrisYMin = 36;
  }
  for (uint8_t i = 0; i < 4; i++) { tetrisDigits[i].value = 0xFF; }
  tetrisSettled = false;
}

// Hand the current HH:MM to the four digits. Only a digit whose value really
// changed is rebuilt - the others keep the blocks they have already dropped.
void tetrisPushTime() {
  uint8_t h = clockHour(sysTime), m = clockMinute(sysTime);
  uint8_t d[4] = { (uint8_t)(h / 10), (uint8_t)(h % 10),
                   (uint8_t)(m / 10), (uint8_t)(m % 10) };
  for (uint8_t i = 0; i < 4; i++) {
    if (d[i] == tetrisDigits[i].value) { continue; }
    tetrisStartDigit(i, d[i]);
    tetrisSettled = false;
  }
}

// Rescale the palette to the master brightness, so the watchface follows the
// light sensor and the brightness slider like the classic one does.
void tetrisApplyBrightness() {
  for (uint8_t i = 0; i < TETRIS_COLOUR_CLASSES; i++) {
    tetrisPal[i] = scaledColorB(TETRIS_PALETTE[i][0], TETRIS_PALETTE[i][1],
                                TETRIS_PALETTE[i][2], effectiveBrightness);
  }
}

// Move every digit on by one fall step. A piece drops one cell per step; when
// it has arrived the next one starts at the top of the digit box.
void tetrisStep() {
  bool allSettled = true;
  for (uint8_t i = 0; i < 4; i++) {
    TetrisDigit &s = tetrisDigits[i];
    if (s.value > 9) { continue; }
    uint8_t total = TETRIS_PIECES_PER_DIGIT[s.value];
    if (s.piece >= total) { continue; }        // this digit is complete
    allSettled = false;
    if (s.fall > 0) { s.fall--; continue; }
    s.piece++;
    if (s.piece < total) { tetrisArmPiece(i); }
  }
  tetrisSettled = allSettled;
}

// Draw one digit: every piece already dropped, plus the one still falling.
//
// The falling piece spins into place. A quarter turn changes the piece's
// bounding box, so a rotated state can be wider than where it will land - the
// library this replaced simply let those pixels run off the panel (measured: 21
// of its 229 fall states left the six-cell digit box, by up to 6 px, which on
// the rightmost digit meant drawing to x=67 on a 64 px panel). Here the spinning
// piece is kept inside its own digit box instead, the way a real game kicks a
// piece off the wall when you rotate against it. Vertically the piece hangs from
// its landing edge, so its bottom travels smoothly however it is turned.
void tetrisDrawDigit(uint8_t idx, int originX, int originY) {
  TetrisDigit &s = tetrisDigits[idx];
  if (s.value > 9) { return; }
  uint8_t total = TETRIS_PIECES_PER_DIGIT[s.value];
  uint8_t last  = (s.piece < total) ? s.piece : (uint8_t)(total - 1);
  for (uint8_t p = 0; p <= last; p++) {
    uint16_t rec    = tetrisPieceAt(s.value, s.variant, p);
    uint8_t  orient = TETRIS_PIECE_ORIENT(rec);
    int      lift   = (p == s.piece) ? s.fall : 0;   // only the current one is airborne
    int      cx     = TETRIS_PIECE_X(rec);
    int      bottom = (int)TETRIS_PIECE_TOP(rec) + (TETRIS_SIZE[orient] & 0x0F) - 1 - lift;

    if (lift > 0) {
      // Turns still to come: the piece is shown that many quarters short of its
      // landing orientation and unwinds one flick at a time on the way down.
      uint8_t turns = tetrisPendingTurns(idx);
      while (turns--) { orient = TETRIS_TURN[orient]; }
      int w = TETRIS_SIZE[orient] >> 4;
      if (cx + w > TETRIS_GRID_W) { cx = TETRIS_GRID_W - w; }   // kick off the wall
      if (cx < 0) { cx = 0; }
    }

    uint16_t col = tetrisPal[s.hue[TETRIS_PIECE_CLASS(rec)]];
    int      bx  = originX + cx * TETRIS_CELL;
    int      by  = originY + (bottom - (TETRIS_SIZE[orient] & 0x0F) + 1) * TETRIS_CELL;
    for (uint8_t c = 0; c < 4; c++) {
      uint8_t cell = TETRIS_SHAPE[orient][c];
      matrix.fillRect(bx + (cell >> 4) * TETRIS_CELL, by + (cell & 0x0F) * TETRIS_CELL,
                      TETRIS_CELL, TETRIS_CELL, col);
    }
  }
}

// The separator between hours and minutes: vertically stacked between the two
// groups in landscape, a horizontal pair in the gap between the rows in portrait.
void tetrisDrawColon() {
  uint16_t c = scaledColorVisible(255, 255, 255);
  if (tetrisLandscape) {
    int x = tetrisXHour + TETRIS_PITCH * 2;   // x 30..33, between the two groups
    matrix.fillRect(x, tetrisYHour + 4,  TETRIS_DOT, TETRIS_DOT, c);  // y 10..13
    matrix.fillRect(x, tetrisYHour + 12, TETRIS_DOT, TETRIS_DOT, c);  // y 18..21
  } else {
    int y = tetrisYHour + 22;                 // y 30..33, in the 8 px gap between rows
    matrix.fillRect(tetrisXHour + 5,  y, TETRIS_DOT, TETRIS_DOT, c);  // x 8..11
    matrix.fillRect(tetrisXHour + 17, y, TETRIS_DOT, TETRIS_DOT, c);  // x 20..23
  }
}

// One pass of the watchface. While blocks are falling this runs at the animation
// step interval; once everything has landed it only repaints when something it
// shows has actually changed, so an idle clock costs almost nothing. Skipping
// matrix.show() is safe: Protomatter keeps refreshing the last frame it was given.
void drawTetrisFace() {
  if (curRotation != tetrisRotation) { tetrisLayout(); }
  tetrisPushTime();

  bool colonOn = (sysTime % 2) == 0;   // 1 Hz blink, in step with the seconds
#if BUTTON_FEEDBACK_ON_MATRIX
  bool fbNow = fbLit;
#else
  bool fbNow = false;
#endif

  // Turning runs on its own clock, so a flick between two fall steps has to be
  // painted when it happens - otherwise the drop rate would quietly limit how
  // finely the rotation setting can act.
  uint8_t spinPending = 0;
  for (uint8_t i = 0; i < 4; i++) { spinPending += tetrisPendingTurns(i); }

  bool stepDue = !tetrisSettled &&
                 (millisNow - tetrisStepLast >= tetrisSettings.dropMs);
  bool changed = tetrisPanelStale || colonOn != tetrisColonDrawn ||
                 effectiveBrightness != tetrisBrightDrawn || fbNow != tetrisFbDrawn ||
                 spinPending != tetrisSpinDrawn;
  if (!stepDue && !changed) { return; }   // leave the last frame standing
  if (stepDue) {
    tetrisStepLast = millisNow;
    tetrisStep();
  }

  tetrisApplyBrightness();
  matrix.fillScreen(0);
  tetrisDrawDigit(0, tetrisXHour, tetrisYHour);
  tetrisDrawDigit(1, tetrisXHour + TETRIS_PITCH, tetrisYHour);
  tetrisDrawDigit(2, tetrisXMin, tetrisYMin);
  tetrisDrawDigit(3, tetrisXMin + TETRIS_PITCH, tetrisYMin);
  if (colonOn) { tetrisDrawColon(); }

  // Same overlays the classic watchface draws, so both behave alike.
  drawStatusPixel();
  drawFeedbackIndicator();
  matrix.show();

  tetrisColonDrawn  = colonOn;
  tetrisBrightDrawn = effectiveBrightness;
  tetrisFbDrawn     = fbNow;
  tetrisPanelStale  = false;
  // After the step, because a piece may have landed and the next one armed.
  tetrisSpinDrawn = 0;
  for (uint8_t i = 0; i < 4; i++) { tetrisSpinDrawn += tetrisPendingTurns(i); }
}
#endif  // WATCHFACE_TETRIS

// Draw the current clock state to the panel. This is the expensive part (clears
// the framebuffer, prints all six digits and pushes via matrix.show()). In the M4's
// AP preview it is throttled to 5 fps while stepClockAnim() keeps the state moving,
// so frames are skipped (latest state shown) instead of the animation slowing down.
void renderClock(void) {
#if defined(CLOCK_DEBUG)
  uint32_t drawStart = micros();
#endif
  matrix.fillScreen(0); // Fill background black

  for (uint8_t i = 0; i < 6; i++) {
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

  drawStatusPixel();
  drawFeedbackIndicator();
#if defined(CLOCK_DEBUG)
  uint32_t showStart = micros();
#endif
  matrix.show();  // AFTER DRAWING, A show() CALL IS REQUIRED TO UPDATE THE MATRIX!
  tetrisPanelStale = true;
#if defined(CLOCK_DEBUG)
  frameStatsAdd(showStart - drawStart, micros() - showStart);
#endif
}

// The watchface to draw. It can change at runtime (the config page previews the
// choice live), and only the visible face is stepped, so whichever takes over is
// resynchronised here: the classic one gets fresh digit strings, which
// stepClockAnim() would otherwise only rebuild on the next second tick, plus
// digits snapped onto their targets; the Tetris one rebuilds its layout and
// drops its digits again.
uint8_t activeWatchface() {
  static uint8_t shown = 0xFF;
  if (settings.watchface == shown) { return shown; }
  shown = settings.watchface;
  sprintf(timeStr, "%02d%02d%02d", clockHour(sysTime),   clockMinute(sysTime),   clockSecond(sysTime));
  sprintf(animStr, "%02d%02d%02d", clockHour(sysTime+1), clockMinute(sysTime+1), clockSecond(sysTime+1));
  applyOrientation(curRotation);   // snaps all six digits, clears animShow[]
#if WATCHFACE_TETRIS
  tetrisRotation = 0xFF;           // force a fresh layout and a fresh drop
#endif
  return shown;
}

// Live (non-AP) mode: advance and draw the selected watchface every loop
// iteration. The Tetris one keeps its own state machine and pacing, so the
// classic fly-in animation is not stepped while it is showing.
void drawClock(void) {
#if WATCHFACE_TETRIS
  if (activeWatchface() == WATCHFACE_TETRIS_ID) { drawTetrisFace(); return; }
#else
  activeWatchface();
#endif
  stepClockAnim();
  renderClock();
}

/* Updates millisNow, sysTime. Keeps track of the looptime using delay(). Updates the animTrigger[] array. */
void timekeeper(void) { 
  /*Call this always in the beginning of an iteration in loop()
    for scheduling of short actions use millisNow; for long-term schedules use hourNow, minuteNow, secondNow or plain sysTime with the clock_time.h helpers eg: clockHour(sysTime)
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
#if PANEL_PACED_LOOP
  delay(1); // show() paces the loop to the panel refresh; just give other tasks a turn
#else
  if(deltaT<loopTime) { delay(loopTime-deltaT); } //delay start of execution until we have the right iteration interval
#endif
  
  millisNow=millis();
  sysTime=clockNow();
 
  if(secondNow != clockSecond(sysTime)) { secondNow=clockSecond(sysTime); secondTrigger=true; }
  else { secondTrigger=false; }
 
  if(secondTrigger && minuteNow != clockMinute(sysTime)) { minuteNow=clockMinute(sysTime); minuteTrigger=true; }
  else { minuteTrigger=false; }

  if(minuteTrigger && hourNow != clockHour(sysTime)) { hourNow=clockHour(sysTime); hourTrigger=true; }
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
    for (uint8_t i = 0; i < sizeof(animTrigger); i++) {
      animTrigger[i] = false;
    }
  }
}

// Updates sysTime from NTP (board_hal.h: the NINA's own SNTP client on the M4,
// lwIP's on the S3). Enables/Disables WiFi when necessary.
void timeSync_WifiLib() {
  if (!ntpSuccess && !ntpRequestActive) {
    ntpTime=netNtpEpoch();
    lastSync=millisNow;
    if(ntpTime != 0) {
#if defined(CLOCK_DEBUG) && defined(DST_TEST_UTC)
      ntpTime = DST_TEST_UTC;   // debug test: start just before a daylight-saving change
      settings.dst = DST_AUTO;  // in RAM only
#endif
      if(clockIsSet()) { timeOffset=ntpTime+tzTotalOffset()-sysTime; } // timeOffset will be positive if acutal time is ahead of sysTime (=sysTime/ system clock is slow) and negative if acutal time is behind sysTime (=sysTime/ system clock is fast)
      else { timeOffset=0; }
      if (settings.dst == DST_AUTO) { dstAutoActive = dstActiveAt(tzRule(settings.tzOffset), ntpTime); }
      clockSet(ntpTime + tzTotalOffset()); // local time: UTC + configurable timezone + daylight saving offset
      Serial.println("NTP success");
      Serial.print("NTP offset: ");
      Serial.println(timeOffset);
      ntpSuccess = true;
      ntpRequestActive = false;
      netRadioOff();
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
    /* This code should to a smooth transition between shown time and NTP time but clockAdjust works on seconds as smallest increment. Need to find way to make the clock work not on systemtime directly or adjus system time in another way
    if(timeOffset != 0) {
      if(abs(timeOffset)<100 || firstSync) {
        clockAdjust(timeOffset);
        timeOffset=0;
        firstSync=false;
      }
      else if(timeOffset>0) {
        timeOffset-=100;
        clockAdjust(100);
      }
      else if(timeOffset<0) {
        timeOffset+=100;
        clockAdjust(-100);
      }
    }
    */
    // Switch the radio on one minute before the sync time, counted in minutes
    // since midnight so it wraps across the hour and midnight (sync 05:00 ->
    // 04:59, 00:00 -> 23:59). "syncTimeMinute-1" alone is -1 for minute 00 and
    // never matches: the radio stayed off, the sync at hh:00 failed and, with
    // ntpSuccess then false for good, no daily resync ever happened again.
    const uint16_t minutesPerDay = 24 * 60;
    uint16_t nowMinute  = hourNow * 60 + minuteNow;
    uint16_t syncMinute = syncTimeHour * 60 + syncTimeMinute;
    if(!wifiEnabled && nowMinute == (syncMinute + minutesPerDay - 1) % minutesPerDay) { // 1 minute before next Sync
      netStaBegin(ssid, pass); // Connect to wifi (and arm a fresh NTP sync)
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
  // netNtpEpoch() returns 0 until SNTP completes (a few seconds after
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

// Prints Wifi connection status, SSID, IP and RSSI to console.
// The numeric status is wl_status_t: 0 = idle, 3 = connected, 6 = disconnected.
// The M4's WiFiNINA adds the AP-mode values 8 = listening, 9 = station joined
// (and returns 255 when the SPI reply to the co-processor timed out).
void printWifiStatus() {
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
  return (long)settings.tzOffset + (dstInEffect() ? 3600L : 0L);
}

// Load settings from flash; fall back to factory defaults on first run / struct change.
void loadSettings() {
  clockStore.begin();
  clockStore.read(settings);
  if (settings.magic != SETTINGS_MAGIC) {
    settings = DEFAULTS;
    saveSettings();
    Serial.println("Settings: defaults written to flash");
  } else if (settings.settingsRev < SETTINGS_REV) {
    // Migration ladder. A rev 0 blob was written when settingsRev and watchface
    // were still struct padding, so do not read a meaning into what is in them -
    // give every field added since an explicit value. Everything the user
    // configured keeps its place, because the struct did not grow.
    if (settings.settingsRev < 1) { settings.watchface = WATCHFACE_CLASSIC; }
    saveSettings();   // stamps the new revision
    Serial.print("Settings: stored layout migrated to rev "); Serial.println(SETTINGS_REV);
  }
#if WATCHFACE_TETRIS
  loadTetrisSettings();
#endif
  applySettings();
}

#if WATCHFACE_TETRIS
// The Tetris watchface keeps its settings in a blob of its own, so the main one
// never has to grow. Same shape of migration: the magic says "mine", the
// revision says how much of it is filled in.
void loadTetrisSettings() {
  tetrisStore.begin("tetris");
  tetrisStore.read(tetrisSettings);
  if (tetrisSettings.magic != TETRIS_SETTINGS_MAGIC) {
    tetrisSettings = TETRIS_DEFAULTS;
    saveTetrisSettings();
    Serial.println("Tetris settings: defaults written to flash");
  }
  tetrisSettings.dropMs = constrain(tetrisSettings.dropMs, TETRIS_DROP_MIN, TETRIS_DROP_MAX);
  tetrisSettings.spinMs = constrain(tetrisSettings.spinMs, TETRIS_SPIN_MIN, TETRIS_SPIN_MAX);
}

void saveTetrisSettings() {
  tetrisSettings.magic = TETRIS_SETTINGS_MAGIC;
  tetrisSettings.rev   = TETRIS_SETTINGS_REV;
  tetrisStore.write(tetrisSettings);
}
#endif

// Write current settings to flash.
void saveSettings() {
  settings.magic       = SETTINGS_MAGIC;
  settings.settingsRev = SETTINGS_REV;
  clockStore.write(settings);
}

// Push settings into the runtime globals that drive the clock.
void applySettings() {
  if (settings.dst > DST_AUTO) { settings.dst = DST_AUTO; }
  if (settings.watchface >= WATCHFACE_COUNT) { settings.watchface = WATCHFACE_CLASSIC; }
#if !WATCHFACE_TETRIS
  settings.watchface = WATCHFACE_CLASSIC;   // this build has no other watchface
#endif
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

// Like scaledColor(), but never dims below a floor that keeps the color visible
// on the 5-bit panel. Used for the critical AP info (SSID/PW/IP) so it can never
// quantise to black even when the clock brightness is very low.
const uint8_t AP_INFO_MIN_BRIGHT = 24;
uint16_t scaledColorVisible(uint8_t r, uint8_t g, uint8_t b) {
  uint8_t br = (effectiveBrightness < AP_INFO_MIN_BRIGHT) ? AP_INFO_MIN_BRIGHT : effectiveBrightness;
  return scaledColorB(r, g, b, br);
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
   User button: single click = daylight-saving mode, triple click = AP on/off,
   long press = fade.
   While the AP is running only the triple click (leave config mode) is active,
   so DST/auto-brightness/fade cannot be changed accidentally while configuring.
   ====================================================================== */
void handleButton() {
  bool pressed = (digitalRead(USER_BUTTON_PIN) == LOW);
  unsigned long t = millisNow;

  if (pressed && !btnPrev) {           // press starts
    btnPressStart = t;
    btnLong = false;
    fbBlinkCount = 0;                  // a new interaction cancels a pending confirmation
  }

  if (!apActive) { // long-press fade only in normal mode (AP: web page drives brightness)
    if (pressed && !btnLong && (t - btnPressStart >= BTN_LONGPRESS_MS)) { // becomes a long press
      btnLong = true;
      fadeStart();
    }
    if (pressed && btnLong) {          // hold -> keep fading the brightness
      fadeStep();
    }
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
    uint8_t clicks = (btnClicks > 3) ? 3 : btnClicks; // 4+ clicks run the 3x function
    bool triggered = true;
    if (clicks == 3)         { if (apActive) { stopAPMode(); } else { startAPMode(); } }
    else if (apActive)       { triggered = false; } // 1x/2x do nothing while configuring
    else if (clicks == 2)    { toggleAutoBright(); }
    else                     { cycleDstMode(); }
    btnClicks = 0;
    if (triggered) { feedbackConfirm(clicks); }
  }

  btnPrev = pressed;
  updateFeedbackLed();
}

// Start the confirmation for a triggered n-click function. Timed from now rather
// than from the loop's millisNow, so a function that blocked for a while
// (bringing up the AP radio) does not swallow the blinks.
void feedbackConfirm(uint8_t clicks) {
  fbBlinkCount = clicks;
  fbBlinkStart = millis();
}

// Lit while the button is held, or during an "on" phase of the confirmation.
bool feedbackLit() {
  if (btnPrev) { return true; }
  if (fbBlinkCount == 0) { return false; }
  const unsigned long period = FB_BLINK_ON_MS + FB_BLINK_OFF_MS;
  unsigned long elapsed = millis() - fbBlinkStart;
  if (elapsed < FB_LEAD_IN_MS) { return false; }  // pause before the first blink
  elapsed -= FB_LEAD_IN_MS;
  if (elapsed >= fbBlinkCount * period) { fbBlinkCount = 0; return false; }
  return (elapsed % period) < FB_BLINK_ON_MS;
}

// Resolve the feedback state once per loop and drive the board LED with it. The
// matrix screens draw the same state (fbLit), so LED and matrix stay in step.
void updateFeedbackLed() {
  bool lit = feedbackLit();
  if (lit != fbLit) {
    fbLit = lit;
    digitalWrite(FEEDBACK_LED_PIN, lit ? HIGH : LOW);
  }
}

// Matrix copy of the feedback LED: a 2x2 square in the bottom-right corner of
// the current rotation, clear of the digits in both orientations. Every screen
// calls this right before matrix.show().
void drawFeedbackIndicator() {
#if BUTTON_FEEDBACK_ON_MATRIX
  if (fbLit) {
    matrix.fillRect(matrix.width() - 2, matrix.height() - 2, 2, 2, scaledColorVisible(255, 255, 255));
  }
#endif
}

// Single click: cycle the daylight-saving mode automatic -> summer -> winter ->
// automatic, shift the running clock to match and show the new mode for ~3 s.
void cycleDstMode() {
  uint8_t next = (settings.dst == DST_AUTO)   ? DST_SUMMER
               : (settings.dst == DST_SUMMER) ? DST_WINTER : DST_AUTO;
  setDstMode(next);
  saveSettings();
  dstMsgUntil = millisNow + 3000;
  Serial.print("DST mode -> "); Serial.println(settings.dst);
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
// Bring the AP radio and its servers up (initial start and watchdog restart).
void apRadioUp() {
  bool up = netApBegin(AP_SSID, AP_PASS);
  IPAddress ip = netApIP();
  // Keep the compiled-in AP_IP_ADDR if the radio could not tell us an address
  // yet - the info screen and the DNS answers need a usable one either way.
  if ((uint32_t)ip != 0) { apIP = ip; }
  apServer.begin();
  dnsUdp.begin(DNS_PORT); // captive portal DNS
  apWatchdogLast = millis(); // fresh grace period before the watchdog polls again
  apLastActivity = millis();
  apBadStatus = 0;
  Serial.print("AP "); Serial.print(up ? "up" : "NOT up");
  Serial.print(" at "); Serial.println(apIP);
}

void startAPMode() {
  if (apActive) { return; }
  Serial.println("Starting config AP...");
  apActive = true;
  millisNow = millis();
  updateBrightness(); // resolve the live brightness for the immediate info screen
  drawAPScreen(); // show SSID/PW/IP immediately, before the slow radio bring-up freezes the loop
  apRadioUp();
}

// Triple click while the AP is running: leave config mode and return to the
// normal clock. How the softAP is actually torn down differs per board, see
// netApEnd() in board_hal.h.
void stopAPMode() {
  if (!apActive) { return; }
  Serial.println("Stopping config AP...");
  apActive = false;
  apClientConnected = false;
  dnsUdp.stop();
  apServer.end(); // release the listening socket (a fresh begin() would leak one)
  netApEnd(ssid, pass); // drop the softAP and rejoin the home WiFi
  if (!ntpSuccess) {
    // The clock has never been NTP-synced (AP opened via the boot button):
    // leave the station side marked active so the pending sync can complete.
    wifiEnabled = true;
    Serial.println("Enabled Wifi for pending NTP sync");
  } else {
    wifiEnabled = false; // same state as after a regular daily sync
  }
  applyOrientation(detectRotation()); // back to the normal clock in the current orientation
}

// Re-create the AP when the radio dropped it: the M4's NINA firmware can crash
// and reboot under captive-portal probe load, and after its reboot the SSID
// stays gone for good while the sketch would keep serving into the void.
// A restart is only triggered by AP_BAD_STATUS_LIMIT consecutive bad status
// reads with no DNS/HTTP traffic - a single 255 read is just an SPI timeout
// while the module is busy, and tearing down a healthy AP every 2 s freezes
// the clock and keeps the phone from ever finishing its captive-portal check.
void apWatchdog() {
#if AP_WATCHDOG_ENABLE
  if (millisNow - apWatchdogLast < AP_WATCHDOG_MS) { return; }
  apWatchdogLast = millisNow;
  if (netApHealthy() || (millisNow - apLastActivity < AP_ACTIVITY_GRACE_MS)) {
    apBadStatus = 0;
    return;
  }
  apBadStatus++;
  Serial.print("AP watchdog: radio reports no AP without traffic (");
  Serial.print(apBadStatus); Serial.println("x)");
  if (apBadStatus < AP_BAD_STATUS_LIMIT) { return; }
  apBadStatus = 0;
  Serial.println("AP watchdog: restarting AP");
  apRadioUp();
  apClientConnected = false; // any station is gone after the restart
  drawAPScreen();            // back to the SSID/PW/IP info screen
#endif
}

// AP info is always shown landscape (the SSID/PW text is too wide for portrait).
// Pick rotation 0 or 2 from the accelerometer so it is never upside down; a
// portrait hold maps to the matching landscape rotation.
uint8_t apInfoRotation() {
  uint8_t r = detectRotation();
  if (r == 3) { return 0; }   // portrait-normal  -> landscape-normal
  if (r == 1) { return 2; }   // portrait-flipped -> landscape-flipped
  return r;                   // already 0 or 2
}

// Show AP connection info on the matrix, landscape (64x32) and oriented upright
// per the accelerometer, because the portrait width is too narrow for the text.
void drawAPScreen() {
  uint8_t r = apInfoRotation();
  matrix.setRotation(r);
  curRotation = r;
  matrix.fillScreen(0);
  matrix.setFont(&Picopixel);
  // Full-saturation colors + a brightness floor so SSID/PW/IP never go black.
  matrix.setTextColor(scaledColorVisible(0, 0, 255));
  matrix.setCursor(0, 8);  matrix.print("SSID "); matrix.print(AP_SSID);
  matrix.setTextColor(scaledColorVisible(0, 220, 0));
  matrix.setCursor(0, 18); matrix.print("PW "); matrix.print(AP_PASS);
  matrix.setTextColor(scaledColorVisible(255, 140, 0));
  matrix.setCursor(0, 28); matrix.print("IP "); matrix.print(apIP);
  drawFeedbackIndicator();
  apScreenFbLit = fbLit;
  matrix.show();
  tetrisPanelStale = true;
}

#if defined(CLOCK_DEBUG)
// Debug build only: the queried host name of a DNS request as text.
String dnsQueryName(const byte *buf, int len) {
  String name;
  int i = 12;
  while (i < len && buf[i] != 0) {
    int labelLen = buf[i++];
    if (name.length()) { name += '.'; }
    for (int k = 0; k < labelLen && i < len; k++) { name += (char)buf[i++]; }
  }
  return name;
}
#endif

// Minimal DNS server: answer every query with the AP IP so any hostname the
// phone looks up (e.g. its connectivity-check host) resolves to us. Drains the
// whole receive queue each call so a burst of queries is answered promptly.
void handleDNS() {
  for (uint8_t guard = 0; guard < 10; guard++) {
    int pktLen = dnsUdp.parsePacket();
    if (pktLen <= 0) { break; }
    apLastActivity = millisNow; // traffic = the AP is alive (watchdog proof of life)
    int n = dnsUdp.read(dnsBuffer, sizeof(dnsBuffer));
    if (n < 12) { continue; } // smaller than a DNS header -> ignore

    // Walk the question's QNAME to find where the answer can be appended.
    int qpos = 12;
    while (qpos < n && dnsBuffer[qpos] != 0) { qpos += dnsBuffer[qpos] + 1; }
    qpos += 1 + 4; // null label + QTYPE(2) + QCLASS(2)
    if (qpos > n || qpos + 16 > (int)sizeof(dnsBuffer)) {
      DEBUG_LOG("dns: malformed query (%d bytes) dropped\n", n);
      continue;
    }

    // Turn the request into a response in place.
    dnsBuffer[2] = 0x81; dnsBuffer[3] = 0x80; // QR=1, RD copied, RA=1
    dnsBuffer[8] = 0x00; dnsBuffer[9] = 0x00; // NSCOUNT = 0
    dnsBuffer[10] = 0x00; dnsBuffer[11] = 0x00; // ARCOUNT = 0 (drop any EDNS/extra records)

    int p = qpos; // response ends after the question unless an answer is appended
    uint16_t qtype = ((uint16_t)dnsBuffer[qpos - 4] << 8) | dnsBuffer[qpos - 3];
    DEBUG_LOG("dns: %s type %u from %s\n", dnsQueryName(dnsBuffer, n).c_str(), qtype,
              dnsUdp.remoteIP().toString().c_str());
    if (qtype == 0x0001) {                           // A query -> answer with the AP IP
      dnsBuffer[6] = 0x00; dnsBuffer[7] = 0x01;      // ANCOUNT = 1
      dnsBuffer[p++] = 0xC0; dnsBuffer[p++] = 0x0C;  // NAME -> pointer to QNAME at offset 12
      dnsBuffer[p++] = 0x00; dnsBuffer[p++] = 0x01;  // TYPE  A
      dnsBuffer[p++] = 0x00; dnsBuffer[p++] = 0x01;  // CLASS IN
      dnsBuffer[p++] = 0x00; dnsBuffer[p++] = 0x00; dnsBuffer[p++] = 0x00; dnsBuffer[p++] = 0x3C; // TTL 60s
      dnsBuffer[p++] = 0x00; dnsBuffer[p++] = 0x04;  // RDLENGTH 4
      dnsBuffer[p++] = apIP[0]; dnsBuffer[p++] = apIP[1]; dnsBuffer[p++] = apIP[2]; dnsBuffer[p++] = apIP[3];
    } else {
      // AAAA/HTTPS(65)/... queries: NOERROR with zero answers. Answering these
      // with an A record is malformed and makes phones retry for seconds before
      // falling back to a plain A lookup.
      dnsBuffer[6] = 0x00; dnsBuffer[7] = 0x00;      // ANCOUNT = 0
    }

    dnsUdp.beginPacket(dnsUdp.remoteIP(), dnsUdp.remotePort());
    dnsUdp.write(dnsBuffer, p);
    dnsUdp.endPacket();
  }
}

// Tiny 204 response for the /live AJAX preview requests (no body needed).
void sendNoContent(Print &c) {
  c.println("HTTP/1.1 204 No Content");
  c.println("Connection: close");
  c.println();
}

// Tiny plain-text response "<brightness> <lux>" for the config page poll:
// effectiveBrightness (slider value in manual mode, sensor-driven in auto mode)
// and the raw unfiltered lux reading (-1 when no sensor / not read yet).
void sendBrightness(Print &c) {
  c.println("HTTP/1.1 200 OK");
  c.println("Content-Type: text/plain");
  c.println("Connection: close");
  c.println();
  c.print(effectiveBrightness);
  c.print(' ');
  if (lastLux >= 0) { c.print(lastLux, 1); } else { c.print("-1"); }
}

// Lightweight 302 to the portal. Used for captive-portal probe URLs so the OS
// shows the "sign in to network" prompt without us shipping the whole form for
// every probe (which floods the few NINA sockets).
void sendCaptiveRedirect(Print &c) {
  c.println("HTTP/1.1 302 Found");
  c.print("Location: http://"); c.print(apIP); c.println("/");
  c.println("Content-Length: 0");
  c.println("Connection: close");
  c.println();
}

// Serve one web client per call (non-blocking between clients).
void handleAP() {
  handleDNS(); // keep the captive-portal DNS responsive

  WiFiClient client = netServerAccept(apServer);
#if defined(CLOCK_DEBUG)
  // Every accepted socket, before the connected() check below can drop it.
  if (client.fd() >= 0) {
    int errBefore = errno;
    bool isConnected = client.connected();
    DEBUG_LOG("http: accepted socket %d from %s, connected %d (errno %d), %d bytes waiting\n",
              client.fd(), client.remoteIP().toString().c_str(), isConnected, errBefore,
              client.available());
  }
#endif
  if (!client) { return; }
  apLastActivity = millisNow; // traffic = the AP is alive (watchdog proof of life)
  // Don't let a slow/silent client stall the loop (and the captive-portal DNS)
  // for the 1 s Stream default per readStringUntil() call.
  netClientTimeoutMs(client, 100);

  apShowClock(); // a client is talking to us -> reliably switch to the live clock

  String reqLine = client.readStringUntil('\n'); // "GET /path?query HTTP/1.1\r"
#if defined(CLOCK_DEBUG)
  String host; // which host the client thinks it is talking to (probe detection)
#endif
  // discard the remaining request headers
  while (client.connected()) {
    String h = client.readStringUntil('\n');
    if (h.length() == 0 || h == "\r") { break; }
#if defined(CLOCK_DEBUG)
    if (h.startsWith("Host:")) { host = h.substring(5); host.trim(); }
#endif
  }
#if defined(CLOCK_DEBUG)
  String shown = reqLine;
  shown.trim();
  DEBUG_LOG("http: %s | host %s | from %s\n", shown.c_str(), host.c_str(),
            client.remoteIP().toString().c_str());
#endif

  // Parse the request target out of "METHOD <target> HTTP/1.1"
  int sp1 = reqLine.indexOf(' ');
  int sp2 = reqLine.indexOf(' ', sp1 + 1);
  String target = (sp1 >= 0 && sp2 > sp1) ? reqLine.substring(sp1 + 1, sp2) : String("/");
  String path = target, query = "";
  int q = target.indexOf('?');
  if (q >= 0) { path = target.substring(0, q); query = target.substring(q + 1); }

#if AP_BUFFERED_SEND
  BufferedWriter out(client); // batch the many small prints into few big SPI writes
#else
  WiFiClient &out = client;
#endif

  bool reboot = false;
  if (path.startsWith("/save")) {
    applyParams(query);
    saveSettings();
#if WATCHFACE_TETRIS
    saveTetrisSettings();
#endif
    sendSavedPage(out);
    reboot = true;
  } else if (path.startsWith("/live")) {
    applyLiveParams(query); // live preview: apply to RAM only, no save, no reboot
    sendNoContent(out);
  } else if (path == "/b") {
    sendBrightness(out); // tiny plain-text poll of the currently rendered brightness
  } else if (path == "/" || path.startsWith("/index")) {
    sendFormPage(out); // the actual config UI (only served on explicit navigation)
  } else {
    // Every captive-portal probe (/generate_204, /hotspot-detect.html, /ncsi.txt,
    // ...) gets a small redirect to "/", which triggers the OS "sign in" prompt.
    sendCaptiveRedirect(out);
  }

#if AP_BUFFERED_SEND
  out.flushBuf();
#endif
  client.stop();
  if (reboot) {
    delay(300);
    boardReset(); // reboot so the new timezone/WiFi settings take full effect
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
  v = getParam(q, "dst");    if (v.length()) { settings.dst = constrain(v.toInt(), DST_WINTER, DST_AUTO); }
#if WATCHFACE_TETRIS
  v = getParam(q, "wf");     if (v.length()) { settings.watchface = constrain(v.toInt(), 0, WATCHFACE_COUNT - 1); }
  v = getParam(q, "tdrop");  if (v.length()) { tetrisSettings.dropMs = constrain(v.toInt(), TETRIS_DROP_MIN, TETRIS_DROP_MAX); }
  v = getParam(q, "tspin");  if (v.length()) { tetrisSettings.spinMs = constrain(v.toInt(), TETRIS_SPIN_MIN, TETRIS_SPIN_MAX); }
#endif
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
#if WATCHFACE_TETRIS
  v = getParam(q, "wf");     if (v.length()) { settings.watchface = constrain(v.toInt(), 0, WATCHFACE_COUNT - 1); }
  v = getParam(q, "tdrop");  if (v.length()) { tetrisSettings.dropMs = constrain(v.toInt(), TETRIS_DROP_MIN, TETRIS_DROP_MAX); }
  v = getParam(q, "tspin");  if (v.length()) { tetrisSettings.spinMs = constrain(v.toInt(), TETRIS_SPIN_MIN, TETRIS_SPIN_MAX); }
#endif
  v = getParam(q, "bright"); if (v.length()) { settings.brightness = constrain(v.toInt(), 0, 255); }
  v = getParam(q, "autob");  if (v.length()) { settings.autoBright = (v == "on") ? 1 : 0; } // updateBrightness() re-seeds the fade
  v = getParam(q, "luxd");   if (v.length()) { settings.luxDark   = (uint16_t)constrain(v.toInt(), 0, 65535); }
  v = getParam(q, "luxb");   if (v.length()) { settings.luxBright = (uint16_t)constrain(v.toInt(), 1, 65535); }
  v = getParam(q, "brmin");  if (v.length()) { settings.brightMin = constrain(v.toInt(), 0, 255); }
  v = getParam(q, "brmax");  if (v.length()) { settings.brightMax = constrain(v.toInt(), 0, 255); }
  v = getParam(q, "speed");  if (v.length()) { settings.animSpeed = constrain(v.toInt(), 4, 60); loopTime = settings.animSpeed; }
  v = getParam(q, "digit");  if (v.length()) { parseHexColor(v, settings.digitR, settings.digitG, settings.digitB); }
  v = getParam(q, "trail");  if (v.length()) { parseHexColor(v, settings.trailR, settings.trailG, settings.trailB); }
}

void sendHttpHeader(Print &c) {
  c.println("HTTP/1.1 200 OK");
  c.println("Content-Type: text/html; charset=utf-8");
  c.println("Connection: close");
  c.println();
}

// Helper: print one <option> with the right "selected" attribute.
void printOption(Print &c, uint8_t cur, uint8_t val, const char *label) {
  c.print("<option value=\""); c.print(val); c.print("\"");
  if (cur == val) { c.print(" selected"); }
  c.print(">"); c.print(label); c.println("</option>");
}

void printTzOptions(Print &c) {
  for (unsigned int i = 0; i < sizeof(TZONES) / sizeof(TZONES[0]); i++) {
    c.print("<option value="); c.print(TZONES[i].off);
    if (TZONES[i].off == settings.tzOffset) { c.print(" selected"); }
    c.print(">"); c.print(TZONES[i].label); c.println("</option>");
  }
}

void sendFormPage(Print &c) {
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
  c.println("<label>Daylight saving</label><select name=dst>");
  printOption(c, settings.dst, DST_AUTO,   "Automatic (rules of the timezone)");
  printOption(c, settings.dst, DST_SUMMER, "Summer time (+1h)");
  printOption(c, settings.dst, DST_WINTER, "Standard / winter time");
  c.println("</select>");

#if WATCHFACE_TETRIS
  // Watchface (live, so the choice can be judged on the panel before saving).
  c.println("<label>Watchface</label><select name=wf onchange=\"liveNow()\">");
  printOption(c, settings.watchface, WATCHFACE_CLASSIC,   "Classic (flying digits, HH MM SS)");
  printOption(c, settings.watchface, WATCHFACE_TETRIS_ID, "Tetris (falling blocks, HH:MM)");
  c.println("</select>");

  // Tetris pace: how fast the blocks come down, and how busily they turn on the
  // way. Separate settings because they are separate things to look at.
  c.print("<label>Tetris drop (ms per block row; higher = slower)</label>");
  c.print("<input type=range min="); c.print(TETRIS_DROP_MIN);
  c.print(" max="); c.print(TETRIS_DROP_MAX);
  c.print(" name=tdrop value="); c.print(tetrisSettings.dropMs);
  c.println(" oninput=\"this.nextElementSibling.value=this.value;live()\" onchange=\"this.nextElementSibling.value=this.value;liveNow()\">");
  c.print("<output>"); c.print(tetrisSettings.dropMs); c.println("</output>");

  c.print("<label>Tetris turn (ms between quarter turns; higher = calmer)</label>");
  c.print("<input type=range min="); c.print(TETRIS_SPIN_MIN);
  c.print(" max="); c.print(TETRIS_SPIN_MAX);
  c.print(" step=10 name=tspin value="); c.print(tetrisSettings.spinMs);
  c.println(" oninput=\"this.nextElementSibling.value=this.value;live()\" onchange=\"this.nextElementSibling.value=this.value;liveNow()\">");
  c.print("<output>"); c.print(tetrisSettings.spinMs); c.println("</output>");
#endif

  // Brightness (live). Manual mode: absolute brightness. Auto mode: relative trim
  // around the sensor value (128 = neutral, lower = darker, higher = brighter).
  c.print("<label>Brightness (0-255; auto mode: 128 = neutral)</label>");
  c.print("<input type=range min=0 max=255 name=bright value="); c.print(settings.brightness);
  // oninput throttles to <=1 update/s while dragging; onchange (release) always sends the final value.
  c.println(" oninput=\"this.nextElementSibling.value=this.value;live()\" onchange=\"this.nextElementSibling.value=this.value;liveNow()\">");
  c.print("<output>"); c.print(settings.brightness); c.println("</output>");
  // Live readout: brightness actually being rendered + the raw sensor lux; polled 1x/s.
  c.print("<div style=\"color:#8c8;font-size:13px\">Current brightness: <span id=cb>"); c.print(effectiveBrightness);
  c.print("</span> &middot; sensor <span id=cl>");
  if (lastLux >= 0) { c.print(lastLux, 1); } else { c.print("--"); }
  c.println("</span> lux</div>");

  // Auto-brightness (BH1750 light sensor). Maps lux -> brightness; when on, the
  // sensor overrides the manual brightness above once per second.
  c.print("<label><input type=checkbox name=autob onchange=liveNow() ");
  if (settings.autoBright) { c.print("checked"); }
  c.println("> Auto brightness (light sensor)</label>");
  c.println("<div class=row>");
  c.print("<div><label>Dark lux</label><input type=number min=0 max=65535 name=luxd onchange=liveNow() value="); c.print(settings.luxDark); c.println("></div>");
  c.print("<div><label>Bright lux</label><input type=number min=1 max=65535 name=luxb onchange=liveNow() value="); c.print(settings.luxBright); c.println("></div>");
  c.println("</div><div class=row>");
  c.print("<div><label>Min brightness</label><input type=number min=0 max=255 name=brmin onchange=liveNow() value="); c.print(settings.brightMin); c.println("></div>");
  c.print("<div><label>Max brightness</label><input type=number min=0 max=255 name=brmax onchange=liveNow() value="); c.print(settings.brightMax); c.println("></div>");
  c.println("</div>");

  // Animation speed (live)
  c.print("<label>Animation speed (ms per pixel, small=fast)</label>");
  c.print("<input type=number min=4 max=60 name=speed value="); c.print(settings.animSpeed);
  c.println(" oninput=live() onchange=liveNow()>");

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
    printOption(c, settings.dir[i], 0, "from top");
    printOption(c, settings.dir[i], 1, "from right");
    printOption(c, settings.dir[i], 2, "from bottom");
    printOption(c, settings.dir[i], 3, "from left");
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
  c.println("var ab=document.getElementsByName('autob')[0].checked?'on':'off';");
#if WATCHFACE_TETRIS
  c.println("var wf='&wf='+g('wf')+'&tdrop='+g('tdrop')+'&tspin='+g('tspin');");
#else
  c.println("var wf='';");   // no watchface selector in this build
#endif
  c.println("fetch('/live?bright='+g('bright')+'&autob='+ab+'&luxd='+g('luxd')+'&luxb='+g('luxb')+'&brmin='+g('brmin')+'&brmax='+g('brmax')+'&speed='+g('speed')+'&digit='+encodeURIComponent(g('digit'))+'&trail='+encodeURIComponent(g('trail'))+wf).catch(function(){});}");
  // Throttle the live stream to <=1 request/s while a slider is dragged (leading +
  // a single trailing send at the 1 s boundary). liveNow() bypasses it on release /
  // discrete changes so the final value always lands at once.
  c.println("function live(){var n=Date.now();if(n-_t>=1000){_t=n;if(_p){clearTimeout(_p);_p=null;}_send();}else if(!_p){_p=setTimeout(function(){_t=Date.now();_p=null;_send();},1000-(n-_t));}}");
  c.println("function liveNow(){if(_p){clearTimeout(_p);_p=null;}_t=Date.now();_send();}");
  // Live brightness readout: poll the rendered brightness once a second, with a
  // single in-flight request so the slow WiFiNINA server is never stacked up.
  c.println("var _bf=false;");
  c.println("function poll(){if(_bf){return;}_bf=true;fetch('/b').then(function(r){return r.text();}).then(function(t){var p=t.split(' ');document.getElementById('cb').textContent=p[0];var lx=parseFloat(p[1]);document.getElementById('cl').textContent=(isNaN(lx)||lx<0)?'--':p[1];_bf=false;}).catch(function(){_bf=false;});}");
  c.println("setInterval(poll,1000);");
  // Palette swatches: full colors only. The pre-dimmed greys are gone; the fly-in
  // color now dims itself relative to the master brightness so it never goes black.
  c.println("var PAL=['#ffffff','#ff0000','#ff8000','#ffff00','#80ff00','#00ff00','#00ff80','#00ffff','#00c0ff','#0000ff','#8000ff','#ff00ff','#ff0080','#ff80c0'];");
  c.println("function mkSw(boxId,name){var box=document.getElementById(boxId),cur=document.getElementsByName(name)[0].value.toLowerCase();");
  c.println("PAL.forEach(function(col){var s=document.createElement('span');s.className='sw'+(col===cur?' sel':'');s.style.background=col;");
  c.println("s.onclick=function(){document.getElementsByName(name)[0].value=col;box.querySelectorAll('.sw').forEach(function(e){e.className='sw';});s.className='sw sel';liveNow();};");
  c.println("box.appendChild(s);});}");
  c.println("mkSw('swDigit','digit');mkSw('swTrail','trail');");
  c.println("</script>");
  c.println("</body></html>");
}

void sendSavedPage(Print &c) {
  sendHttpHeader(c);
  c.println("<!DOCTYPE html><html><head><meta charset=utf-8>");
  c.println("<meta name=viewport content=\"width=device-width,initial-scale=1\">");
  c.println("<title>Saved</title><style>body{font-family:sans-serif;background:#111;color:#eee;padding:24px}</style>");
  c.println("</head><body><h1>Saved</h1>");
  c.println("<p>Settings have been saved. The clock restarts and reconnects to the WiFi.</p>");
  c.println("</body></html>");
}
