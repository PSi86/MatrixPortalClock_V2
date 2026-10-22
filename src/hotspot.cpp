// The hotspot: the clock's own access point, its captive portal and its
// screens (hotspot.h).
#include "hotspot.h"

#include <Fonts/Picopixel.h>
#include "brightness.h"
#include "clock_state.h"
#include "web_server.h"
#include "console.h"
#include "home_assistant.h"
#include "input.h"
#include "lan_access.h"
#include "time_sync.h"
#include "wifi_station.h"

// What only this module calls, ahead of its use.
static void apRadioUp();
static void scanNetworks();
static uint8_t apInfoRotation();
#if defined(CLOCK_DEBUG)
static String dnsQueryName(const byte *buf, int len);
#endif
static void handleDNS();

// WLAN access point config mode --------------------------------------------
#define AP_SSID "MatrixClock"   // its password: AP_PASS (hotspot.h)
bool        apActive = false;
#if BOARD_BUTTON_COUNT == 0
// A clock without buttons whose gesture sensor was not found has no input to
// open the hotspot with. It needs the hotspot only when it cannot reach its
// home WiFi (Peter, 2026-10-18, in place of the 5 minutes after every
// power-up of 2026-10-04); otherwise the web app on the home network, on
// from the start on such a clock (loadLanSettings()), is its way in:
// - no home WiFi stored: the hotspot opens at the start and stays open;
// - the stored one does not connect for AP_FALLBACK_MS: the hotspot opens for
//   AP_AUTO_OPEN_MS, closes then unless a phone is on it, and the clock tries
//   its WiFi again.
const unsigned long AP_FALLBACK_MS  = 2UL * 60UL * 1000UL;
const unsigned long AP_AUTO_OPEN_MS = 5UL * 60UL * 1000UL;
static unsigned long apAutoCloseAt = 0;       // millis() it closes itself; 0 = it does not
static unsigned long apAutoCheckLast = 0;
static unsigned long joinTroubleSince = 0;    // millis() the station started failing to connect; 0 = it does not
#endif
static unsigned long apStatusLast = 0;        // last time the AP connection status was polled
static unsigned long apNoStationSince = 0;    // preview: since when no phone is connected; 0 = one is
const unsigned long AP_NO_STATION_MS = 5000;   // then the info screen comes back
WiFiServer  apServer(80);
// Captive portal: a tiny DNS server answers every lookup with the AP IP so the
// phone's connectivity check is hijacked and the config page pops up by itself.
static IPAddress apIP(AP_IP_ADDR);    // 4.3.2.1 on both boards, see board_hal.h; re-read once the radio is up
static WiFiUDP  dnsUdp;
const uint16_t DNS_PORT = 53;
static byte     dnsBuffer[512];

// Networks in range, from the scan the hotspot makes when it opens, strongest
// first; the config page offers them. A name seen from several access points
// is listed once, with its strongest signal.
// (SeenNetwork and SEEN_MAX: hotspot.h.)
SeenNetwork seenNetworks[SEEN_MAX];
uint8_t     seenCount = 0;
bool        scanFailed = false;   // the last scan did not run at all (not: found nothing)

/* ======================================================================
   WLAN access point configuration mode
   ====================================================================== */
// Bring the AP radio and its servers up (startAPMode()).
static void apRadioUp() {
  bool up = netApBegin(AP_SSID, AP_PASS);
  IPAddress ip = netApIP();
  // Keep the compiled-in AP_IP_ADDR if the radio could not tell us an address
  // yet - the info screen and the DNS answers need a usable one either way.
  if ((uint32_t)ip != 0) { apIP = ip; }
  apServer.begin();
  // Captive portal DNS: without it a phone does not open the page by itself.
  if (!dnsUdp.begin(DNS_PORT)) { Console.println("Captive portal DNS did NOT start"); }
  Console.print("AP "); Console.print(up ? "up" : "NOT up");
  Console.print(" at "); Console.println(apIP);
}

