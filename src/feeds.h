/* -------------------------------------------------------------------------
   feeds.h - the info band's own sources (band_data.h has them and the
   fetch task): when each is fetched, and what its results bring. A source
   due wants the station; its fetch goes to the task once the station is
   up; a fetch that failed is tried again after a back-off. The items a
   fetch brings go to band_items.h. The place and the sources that are on
   are the feed settings (settings.h).

   A module of its own since 2026-10-22 (code review of 2026-10-14, point 10).
   ------------------------------------------------------------------------- */
#ifndef FEEDS_H
#define FEEDS_H

#include <Arduino.h>
#include "board_hal.h"
#include "band_data.h"   // FEED_COUNT

// Per source: when it is due, and how its last fetch went.
struct FeedRun {
  unsigned long dueAt;     // millis()
  uint8_t  failures;       // in a row, for the back-off
  int16_t  status;         // of the last fetch (FeedResult)
  time_t   okAt;           // UTC of the last good fetch; 0 = none yet
  uint32_t ms;             // how long the last fetch took
  int32_t  bytes;          // and how large its answer was
  char     error[40];
};
extern FeedRun       feedRuns[FEED_COUNT];
extern bool          feedBusy;          // a job is with the fetch task
#if BOARD_WAVESHARE_WALL
extern unsigned long feedSentAt;        // millis() the last job was handed to it (wallOutputWatch())
#endif
extern char          feedPlace[32];     // the warnings' place, as the DWD names it

void loadFeedSettings();   // at start-up: the settings and the fetch task
bool feedOn(uint8_t f);    // source f is switched on
void feedsUpdate();        // on every pass outside the hotspot
#if defined(CLOCK_DEBUG)
void feedRetrySelfTest();
#endif
void applyFeedParams(const String &q);   // the web app's info band settings (/api/save)

#endif  // FEEDS_H
