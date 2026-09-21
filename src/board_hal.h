/* ----------------------------------------------------------------------
   Hardware abstraction for the two supported clock boards:

     Adafruit MatrixPortal M4   SAMD51 + WiFiNINA (ESP32 co-processor over SPI)
     Adafruit MatrixPortal S3   ESP32-S3, WiFi on the main MCU

   Everything that genuinely differs between them lives here: matrix and button
   pins, the WiFi / NTP calls, the settings storage backend and the reset
   instruction. The sketch itself stays board independent.
   ------------------------------------------------------------------------- */
#pragma once

#include <Arduino.h>

/* ======================================================================
   Board detection
   ====================================================================== */
#if defined(ARDUINO_ADAFRUIT_MATRIXPORTAL_ESP32S3)
  #define BOARD_MATRIXPORTAL_S3 1
  #define BOARD_MATRIXPORTAL_M4 0
#elif defined(_VARIANT_MATRIXPORTAL_M4_)
  #define BOARD_MATRIXPORTAL_S3 0
  #define BOARD_MATRIXPORTAL_M4 1
#else
  #error "Unsupported board - build env:adafruit_matrixportal_s3 or env:adafruit_matrix_portal_m4"
#endif

/* ======================================================================
   Pins
   ====================================================================== */
#if BOARD_MATRIXPORTAL_S3

  // HUB75 pins of the MatrixPortal S3. Protomatter drives them through the
  // ESP32-S3 LCD_CAM peripheral, which MUXes freely, so the order is arbitrary.
  #define MATRIX_RGB_PINS   {42, 41, 40, 38, 39, 37}
  #define MATRIX_ADDR_PINS  {45, 36, 48, 35, 21}
  #define MATRIX_CLOCK_PIN  2
  #define MATRIX_LATCH_PIN  47
  #define MATRIX_OE_PIN     14

  // UP button. CAUTION: D2 - the M4's UP button - is the matrix CLOCK line on
  // this board, so the pin has to move. Neither button has an external pull-up
  // on the S3; pressing pulls the input low, hence INPUT_PULLUP.
  #define USER_BUTTON_PIN   PIN_BUTTON_UP   // GPIO6

#else // BOARD_MATRIXPORTAL_M4

  #define MATRIX_RGB_PINS   {7, 8, 9, 10, 11, 12}
  #define MATRIX_ADDR_PINS  {17, 18, 19, 20, 21}
  #define MATRIX_CLOCK_PIN  14
  #define MATRIX_LATCH_PIN  15
  #define MATRIX_OE_PIN     16

  // UP button = D2, DOWN button = D3, both active LOW (INPUT_PULLUP).
  #define USER_BUTTON_PIN   2

#endif

// Button feedback LED: the red user LED on D13, same pin on both boards.
#define FEEDBACK_LED_PIN LED_BUILTIN

// Onboard LIS3DH accelerometer: same part and same (non-standard) I2C address
// on both boards. Wire.begin() picks up each variant's default SDA/SCL.
#define ACCEL_I2C_ADDR 0x19

/* ======================================================================
   Address of the config access point (and of its web UI), both boards
   ====================================================================== */
// Deliberately NOT a private address (same choice as WLED). Our captive-portal
// DNS answers every lookup with this address, including Android's check host
// connectivitycheck.gstatic.com. If that answer is private (10/8, 172.16/12,
// 192.168/16), Android's NetworkMonitor skips its HTTP check altogether (the
// PRIVATE_IP rule in sendDnsAndHttpProbes()) and reports "connected, no
// internet" instead of "sign in to network", so the portal never pops up.
// The address only exists inside the clock's own isolated AP.
#define AP_IP_ADDR 4, 3, 2, 1

/* ======================================================================
   Frame pacing, and the clock preview while a config-AP client is connected
   ====================================================================== */
