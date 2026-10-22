// The Home Assistant link (home_assistant.h).
#include "home_assistant.h"

#include <ArduinoJson.h>
#include "band_items.h"
#include "board_hal.h"
#include "brightness.h"
#include "clock_state.h"
#include "hotspot.h"
#include "clock_time.h"
#include "ha_link.h"
#include "item_icons.h"
#include "lan_access.h"
#include "wifi_station.h"

// What only this module calls, ahead of its use.
static bool saveHaSettings();
static void haBuildAnnounce();
static void haSensorConfig(String &out, const JsonDocument &dev, const char *name, const char *suffix, const char *deviceClass, const char *unit, int8_t decimals);
static void haFailed();
static void haStartLink();
static bool haValueNow(uint8_t i, float &v, char *payload, size_t size);
static void haSendValues();
static void haValuesAgain();
static void haApply(const HaIn &m);
static void haDropItems();
static void haErrorText(const HaIn &m);
static bool haNameOk(const char *name);
static void haIconLook(JsonDocument &doc, BandItem &it);
static void haItem(const char *name, const char *payload, uint16_t len);
static void haMessage(const char *payload, uint16_t len);
static const char *haBlocked();

// The Home Assistant link ------------------------------------------------------------
// MQTT through Home Assistant's broker (src/ha_link.h): its address and the
// clock's login there, a blob of its own (NVS key "ha"). The password is kept
// as typed, as the home WiFi's is: the broker needs it so.
#define HA_SETTINGS_MAGIC 0x4A11
#define HA_SETTINGS_REV   2
struct HaSettings {
  uint16_t magic;
  uint8_t  rev;
  uint8_t  on;            // 1 = linked
  uint16_t port;          // the broker's
  uint8_t  minS;          // a changed value goes out at most this often, s (rev 2; was reserved)
  uint8_t  maxMin;        // an unchanged one still this often, min (rev 2; was reserved)
  char     host[64];      // the broker's name or address
  char     user[64];
  char     pass[64];
};
static HaSettings haSettings;
static SettingsStore<HaSettings> haStore;
const uint16_t HA_PORT_DEFAULT = 1883;
const uint8_t  HA_MIN_S_DEFAULT = 60, HA_MAX_MIN_DEFAULT = 15;   // Peter, 2026-10-22
const uint8_t  HA_MIN_S_LOW = 5;   // the shortest the web app takes

// What the link does, for the config page.
enum HaState : uint8_t {
  HA_OFF,          // switched off
  HA_BLOCKED,      // switched on, but it lacks something (haBlocked())
  HA_WAITING,      // waits for the station
  HA_FAILED,       // the client did not start (no RAM); again after HA_RETRY_MS
  HA_CONNECTING,   // the client runs and is not connected, yet or again
  HA_UP,           // connected
  HA_STOPPING,     // the worker says goodbye and stops the client (up to a connect's timeout)
};
static HaState       haState = HA_OFF;
// Where the client is, as far as the loop has asked the worker for it. From
// the start command to the worker's "stopped" the link holds the station
// (RADIO_HA), so the goodbye still goes out.
enum HaPhase : uint8_t { HA_PHASE_IDLE, HA_PHASE_STARTING, HA_PHASE_RUNNING, HA_PHASE_STOPPING };
static HaPhase       haPhase = HA_PHASE_IDLE;
static bool          haChanged = false;      // new settings: the client starts again with them
static unsigned long haFailedAt = 0;         // millis() the client last failed to start; 0 = it did not
static char          haError[80] = "";       // what went wrong last, until the next connect
static time_t        haUpSince = 0;          // UTC of the connect
static uint32_t      haLostSeen = 0;         // haLink().lost as last reported
// The sensors' values for Home Assistant, each on its own topic: a value goes
// out when it moved by its step since it was last sent, at most every
// haSettings.minS, and unchanged still every haSettings.maxMin, so Home
// Assistant sees the sensor alive (haSendValues()). Inside the clock they are
// read as often as they need: the light five times a second, the room every
// 5 s.
enum HaValueId : uint8_t { HA_VALUE_LIGHT, HA_VALUE_TEMP, HA_VALUE_HUM, HA_VALUE_COUNT };
struct HaValue {
  const char *suffix;   // its topic past the clock's prefix
  float step;           // what it must move by to count as changed
  bool relative;        // the step a share of the value (at least 1)
  float last;           // as last sent
  unsigned long at;     // millis() it was last sent; 0 = not since the connect
};
static HaValue haValues[HA_VALUE_COUNT] = {
  { "light",       0.10f, true,  0, 0 },   // 10 %, at least 1 lx (Peter, 2026-10-22)
  { "temperature", 0.20f, false, 0, 0 },   // 0.2 C
  { "humidity",    1.00f, false, 0, 0 },   // 1 %
};
const unsigned long HA_RETRY_MS = 60UL * 1000UL;   // after the client failed to start
const unsigned long HA_STOP_WAIT_MS = 2000;        // the hotspot waits this long for the goodbye
const uint8_t  HA_MESSAGES_MAX = 4;      // messages kept at a time, as items "*msg1" ... (no item name has a '*')
const uint16_t HA_MESSAGE_S = 3600;      // how long a message counts when it does not say

