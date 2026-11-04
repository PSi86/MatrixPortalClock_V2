/* -------------------------------------------------------------------------
   web_api.h - the web app: one page, webui/index.html, which the build
   gzips into webui_page.h (scripts/embed_webui.py), and the JSON interface
   it works on - /api/state (everything once), /api/status (what moves),
   /api/panel (the canvas as it is drawn), the forms that save and the ones
   tried live, the GIF pack's upload, the time set by hand. The web server
   (web_server.h) hands each request here.

   A module of its own since 2026-10-22 (code review of 2026-10-14, point 10).
   ------------------------------------------------------------------------- */
#ifndef WEB_API_H
#define WEB_API_H

#include <Arduino.h>
#include <WiFi.h>

// A request to /api/...: answered into c; what it asks for once the answer is
// out - the hotspot closed, a sync at once, a restart - comes back in the flags.
void webApi(Print &c, const String &path, bool isPost, const String &body, bool &closeHotspot, bool &syncNow, bool &restart);
void sendWebApp(Print &c);   // the page itself
void apiPackUpload(WiFiClient &client, Print &out, long length);   // POST /api/pack: read at once, its progress on the panel

// For an upload read at once (the GIF pack, a firmware): up to want bytes of
// the body, fewer when it stops; the rest of a refused one read and dropped,
// so the answer reaches the client; a fetch under way ended before the flash
// writes; the panel showing how far it is.
size_t uploadRead(WiFiClient &client, uint8_t *buf, size_t want);
void   uploadDrain(WiFiClient &client, long left);
void   uploadSettle(const char *what);
void   drawUploadProgress(const char *title, uint32_t done, uint32_t total);

#endif  // WEB_API_H