#if BOARD_MATRIXPORTAL_S3
  // Drawing a frame takes about 0.5 ms on the S3, far less than one panel
  // refresh (about 6 ms at the measured 166 Hz). So the loop runs in step with
  // the panel: show() waits for the refresh that takes over the new frame, and
  // the animation moves one pixel every whole number of refreshes, which keeps
  // every step equally long. A fixed millisecond loop drifts against the
  // refresh and shows steps for 1, 2 or 3 refreshes instead.
  #define PANEL_PACED_LOOP 1
  // The ESP32-S3 serves the page from its own RAM over lwIP, so the preview
  // runs at the full frame rate without getting in the web server's way.
  #define AP_PREVIEW_INTERVAL_MS 0
  // The Tetris watchface. Nothing in it is board specific any more, but it has
  // only been tested on the S3, so it stays switched off elsewhere until the M4
  // gets a hardware test.
  #define WATCHFACE_TETRIS 1
#else
  // The M4 keeps its fixed millisecond loop time.
  #define PANEL_PACED_LOOP 0
  // Every WiFiNINA socket write is an SPI round trip to the co-processor (and
  // is further slowed by the matrix refresh interrupt), so the preview has to
  // stay at 5 fps to leave the radio enough CPU to serve a page at all.
  #define AP_PREVIEW_INTERVAL_MS 200
  // Untested on this board, so only the classic watchface exists here. Turning
  // it on also needs the AP preview to keep respecting AP_PREVIEW_INTERVAL_MS,
  // or the Tetris face would out-draw what WiFiNINA can serve around.
  #define WATCHFACE_TETRIS 0
#endif

/* ======================================================================
   Settings persistence

   S3: NVS, through the Preferences library. The nvs partition sits at 0x9000
       and is not part of the images esptool writes on an upload (bootloader,
       partition table, otadata, app, UF2 bootloader), so the settings survive
       a re-flash.
   M4: a FIXED address in the top 8 KB block of the 512 KB flash, OUTSIDE the
       program image. The upload (bossac --write --offset 0x4000, no --erase)
       only touches the sketch region from 0x4000 up, so this block is left
       alone. The FlashStorage() macro instead reserves a zero-initialised
       array *inside* the image, which every upload overwrites - that was why
       all settings reset to defaults on each flash.
   ====================================================================== */
#if BOARD_MATRIXPORTAL_S3

#include <Preferences.h>

// The key lets a build keep more than one blob side by side in the same
// namespace. It defaults to the one the clock has always used, so existing
// settings stay exactly where they are.
template <typename T>
class SettingsStore {
 public:
  void begin(const char *key = "settings") {
    _key = key;
    _prefs.begin("matrixclock", false);
  }

  // Reads the stored blob. A blob SHORTER than the struct is accepted and the
  // rest left zeroed - that is a layout from an earlier firmware, which
  // loadSettings() then migrates by revision. Anything longer comes from a
  // NEWER firmware and is refused, leaving the destination zeroed so the
  // caller's magic check falls back to the factory defaults.
  bool read(T &dst) {
    memset(&dst, 0, sizeof(T));
    size_t stored = _prefs.getBytesLength(_key);
    if (stored == 0 || stored > sizeof(T)) { return false; }
    if (_prefs.getBytes(_key, &dst, sizeof(T)) != stored) {
      memset(&dst, 0, sizeof(T));
      return false;
    }
    return true;
  }

  void write(T &src) { _prefs.putBytes(_key, &src, sizeof(T)); }

 private:
  Preferences _prefs;
  const char *_key = "settings";
};

#else // BOARD_MATRIXPORTAL_M4

#include <FlashStorage_SAMD.h>

#define SETTINGS_FLASH_ADDR 0x0007E000UL   // last 8 KB erase block of the 512 KB flash

// The key argument exists only so the call sites match the S3 backend; this
// board has one fixed address and therefore room for a single blob.
template <typename T>
class SettingsStore {
 public:
  void begin(const char *key = "settings") { (void)key; }
  bool read(T &dst)  { _flash.read(dst); return true; }
  void write(T &src) { _flash.write(src); }

 private:
  static_assert(sizeof(T) <= 8192, "Settings must fit in one 8 KB flash block");
  FlashStorageClass<T> _flash{(const void *)SETTINGS_FLASH_ADDR};
};

#endif

/* ======================================================================
   Serial console
   ====================================================================== */
