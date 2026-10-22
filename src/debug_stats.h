/* -------------------------------------------------------------------------
   debug_stats.h - the debug build's running figures on the console (at log
   level 2, console.h): the loop's gaps, the face's passes and frames, the
   hotspot's loop, the web server's steps, and the RAM. A release build has
   none: there each of them is an empty inline function, so a call needs no
   #if of its own - only the timing a call is given does.

   A module of its own since 2026-10-22 (code review of 2026-10-14, point 10).
   ------------------------------------------------------------------------- */
#ifndef DEBUG_STATS_H
#define DEBUG_STATS_H

#include <Arduino.h>

struct WebConn;

#if defined(CLOCK_DEBUG)
extern uint32_t faceDrawStart;   // micros() when faceUpdate() began the face's pass
extern uint32_t faceDraws;       // frames a face drew since the last figure
void debugMemoryLog();           // the RAM every 30 s
void loopGapStats();             // on every pass: the time since the last
void apLoopStats(uint32_t webUs, uint32_t panelUs);
void frameStatsAdd(uint32_t drawUs, uint32_t showUs);
void facePassStats(uint32_t us);
void webStatsPass(uint32_t us);
void webStatsDone(WebConn &w, bool dropped);
void webStatsPrint();
#else
inline void debugMemoryLog() {}
inline void loopGapStats() {}
inline void webStatsDone(WebConn &, bool) {}
inline void webStatsPrint() {}
#endif

#endif  // DEBUG_STATS_H
