// The web server beside the loop, and its answers (web_server.h).
#include "web_server.h"

#include "clock_state.h"
#include "debug_stats.h"
#include "console.h"
#include "hotspot.h"
#include "lan_access.h"
#include "settings_table.h"   // hexVal()
#include "time_sync.h"
#include "web_api.h"
#include "web_request.h"

// What only this module calls, ahead of its use.
static void webAccept(bool lan);
static void webStep(WebConn &w);
static void webReadHead(WebConn &w);
static void webReadBody(WebConn &w);
static void webAnswer(WebConn &w);
static void webSend(WebConn &w);
static void webClose(WebConn &w, bool dropped);
static void webAfter();
static String urldecode(const String &s);

// (HTTP_BODY_MAX, the largest POST body taken: web_server.h.)

// The web server runs beside the loop (webUpdate()): a few connections at a
// time, each read, answered and sent a piece per pass. Until 2026-10-21 it
// served one request at a time and held the loop for it - 15 ms on average,
// 35 ms at most for the web app's panel preview (S3, debug build) - so the
// running text and the fly-in stuttered while the page was open.
const uint8_t       WEB_CONNS      = 4;      // browsers open a few at once, some only in case
const unsigned long WEB_HEAD_MS    = 2000;   // the request line and headers have this long to arrive
const unsigned long WEB_STALL_MS   = 5000;   // a body or an answer that moves no byte for this long is dropped
const size_t        WEB_HEAD_MAX   = 4096;   // the request line and headers together
const size_t        WEB_SEND_MAX   = 2920;   // bytes handed to the socket a pass: two full TCP segments
const unsigned long WEB_AFTER_MS   = 300;    // the answer reaches the phone before the hotspot closes or the clock restarts
static WebConn webConns[WEB_CONNS];
// What an answered request asked for once its answer is out: the hotspot
// closed (after saving), with a join of the home WiFi at once, or a restart.
static bool webCloseHotspot = false, webSyncNow = false, webRestart = false;
static unsigned long webAfterAt = 0;   // millis() they are done; 0 = none waits

/* ----------------------------------------------------------------------
   The web server, beside the loop. A connection is taken while one of the
   WEB_CONNS places is free; its request is read as far as it has arrived,
   answered whole into RAM once it is in, and the answer sent as far as the
   socket takes it, at most WEB_SEND_MAX bytes a pass. So no pass waits for
   the network. Only a GIF pack's upload is still read at once
   (apiPackUpload()), with its progress on the panel.
   ---------------------------------------------------------------------- */

// One pass: a new connection if one waits (listening: the hotspot's or the
// home network's server is up; lan: it is the home network's), every open one
// a step on, and what an answer asked for once it is out.
void webUpdate(bool listening, bool lan) {
  if (listening) { webAccept(lan); }
  for (uint8_t i = 0; i < WEB_CONNS; i++) {
    if (webConns[i].phase != WEB_FREE) { webStep(webConns[i]); }
  }
  webAfter();
  webStatsPrint();
}

static void webAccept(bool lan) {
  WebConn *w = nullptr;
  for (uint8_t i = 0; i < WEB_CONNS && !w; i++) {
    if (webConns[i].phase == WEB_FREE) { w = &webConns[i]; }
  }
  if (!w) { return; }   // all taken: it waits in the server's backlog
  WiFiClient client = netServerAccept(apServer);
#if defined(CLOCK_DEBUG)
  // Every accepted socket, before the connected() check below can drop it.
  if (consoleLevel >= 2 && client.fd() >= 0) {
    int errBefore = errno;
    bool isConnected = client.connected();
    Console.printf("http: accepted socket %d from %s, connected %d (errno %d), %d bytes waiting\n",
                   client.fd(), client.remoteIP().toString().c_str(), isConnected, errBefore,
                   client.available());
  }
#endif
  if (!client) { return; }
  w->client = client;
  w->phase = WEB_HEAD;
  w->lan = lan;
  w->req = HttpRequest();
  w->line = "";
  w->line.reserve(128);
  w->gotLine = false;
  w->headBytes = 0;
  w->acceptedAt = w->progressAt = millisNow;
  w->sent = 0;
  w->closeHotspot = w->syncNow = w->restart = false;
  if (!lan) { apShowClock(); }   // a client is talking to us -> reliably switch to the live clock
}