// Debug-build output (env:adafruit_matrixportal_s3_debug sets CLOCK_DEBUG).
// Both compile to nothing in the normal builds. BOOT_TRACE pushes its line out
// before continuing so it survives a reset right after it.
#if defined(CLOCK_DEBUG)
  #define BOOT_TRACE(msg) do { Serial.println(msg); Serial.flush(); } while (0)
  #define DEBUG_LOG(...)  do { Serial.printf(__VA_ARGS__); } while (0)
#else
  #define BOOT_TRACE(msg) do { } while (0)
  #define DEBUG_LOG(...)  do { } while (0)
#endif

inline void boardSerialBegin(unsigned long baud) {
  Serial.begin(baud);
#if defined(BOOT_WAIT_FOR_SERIAL_MS)
  // Debug aid (env:adafruit_matrixportal_s3_debug): hold the boot until a serial
  // monitor is attached or the time runs out, so the first messages - and the
  // last one before an early crash - are not lost.
  for (unsigned long t0 = millis(); !Serial && millis() - t0 < BOOT_WAIT_FOR_SERIAL_MS; ) { delay(10); }
  Serial.setDebugOutput(true);
  Serial.printf("boot: serial attached after %lu ms\n", millis());
#if BOARD_MATRIXPORTAL_S3
  // Why did the previous run end? (1 power-on, 2 reset pin, 3 software,
  // 4 panic, 5 int. watchdog, 6 task watchdog, 7 other watchdog, 9 brownout)
  Serial.printf("boot: reset reason %d\n", (int)esp_reset_reason());
  // TinyUF2's bootloader charges an RC element on GPIO1 during its 0.5 s
  // detection window and reads any reset while it is still charged as a double
  // reset -> UF2 mode. Staying here a while lets a crash further on reboot into
  // the app again, so its reset reason gets printed above.
  while (millis() < 3000) { delay(10); }
#endif
  Serial.flush();
#elif BOARD_MATRIXPORTAL_S3 && ARDUINO_USB_CDC_ON_BOOT
  // The S3 console is native USB CDC. Without this, every write blocks for up
  // to 100 ms whenever the port is enumerated but nothing is reading it, which
  // would stutter the animation as soon as the clock is plugged into a PC.
  // (Built with -D ARDUINO_USB_CDC_ON_BOOT=0 the console is UART0 instead and
  // the board brings up no USB device at all.)
  Serial.setTxTimeoutMs(0);
#endif
}

/* ======================================================================
   Why did the last run end?
   ====================================================================== */
// Short label for the reset cause, meant to be readable on the panel when the
// clock runs without a serial console (e.g. on a power supply). "POWER" is a
// normal power-up, "BUTTON" the reset button; "BROWN" (supply voltage dipped),
// "PANIC" (crash) and the watchdogs point at a real problem.
inline const char *boardResetReasonText() {
#if BOARD_MATRIXPORTAL_S3
  switch (esp_reset_reason()) {
    case ESP_RST_POWERON:  return "POWER";
    case ESP_RST_EXT:      return "BUTTON";
    case ESP_RST_SW:       return "SOFT";
    case ESP_RST_PANIC:    return "PANIC";
    case ESP_RST_INT_WDT:  return "IWDT";
    case ESP_RST_TASK_WDT: return "TWDT";
    case ESP_RST_WDT:      return "WDT";
    case ESP_RST_BROWNOUT: return "BROWN";
    case ESP_RST_DEEPSLEEP: return "SLEEP";
    default:               return "OTHER";
  }
#else
  // SAMD51 reset cause register (RSTC->RCAUSE), one bit per source.
  uint8_t cause = RSTC->RCAUSE.reg;
  if (cause & RSTC_RCAUSE_POR)    { return "POWER"; }
  if (cause & RSTC_RCAUSE_EXT)    { return "BUTTON"; }
  if (cause & RSTC_RCAUSE_SYST)   { return "SOFT"; }
  if (cause & RSTC_RCAUSE_WDT)    { return "WDT"; }
  if (cause & RSTC_RCAUSE_BODCORE) { return "BROWN"; }
  if (cause & RSTC_RCAUSE_BODVDD) { return "BROWN"; }
  return "OTHER";
#endif
}

/* ======================================================================
   Boot breadcrumb
   ====================================================================== */