/* ----------------------------------------------------------------------
   The Home Assistant link (src/ha_link.h has the client and its topics):
   when the client runs, what arrives, and the config page's part. Like
   every source that is pushed to the clock it needs a stored home WiFi and
   continuous network access. While it runs it holds the station as well
   (RADIO_HA), so that switching the link or the access off still lets it
   say goodbye before the radio goes.
   ---------------------------------------------------------------------- */

// "06c16c": the end of the host name, the last three bytes of the MAC.
static const char *haId() { return netHostname() + strlen("matrixclock-"); }

void loadHaSettings() {
  haStore.begin("ha");
  haStore.read(haSettings);
  if (haSettings.magic != HA_SETTINGS_MAGIC) {
    memset(&haSettings, 0, sizeof(haSettings));
    haSettings.port = HA_PORT_DEFAULT;
  }
  // Rev 2 brings the two intervals, in what rev 1 held as reserved (0).
  if (haSettings.minS < HA_MIN_S_LOW) { haSettings.minS = HA_MIN_S_DEFAULT; }
  if (haSettings.maxMin == 0)         { haSettings.maxMin = HA_MAX_MIN_DEFAULT; }
  haTopics(haId());
  if (haSettings.on) {
    Console.printf("Home Assistant: on, broker %s:%u, topics %s...\n", haSettings.host, haSettings.port, haLink().prefix);
  }
}

bool saveHaSettings() {
  haSettings.magic = HA_SETTINGS_MAGIC;
  haSettings.rev   = HA_SETTINGS_REV;
  return haStore.write(haSettings);
}

// What the link lacks to run, or nullptr: the page says so where it is
// switched on.
const char *haBlocked() {
  if (!wifiStored())       { return "wifi"; }
  if (!lanIsOn())          { return "always"; }   // continuous network access
  if (!haSettings.host[0]) { return "host"; }
  return nullptr;
}

