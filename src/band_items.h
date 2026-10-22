/* -------------------------------------------------------------------------
   band_items.h - the info band's items, from every source: the clock's own
   fetches (FeedId, band_data.h) and what Home Assistant sends. One list, at
   most BAND_ITEMS_MAX; an item replaces the one of its source with its key,
   gets the next number of the order they came in, and goes when it runs
   out or its source takes it away. Only the loop touches them. How they
   show - notices, cards, the wall clock's band - is the business of those.

   A module of its own since 2026-10-22 (code review of 2026-10-14, point 10).
   ------------------------------------------------------------------------- */
#ifndef BAND_ITEMS_H
#define BAND_ITEMS_H

#include <Arduino.h>
#include "band_data.h"

const uint8_t  SOURCE_HA = FEED_COUNT;   // BandItem.feed of what Home Assistant sent
const uint8_t  BAND_ITEMS_MAX = 16;
extern BandItem bandItems[BAND_ITEMS_MAX];
extern uint8_t  bandItemCount;
extern time_t   bandNextUntil;   // the earliest until of them (UTC), when the next one runs out; 0 = no items

// Takes an item, in place of the one of its source with its key; says on the
// console what changed.
void   bandPut(const BandItem &n);
int8_t bandFind(uint8_t f, const char *key);   // the item of source f with this key; -1 when there is none
void   bandRemove(uint8_t f, const char *key); // takes it away, if there is one
void   bandRemoveAt(uint8_t i);
// Drops the items of source f, or only those its new result no longer has.
void   bandDropFeed(uint8_t f, const FeedResult *keep = nullptr);
void   bandExpire();                           // the items that have run out go
// "19:00", or "Thu 19:00" when it is not today, in the clock's local time.
void   bandWhen(char *buf, size_t size, time_t utc);
void   printBandItem(const char *mark, const BandItem &it);   // to the console
// Whether an item is a message from Home Assistant: its key is "*msg1" to
// "*msg4" (haMessage()), and no item name has a '*'.
bool   isMessage(const BandItem &it);

#endif  // BAND_ITEMS_H
