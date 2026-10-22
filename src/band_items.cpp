// The info band's items, from every source (band_items.h).
#include "band_items.h"

#include "clock_state.h"
#include "clock_time.h"
#include "notices.h"
#include "time_sync.h"

// The items, from every source; only the loop touches them.
// (BAND_ITEMS_MAX: band_items.h.)
BandItem bandItems[BAND_ITEMS_MAX];
uint8_t  bandItemCount = 0;
time_t   bandNextUntil = 0;   // the earliest until of them (UTC), when the next one runs out; 0 = no items
static uint32_t bandSeq = 0;         // the number the last item taken got (BandItem.seq)

// "19:00", or "Thu 19:00" when it is not today, in the clock's local time.
void bandWhen(char *buf, size_t size, time_t utc) {
  FeedJob now = { 0, 0, 0, clockNow(), (int32_t)tzTotalOffset() };
  feedWhen(buf, size, utc, now);
}

// The S3's USB console has dropped part of these lines (2026-10-08): about
// 128 bytes into a burst of output, whatever the lines' length, some 60 bytes
// went missing; the cause is not found yet. The config page lists the items
// in full.
void printBandItem(const char *mark, const BandItem &it) {
  char until[16];
  bandWhen(until, sizeof(until), it.until);
  Console.printf("Info band %s %s (%s, until %s, icon %s", mark, it.key, it.level == LEVEL_ALERT ? "alert" : "info", until,
                it.icon < ICON_COUNT && it.icon != ICON_NONE ? ITEM_ICON_NAMES[it.icon] : "none");
  if (it.severity)  { Console.printf(", level %u", (unsigned)it.severity); }
  if (it.intensity) { Console.printf(", %s", ITEM_INTENSITY_NAMES[it.intensity]); }
  if (it.chance <= 100) { Console.printf(", %u %%", (unsigned)it.chance); }
  Console.printf("):\n  %s\n", it.text);
}

// The earliest until of all items, worked out anew after every change to
// them; bandPut() and bandRemoveAt() are the only two that make one.
static void bandFindNextUntil() {
  bandNextUntil = 0;
  for (uint8_t i = 0; i < bandItemCount; i++) {
    if (bandNextUntil == 0 || bandItems[i].until < bandNextUntil) { bandNextUntil = bandItems[i].until; }
  }
}

void bandRemoveAt(uint8_t i) {
  for (uint8_t j = i + 1; j < bandItemCount; j++) { bandItems[j - 1] = bandItems[j]; }
  bandItemCount--;
  bandFindNextUntil();
}

// The item of source f with this key; -1 when there is none.
int8_t bandFind(uint8_t f, const char *key) {
  for (uint8_t i = 0; i < bandItemCount; i++) {
    if (bandItems[i].feed == f && strcmp(bandItems[i].key, key) == 0) { return (int8_t)i; }
  }
  return -1;
}

// Takes an item, in place of the one of its source with its key; says what
// changed. A new item gets the next number of the order they came in; one
// that replaces the same item (the same tag) keeps its number.
void bandPut(const BandItem &n) {
  BandItem it = n;
  int8_t i = bandFind(n.feed, n.key);
  if (i >= 0) {
    bool same = strcmp(bandItems[i].text, n.text) == 0 && bandItems[i].level == n.level;
    it.seq = bandItems[i].tag == n.tag ? bandItems[i].seq : ++bandSeq;
    bandItems[i] = it;
    bandFindNextUntil();
    if (!same) { printBandItem("~", it); }
    return;
  }
  if (bandItemCount == BAND_ITEMS_MAX) {
    Console.printf("Info band: no room for %s\n", n.key);
    return;
  }
  it.seq = ++bandSeq;
  bandItems[bandItemCount++] = it;
  bandFindNextUntil();
  printBandItem("+", it);
}

// Takes the item of source f with this key away, if there is one.
void bandRemove(uint8_t f, const char *key) {
  int8_t i = bandFind(f, key);
  if (i < 0) { return; }
  Console.printf("Info band - %s\n", key);
  noticeForget(bandItems[i]);
  bandRemoveAt((uint8_t)i);
}

// Drops the items of source f, or only those its new result no longer has.
void bandDropFeed(uint8_t f, const FeedResult *keep) {
  for (int i = (int)bandItemCount - 1; i >= 0; i--) {
    if (bandItems[i].feed != f) { continue; }
    bool kept = false;
    for (uint8_t k = 0; keep && k < keep->count; k++) { kept |= strcmp(keep->item[k].key, bandItems[i].key) == 0; }
    if (kept) { continue; }
    Console.printf("Info band - %s\n", bandItems[i].key);
    bandRemoveAt((uint8_t)i);
  }
}

// The items that have run out go.
void bandExpire() {
  time_t now = clockNow();
  for (int i = (int)bandItemCount - 1; i >= 0; i--) {
    if (bandItems[i].until > now) { continue; }
    Console.printf("Info band - %s (ran out)\n", bandItems[i].key);
    noticeForget(bandItems[i]);
    bandRemoveAt((uint8_t)i);
  }
}

// Whether an item is a message from Home Assistant: its key is "*msg1" to
// "*msg4" (haMessage()), and no item name has a '*'.
bool isMessage(const BandItem &it) {
  return it.feed == SOURCE_HA && it.key[0] == '*';
}