// The discovery configs: an entity to send the clock a message (notify,
// Home Assistant 2024.5 on), and the light sensor's lux when there is one.
// Without a sensor its config is taken back, in case it was there before.
void haBuildAnnounce() {
  HaLink &l = haLink();
  const char *id = haId();
  JsonDocument dev;
  dev["identifiers"][0] = netHostname();
  dev["name"] = String("Matrix Clock ") + id;
  dev["model"] = BOARD_NAME;
  dev["sw_version"] = fwVersion();
  dev["configuration_url"] = String("http://") + netHostname() + ".local/";

  JsonDocument msg;
  msg["name"] = "Message";
  msg["unique_id"] = String(netHostname()) + "-message";
  msg["command_topic"] = l.message;
  msg["command_template"] = "{\"text\": {{ value | tojson }}}";
  msg["availability_topic"] = l.status;
  msg["qos"] = 1;
  msg["device"] = dev;
  snprintf(l.announceTopic[0], sizeof(l.announceTopic[0]), "homeassistant/notify/%s/message/config", id);
  l.announce[0] = "";
  serializeJson(msg, l.announce[0]);

  snprintf(l.announceTopic[1], sizeof(l.announceTopic[1]), "homeassistant/sensor/%s/light/config", id);
  l.announce[1] = "";
  if (luxOK) { haSensorConfig(l.announce[1], dev, "Light", "light", "illuminance", "lx", -1); }
#if BOARD_WAVESHARE_WALL
  // The room's temperature and humidity, from the board's SHTC3. A board
  // without one announces neither (no topic).
  snprintf(l.announceTopic[2], sizeof(l.announceTopic[2]), "homeassistant/sensor/%s/temperature/config", id);
  snprintf(l.announceTopic[3], sizeof(l.announceTopic[3]), "homeassistant/sensor/%s/humidity/config", id);
  l.announce[2] = "";
  l.announce[3] = "";
  if (climateOK) {
    haSensorConfig(l.announce[2], dev, "Temperature", "temperature", "temperature", "°C", 1);
    haSensorConfig(l.announce[3], dev, "Humidity", "humidity", "humidity", "%", 0);
  }
#endif
}

// A sensor's discovery config: its name, its topic past the clock's prefix
// (haValues), Home Assistant's device class and unit, and the decimals it
// shows (-1: Home Assistant's choice).
void haSensorConfig(String &out, const JsonDocument &dev, const char *name, const char *suffix, const char *deviceClass,
                    const char *unit, int8_t decimals) {
  HaLink &l = haLink();
  JsonDocument s;
  s["name"] = name;
  s["unique_id"] = String(netHostname()) + "-" + suffix;
  s["state_topic"] = String(l.prefix) + suffix;
  s["device_class"] = deviceClass;
  s["unit_of_measurement"] = unit;
  s["state_class"] = "measurement";
  if (decimals >= 0) { s["suggested_display_precision"] = decimals; }
  s["availability_topic"] = l.status;
  s["device"] = dev;
  serializeJson(s, out);
}

void haFailed() {
  haFailedAt = millisNow ? millisNow : 1;
  haState = HA_FAILED;
  Console.println("Home Assistant: no RAM for the MQTT client - again in a minute");
}

// Asks the worker for the client, with the settings as they are now.
void haStartLink() {
  if (!haBegin()) { haFailed(); return; }
  HaLink &l = haLink();
  haBuildAnnounce();
  strlcpy(l.host, haSettings.host, sizeof(l.host));
  strlcpy(l.user, haSettings.user, sizeof(l.user));
  strlcpy(l.pass, haSettings.pass, sizeof(l.pass));
  strlcpy(l.clientId, netHostname(), sizeof(l.clientId));
  l.port = haSettings.port;
  HaCmd c = {};
  c.kind = HA_CMD_START;
  if (!haCommand(c)) { return; }   // the worker is busy: again on the next pass
  haPhase = HA_PHASE_STARTING;
  haChanged = false;
  radioUsers |= RADIO_HA;
  haState = HA_CONNECTING;
  Console.printf("Home Assistant: connecting to %s:%u\n", haSettings.host, haSettings.port);
}

// Asks the worker to say goodbye and stop the client. The loop goes on at
// once; only the hotspot waits (up to HA_STOP_WAIT_MS), as it takes the
// radio right after.
void haStopLink(bool wait) {
  if (haPhase == HA_PHASE_STARTING || haPhase == HA_PHASE_RUNNING) {
    HaCmd c = {};
    c.kind = HA_CMD_STOP;
    c.connected = (haState == HA_UP) ? 1 : 0;
    if (!haCommand(c)) { return; }   // the worker is busy: again on the next pass
    haPhase = HA_PHASE_STOPPING;
    haState = HA_STOPPING;
  }
  if (!wait || haPhase != HA_PHASE_STOPPING) { return; }
  static HaIn m;   // haUpdate()'s is busy only when it calls this
  unsigned long t0 = millis();
  while (haPhase == HA_PHASE_STOPPING && millis() - t0 < HA_STOP_WAIT_MS) {
    if (xQueueReceive(haLink().in, &m, pdMS_TO_TICKS(50)) == pdTRUE) { haApply(m); }
  }
}

