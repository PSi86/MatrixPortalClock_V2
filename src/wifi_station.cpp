// The home WiFi and the station joined to it (wifi_station.h).
#include "wifi_station.h"

#include <WiFi.h>
#include "board_hal.h"
#include "clock_state.h"
#if __has_include("arduino_secrets.h")
  #include "arduino_secrets.h"
#endif

#define WIFI_CREDS_MAGIC 0x57F1
#define WIFI_CREDS_REV   1
WifiCreds wifiCreds;
static SettingsStore<WifiCreds> wifiStore;

JoinOutcome   joinOutcome    = JOIN_NOT_TRIED;
uint8_t       joinFailStatus = 0;
int8_t        joinRssi       = 0;
unsigned long joinOutcomeAt  = 0;

StationState  stationState = STA_OFF;
uint8_t       radioUsers   = RADIO_SYNC;      // the first sync is due at start-up (syncState)
static unsigned long joinAskedAt  = 0;        // millis() the join was last started, for the re-join pace
static unsigned long joinPolledAt = 0;        // millis() the station was last polled
static const unsigned long JOIN_RETRY_MS = 10000;    // between joins while the station does not connect
// The home WiFi forgotten while joined: the station stays a moment for what
// still has to go out over it (stationUpdate()).
static unsigned long wifiForgottenAt = 0;            // millis() it was forgotten; 0 = not since the start
static const unsigned long FORGET_ANSWER_MS = 2000;  // the web app's answer
static const unsigned long FORGET_LINK_MS   = 10000; // the Home Assistant link's goodbye, at most
static const unsigned long JOIN_POLL_MS  = 500;      // between looks at whether it has

// ---- The home WiFi ---------------------------------------------------------------

// The home WiFi from flash. A clock that has never stored one takes the
// credentials the build brings (arduino_secrets.h), and stores them - once:
// from then on the stored ones count, also when they have been forgotten.
void loadWifiCreds() {
  wifiStore.begin("wifi");
  wifiStore.read(wifiCreds);
  if (wifiCreds.magic == WIFI_CREDS_MAGIC) {
    wifiCreds.ssid[sizeof(wifiCreds.ssid) - 1] = '\0';   // whatever is stored, the strings end
    wifiCreds.pass[sizeof(wifiCreds.pass) - 1] = '\0';
    return;
  }
  memset(&wifiCreds, 0, sizeof(wifiCreds));
#if defined(SECRET_SSID) && defined(SECRET_PASS)
  strncpy(wifiCreds.ssid, SECRET_SSID, sizeof(wifiCreds.ssid) - 1);
  strncpy(wifiCreds.pass, SECRET_PASS, sizeof(wifiCreds.pass) - 1);
  if (wifiStored()) {
    saveWifiCreds();
    Console.println("WiFi: the credentials of the build were copied into flash");
  }
#endif
}

bool saveWifiCreds() {
  wifiCreds.magic = WIFI_CREDS_MAGIC;
  wifiCreds.rev   = WIFI_CREDS_REV;
  return wifiStore.write(wifiCreds);
}

bool forgetWifi() {
  memset(wifiCreds.ssid, 0, sizeof(wifiCreds.ssid));
  memset(wifiCreds.pass, 0, sizeof(wifiCreds.pass));
  wifiCreds.verified = 0;
  bool stored = saveWifiCreds();
  joinOutcome = JOIN_NOT_TRIED;
  wifiForgottenAt = millisNow ? millisNow : 1;   // the station stays a moment (stationUpdate())
  Console.println("WiFi: forgotten");
  return stored;
}

bool wifiStored() {
  return wifiCreds.ssid[0] != '\0';
}

// ---- How the last join went -------------------------------------------------------

// A join that has not connected: keep why, for the config page.
static void noteJoinFailed() {
  joinOutcome    = JOIN_FAILED;
  joinFailStatus = netStaStatus();
  joinOutcomeAt  = millisNow;
}

const char *joinFailText(uint8_t status) {
  switch (status) {
    case WL_NO_SSID_AVAIL:   return "network not found";
    case WL_CONNECT_FAILED:  return "the network refused the connection (wrong password?)";
    case WL_CONNECTION_LOST: return "connection lost";
    default:                 return "no connection";
  }
}

// ---- The station -------------------------------------------------------------------

void stationJoin() {
  Console.print("Joining WiFi "); Console.println(wifiCreds.ssid);
  netStaBegin(wifiCreds.ssid, wifiCreds.pass);   // also arms a fresh NTP sync
  joinAskedAt  = millisNow;
  stationState = STA_JOINING;
}

bool stationUp() { return stationState == STA_UP; }

// Prints Wifi connection status, SSID, IP and RSSI to console.
// The numeric status is wl_status_t: 0 = idle, 3 = connected, 6 = disconnected.
static void printWifiStatus() {
  Console.print("WiFi Status: ");
  Console.println(WiFi.status());

  // print the SSID of the network you're attached to:
  Console.print("SSID: ");
  Console.println(WiFi.SSID());

  // print your board's IP address:
  IPAddress ip = WiFi.localIP();
  Console.print("IP Address: ");
  Console.println(ip);

  // print the received signal strength:
  long rssi = WiFi.RSSI();
  Console.print("signal strength (dBm): ");
  Console.println(rssi);
}

// Without a stored WiFi there is nothing to join, and whoever wants the
// station simply goes on waiting. Forgotten while joined, the station stays
// for FORGET_ANSWER_MS, so the web app's answer gets out over it, and while
// the Home Assistant link still holds it, so its goodbye does (up to
// FORGET_LINK_MS). Until 2026-10-22 the radio went on the next pass: by the
// code, before the answer went out on the home network, and before the
// link's goodbye, so Home Assistant would have learned it from the broker's
// last will only after the keepalive ran out (code review of 2026-10-14).
void stationUpdate() {
  if (!wifiStored() && stationState == STA_UP && wifiForgottenAt) {
    unsigned long since = millisNow - wifiForgottenAt;
    if (since < FORGET_ANSWER_MS || ((radioUsers & RADIO_HA) && since < FORGET_LINK_MS)) { return; }
  }
  if (radioUsers == 0 || !wifiStored()) {
    if (stationState != STA_OFF) {
      if (stationState == STA_JOINING) { noteJoinFailed(); }   // read before the radio goes
      netRadioOff();
      Console.println("Disabled Wifi");
      stationState = STA_OFF;
    }
    return;
  }
  if (stationState == STA_OFF) { stationJoin(); return; }
  // Looked at twice a second, not on every pass.
  if (millisNow - joinPolledAt < JOIN_POLL_MS) { return; }
  joinPolledAt = millisNow;
  if (stationState == STA_JOINING) {
    if (netStaConnected()) {
      Console.println("Connected to WiFi");
      printWifiStatus();
      netDnsSettle();   // before stationUp(): what looks names up waits for it (board_hal.h)
      if (!bootStageCleared) { boardBootStageWrite(BOOT_STAGE_RUN); }
      joinOutcome   = JOIN_SUCCEEDED;
      joinRssi      = netStaRssi();
      joinOutcomeAt = millisNow;
      stationState  = STA_UP;
    } else if (millisNow - joinAskedAt >= JOIN_RETRY_MS) {
      noteJoinFailed();
      stationJoin();   // not joined after all this time: start the join again
    }
  } else if (!netStaConnected()) {
    Console.println("WiFi connection lost");
    stationJoin();
  }
}