void startAPMode() {
  if (apActive) { return; }
  Console.println("Starting config AP...");
  apActive = true;
  millisNow = millis();
  // The hotspot takes the radio: a sync attempt under way is cut off, and a
  // fresh one starts when the hotspot closes (stopAPMode()). Whatever the
  // station had is gone.
  haStopLink(true);     // says goodbye to Home Assistant while the station still has the radio
  if (syncAttempting(syncState)) { setSyncState(SYNC_PAUSED); }
  stationState = STA_OFF;
  lanStop();            // the hotspot's server takes port 80 over
  updateBrightness(); // resolve the live brightness for the immediate info screen
  setScreen(SCREEN_HOTSPOT_INFO); // draws SSID/PW/IP at once, before the slow radio bring-up freezes the loop
  scanNetworks();     // while the radio is still a station, for the config page's list
  apRadioUp();
}

// Scan the networks in range into seenNetworks: names only (no hidden ones),
// each once with its strongest signal, the strongest SEEN_MAX, strongest first.
static void scanNetworks() {
  int found = netScan();
  seenCount = 0;
  scanFailed = (found < 0);
  if (scanFailed) { Console.print("WiFi scan failed: "); Console.println(found); }
  for (int i = 0; i < found; i++) {
    SeenNetwork net;
    netScanResult(i, net.ssid, sizeof(net.ssid), net.rssi, net.open);
    if (net.ssid[0] == '\0') { continue; }
    int same = -1, weakest = 0;
    for (uint8_t j = 0; j < seenCount; j++) {
      if (strcmp(seenNetworks[j].ssid, net.ssid) == 0) { same = j; }
      if (seenNetworks[j].rssi < seenNetworks[weakest].rssi) { weakest = j; }
    }
    if (same >= 0) {
      if (net.rssi > seenNetworks[same].rssi) { seenNetworks[same].rssi = net.rssi; }
    } else if (seenCount < SEEN_MAX) {
      seenNetworks[seenCount++] = net;
    } else if (net.rssi > seenNetworks[weakest].rssi) {
      seenNetworks[weakest] = net;
    }
  }
  netScanDone();
  for (uint8_t i = 1; i < seenCount; i++) {   // strongest first
    for (uint8_t j = i; j > 0 && seenNetworks[j].rssi > seenNetworks[j - 1].rssi; j--) {
      SeenNetwork t = seenNetworks[j]; seenNetworks[j] = seenNetworks[j - 1]; seenNetworks[j - 1] = t;
    }
  }
  Console.print("WiFi scan: "); Console.print(seenCount); Console.println(" networks");
}

// Close the hotspot (BACK on its screen, or 3x with Classic clicks): leave
// config mode and return to the normal clock. How the softAP is actually torn
// down differs per board, see netApEnd() in board_hal.h.
void stopAPMode() {
  if (!apActive) { return; }
  Console.println("Stopping config AP...");
  apActive = false;
#if BOARD_BUTTON_COUNT == 0
  apAutoCloseAt = 0;
#endif
  dnsUdp.stop();
  apServer.end(); // release the listening socket (a fresh begin() would leak one)
  netApEnd(); // drop the softAP
  // A sync that is pending (e.g. the AP was opened from the boot button)
  // starts again at once. Then the station joins if anyone needs it - the AP
  // had the radio, so whatever the station had before is gone - and the radio
  // goes off otherwise, as after a regular sync.
  if (syncStateDue(syncState)) { setSyncState(SYNC_JOINING); }
  if (radioUsers && wifiStored()) {
    stationJoin();
    Console.println("Enabled Wifi for a pending sync or the info band");
  } else {
    netRadioOff();
    stationState = STA_OFF;
    Console.println("Disabled Wifi");
  }
  setScreen(SCREEN_FACE); // back to the normal clock, in the current orientation
}

// Leave the AP-info screen and show the live clock (called once a client appears).
void apShowClock() {
  if (screen == SCREEN_HOTSPOT_INFO) { setScreen(SCREEN_HOTSPOT_PREVIEW); }
}