// A value's figure now, and its payload; false when there is none (no sensor,
// or no reading yet).
bool haValueNow(uint8_t i, float &v, char *payload, size_t size) {
  switch (i) {
    case HA_VALUE_LIGHT:
      if (!luxOK || luxAvg < 0) { return false; }
      v = luxAvg;   // the filtered light the brightness follows
      snprintf(payload, size, "%ld", lroundf(v));
      return true;
#if BOARD_WAVESHARE_WALL
    case HA_VALUE_TEMP: {
      if (!roomValid) { return false; }
      v = roomTempNow();
      long t = lroundf(v * 10.0f);   // tenths, without the C library's float printing
      snprintf(payload, size, "%s%ld.%ld", t < 0 ? "-" : "", labs(t) / 10, labs(t) % 10);
      return true;
    }
    case HA_VALUE_HUM:
      if (!roomValid) { return false; }
      v = roomHumNow();
      snprintf(payload, size, "%ld", lroundf(v));
      return true;
#endif
    default:
      return false;
  }
}

// While connected, on every pass: each value that is due (haValues).
void haSendValues() {
  unsigned long minMs = haSettings.minS * 1000UL, maxMs = haSettings.maxMin * 60000UL;
  for (uint8_t i = 0; i < HA_VALUE_COUNT; i++) {
    HaValue &hv = haValues[i];
    HaCmd c = {};
    float v;
    if (!haValueNow(i, v, c.payload, sizeof(c.payload))) { continue; }
    unsigned long since = millisNow - hv.at;
    float step = hv.relative ? max(1.0f, hv.step * hv.last) : hv.step;
    bool changed = fabsf(v - hv.last) >= step;
    if (hv.at != 0 && since < maxMs && !(changed && since >= minMs)) { continue; }
    c.kind = HA_CMD_SEND;
    strlcpy(c.suffix, hv.suffix, sizeof(c.suffix));
    if (!haCommand(c)) { return; }   // the worker's queue is full: on a later pass
    hv.last = v;
    hv.at = millisNow ? millisNow : 1;
  }
}

// Every value goes out again at once: after a connect, and when Home
// Assistant has started anew.
void haValuesAgain() {
  for (uint8_t i = 0; i < HA_VALUE_COUNT; i++) { haValues[i].at = 0; }
}

// On every pass outside the hotspot: takes what the worker and the client's
// task passed on, has the client run while the link is on and has what it
// needs, and sends the sensors' values when they are due. Never waits for the
// network.
void haUpdate() {
  static HaIn m;   // 0.5 KB, off the loop's stack
  while (haLink().in && xQueueReceive(haLink().in, &m, 0) == pdTRUE) { haApply(m); }
  uint32_t lost = haLink().lost.load();
  if (lost != haLostSeen) {
    Console.printf("Home Assistant: %lu messages lost, the loop was busy\n", (unsigned long)(lost - haLostSeen));
    haLostSeen = lost;
  }
  bool want = haSettings.on && !haBlocked();
  if (haPhase == HA_PHASE_STARTING || haPhase == HA_PHASE_RUNNING) {
    if (!want || haChanged) {
      haStopLink(false);
    } else if (haState == HA_UP) {
      haSendValues();
    }
    return;
  }
  if (haPhase == HA_PHASE_STOPPING) { return; }   // a new start waits for the old client to be gone
  if (!want) {
    haState = haSettings.on ? HA_BLOCKED : HA_OFF;
    return;
  }
  if (!stationUp()) { haState = HA_WAITING; return; }
  if (haFailedAt && millisNow - haFailedAt < HA_RETRY_MS) { haState = HA_FAILED; return; }
  haStartLink();
}