// How far the boot got, kept in flash so it survives a crash AND the trip
// through the UF2 bootloader (which swallows the reset reason). The clock
// writes a stage while starting and clears it once it has run for a while;
// a value left behind therefore names the step the previous run died in.
// M4: no breadcrumb, its settings block is a single fixed struct.
inline void boardBootStageWrite(uint8_t stage) {
#if BOARD_MATRIXPORTAL_S3
  Preferences p;
  if (p.begin("matrixclock", false)) { p.putUChar("bootstage", stage); p.end(); }
#else
  (void)stage;
#endif
}

inline uint8_t boardBootStageRead() {
#if BOARD_MATRIXPORTAL_S3
  Preferences p;
  if (!p.begin("matrixclock", true)) { return 0; }
  uint8_t stage = p.getUChar("bootstage", 0);
  p.end();
  return stage;
#else
  return 0;
#endif
}

// True for the two harmless causes, so a normal start shows no extra screen.
inline bool boardResetWasNormal() {
  const char *r = boardResetReasonText();
  return (strcmp(r, "POWER") == 0) || (strcmp(r, "BUTTON") == 0);
}

/* ======================================================================
   Reboot
   ====================================================================== */
inline void boardReset() {
#if BOARD_MATRIXPORTAL_S3
  ESP.restart();
#else
  NVIC_SystemReset();
#endif
}

/* ======================================================================
   WiFi / NTP

   The M4 talks to a WiFiNINA co-processor that runs its own SNTP client, so
   WiFi.getTime() is all there is. The S3 runs lwIP itself: SNTP is started
   explicitly and a sync notification tells us when a *fresh* timestamp landed.
   (Asking whether time() merely looks plausible would not do - the system clock
   keeps running across a radio power cycle, so it would happily hand back a
   day-old, drifted value instead of a new reading.)
   ====================================================================== */
#if BOARD_MATRIXPORTAL_S3

#include <WiFi.h>
#include <WiFiUdp.h>
#include <esp_sntp.h>
#include <esp_netif.h>
#include <dhcpserver/dhcpserver.h>   // OFFER_DNS

// Time sources for the S3's own SNTP client. Point the first one at a server on
// the LAN (e.g. a Fritz!Box at 192.168.2.1) if the clock has no internet access.
#ifndef NTP_SERVER_1
  #define NTP_SERVER_1 "pool.ntp.org"
#endif
#ifndef NTP_SERVER_2
  #define NTP_SERVER_2 "time.nist.gov"
#endif

// State of the SNTP client. Held in function-local statics so this header stays
// self-contained (one shared instance no matter how often it is included).
inline volatile bool &netNtpFresh()   { static volatile bool fresh = false;   return fresh; }
inline bool          &netSntpRunning(){ static bool          running = false; return running; }

// Called from the SNTP task once a server reply has been applied to the clock.
inline void netNtpSyncCallback(struct timeval *) { netNtpFresh() = true; }

// Discard the last sync so netNtpEpoch() only reports a genuinely new one.
inline void netNtpRequestFresh() {
  netNtpFresh()     = false;
  netSntpRunning()  = false;   // SNTP is (re)started on the next poll
}

// Radio transmit power, set after every mode change because a mode change resets
// it. Transmit bursts are the clock's highest current draw: at the default
// 19.5 dBm the board reset while joining the WiFi on every power supply tried
// (boot breadcrumb DIED3, sometimes reset reason BROWN), while it ran through
// at 11 dBm; on a PC USB port both work. 11 dBm roughly halves the peak and is
// plenty for a home network (measured -56 dBm there).
#ifndef WIFI_TX_POWER
  #define WIFI_TX_POWER WIFI_POWER_11dBm
#endif

inline void netRadioInit() {
  WiFi.persistent(false);   // don't rewrite the stored credentials on every boot
  BOOT_TRACE("net: WiFi.mode(WIFI_STA) ...");
  WiFi.mode(WIFI_STA);
  WiFi.setTxPower(WIFI_TX_POWER);
  BOOT_TRACE("net: WiFi.mode(WIFI_STA) done");
}

inline void netStaBegin(const char *ssid, const char *pass) {
  WiFi.mode(WIFI_STA);
  WiFi.setTxPower(WIFI_TX_POWER);
  WiFi.begin(ssid, pass);   // non-blocking on the ESP32
  netNtpRequestFresh();
}