// A connection a step on: as much of its request as has arrived and, once it
// is in, its answer; or a piece of the answer; or, once all of it is out, the
// close. Each of these takes up to 2 ms (S3), so a pass does one of them.
static void webStep(WebConn &w) {
#if defined(CLOCK_DEBUG)
  uint32_t t0 = micros();
#endif
  if (w.phase == WEB_SEND) {
    webSend(w);
  } else {
    if (w.phase == WEB_HEAD) { webReadHead(w); }
    if (w.phase == WEB_BODY) { webReadBody(w); }
  }
#if defined(CLOCK_DEBUG)
  webStatsPass(micros() - t0);
#endif
}

// The request line and the headers, a byte at a time as far as they have
// arrived - no further, so a body stays in the socket for whoever reads it.
// available() asks lwIP each time, so it is asked once for every batch that
// has arrived (once a byte, the headers took 2.7 ms).
static void webReadHead(WebConn &w) {
  int n;
  while ((n = w.client.available()) > 0) {
    while (n-- > 0) {
      int c = w.client.read();
      if (c < 0) { break; }
      w.progressAt = millisNow;
      if (++w.headBytes > WEB_HEAD_MAX) { webClose(w, true); return; }
      if (c == '\r') { continue; }
      if (c != '\n') { w.line += (char)c; continue; }
      if (!w.gotLine) {
        if (w.line.length()) { httpRequestLine(w.req, w.line); w.gotLine = true; }   // a blank line before it does not count
      } else if (w.line.length() == 0) {
        w.phase = WEB_BODY;   // the blank line after the headers
        w.line = String();
        return;
      } else {
        httpHeader(w.req, w.line);
      }
      w.line = "";
    }
  }
  // Browsers open a connection or two in case they need one; one that has not
  // sent its request in time gives its place up.
  if (millisNow - w.acceptedAt > WEB_HEAD_MS || !w.client.connected()) { webClose(w, true); }
}

// A form's body, as far as it has arrived; then the answer. A GIF pack's
// upload reads its own (apiPackUpload()), and a body too large for a form is
// not read.
static void webReadBody(WebConn &w) {
  HttpRequest &r = w.req;
  bool form = r.isPost && r.path != "/api/pack" && r.contentLength > 0 && r.contentLength <= (long)HTTP_BODY_MAX;
  if (form && (long)r.body.length() < r.contentLength) {
    if (r.body.length() == 0) { r.body.reserve(r.contentLength); }
    char buf[256];
    while ((long)r.body.length() < r.contentLength && w.client.available() > 0) {
      int n = w.client.read((uint8_t *)buf, (size_t)min((long)sizeof(buf), r.contentLength - (long)r.body.length()));
      if (n <= 0) { break; }
      r.body.concat(buf, (unsigned int)n);
      w.progressAt = millisNow;
    }
    if ((long)r.body.length() < r.contentLength) {
      // A form that stops arriving is dropped, not answered in part.
      if (millisNow - w.progressAt > WEB_STALL_MS || !w.client.connected()) { webClose(w, true); }
      return;
    }
  }
  webAnswer(w);
}

