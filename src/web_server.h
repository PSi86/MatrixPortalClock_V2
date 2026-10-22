/* -------------------------------------------------------------------------
   web_server.h - the web server, beside the loop: the hotspot's on port 80
   and, on the home network, the one lan_access.h opens. A few connections at
   a time, each read, answered whole into RAM and sent a piece per pass, so
   no pass of the loop waits for the network (web_request.h is a connection).
   What a request asks for is web_api.h's business; the answers' HTTP and
   JSON framing is here.

   A module of its own since 2026-10-22 (code review of 2026-10-14, point 10).
   ------------------------------------------------------------------------- */
#ifndef WEB_SERVER_H
#define WEB_SERVER_H

#include <Arduino.h>
#include <ArduinoJson.h>

// The largest POST body taken: the web app sends all its settings at once.
const size_t HTTP_BODY_MAX = 4096;

// One pass: a new connection if one waits (listening: the hotspot's or the
// home network's server is up; lan: it is the home network's), every open
// one a step on, and what an answer asked for once it is out.
void webUpdate(bool listening, bool lan);

// The answers.
String getParam(const String &query, const String &key);   // a form's or query's value; "" if absent
void sendJson(Print &c, JsonDocument &doc, int code = 200);
void sendJsonError(Print &c, int code, const char *text);
void sendNoContent(Print &c);                               // 204

#endif  // WEB_SERVER_H