inline bool netStaConnected() { return WiFi.status() == WL_CONNECTED; }

inline void netRadioOff() {
#if ESP_IDF_VERSION_MAJOR >= 5
  esp_sntp_stop();          // ESP-IDF 5 renamed it (Arduino-ESP32 3.x)
#else
  sntp_stop();              // ESP-IDF 4.4 has no esp_ alias for it
#endif
  netSntpRunning() = false;
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
}

// Returns a UTC epoch once a fresh SNTP sync has completed, 0 while it has not.
inline uint32_t netNtpEpoch() {
  if (!netStaConnected()) { return 0; }
  if (!netSntpRunning()) {
    sntp_set_time_sync_notification_cb(netNtpSyncCallback);
    configTime(0, 0, NTP_SERVER_1, NTP_SERVER_2); // TZ = UTC; the sketch adds the offset
    netSntpRunning() = true;
  }
  if (!netNtpFresh()) { return 0; }
  return (uint32_t)time(nullptr);
}

// Explicitly hand our own address out as the DNS server in every DHCP lease, so
// the phone's lookups - including its captive-portal check - reach our DNS
// hijack no matter what the softAP DHCP server would advertise by default.
// (Verified on hardware: the S24 then queries us and the portal pops up.)
inline void netApOfferSelfAsDns() {
  esp_netif_t *ap = esp_netif_get_handle_from_ifkey("WIFI_AP_DEF");
  if (ap == nullptr) { return; }
  esp_netif_ip_info_t ipInfo;
  if (esp_netif_get_ip_info(ap, &ipInfo) != ESP_OK) { return; }

  esp_netif_dns_info_t dns = {};
  dns.ip.type = ESP_IPADDR_TYPE_V4;
  dns.ip.u_addr.ip4.addr = ipInfo.ip.addr;
  uint8_t offer = OFFER_DNS;

  // The option can only be changed while the DHCP server is stopped. No client
  // can be connected yet at this point, so nobody loses a lease over it.
  esp_err_t eStop  = esp_netif_dhcps_stop(ap);
  esp_err_t eDns   = esp_netif_set_dns_info(ap, ESP_NETIF_DNS_MAIN, &dns);
  esp_err_t eOpt   = esp_netif_dhcps_option(ap, ESP_NETIF_OP_SET, ESP_NETIF_DOMAIN_NAME_SERVER, &offer, sizeof(offer));
  esp_err_t eStart = esp_netif_dhcps_start(ap);
  DEBUG_LOG("ap: DHCP offers DNS %s (stop 0x%x, set_dns 0x%x, option 0x%x, start 0x%x)\n",
            IPAddress(ipInfo.ip.addr).toString().c_str(), eStop, eDns, eOpt, eStart);
  (void)eStop; (void)eDns; (void)eOpt; (void)eStart;
}

inline bool netApBegin(const char *ssid, const char *pass) {
  WiFi.persistent(false);   // also needed here: the recovery AP starts before netRadioInit()
  WiFi.disconnect(true);    // stop the station side from retrying underneath us
  WiFi.mode(WIFI_AP);
  WiFi.setTxPower(WIFI_TX_POWER);
  const IPAddress apAddr(AP_IP_ADDR);
  bool cfgOk = WiFi.softAPConfig(apAddr, apAddr, IPAddress(255, 255, 255, 0));
  DEBUG_LOG("ap: softAPConfig(%s) %s\n", apAddr.toString().c_str(), cfgOk ? "ok" : "FAILED");
  (void)cfgOk;
  bool ok = WiFi.softAP(ssid, pass);
  if (ok) { netApOfferSelfAsDns(); }
  return ok;
}

inline void netApEnd(const char *ssid, const char *pass) {
  WiFi.softAPdisconnect(true);
  netStaBegin(ssid, pass);
}

inline bool netApHasStation() { return WiFi.softAPgetStationNum() > 0; }
inline bool netApHealthy()    { return (WiFi.getMode() & WIFI_MODE_AP) != 0; }
inline IPAddress netApIP()    { return WiFi.softAPIP(); }

