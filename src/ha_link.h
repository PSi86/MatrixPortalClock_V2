/* ----------------------------------------------------------------------
   Home Assistant link: MQTT through the broker Home Assistant runs (the
   Mosquitto add-on), with the client ESP-IDF brings along (esp-mqtt, in the
   core's libraries). It runs in a task of its own, keeps the connection
   alive and connects again by itself after a break (concept doc, section
   "Sources and the Home Assistant link").

   Topics, <id> being the last three bytes of the MAC, as in the host name:
     mpclock/<id>/item/<name>  Home Assistant to the clock, retained: an item
                               for the band, JSON text, icon, level (info or
                               alert) and until (Unix time, required, so an
                               item never outlives Home Assistant); an empty
                               payload takes it away
     mpclock/<id>/message      Home Assistant to the clock: JSON text, title,
                               level, icon, seconds
     mpclock/<id>/status       the clock to Home Assistant, retained: online,
                               or offline as the last will
     mpclock/<id>/light        the clock to Home Assistant: the light sensor's lux
     homeassistant/<component>/<id>/<object>/config
                               the clock to Home Assistant, retained: MQTT
                               discovery, so the clock is a device without YAML

   The loop never calls the client: its task holds the client's lock for as
   long as a connect takes (up to the network timeout), and a stop waits for
   that task to end - measured on 2026-10-09, a stop from the loop held the
   panel for 5 and 9 s. So a worker task of our own makes every call (start,
   stop, send), on commands from the loop (HaCmd), and the client's task only
   copies what arrives into a queue (HaIn); the loop takes it from there, and
   only the loop touches the items. The S3 boards only.
   ---------------------------------------------------------------------- */
#pragma once

#include <Arduino.h>
#include <atomic>
#include <errno.h>
#include <mqtt_client.h>
#include <esp_tls_errors.h>
#include <esp_heap_caps.h>
#include <freertos/idf_additions.h>

// ---- To the loop ---------------------------------------------------------------

enum HaInKind : uint8_t {
  HA_IN_STARTED, HA_IN_START_FAILED, HA_IN_STOPPED,                  // from the worker
  HA_IN_CONNECTED, HA_IN_DISCONNECTED, HA_IN_ERROR, HA_IN_DATA,      // from the client's task
};
enum HaError : uint8_t { HA_ERR_NETWORK, HA_ERR_REFUSED, HA_ERR_TOO_LONG, HA_ERR_OTHER };

const uint16_t HA_TOPIC_MAX   = 48;    // a topic past the clock's own prefix, with its NUL
const uint16_t HA_PAYLOAD_MAX = 384;   // the longest payload taken; a longer one is refused

struct HaIn {
  uint8_t  kind;          // HaInKind
  uint8_t  error;         // HA_IN_ERROR: HaError
  uint8_t  retained;      // HA_IN_DATA: sent as a retained message
  uint8_t  own;           // HA_IN_DATA: under the clock's prefix, which topic has taken off
  int32_t  detail;        // HA_IN_ERROR: the socket's errno, the broker's return code, or the length refused
  int32_t  tlsError;      // HA_IN_ERROR: esp-tls's last error (the plain TCP connect goes through it too)
  uint16_t len;           // HA_IN_DATA: bytes in payload
  char     topic[HA_TOPIC_MAX];
  char     payload[HA_PAYLOAD_MAX + 1];   // NUL-terminated
};

// ---- From the loop ---------------------------------------------------------------

enum HaCmdKind : uint8_t { HA_CMD_START, HA_CMD_STOP, HA_CMD_SEND, HA_CMD_ANNOUNCE };

struct HaCmd {
  uint8_t kind;           // HaCmdKind
  uint8_t qos;            // HA_CMD_SEND
  uint8_t retain;         // HA_CMD_SEND
  uint8_t connected;      // HA_CMD_STOP: connected, so it says goodbye first
  char    suffix[16];     // HA_CMD_SEND: the topic past the clock's prefix
  char    payload[32];    // HA_CMD_SEND
};

// ---- The link ----------------------------------------------------------------------

// What the worker and the client's task read while the client exists: the
// loop sets it before HA_CMD_START and leaves it alone until HA_IN_STOPPED.
const uint8_t HA_ANNOUNCE_MAX = 2;   // discovery configs: the message entity and the light sensor
struct HaLink {
  esp_mqtt_client_handle_t client = nullptr;   // only the worker touches it
  QueueHandle_t in = nullptr;                  // to the loop
  QueueHandle_t cmd = nullptr;                 // from the loop
  char     host[64] = "", user[64] = "", pass[64] = "", clientId[32] = "";
  uint16_t port = 1883;
  char     prefix[24] = "";           // "mpclock/06c16c/"
  char     status[40] = "";           // the status topic
  char     items[40] = "";            // the items' filter, "mpclock/06c16c/item/+"
  char     message[40] = "";          // the message topic
  char     announceTopic[HA_ANNOUNCE_MAX][64] = {};
  String   announce[HA_ANNOUNCE_MAX];  // empty takes a config back (a sensor no longer found)
  std::atomic<uint32_t> lost{0};       // what the loop did not take in time
};
inline HaLink &haLink() { static HaLink l; return l; }

