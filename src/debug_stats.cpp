// The debug build's running figures on the console (debug_stats.h).
#include "debug_stats.h"

#if defined(CLOCK_DEBUG)
#include <esp_heap_caps.h>
#include "clock_state.h"
#include "console.h"
#include "settings.h"
#include "web_request.h"

// Debug build only: frame timing, printed once per second. "frames" is how often
// a face was drawn, "panel" the panel's refresh rate (updatePanelRate()),
// "draw" the time from the face's pass to the frame (faceDrawStart) and "show"
// the time show() spent handing it over (with double buffering it waits for
// the next panel refresh).
struct FrameStats { uint32_t frames, drawSum, drawMax, showSum, showMax, since; };
static FrameStats frameStats = {};
uint32_t faceDrawStart = 0;   // micros() when faceUpdate() began the face's pass

// Debug build only: the RAM every 30 s - internal free, its lowest since the
// start and its largest free block, and the free PSRAM. The panel driver's
// buffer, the WiFi and the TLS sessions all want internal RAM.
void debugMemoryLog() {
  static unsigned long last = 0;
  if (last != 0 && millisNow - last < 30000) { return; }
  last = millisNow ? millisNow : 1;
  if (consoleLevel < 2) { return; }   // a running figure (console.h)
  Console.printf("RAM: internal free %u, lowest %u, largest block %u; PSRAM free %u\n",
                (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                (unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL),
                (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL),
                (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
}

// Debug build only: the loop while the hotspot is up, printed every 2 s - how
// often it ran, how long the web side (handleAP) and the panel side (preview)
// took on average and at most, and the free internal RAM.
void apLoopStats(uint32_t webUs, uint32_t panelUs) {
  static uint32_t passes = 0, webSum = 0, webMax = 0, panelSum = 0, panelMax = 0, since = 0;
  passes++;
  webSum += webUs;     if (webUs > webMax)     { webMax = webUs; }
  panelSum += panelUs; if (panelUs > panelMax) { panelMax = panelUs; }
  if (millis() - since < 2000) { return; }
  if (consoleLevel >= 2) {   // a running figure (console.h)
    Console.printf("AP loop: %lu passes, web avg %lu max %lu us, panel avg %lu max %lu us, "
                  "RAM free %u (min %u, largest %u)\n",
                  (unsigned long)passes, (unsigned long)(webSum / passes), (unsigned long)webMax,
                  (unsigned long)(panelSum / passes), (unsigned long)panelMax,
                  (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                  (unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL),
                  (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
  }
  passes = webSum = webMax = panelSum = panelMax = 0;
  since = millis();
}

void frameStatsAdd(uint32_t drawUs, uint32_t showUs) {
  FrameStats &f = frameStats;
  f.frames++;
  f.drawSum += drawUs; if (drawUs > f.drawMax) { f.drawMax = drawUs; }
  f.showSum += showUs; if (showUs > f.showMax) { f.showMax = showUs; }
  if (millis() - f.since < 1000) { return; }
  if (consoleLevel >= 2) {   // a running figure (console.h)
    Console.printf("frames %lu/s panel %lu Hz draw avg %lu max %lu us show avg %lu max %lu us\n",
                  (unsigned long)f.frames, (unsigned long)panelHz,
                  (unsigned long)(f.drawSum / f.frames), (unsigned long)f.drawMax,
                  (unsigned long)(f.showSum / f.frames), (unsigned long)f.showMax);
  }
  f = FrameStats{};
  f.since = millis();
}

// Debug build: the face's passes, printed every 2 s - how often the loop ran
// it, how long a pass took on average and at most (handing a frame over and
// waiting for the panel included), and how many frames it drew.
uint32_t faceDraws = 0;
void facePassStats(uint32_t us) {
  static uint32_t passes = 0, sum = 0, most = 0, since = 0;
  passes++;
  sum += us; if (us > most) { most = us; }
  if (millis() - since < 2000) { return; }
  if (consoleLevel >= 2) {   // a running figure (console.h)
    Console.printf("face %u: %lu passes, avg %lu max %lu us, %lu frames\n", (unsigned)settings.watchface,
                  (unsigned long)passes, (unsigned long)(sum / passes), (unsigned long)most, (unsigned long)faceDraws);
  }
  passes = sum = most = faceDraws = 0;
  since = millis();
}

// Debug build: the time from one pass of the loop to the next, printed every
// 2 s - the longest, and how many took over 20 ms (a jolt on the panel). A
// pass that waits for anything shows here, the web server's among them.
void loopGapStats() {
  static uint32_t last = 0, passes = 0, most = 0, slow = 0, since = 0;
  uint32_t now = micros();
  if (last != 0) {
    uint32_t gap = now - last;
    passes++;
    if (gap > most) { most = gap; }
    if (gap > 20000) { slow++; }
  }
  last = now;
  if (millis() - since < 2000) { return; }
  if (consoleLevel >= 2 && passes) {   // a running figure (console.h)
    Console.printf("loop: %lu passes, longest %lu us apart, %lu over 20 ms\n",
                   (unsigned long)passes, (unsigned long)most, (unsigned long)slow);
  }
  passes = most = slow = 0;
  since = millis();
  last = micros();   // the print above is not the loop's
}

// Debug build: the web server, printed every 2 s while it has work - the
// requests answered and the connections dropped, its steps (one connection a
// pass) on average and at most, and the longest request from its connection
// to its close.
struct WebStats { uint32_t answered, dropped, steps, stepSum, stepMax, longestMs, since; };
static WebStats webStats = {};

void webStatsPass(uint32_t us) {
  webStats.steps++;
  webStats.stepSum += us;
  if (us > webStats.stepMax) { webStats.stepMax = us; }
}

void webStatsDone(WebConn &w, bool dropped) {
  if (dropped) { webStats.dropped++; } else { webStats.answered++; }
  uint32_t ms = millisNow - w.acceptedAt;
  if (ms > webStats.longestMs) { webStats.longestMs = ms; }
}

void webStatsPrint() {
  if (millis() - webStats.since < 2000) { return; }
  if (consoleLevel >= 2 && webStats.steps) {   // a running figure (console.h)
    Console.printf("web: %lu answered, %lu dropped; steps avg %lu max %lu us; longest request %lu ms\n",
                   (unsigned long)webStats.answered, (unsigned long)webStats.dropped,
                   (unsigned long)(webStats.stepSum / webStats.steps), (unsigned long)webStats.stepMax,
                   (unsigned long)webStats.longestMs);
  }
  webStats = WebStats{};
  webStats.since = millis();
}
#endif