void haApply(const HaIn &m) {
  // The worker's answers.
  if (m.kind == HA_IN_STARTED) {
    if (haPhase == HA_PHASE_STARTING) { haPhase = HA_PHASE_RUNNING; }
    return;
  }
  if (m.kind == HA_IN_START_FAILED || m.kind == HA_IN_STOPPED) {
    haPhase = HA_PHASE_IDLE;
    radioUsers &= (uint8_t)~RADIO_HA;
    if (m.kind == HA_IN_START_FAILED) { haFailed(); }
    else { haState = HA_OFF; Console.println("Home Assistant: link stopped"); }
    return;
  }
  // The client's: none counts once its stop was asked for, nor once new
  // settings came (haChanged until the next start): what it had queued
  // before the next pass asks for the stop would otherwise bring back the
  // items switching the link off took away.
  if ((haPhase != HA_PHASE_STARTING && haPhase != HA_PHASE_RUNNING) || haChanged) { return; }
  switch (m.kind) {
    case HA_IN_CONNECTED:
      haState   = HA_UP;
      haUpSince = clockIsSet() ? clockNow() : 0;
      haError[0] = '\0';
      haValuesAgain();
      haDropItems();   // the broker sends the retained ones anew at once
      Console.printf("Home Assistant: connected to %s:%u\n", haSettings.host, haSettings.port);
      break;
    case HA_IN_DISCONNECTED:
      if (haState == HA_UP) { Console.println("Home Assistant: connection lost - the client tries again"); }
      haState = HA_CONNECTING;
      break;
    case HA_IN_ERROR:
      if (m.error == HA_ERR_TOO_LONG) {
        Console.printf("Home Assistant: %s refused, %ld bytes (%u taken)\n", m.topic, (long)m.detail, HA_PAYLOAD_MAX);
        break;
      }
      haErrorText(m);
      Console.printf("Home Assistant: %s\n", haError);
      break;
    case HA_IN_DATA:
      if (!m.own) {
        // Home Assistant has started anew: it has forgotten what is not retained.
        if (strcmp(m.topic, "homeassistant/status") == 0 && strcmp(m.payload, "online") == 0) {
          Console.println("Home Assistant has started - the clock announces itself again");
          HaCmd c = {};
          c.kind = HA_CMD_ANNOUNCE;
          haCommand(c);
          haValuesAgain();
        }
      } else if (strncmp(m.topic, "item/", 5) == 0) {
        haItem(m.topic + 5, m.payload, m.len);
      } else if (strcmp(m.topic, "message") == 0) {
        // The broker sets the retain flag only on a kept message it hands out
        // at a (re)connect, never on one it passes on live: such a message
        // was shown when it came, and would come again with every connect.
        // An empty one is how a kept message is taken off the broker.
        if (m.retained)    { Console.println("Home Assistant: a message kept by the broker is not shown again"); }
        else if (m.len)    { haMessage(m.payload, m.len); }
      }
      break;
    default:
      break;
  }
}

// The items Home Assistant sent, but the messages.
void haDropItems() {
  for (int i = (int)bandItemCount - 1; i >= 0; i--) {
    if (bandItems[i].feed == SOURCE_HA && !isMessage(bandItems[i])) { bandRemoveAt((uint8_t)i); }
  }
}