// The clock's topics; id is "06c16c".
inline void haTopics(const char *id) {
  HaLink &l = haLink();
  snprintf(l.prefix, sizeof(l.prefix), "mpclock/%s/", id);
  snprintf(l.status, sizeof(l.status), "%sstatus", l.prefix);
  snprintf(l.items, sizeof(l.items), "%sitem/+", l.prefix);
  snprintf(l.message, sizeof(l.message), "%smessage", l.prefix);
}

// Waits up to a second for the loop, which empties the queue on every pass;
// what it does not take in that time is lost, and counted.
inline void haPost(const HaIn &m) {
  if (xQueueSend(haLink().in, &m, pdMS_TO_TICKS(1000)) != pdTRUE) { haLink().lost++; }
}

// Online and the discovery configs, all retained: on every connect, and
// again when Home Assistant has started anew. Queued in the client's outbox.
// Only in the worker or the client's task.
inline void haAnnounce() {
  HaLink &l = haLink();
  if (!l.client) { return; }
  esp_mqtt_client_enqueue(l.client, l.status, "online", 0, 1, 1, true);
  for (uint8_t i = 0; i < HA_ANNOUNCE_MAX; i++) {
    if (!l.announceTopic[i][0]) { continue; }
    esp_mqtt_client_enqueue(l.client, l.announceTopic[i], l.announce[i].c_str(), (int)l.announce[i].length(), 1, 1, true);
  }
}

// ---- The client's task --------------------------------------------------------------

inline void haEvent(void *, esp_event_base_t, int32_t id, void *data) {
  esp_mqtt_event_handle_t e = (esp_mqtt_event_handle_t)data;
  HaLink &l = haLink();
  static HaIn m;   // 0.5 KB, kept off the task's stack; only this task comes here
  memset(&m, 0, sizeof(m));
  switch ((esp_mqtt_event_id_t)id) {
    case MQTT_EVENT_CONNECTED:
      m.kind = HA_IN_CONNECTED;
      haPost(m);   // first: the loop clears the old items before the retained ones come again
      esp_mqtt_client_subscribe(e->client, l.items, 1);
      esp_mqtt_client_subscribe(e->client, l.message, 1);
      esp_mqtt_client_subscribe(e->client, "homeassistant/status", 1);
      haAnnounce();
      break;
    case MQTT_EVENT_DISCONNECTED:
      m.kind = HA_IN_DISCONNECTED;
      haPost(m);
      break;
    case MQTT_EVENT_ERROR:
      m.kind = HA_IN_ERROR;
      m.error = HA_ERR_OTHER;
      if (e->error_handle) {
        const esp_mqtt_error_codes_t *er = e->error_handle;
        if (er->error_type == MQTT_ERROR_TYPE_CONNECTION_REFUSED) {
          m.error  = HA_ERR_REFUSED;
          m.detail = er->connect_return_code;
        } else if (er->error_type == MQTT_ERROR_TYPE_TCP_TRANSPORT) {
          m.error    = HA_ERR_NETWORK;
          m.detail   = er->esp_transport_sock_errno;
          m.tlsError = er->esp_tls_last_esp_err;
        } else {
          m.detail = er->error_type;
        }
      }
      haPost(m);
      break;
    case MQTT_EVENT_DATA: {
      if (e->current_data_offset != 0) { break; }   // the rest of a long one, refused with its start
      size_t plen = strlen(l.prefix);
      bool own = e->topic_len > (int)plen && strncmp(e->topic, l.prefix, plen) == 0;
      int tlen = e->topic_len - (own ? (int)plen : 0);
      memcpy(m.topic, e->topic + (own ? plen : 0), (size_t)min(tlen, (int)HA_TOPIC_MAX - 1));
      m.own = own ? 1 : 0;
      if (e->total_data_len > HA_PAYLOAD_MAX || tlen >= (int)HA_TOPIC_MAX) {
        m.kind   = HA_IN_ERROR;
        m.error  = HA_ERR_TOO_LONG;
        m.detail = e->total_data_len;
        haPost(m);
        break;
      }
      m.kind     = HA_IN_DATA;
      m.retained = e->retain ? 1 : 0;
      m.len      = (uint16_t)e->data_len;
      memcpy(m.payload, e->data, m.len);
      m.payload[m.len] = '\0';
      haPost(m);
      break;
    }
    default:
      break;
  }
}

// ---- The worker ---------------------------------------------------------------------

