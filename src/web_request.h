/* ----------------------------------------------------------------------
   One connection to the clock's web server: the request as it arrives, and
   the answer built whole in RAM before any of it is sent. The sketch reads,
   answers and sends over several passes of the loop (webUpdate()), so the
   loop never waits for the network.
   ---------------------------------------------------------------------- */
#ifndef WEB_REQUEST_H
#define WEB_REQUEST_H

#include <Arduino.h>
#include <WiFi.h>
#include <esp_heap_caps.h>
#include <lwip/sockets.h>   // lwip_send() with MSG_DONTWAIT: sending without waiting

struct HttpRequest {
  String line;            // "GET /path?query HTTP/1.1"
  String path, query, body;
  String auth;            // the Authorization header: the password on the home network
  String host;            // the host the client thinks it talks to (captive-portal probes)
  long contentLength = 0;
  bool isPost = false;
};

// The request line: method, path and query.
inline void httpRequestLine(HttpRequest &r, const String &line) {
  r.line = line;
  r.line.trim();
  int sp1 = r.line.indexOf(' ');
  int sp2 = r.line.indexOf(' ', sp1 + 1);
  String target = (sp1 >= 0 && sp2 > sp1) ? r.line.substring(sp1 + 1, sp2) : String("/");
  int q = target.indexOf('?');
  r.path = q >= 0 ? target.substring(0, q) : target;
  r.query = q >= 0 ? target.substring(q + 1) : String();
  r.isPost = r.line.startsWith("POST ");
}

// One header line: the length of a form's body, the password, the host.
inline void httpHeader(HttpRequest &r, const String &h) {
  if (h.startsWith("Content-Length:") || h.startsWith("content-length:")) { r.contentLength = h.substring(15).toInt(); }
  if (h.startsWith("Authorization:") || h.startsWith("authorization:")) { r.auth = h.substring(14); r.auth.trim(); }
  if (h.startsWith("Host:") || h.startsWith("host:")) { r.host = h.substring(5); r.host.trim(); }
}

// An answer built whole before any of it is sent: in PSRAM, grown as it is
// written - the web app's page is the largest, 27 KB gzipped - and given back
// after a large one. A write it has no room for is dropped, and the client
// gets a short answer.
class ResponseBuffer : public Print {
 public:
  size_t write(uint8_t b) override { return write(&b, 1); }
  size_t write(const uint8_t *data, size_t size) override {
    if (!room(_len + size)) { return 0; }
    memcpy(_buf + _len, data, size);
    _len += size;
    return size;
  }
  const uint8_t *data() const { return _buf; }
  size_t length() const { return _len; }
  void clear() {
    _len = 0;
    if (_cap > KEEP) {
      heap_caps_free(_buf);
      _buf = nullptr;
      _cap = 0;
    }
  }

 private:
  static const size_t KEEP = 8192;
  bool room(size_t need) {
    if (need <= _cap) { return true; }
    size_t cap = _cap ? _cap : 2048;
    while (cap < need) { cap *= 2; }
    void *b = heap_caps_realloc(_buf, cap, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!b) { b = heap_caps_realloc(_buf, cap, MALLOC_CAP_8BIT); }
    if (!b) { return false; }
    _buf = (uint8_t *)b;
    _cap = cap;
    return true;
  }
  uint8_t *_buf = nullptr;
  size_t _len = 0, _cap = 0;
};

// Where a connection stands: its request line and headers arriving, its
// form's body arriving, its answer going out.
enum WebPhase : uint8_t { WEB_FREE, WEB_HEAD, WEB_BODY, WEB_SEND };

struct WebConn {
  WiFiClient client;
  WebPhase phase = WEB_FREE;
  bool lan = false;            // from the home network (the password is asked for)
  HttpRequest req;
  String line;                 // the header line arriving
  bool gotLine = false;        // the request line is in
  size_t headBytes = 0;
  unsigned long acceptedAt = 0;
  unsigned long progressAt = 0;   // the last byte read or sent
  ResponseBuffer out;
  size_t sent = 0;
  // What the request asked for once its answer is out: the hotspot closed
  // (after saving), with a join of the home WiFi at once, or a restart.
  bool closeHotspot = false, syncNow = false, restart = false;
};

#endif  // WEB_REQUEST_H