// What went wrong, in words, for the config page.
void haErrorText(const HaIn &m) {
  char where[80];
  snprintf(where, sizeof(where), "%s:%u", haSettings.host, haSettings.port);
  if (m.error == HA_ERR_REFUSED) {
    if (m.detail == MQTT_CONNECTION_REFUSE_BAD_USERNAME || m.detail == MQTT_CONNECTION_REFUSE_NOT_AUTHORIZED) {
      strlcpy(haError, "The broker refused the user name or password", sizeof(haError));
    } else if (m.detail == MQTT_CONNECTION_REFUSE_ID_REJECTED) {
      strlcpy(haError, "The broker refused the clock's client name", sizeof(haError));
    } else {
      snprintf(haError, sizeof(haError), "The broker refused the connection (code %ld)", (long)m.detail);
    }
  } else if (m.error == HA_ERR_NETWORK) {
    if (m.tlsError == ESP_ERR_ESP_TLS_CANNOT_RESOLVE_HOSTNAME) {
      snprintf(haError, sizeof(haError), "Address not found: %s", haSettings.host);
    } else if (m.detail == ECONNREFUSED) {
      snprintf(haError, sizeof(haError), "Nothing answers at %s", where);
    } else if (m.tlsError == ESP_ERR_ESP_TLS_CONNECTION_TIMEOUT || m.detail == ETIMEDOUT) {
      snprintf(haError, sizeof(haError), "No answer from %s", where);
    } else if (m.detail == EHOSTUNREACH) {
      snprintf(haError, sizeof(haError), "%s cannot be reached", where);
    } else {
      snprintf(haError, sizeof(haError), "The connection to %s failed (%s, errno %ld)", where,
               m.tlsError ? esp_err_to_name(m.tlsError) : "no TLS error", (long)m.detail);
    }
  } else {
    snprintf(haError, sizeof(haError), "MQTT error %ld", (long)m.detail);
  }
}

// A name Home Assistant gives an item: 1 to 23 of a-z, A-Z, 0-9, _ and -.
bool haNameOk(const char *name) {
  size_t n = strlen(name);
  if (n == 0 || n >= sizeof(BandItem::key)) { return false; }
  for (const char *p = name; *p; p++) {
    if (!isalnum((unsigned char)*p) && *p != '_' && *p != '-') { return false; }
  }
  return true;
}

// The optional fields of an item or a message that its icon shows: a
// warning's level (severity: minor, moderate, severe, extreme), how heavy
// rain or snow is (intensity: light, moderate, heavy) and how likely it is
// (chance, 0 to 100 %). A value that is none of them counts as not given.
void haIconLook(JsonDocument &doc, BandItem &it) {
  it.severity  = itemSeverityByName(doc["severity"] | "");
  it.intensity = itemIntensityByName(doc["intensity"] | "");
  JsonVariant chance = doc["chance"];
  it.chance = chance.is<float>() ? (uint8_t)constrain(lroundf(chance.as<float>()), 0L, 100L) : CHANCE_UNKNOWN;
}

// mpclock/<id>/item/<name>: an item for the band, or its end (an empty
// payload, or an until that has passed).
void haItem(const char *name, const char *payload, uint16_t len) {
  if (!haNameOk(name)) {
    Console.printf("Home Assistant: item name \"%s\" not taken (1 to 23 of a-z, 0-9, _ and -)\n", name);
    return;
  }
  if (len == 0) { bandRemove(SOURCE_HA, name); return; }
  JsonDocument doc;
  DeserializationError e = deserializeJson(doc, payload, len);
  if (e || !doc.is<JsonObject>()) {
    Console.printf("Home Assistant: item %s is no JSON object (%s)\n", name, e ? e.c_str() : "another JSON value");
    return;
  }
  long long until = doc["until"].as<long long>();
  if (until <= 0) {
    Console.printf("Home Assistant: item %s has no until (Unix time) - not taken\n", name);
    return;
  }
  if (clockIsSet() && (time_t)until <= clockNow()) { bandRemove(SOURCE_HA, name); return; }
  const char *text = doc["text"] | "";
  uint8_t icon = itemIconByName(doc["icon"] | "");
  if (!text[0] && icon == ICON_NONE) {
    Console.printf("Home Assistant: item %s has neither a text nor an icon - not taken\n", name);
    return;
  }
  BandItem it = {};
  strlcpy(it.key, name, sizeof(it.key));
  itemTextCopy(it.text, text, sizeof(it.text));
  it.icon  = icon;
  it.level = strcmp(doc["level"] | "info", "alert") == 0 ? LEVEL_ALERT : LEVEL_INFO;
  haIconLook(doc, it);
  itemTextCopy(it.shortText, doc["short"] | "", sizeof(it.shortText));   // the card's two lines, "a|b"
  it.feed  = SOURCE_HA;
  it.until = (time_t)until;
  it.tag   = itemTag(name);   // a new text keeps the alert; switched from info to alert it comes up again
  bandPut(it);
}