// Read timeout for one HTTP client, in milliseconds.
inline void netClientTimeoutMs(WiFiClient &c, uint16_t ms) {
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  c.setTimeout(ms);         // 3.x: NetworkClient uses Stream's millisecond timeout
#else
  // 2.x: WiFiClient::setTimeout() takes SECONDS (it feeds SO_RCVTIMEO), so the
  // millisecond read timeout has to be set on the Stream base separately.
  c.setTimeout(1);
  c.Stream::setTimeout(ms);
#endif
}

// Next pending HTTP connection. accept() is the 3.x name (available() is
// deprecated there); in 2.x both are the same function.
inline WiFiClient netServerAccept(WiFiServer &s) { return s.accept(); }

inline void netPrintRadioInfo() {
  Serial.print("ESP-IDF SDK: ");
  Serial.println(ESP.getSdkVersion());
}

#else // BOARD_MATRIXPORTAL_M4

#include <SPI.h>
#include <WiFiNINA.h>

inline void netRadioInit() {
  WiFi.status();  // first SPI transaction wakes the co-processor
  delay(1000);    // ...and it needs a moment before it answers reliably
}

inline void netNtpRequestFresh() {} // the NINA firmware re-syncs on its own

inline void netStaBegin(const char *ssid, const char *pass) {
  WiFi.setTimeout(100);     // non-blocking begin: don't stall the clock while it joins
  WiFi.begin(ssid, pass);
}

inline bool netStaConnected() { return WiFi.status() == WL_CONNECTED; }

inline void netRadioOff() { WiFi.end(); }

inline uint32_t netNtpEpoch() { return (uint32_t)WiFi.getTime(); }

inline bool netApBegin(const char *ssid, const char *pass) {
  WiFi.end();
  delay(100);
  // The NINA firmware applies a stored static config to the AP when it starts
  // (AP_START handler). All four values have to be given: its setIPconfig
  // command takes ip/gateway/mask exactly as sent, so the shorter WiFi.config()
  // overloads would hand the AP a 0.0.0.0 netmask. dns = the AP itself.
  const IPAddress apAddr(AP_IP_ADDR);
  WiFi.config(apAddr, apAddr, apAddr, IPAddress(255, 255, 255, 0));
  WiFi.beginAP(ssid, pass);
  unsigned long t0 = millis();
  while (millis() - t0 < 6000) { // wait for the radio (a station may rejoin instantly)
    uint8_t st = WiFi.status();
    if (st == WL_AP_LISTENING || st == WL_AP_CONNECTED) { return true; }
    delay(100);
  }
  return false;
}

// WiFi.end() is useless here - wifiDriverDeinit() is an empty function in this
// driver, so the co-processor would keep beaconing the AP forever. What actually
// tears the softAP down is switching the module back to station mode via
// WiFi.begin(); whether the home-WiFi join succeeds does not matter.
// The static AP address has to be dropped first: the NINA keeps its "static IP"
// flag across modes and begin() then skips DHCP, which would leave the clock in
// the home WiFi without an address (no NTP). A reboot clears it anyway, because
// WiFiNINA hard-resets the co-processor at start-up.
inline void netApEnd(const char *ssid, const char *pass) {
  const IPAddress none(0, 0, 0, 0);
  WiFi.config(none, none, none, none);
  netStaBegin(ssid, pass);
}

inline bool netApHasStation() { return WiFi.status() == WL_AP_CONNECTED; }
inline bool netApHealthy() {
  uint8_t st = WiFi.status();
  return (st == WL_AP_LISTENING || st == WL_AP_CONNECTED);
}
inline IPAddress netApIP() { return WiFi.localIP(); }

inline void netClientTimeoutMs(WiFiClient &c, uint16_t ms) { c.setTimeout(ms); }

// Keep available() here: in WiFiNINA it only hands out a client once its request
// bytes have arrived, while accept() returns every new connection immediately -
// over the slow SPI link that would turn late requests into empty ones.
inline WiFiClient netServerAccept(WiFiServer &s) { return s.available(); }

inline void netPrintRadioInfo() {
  String fv = WiFi.firmwareVersion();
  Serial.print("NINA FW: ");
  Serial.println(fv);
  if (fv < WIFI_FIRMWARE_LATEST_VERSION) { Serial.println("Please upgrade the firmware"); }
}

#endif