// Makes the client from what the loop set in haLink() (esp-mqtt copies the
// strings) and starts it; it connects, and again after every break, by itself.
inline bool haClientStart() {
  HaLink &l = haLink();
  if (l.client) { return true; }
  esp_mqtt_client_config_t cfg = {};
  cfg.broker.address.hostname  = l.host;
  cfg.broker.address.port      = l.port;
  cfg.broker.address.transport = MQTT_TRANSPORT_OVER_TCP;   // on the home network, without TLS
  cfg.credentials.client_id    = l.clientId;
  cfg.credentials.username     = l.user[0] ? l.user : nullptr;
  cfg.credentials.authentication.password = l.pass[0] ? l.pass : nullptr;
  cfg.session.last_will.topic  = l.status;
  cfg.session.last_will.msg    = "offline";
  cfg.session.last_will.qos    = 1;
  cfg.session.last_will.retain = 1;
  cfg.session.keepalive        = 60;      // the broker calls the clock offline about 90 s after it went
  cfg.network.timeout_ms       = 5000;
  cfg.network.reconnect_timeout_ms = 10000;
  cfg.outbox.limit             = 8192;    // what waits for a connection, in bytes
  l.client = esp_mqtt_client_init(&cfg);
  if (!l.client) { return false; }
  esp_mqtt_client_register_event(l.client, MQTT_EVENT_ANY, haEvent, nullptr);
  if (esp_mqtt_client_start(l.client) != ESP_OK) {
    esp_mqtt_client_destroy(l.client);
    l.client = nullptr;
    return false;
  }
  return true;
}

// Says goodbye while connected (offline, retained: a clean disconnect sends
// no last will) and stops the client; both wait for the client's lock, so
// for a connect under way.
inline void haClientStop(bool connected) {
  HaLink &l = haLink();
  if (!l.client) { return; }
  if (connected) { esp_mqtt_client_publish(l.client, l.status, "offline", 0, 1, 1); }
  esp_mqtt_client_stop(l.client);
  esp_mqtt_client_destroy(l.client);
  l.client = nullptr;
}

inline void haWorker(void *) {
  HaLink &l = haLink();
  static HaIn m;   // the worker's own; the client's task has another
  HaCmd c;
  for (;;) {
    if (xQueueReceive(l.cmd, &c, portMAX_DELAY) != pdTRUE) { continue; }
    switch (c.kind) {
      case HA_CMD_START:
        memset(&m, 0, sizeof(m));
        m.kind = haClientStart() ? HA_IN_STARTED : HA_IN_START_FAILED;
        haPost(m);
        break;
      case HA_CMD_STOP:
        haClientStop(c.connected != 0);
        memset(&m, 0, sizeof(m));
        m.kind = HA_IN_STOPPED;
        haPost(m);
        break;
      case HA_CMD_SEND:
        if (l.client) {
          char topic[64];
          snprintf(topic, sizeof(topic), "%s%s", l.prefix, c.suffix);
          esp_mqtt_client_enqueue(l.client, topic, c.payload, 0, c.qos, c.retain, true);
        }
        break;
      case HA_CMD_ANNOUNCE:
        haAnnounce();
        break;
    }
  }
}

// ---- For the loop -------------------------------------------------------------------

// The queues and the worker, made when the link first starts; false when
// there was no RAM for them. The queue to the loop (3.6 KB) and the worker's
// stack go to PSRAM when there is some, as neither touches the flash: the
// internal RAM is short while a TLS fetch runs.
const uint32_t HA_WORKER_STACK = 4096;
const UBaseType_t HA_IN_DEPTH = 8;
inline bool haBegin() {
  HaLink &l = haLink();
  if (l.cmd) { return true; }
  if (!l.in) { l.in = xQueueCreateWithCaps(HA_IN_DEPTH, sizeof(HaIn), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT); }
  if (!l.in) { l.in = xQueueCreate(HA_IN_DEPTH, sizeof(HaIn)); }
  if (!l.in) { return false; }
  l.cmd = xQueueCreate(4, sizeof(HaCmd));   // before the worker, which waits on it at once
  if (!l.cmd) { return false; }
  if (xTaskCreatePinnedToCoreWithCaps(haWorker, "ha", HA_WORKER_STACK, nullptr, 2, nullptr, 0,
                                      MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) == pdPASS ||
      xTaskCreatePinnedToCore(haWorker, "ha", HA_WORKER_STACK, nullptr, 2, nullptr, 0) == pdPASS) {
    return true;
  }
  vQueueDelete(l.cmd);
  l.cmd = nullptr;
  return false;
}

// Hands the worker a command without waiting; false when its queue is full.
inline bool haCommand(const HaCmd &c) {
  return haLink().cmd && xQueueSend(haLink().cmd, &c, 0) == pdTRUE;
}