// mpclock/<id>/message: for `seconds` (HA_MESSAGE_S unless it says, 5 to
// 3600), an item like the others, up to HA_MESSAGES_MAX at a time: a new one
// takes a free place, or the oldest message's - an urgent one's only when all
// of them are urgent. On the 64x32 clock its text
// runs through, an urgent one's (level alert) after its icon, each message
// on its own.
void haMessage(const char *payload, uint16_t len) {
  static uint32_t messages = 0;
  JsonDocument doc;
  DeserializationError e = deserializeJson(doc, payload, len);
  if (e || !doc.is<JsonObject>()) {
    Console.printf("Home Assistant: the message is no JSON object (%s)\n", e ? e.c_str() : "another JSON value");
    return;
  }
  const char *text = doc["text"] | "", *title = doc["title"] | "";
  if (!text[0] && !title[0]) { Console.println("Home Assistant: an empty message"); return; }
  long seconds = constrain(doc["seconds"] | (long)HA_MESSAGE_S, 5L, 3600L);
  char line[HA_PAYLOAD_MAX + 1];
  if (title[0] && text[0]) { snprintf(line, sizeof(line), "%s | %s", title, text); }   // the band's separator
  else                     { strlcpy(line, text[0] ? text : title, sizeof(line)); }
  BandItem it = {};
  int8_t oldest = -1;   // the place to take when none is free: the oldest message, an urgent one only when all are
  for (uint8_t m = 1; m <= HA_MESSAGES_MAX; m++) {
    snprintf(it.key, sizeof(it.key), "*msg%u", m);
    int8_t i = bandFind(SOURCE_HA, it.key);
    if (i < 0) { oldest = -1; break; }   // a free place
    if (oldest < 0) { oldest = i; continue; }
    bool urgent = bandItems[i].level == LEVEL_ALERT, oldestUrgent = bandItems[oldest].level == LEVEL_ALERT;
    if (urgent != oldestUrgent ? oldestUrgent : bandItems[i].seq < bandItems[oldest].seq) { oldest = i; }
  }
  if (oldest >= 0) { strlcpy(it.key, bandItems[oldest].key, sizeof(it.key)); }
  itemTextCopy(it.text, line, sizeof(it.text));
  it.icon  = itemIconByName(doc["icon"] | "");
  it.level = strcmp(doc["level"] | "info", "alert") == 0 ? LEVEL_ALERT : LEVEL_INFO;
  haIconLook(doc, it);
  it.feed  = SOURCE_HA;
  it.until = clockNow() + seconds;
  it.tag   = ++messages;
  bandPut(it);
}

// The settings for the page; never the password, only whether there is one.
void haSettingsInto(JsonObject o) {
  o["on"] = haSettings.on != 0;
  o["host"] = haSettings.host;
  o["port"] = haSettings.port;
  o["user"] = haSettings.user;
  o["hasPassword"] = haSettings.pass[0] != 0;
  o["topics"] = haLink().prefix;
  o["minS"] = haSettings.minS;
  o["maxMin"] = haSettings.maxMin;
}

// What the link does, for the page. The hotspot stops it while it is open.
void haStatusInto(JsonObject o) {
  static const char *const STATE_NAMES[] = { "off", "blocked", "waiting", "failed", "connecting", "up", "stopping" };
  o["state"] = (apActive && haSettings.on) ? "hotspot" : STATE_NAMES[haState];
  const char *blocked = haBlocked();
  if (haState == HA_BLOCKED && blocked) { o["blocked"] = blocked; }
  if (haError[0]) { o["error"] = haError; }
  if (haState == HA_UP && haUpSince) {
    char when[16];
    bandWhen(when, sizeof(when), haUpSince);
    o["since"] = when;
  }
  uint8_t n = 0;
  for (uint8_t i = 0; i < bandItemCount; i++) { n += bandItems[i].feed == SOURCE_HA; }
  o["items"] = n;
}

