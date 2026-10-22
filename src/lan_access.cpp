// The web app on the home network (lan_access.h).
#include "lan_access.h"

#include <ESPmDNS.h>
#include <mbedtls/sha256.h>
#include <mbedtls/base64.h>
#include "board_hal.h"
#include "clock_state.h"
#include "wifi_station.h"   // the station it holds joined while it is on

// A blob of its own (NVS key "lan").
#define LAN_SETTINGS_MAGIC 0x1A4E
#define LAN_SETTINGS_REV   1
struct LanSettings {
  uint16_t magic;
  uint8_t  rev;
  uint8_t  on;            // 1 = reachable on the home network
  uint8_t  hasPassword;   // 1 once a password was set
  uint8_t  reserved[3];
  uint8_t  salt[16];
  uint8_t  hash[32];      // SHA-256 of salt and password
};
static LanSettings lanSettings;
static SettingsStore<LanSettings> lanStore;
static bool          lanServing = false;        // the server listens on the home network
static uint8_t       lanFails = 0;              // wrong passwords in a row
static unsigned long lanLockedAt = 0;           // millis() of the fifth; no password is checked for LAN_LOCK_MS
static bool          lanLocked = false;
static const unsigned long LAN_LOCK_MS = 30UL * 1000UL;

static bool saveLanSettings() {
  lanSettings.magic = LAN_SETTINGS_MAGIC;
  lanSettings.rev = LAN_SETTINGS_REV;
  return lanStore.write(lanSettings);
}

// While it is on, the station stays joined.
static void lanWantRadio() {
  if (lanSettings.on) { radioUsers |= RADIO_LAN; } else { radioUsers &= (uint8_t)~RADIO_LAN; }
}

static void lanHash(const char *pw, uint8_t out[32]) {
  uint8_t buf[16 + 64];
  size_t n = strnlen(pw, 63);
  memcpy(buf, lanSettings.salt, 16);
  memcpy(buf + 16, pw, n);
  mbedtls_sha256(buf, 16 + n, out, 0);
}

// A new password, with a new salt.
static void lanSetPassword(const char *pw) {
  for (uint8_t i = 0; i < 16; i += 4) {
    uint32_t r = esp_random();
    memcpy(lanSettings.salt + i, &r, 4);
  }
  lanHash(pw, lanSettings.hash);
  lanSettings.hasPassword = 1;
}

void loadLanSettings() {
  lanStore.begin("lan");
  lanStore.read(lanSettings);
  if (lanSettings.magic != LAN_SETTINGS_MAGIC) {
    memset(&lanSettings, 0, sizeof(lanSettings));
    lanSettings.magic = LAN_SETTINGS_MAGIC;
    lanSettings.rev = LAN_SETTINGS_REV;
  }
  // Like the hotspot, the access comes with a password set: the hotspot's
  // (decided by Peter, 2026-10-08). The page says while it is still that one.
  if (!lanSettings.hasPassword) {
    lanSetPassword(AP_PASS);
#if BOARD_BUTTON_COUNT == 0
    // A clock without buttons (the wall clock) is set up on the home network:
    // without its gesture sensor that is its only way in (Peter, 2026-10-18).
    lanSettings.on = 1;
#else
    lanSettings.on = 0;
#endif
    saveLanSettings();
  }
  lanWantRadio();
  if (lanSettings.on) { Console.printf("Web app on the home network: on, as %s.local\n", netHostname()); }
}

bool lanIsOn() { return lanSettings.on != 0; }
bool lanHasPassword() { return lanSettings.hasPassword != 0; }

void lanSwitch(bool on) {
  lanSettings.on = on ? 1 : 0;
  saveLanSettings();
  lanWantRadio();
}

bool lanPasswordIs(const char *pw) {
  uint8_t h[32];
  lanHash(pw, h);
  return lanSettings.hasPassword && memcmp(h, lanSettings.hash, 32) == 0;
}

// "Basic" and base64 of "user:password", any user name.
bool lanAuthorized(Print &c, const String &auth) {
  if (lanLocked && millisNow - lanLockedAt < LAN_LOCK_MS) {
    sendJsonError(c, 429, "Too many wrong passwords. Wait half a minute.");
    return false;
  }
  lanLocked = false;
  bool ok = false;
  if (lanSettings.hasPassword && auth.startsWith("Basic ")) {
    String b64 = auth.substring(6);
    b64.trim();
    unsigned char dec[200];
    size_t n = 0;
    if (mbedtls_base64_decode(dec, sizeof(dec) - 1, &n, (const unsigned char *)b64.c_str(), b64.length()) == 0) {
      dec[n] = '\0';
      const char *colon = strchr((const char *)dec, ':');
      uint8_t h[32];
      lanHash(colon ? colon + 1 : (const char *)dec, h);
      uint8_t diff = 0;
      for (uint8_t i = 0; i < 32; i++) { diff |= h[i] ^ lanSettings.hash[i]; }   // the same time for every wrong byte
      ok = (diff == 0);
    }
  }
  if (ok) { lanFails = 0; return true; }
  if (auth.length() && ++lanFails >= 5) {
    lanFails = 0;
    lanLocked = true;
    lanLockedAt = millisNow;
    Console.println("Web app: five wrong passwords - no more tries for 30 s");
  }
  sendJsonError(c, 401, "The clock's password is needed.");
  return false;
}

// Switching on needs a password.
void apiLan(Print &c, const String &body) {
  bool on = getParam(body, "on") == "on";
  String pw = getParam(body, "pw");
  if (pw.length() && (pw.length() < 8 || pw.length() > 63)) {
    sendJsonError(c, 400, "The password must be 8 to 63 characters long.");
    return;
  }
  if (on && !pw.length() && !lanSettings.hasPassword) {
    sendJsonError(c, 400, "Set a password to switch the home network access on.");
    return;
  }
  if (pw.length()) { lanSetPassword(pw.c_str()); }
  lanSettings.on = on ? 1 : 0;
  bool stored = saveLanSettings();
  lanWantRadio();
  Console.printf("Web app on the home network: %s%s\n", on ? "on" : "off", pw.length() ? ", new password" : "");
  if (!stored) {
    sendJsonError(c, 500, "Not saved: the clock could not write its flash. This works until the next restart.");
    return;
  }
  JsonDocument d;
  d["ok"] = true;
  d["on"] = on;
  d["host"] = netHostname();
  sendJson(c, d);
}

void lanStop() {
  if (!lanServing) { return; }
  apServer.end();
  MDNS.end();
  WiFi.setSleep(true);
  lanServing = false;
  Console.println("Web app on the home network: stopped");
}

void lanUpdate() {
  bool want = lanSettings.on && stationUp();
  if (want && !lanServing) {
    // No modem sleep while the page may be open: with it, every answer waited
    // for the next beacon, and the page took 1.2 s to load (2026-10-08).
    WiFi.setSleep(false);
    apServer.begin();
    bool named = MDNS.begin(netHostname());
    if (named) { MDNS.addService("http", "tcp", 80); }
    lanServing = true;
    Console.printf("Web app on the home network: http://%s.local, http://%s\n", netHostname(),
                  WiFi.localIP().toString().c_str());
    if (!named) { Console.println("Web app: mDNS did not start; the address works"); }
  } else if (!want && lanServing) {
    lanStop();
  }
  webUpdate(lanServing, true);   // also without the server: a connection still open finishes
}