// The answer to a request that is in, whole into the connection's buffer.
static void webAnswer(WebConn &w) {
  HttpRequest &r = w.req;
#if defined(CLOCK_DEBUG)
  if (consoleLevel >= 2) {
    Console.printf("http: %s | host %s | from %s\n", r.line.c_str(), r.host.c_str(),
                   w.client.remoteIP().toString().c_str());
  }
#endif
  Print &out = w.out;
  // Saving used to restart the clock so a new timezone took effect. Since the
  // clock holds UTC every setting works at once, and a restart would only throw
  // away the time - one set from a phone, on a clock without WiFi, for good.
  // So saving closes the hotspot instead, which is what the restart did too.
  bool closeHotspot = false, syncNow = false, restart = false;
  if (r.path.startsWith("/api/")) {
    if (!w.lan || lanAuthorized(out, r.auth)) {
      if (r.isPost && r.path == "/api/pack") { apiPackUpload(w.client, out, r.contentLength); }
      // A form too large to read (webReadBody()) is refused, not answered as
      // an empty one: /api/save would take every checkbox as off and store it.
      else if (r.isPost && r.contentLength > (long)HTTP_BODY_MAX) {
        sendJsonError(out, 413, "The form is too large for the clock.");
      }
      else { webApi(out, r.path, r.isPost, r.body, closeHotspot, syncNow, restart); }
    }
  } else if (r.path == "/" || r.path.startsWith("/index")) {
    sendWebApp(out);
  } else if (w.lan) {
    sendJsonError(out, 404, "Unknown request.");
  } else {
    sendCaptiveRedirect(out);   // the captive-portal probes, as below
  }
  w.closeHotspot = closeHotspot && !w.lan;
  w.syncNow = syncNow;
  w.restart = restart;
  r.body = String();
  w.phase = WEB_SEND;
  w.sent = 0;
  w.progressAt = millisNow;
}

// As much of the answer as the socket takes now, at most WEB_SEND_MAX bytes;
// the connection closed in the pass after all of it is out.
static void webSend(WebConn &w) {
  size_t len = w.out.length();
  if (w.sent >= len) { webClose(w, false); return; }
  size_t n = min(len - w.sent, WEB_SEND_MAX);
  int put = lwip_send(w.client.fd(), w.out.data() + w.sent, n, MSG_DONTWAIT);
  if (put > 0) {
    w.sent += (size_t)put;
    w.progressAt = millisNow;
  } else if (put < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
    webClose(w, true);   // the client is gone
    return;
  }
  if (millisNow - w.progressAt > WEB_STALL_MS) { webClose(w, true); }
}

// A connection closed - its answer out, or dropped - and its place free. What
// its request asked for is done WEB_AFTER_MS later, answer out or not.
static void webClose(WebConn &w, bool dropped) {
  webStatsDone(w, dropped);
  w.client.stop();
  if (w.closeHotspot || w.restart) {
    webCloseHotspot = webCloseHotspot || w.closeHotspot;
    webSyncNow = webSyncNow || w.syncNow;
    webRestart = webRestart || w.restart;
    webAfterAt = (millisNow + WEB_AFTER_MS) | 1;   // 0 means none
  }
  w.phase = WEB_FREE;
  w.req = HttpRequest();
  w.line = String();
  w.out.clear();
}

// What an answered request asked for, once its answer has had WEB_AFTER_MS to
// reach the phone: the hotspot closed (the page has to arrive before the
// hotspot goes), or the clock restarted.
static void webAfter() {
  if (webAfterAt == 0 || (long)(millisNow - webAfterAt) < 0) { return; }
  webAfterAt = 0;
  if (webCloseHotspot && apActive) {
    if (webSyncNow) { setSyncState(SYNC_JOINING); }   // stopAPMode() then joins at once
    stopAPMode();
  }
  webCloseHotspot = webSyncNow = false;
  if (webRestart) { ESP.restart(); }
}


/* ---- HTTP helpers -------------------------------------------------------- */

// (hexVal(), parseHexColor(), toHex() and tenthsOf(), which read the web
// app's values, are in settings_table.h.)
static String urldecode(const String &s) {
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

// Tiny 204 response for the /live AJAX preview requests (no body needed).
void sendNoContent(Print &c) {
  c.println("HTTP/1.1 204 No Content");
  c.println("Connection: close");
  c.println();
}

void sendJson(Print &c, JsonDocument &doc, int code) {   // code: 200 unless given (web_server.h)
  c.print("HTTP/1.1 "); c.print(code);
  c.println(code == 200 ? " OK" : code == 400 ? " Bad Request" : code == 404 ? " Not Found" :
            code == 413 ? " Content Too Large" : " Error");
  c.println("Content-Type: application/json");
  c.println("Cache-Control: no-store");
  c.println("Connection: close");
  c.println();
  serializeJson(doc, c);
}

void sendJsonError(Print &c, int code, const char *text) {
  JsonDocument d;
  d["error"] = text;
  sendJson(c, d, code);
}