// While the AP is up: show connection info until a client appears, then switch to
// the live clock so brightness/color/speed changes are visible in real time.
void updateApDisplay() {
  if (screen == SCREEN_HOTSPOT_INFO) {
    if (millisNow - apStatusLast >= 500) { // periodically check whether a station joined
      apStatusLast = millisNow;
      if (netApHasStation()) { apShowClock(); return; }
    }
    // The info screen is static, so it is only redrawn when the panel no longer
    // shows it as it should, e.g. when the press indicator flipped.
    if (panelDirty) { drawAPScreen(); }
    return;
  }
  // A phone that has left: after a few seconds without one the info screen
  // comes back, so the panel no longer looks like the plain clock while the
  // hotspot is still up and only holding UP works.
  if (millisNow - apStatusLast >= 500) {
    apStatusLast = millisNow;
    if (netApHasStation())     { apNoStationSince = 0; }
    else if (!apNoStationSince) { apNoStationSince = millisNow; }
    else if (millisNow - apNoStationSince >= AP_NO_STATION_MS) {
      apNoStationSince = 0;
      setScreen(SCREEN_HOTSPOT_INFO);
      return;
    }
  }
  // SCREEN_HOTSPOT_PREVIEW: a client is connected, and sees the face.
  faceUpdate();
}

#if BOARD_BUTTON_COUNT == 0
// A clock without inputs whose stored WiFi has not connected for
// AP_FALLBACK_MS opens the hotspot for a while (apAutoClose()). Not while the
// station is off because nobody needs it, nor while it is connected.
void apFallback() {
  if (gestureOK || apActive || !wifiStored() || stationState != STA_JOINING) { joinTroubleSince = 0; return; }
  if (joinTroubleSince == 0) { joinTroubleSince = millisNow ? millisNow : 1; return; }
  if (millisNow - joinTroubleSince < AP_FALLBACK_MS) { return; }
  joinTroubleSince = 0;
  Console.println("The home WiFi does not connect: the hotspot opens for 5 minutes");
  startAPMode();
  apAutoCloseAt = millis() + AP_AUTO_OPEN_MS;
  if (apAutoCloseAt == 0) { apAutoCloseAt = 1; }
}

// Closes the hotspot that opened for want of the home WiFi (apFallback())
// once its time is up and no phone is connected to it; a phone on it keeps it
// open. One that opened for want of any WiFi stored stays.
void apAutoClose() {
  if (apAutoCloseAt == 0 || (long)(millisNow - apAutoCloseAt) < 0) { return; }
  if (millisNow - apAutoCheckLast < 1000) { return; }
  apAutoCheckLast = millisNow;
  if (netApHasStation()) { return; }
  Console.println("Hotspot: its time after the start is over");
  stopAPMode();
}
#endif

// AP info is always shown landscape (the SSID/PW text is too wide for portrait).
// Pick rotation 0 or 2 from the device rotation so it is never upside down; a
// portrait hold maps to the matching landscape rotation.
static uint8_t apInfoRotation() {
  if (deviceRotation == 3) { return 0; }   // portrait-normal  -> landscape-normal
  if (deviceRotation == 1) { return 2; }   // portrait-flipped -> landscape-flipped
  return deviceRotation;                   // already 0 or 2
}

// Show AP connection info on the matrix, landscape (64x32) and oriented upright
// per the accelerometer, because the portrait width is too narrow for the text.
void drawAPScreen() {
  setScreenRotation(apInfoRotation());
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
  matrix.show();
  panelDirty = false;
}

#if defined(CLOCK_DEBUG)
// Debug build only: the queried host name of a DNS request as text.
static String dnsQueryName(const byte *buf, int len) {
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
static void handleDNS() {
  for (uint8_t guard = 0; guard < 10; guard++) {
    int pktLen = dnsUdp.parsePacket();
    if (pktLen <= 0) { break; }
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

// Lightweight 302 to the portal. Used for captive-portal probe URLs so the OS
// shows the "sign in to network" prompt without us shipping the whole page for
// every probe.
void sendCaptiveRedirect(Print &c) {
  c.println("HTTP/1.1 302 Found");
  c.print("Location: http://"); c.print(apIP); c.println("/");
  c.println("Content-Length: 0");
  c.println("Connection: close");
  c.println();
}

// The hotspot's side of the loop: the captive portal's DNS, then the web
// server.
void handleAP() {
  handleDNS(); // keep the captive-portal DNS responsive
  webUpdate(true, false);
}