// /api/ha: on (on or off), host, port, user and pw (empty keeps the one
// stored, unless the user is cleared too), and hamin (s) and hamax (min), the
// sensors' values at most and at least that often (haValues; absent keeps
// them). Taken at once: another link starts the client again with it, the
// intervals count from the next value.
void apiHa(Print &c, const String &body) {
  bool on = getParam(body, "on") == "on";
  String host = getParam(body, "host"), user = getParam(body, "user"), pw = getParam(body, "pw");
  String portText = getParam(body, "port");
  host.trim();
  user.trim();
  portText.trim();
  long port = portText.length() ? portText.toInt() : HA_PORT_DEFAULT;
  if (host.length() >= sizeof(haSettings.host) || host.indexOf(' ') >= 0) {
    sendJsonError(c, 400, "The broker's address is a name or an IP address, up to 63 characters.");
    return;
  }
  if (port < 1 || port > 65535) { sendJsonError(c, 400, "The port is a number from 1 to 65535."); return; }
  if (user.length() >= sizeof(haSettings.user) || pw.length() >= sizeof(haSettings.pass)) {
    sendJsonError(c, 400, "User name and password have up to 63 characters each.");
    return;
  }
  if (on && !host.length()) { sendJsonError(c, 400, "Enter the broker's address to switch the link on."); return; }
  // The values' intervals (haValues); absent keeps them.
  String minText = getParam(body, "hamin"), maxText = getParam(body, "hamax");
  long minS = minText.length() ? minText.toInt() : haSettings.minS;
  long maxMin = maxText.length() ? maxText.toInt() : haSettings.maxMin;
  if (minS < HA_MIN_S_LOW || minS > 255) { sendJsonError(c, 400, "Send at most every 5 to 255 s."); return; }
  if (maxMin < 1 || maxMin > 255) { sendJsonError(c, 400, "Send at least every 1 to 255 min."); return; }
  if (maxMin * 60 < minS) { sendJsonError(c, 400, "The longest interval must not be shorter than the shortest."); return; }
  bool wasOn = haSettings.on != 0;
  HaSettings before = haSettings;
  strlcpy(haSettings.host, host.c_str(), sizeof(haSettings.host));
  haSettings.port = (uint16_t)port;
  strlcpy(haSettings.user, user.c_str(), sizeof(haSettings.user));
  if (pw.length())         { strlcpy(haSettings.pass, pw.c_str(), sizeof(haSettings.pass)); }
  else if (!user.length()) { haSettings.pass[0] = '\0'; }   // a password belongs to a user
  haSettings.on = on ? 1 : 0;
  haSettings.minS = (uint8_t)minS;
  haSettings.maxMin = (uint8_t)maxMin;
  bool stored = saveHaSettings();
  // Only another link starts the client anew; the intervals count at once.
  bool relink = haSettings.on != before.on || haSettings.port != before.port ||
                strcmp(haSettings.host, before.host) != 0 || strcmp(haSettings.user, before.user) != 0 ||
                strcmp(haSettings.pass, before.pass) != 0;
  if (relink) {
    haChanged = true;
    haFailedAt = 0;
    haError[0] = '\0';
  }
  if (wasOn && !on) { bandDropFeed(SOURCE_HA); }   // switched off, the link takes its items with it
  Console.printf("Home Assistant: %s, broker %s:%u%s, values at most every %u s, at least every %u min\n", on ? "on" : "off",
                haSettings.host, haSettings.port, pw.length() ? ", new password" : "", haSettings.minS, haSettings.maxMin);
  if (!stored) {
    sendJsonError(c, 500, "Not saved: the clock could not write its flash. The link works so until the next restart.");
    return;
  }
  JsonDocument d;
  d["ok"] = true;
  haSettingsInto(d["ha"].to<JsonObject>());
  sendJson(c, d);
}
