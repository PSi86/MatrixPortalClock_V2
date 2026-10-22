// The info band's items on the 64x32 clock: notices and info cards (notices.h).
#include "notices.h"

#include "band_items.h"
#include "clock_state.h"
#include "console.h"
#include "face.h"
#include "info_band.h"
#include "input.h"
#include "item_icons.h"   // the icons and their pictures (only this file draws them)
#include "menu.h"
#include "panel_rules.h"
#include "settings.h"

// What only this module calls, ahead of its use.
struct NoticeStep;
struct CardLayout;
static bool isNotice(const BandItem &it);
static int8_t noticeSeenIndex(uint8_t feed, uint32_t tag);
static void noticeSeenAdd(const BandItem &it);
static bool infoIsNew(const BandItem &it);
static uint8_t infoRank(const BandItem &it);
static bool noticeBefore(uint8_t x, uint8_t y);
static void noticeAddStep(const BandItem &it, uint8_t phase);
static int8_t noticeItem(const NoticeStep &s);
static bool noticeStepsSame(uint8_t a, uint8_t b);
static bool noticeStepShow(uint8_t i);
static bool noticeStart();
static void noticeStepsForNew();
static bool infoRoundReady();
static bool infoCardsStart(bool round);
static unsigned long noticeNewMs();
static IconRgb airRgb(uint8_t band);
static IconRgb chanceRgb(IconRgb c, uint8_t chance);
static IconRgb roleRgb(uint8_t role, const IconLook &look);
static void drawItemIcon(int16_t x, int16_t y, const IconLook &look);
static CardLayout cardLayout();
static int16_t cardLineWidth(const char *s, uint8_t size);
static void drawCardLine(const char *s, int16_t x, int16_t y, uint8_t size, uint16_t colour);

// The items on the 64x32 clock, which has no room for the band (concept doc,
// "Info for every clock"). The notices - the items at alert level and the
// messages from Home Assistant: a new alert comes up as a banner with its
// icon alone, a new message as text running through twice, an urgent message
// as both; the face carries a marker while there are any (red for an alert,
// white for messages alone). The other items, the information, come as cards:
// once when they are new, and all of them in the info round every
// roundMinutes. "Show notifications" shows everything once more.
//
// The items already shown, by source and tag; information also by its icon,
// so the weather comes again when it turns from cloudy to rain. One its
// source takes away, or one that runs out, is over and forgotten at once.
// One dropped for a technical reason stays known for a while, so one that
// comes back (a reconnect to Home Assistant, a fetch that missed it) is not
// shown again.
struct NoticeSeen { uint8_t feed; uint32_t tag; uint8_t icon; bool notice; unsigned long lastAt; };
const uint8_t       NOTICE_SEEN_MAX  = 24;
const unsigned long NOTICE_FORGET_MS = 15UL * 60UL * 1000UL;
// (The alert colour, ALERT_R, ALERT_G and ALERT_B: notices.h.)
static NoticeSeen noticeSeen[NOTICE_SEEN_MAX];
static uint8_t    noticeSeenCount = 0;
NoticeMark noticeMark = MARK_NONE;   // what the face's corner shows

// What is on the panel: the banners of one or more items, one step after the
// other - an icon, a text that runs through, or an info card. A step names
// its item by source and tag, so one that went meanwhile is left out.
enum NoticePhase : uint8_t { PHASE_ICON, PHASE_TEXT, PHASE_CARD };
struct NoticeStep { uint8_t feed; uint32_t tag; uint8_t phase; };
const uint8_t NOTICE_STEPS_MAX = 2 * BAND_ITEMS_MAX;
static NoticeStep noticeSteps[NOTICE_STEPS_MAX];
uint8_t    noticeStepCount = 0;   // 0: none on the panel
static uint8_t noticeStepAt = 0;  // the step on the panel
unsigned long noticeStepMs = 0;   // how long it stays
bool       noticeCardsRound = false;   // the steps on the panel are cards of their own accord: an input ends them

// An info card: the item's icon, and beside it (below it, upright) its short
// form in two lines - a first line of three characters or fewer at double
// size - or, without a short form, its text running through once.
//
// The info round goes by the clock's time (Peter, 2026-10-11): on the minutes
// roundMinutes divides - every 15 min at :00, :15, :30 and :45 - once the face
// has shown the new time (the classic face's fly-in, the Tetris blocks, the
// muncher's walk and hour show) and stood at rest for ROUND_AFTER_FACE_MS. One
// that cannot start within ROUND_LATEST_MS of its minute is left out. A GIF of
// its own accord gives way to it, and waits ROUND_GIF_GAP_MS after it.
const unsigned long CARD_MS = 4000;                 // a card with its two lines
// (ROUND_GIF_GAP_MS, at least this between a round and a GIF: panel_rules.h.)
const unsigned long ROUND_AFTER_FACE_MS = 3000;
const unsigned long ROUND_LATEST_MS = 60000;
static char   cardLine[2][8];                       // the two lines, in the font's code page (CP437)
bool          cardRuns = false;                     // no short form: the text runs (runText)
unsigned long roundMinuteAt = 0;                    // millis() the round's minute began; 0 = no round waits
bool          noticesPending = false;               // messages or new cards wait for the minute's place for them
static unsigned long faceRestAt = 0;                // millis() since the face is at rest; 0 = it is not
unsigned long infoCardsEndedAt = 0;                 // a GIF keeps ROUND_GIF_GAP_MS after info cards

// A text running through: right to left in the 5x7 font, twice when it is
// new - the first run catches the eye, the second lets it be read - and once
// when it is asked for again, by someone already looking.
// (RUN_PX_PER_S, about five characters a second: notices.h.)
static uint8_t  noticeRuns = 2;            // how often the texts of the notices on show run through
const unsigned long ICON_BEFORE_TEXT_MS = 3000;   // an urgent message's icon, before its text
const unsigned long ALERT_BANNER_MS = 10000;      // an alert's icon, when no text follows it
static char     runText[sizeof(BandItem::text)];  // in the font's code page (CP437)
static uint16_t runWidth = 0;              // its width in pixels
static uint8_t  runR = 255, runG = 255, runB = 255;
static int16_t  runLastX = INT16_MIN;      // where it was drawn last

// Which face events close a notice on the panel (and go on to the next):
// set on the Inputs page, a blob of its own (NVS key "closes"). Nothing
// stored: what closes every other banner - a press of a button or a
// gesture, not a knock or a hand that comes near.
#define NOTICE_KEYS_MAGIC 0xC105
#define NOTICE_KEYS_REV   1
struct NoticeKeys {
  uint16_t magic;
  uint8_t  rev;
  uint8_t  count;                          // events stored (FACE_EVENT_COUNT when written)
  uint8_t  closes[FACE_KEYS_MAX];          // 1: this face event closes a notice, in FACE_EVENTS order
};
uint8_t noticeCloses[FACE_EVENT_COUNT];
static SettingsStore<NoticeKeys> noticeKeysStore;

// The banner on show: its icon and how it looks (BANNER_ALERT, BANNER_CARD),
// and an info card's parts (cardLayout()).
static IconLook bannerLook = { 0, 0, 0, 0 };
struct CardLayout { int16_t iconX, iconY, areaX, areaW, lineY[2], bigY[2], runY; };

// ---- Notices: alerts and messages on the 64x32 clock -----------------------------


// Whether an item is a notice on the 64x32 clock: an alert, or a message.
// The others, the information, come there as cards.
static bool isNotice(const BandItem &it) {
  return it.level == LEVEL_ALERT || isMessage(it);
}

// How this clock shows an item, for the web app's list: the 64x32 clock has
// no band, the wall clock has one.
const char *itemShowsHere(const BandItem &it) {
#if BAND_ON_PANEL
  return it.level == LEVEL_ALERT ? "banner, then in the band" : "in the band";
#endif
  if (isMessage(it))            { return it.level == LEVEL_ALERT ? "banner, then running text" : "running text"; }
  if (it.level == LEVEL_ALERT)  { return "banner"; }
  return "card";
}

// The icon a notice shows: its own, the warning sign for one without.
uint8_t noticeIcon(const BandItem &it) {
  return (it.icon != ICON_NONE && it.icon < ICON_COUNT) ? it.icon : ICON_WARNING;
}

// The notice already shown with this source and tag; -1 when there is none.
static int8_t noticeSeenIndex(uint8_t feed, uint32_t tag) {
  for (uint8_t i = 0; i < noticeSeenCount; i++) {
    if (noticeSeen[i].feed == feed && noticeSeen[i].tag == tag) { return (int8_t)i; }
  }
  return -1;
}

// An item that is over - taken away by its source, or run out - is no
// longer known as shown, so the same item sent again comes up as new.
void noticeForget(const BandItem &it) {
  int8_t s = noticeSeenIndex(it.feed, it.tag);
  if (s >= 0) { noticeSeen[s] = noticeSeen[--noticeSeenCount]; }
}

// Keeps an item as shown, as it is now (its icon, whether it is a notice);
// when the list is full, in place of the one gone the longest.
static void noticeSeenAdd(const BandItem &it) {
  int8_t s = noticeSeenIndex(it.feed, it.tag);
  uint8_t i = s >= 0 ? (uint8_t)s : noticeSeenCount;
  if (s < 0 && i == NOTICE_SEEN_MAX) {
    i = 0;
    for (uint8_t j = 1; j < noticeSeenCount; j++) {
      if (millisNow - noticeSeen[j].lastAt > millisNow - noticeSeen[i].lastAt) { i = j; }
    }
  } else if (s < 0) {
    noticeSeenCount++;
  }
  noticeSeen[i] = { it.feed, it.tag, it.icon, isNotice(it), millisNow };
}

// Whether information is new: not shown yet, or shown with another icon.
static bool infoIsNew(const BandItem &it) {
  int8_t s = noticeSeenIndex(it.feed, it.tag);
  return s < 0 || noticeSeen[s].icon != it.icon;
}

// The information's order on the panel, the band's: DWD warnings first, then
// rain soon, the weather, the air quality, then Home Assistant's items in the
// order they came.
static uint8_t infoRank(const BandItem &it) {
  if (it.feed == FEED_WARNINGS) { return 0; }
  if (it.feed == FEED_WEATHER)  { return strcmp(it.key, "rain") == 0 ? 1 : 2; }
  if (it.feed == FEED_AIR)      { return 3; }
  return 4;
}

// The information in that order, as positions in bandItems; only what is new
// with `newOnly`. Returns how many.
uint8_t infoOrder(uint8_t *order, bool newOnly) {
  uint8_t n = 0;
  for (uint8_t i = 0; i < bandItemCount; i++) {
    if (isNotice(bandItems[i]) || (newOnly && !infoIsNew(bandItems[i]))) { continue; }
    order[n++] = i;
  }
  for (uint8_t a = 1; a < n; a++) {   // insertion sort, as noticeOrder()
    uint8_t k = order[a], b = a;
    while (b > 0 && (infoRank(bandItems[k]) < infoRank(bandItems[order[b - 1]]) ||
                     (infoRank(bandItems[k]) == infoRank(bandItems[order[b - 1]]) &&
                      bandItems[k].seq < bandItems[order[b - 1]].seq))) {
      order[b] = order[b - 1];
      b--;
    }
    order[b] = k;
  }
  return n;
}

// Whether item x is shown before item y: alerts before messages, each in the
// order they came.
static bool noticeBefore(uint8_t x, uint8_t y) {
  const BandItem &a = bandItems[x], &b = bandItems[y];
  if (a.level != b.level) { return a.level == LEVEL_ALERT; }
  return a.seq < b.seq;
}

// The notices in the order they are shown, as positions in bandItems; only
// those not shown yet with `newOnly`. Returns how many.
uint8_t noticeOrder(uint8_t *order, bool newOnly) {
  uint8_t n = 0;
  for (uint8_t i = 0; i < bandItemCount; i++) {
    if (!isNotice(bandItems[i])) { continue; }
    if (newOnly && noticeSeenIndex(bandItems[i].feed, bandItems[i].tag) >= 0) { continue; }
    order[n++] = i;
  }
  for (uint8_t a = 1; a < n; a++) {   // insertion sort: there are 16 items at most
    uint8_t k = order[a], b = a;
    while (b > 0 && noticeBefore(k, order[b - 1])) { order[b] = order[b - 1]; b--; }
    order[b] = k;
  }
  return n;
}

static void noticeAddStep(const BandItem &it, uint8_t phase) {
  if (noticeStepCount < NOTICE_STEPS_MAX) { noticeSteps[noticeStepCount++] = { it.feed, it.tag, phase }; }
}

// The item a step names; -1 when it has gone.
static int8_t noticeItem(const NoticeStep &s) {
  for (uint8_t i = 0; i < bandItemCount; i++) {
    if (bandItems[i].feed == s.feed && bandItems[i].tag == s.tag) { return (int8_t)i; }
  }
  return -1;
}

static bool noticeStepsSame(uint8_t a, uint8_t b) {
  return noticeSteps[a].feed == noticeSteps[b].feed && noticeSteps[a].tag == noticeSteps[b].tag;
}

// A text for the 5x7 font, which draws code page 437: the letters it has
// beyond ASCII (ä ö ü ß, é è à ç ñ ...) and the degree sign as their CP437
// bytes, a line break as a space, any other character as '?'. Never longer
// than the UTF-8 text.
void textToCp437(char *dst, const char *src, size_t size) {
  struct Cp437 { uint16_t code; uint8_t c; };
  static const Cp437 MAP[] = {
    { 0xE4, 0x84 }, { 0xF6, 0x94 }, { 0xFC, 0x81 }, { 0xC4, 0x8E }, { 0xD6, 0x99 }, { 0xDC, 0x9A },
    { 0xDF, 0xE1 }, { 0xB0, 0xF8 }, { 0xE9, 0x82 }, { 0xE8, 0x8A }, { 0xEA, 0x88 }, { 0xEB, 0x89 },
    { 0xE0, 0x85 }, { 0xE2, 0x83 }, { 0xE7, 0x87 }, { 0xEF, 0x8B }, { 0xEE, 0x8C }, { 0xF4, 0x93 },
    { 0xFB, 0x96 }, { 0xF9, 0x97 }, { 0xF1, 0xA4 }, { 0xD1, 0xA5 }, { 0xE1, 0xA0 }, { 0xED, 0xA1 },
    { 0xF3, 0xA2 }, { 0xFA, 0xA3 }, { 0xC9, 0x90 }, { 0xC7, 0x80 }, { 0xE5, 0x86 }, { 0xC5, 0x8F },
  };
  size_t n = 0;
  const uint8_t *p = (const uint8_t *)src;
  while (*p && n + 1 < size) {
    uint32_t code = '?';
    uint8_t len = 1;
    if (*p < 0x80) {
      code = *p;
    } else if ((p[0] & 0xE0) == 0xC0 && (p[1] & 0xC0) == 0x80) {
      code = ((uint32_t)(p[0] & 0x1F) << 6) | (p[1] & 0x3F);
      len = 2;
    } else if ((p[0] & 0xF0) == 0xE0 && (p[1] & 0xC0) == 0x80 && (p[2] & 0xC0) == 0x80) {
      code = ((uint32_t)(p[0] & 0x0F) << 12) | ((uint32_t)(p[1] & 0x3F) << 6) | (p[2] & 0x3F);
      len = 3;
    } else if ((p[0] & 0xF8) == 0xF0 && (p[1] & 0xC0) == 0x80 && (p[2] & 0xC0) == 0x80 && (p[3] & 0xC0) == 0x80) {
      code = 0xFFFF;   // beyond the font, as every emoji is
      len = 4;
    }
    char c = '?';
    if (code < 0x20)                     { c = ' '; }
    else if (code < 0x7F)                { c = (char)code; }
    else { for (const Cp437 &m : MAP) { if (m.code == code) { c = (char)m.c; break; } } }
    dst[n++] = c;
    p += len;
  }
  dst[n] = '\0';
}

// On the panel a text's separator is the font's middle dot: " | " sets its
// parts apart (bandWrap()), but the 5x7 font draws '|' broken in the middle,
// like a colon (Peter, 2026-10-18). The texts keep their '|', which every
// keyboard has; only what the panel shows changes.
const char PANEL_SEPARATOR = (char)0xF9;   // CP437's middle dot, 2x2 at mid height
void panelSeparators(char *s) {
  for (; *s; s++) {
    if (*s == '|') { *s = PANEL_SEPARATOR; }
  }
}

// Puts step i on the panel; false when its item has gone meanwhile. An icon
// stands ALERT_BANNER_MS, or ICON_BEFORE_TEXT_MS when the notice's text comes
// next; a text runs through noticeRuns times; a card stands CARD_MS, or as
// long as its text takes to run through once.
static bool noticeStepShow(uint8_t i) {
  int8_t k = noticeItem(noticeSteps[i]);
  if (k < 0) { return false; }
  const BandItem &it = bandItems[k];
  if (noticeSteps[i].phase == PHASE_CARD) {
    bannerKind = BANNER_CARD;
    bannerLook = { (uint8_t)(it.icon < ICON_COUNT ? it.icon : ICON_NONE), it.severity, it.intensity, it.chance };
    cardRuns = it.shortText[0] == '\0';
    if (cardRuns) {
      textToCp437(runText, it.text, sizeof(runText));
      panelSeparators(runText);
      runWidth = (uint16_t)(strlen(runText) * 6);
      runLastX = INT16_MIN;
      CardLayout L = cardLayout();
      unsigned long aw = bannerLook.icon != ICON_NONE ? (unsigned long)L.areaW : (unsigned long)matrix.width();
      noticeStepMs = max(CARD_MS, (runWidth + aw) * 1000UL / RUN_PX_PER_S);
    } else {
      char line[sizeof(it.shortText)];
      strlcpy(line, it.shortText, sizeof(line));
      char *bar = strchr(line, '|');
      if (bar) { *bar = '\0'; }
      textToCp437(cardLine[0], line, sizeof(cardLine[0]));
      textToCp437(cardLine[1], bar ? bar + 1 : "", sizeof(cardLine[1]));
      noticeStepMs = CARD_MS;
    }
    Console.printf("Info card: %s, icon %s, %s\n", it.key, bannerLook.icon != ICON_NONE ? ITEM_ICON_NAMES[bannerLook.icon] : "none",
                  cardRuns ? "its text running" : it.shortText);
  } else if (noticeSteps[i].phase == PHASE_ICON) {
    bool textNext = i + 1 < noticeStepCount && noticeStepsSame(i, i + 1) && noticeSteps[i + 1].phase == PHASE_TEXT;
    bannerKind   = BANNER_ALERT;
    bannerLook   = { noticeIcon(it), it.severity, it.intensity, it.chance };
    noticeStepMs = textNext ? ICON_BEFORE_TEXT_MS : ALERT_BANNER_MS;
    Console.printf("Notice: %s, icon %s\n", it.key, ITEM_ICON_NAMES[bannerLook.icon]);
  } else {
    textToCp437(runText, it.text, sizeof(runText));
    panelSeparators(runText);
    runWidth = (uint16_t)(strlen(runText) * 6);   // 5 px a character and 1 between
    bool alert = it.level == LEVEL_ALERT;
    runR = alert ? ALERT_R : 255; runG = alert ? ALERT_G : 255; runB = alert ? ALERT_B : 255;
    runLastX = INT16_MIN;
    bannerKind   = BANNER_RUN;
    noticeStepMs = (unsigned long)noticeRuns * (runWidth + matrix.width()) * 1000UL / RUN_PX_PER_S;
    Console.printf("Notice: %s, text for %lu ms\n", it.key, noticeStepMs);
  }
  noticeStepAt = i;
  bannerOpen();
  return true;
}

// Starts the steps written, from the first whose notice is still there;
// false when none is.
static bool noticeStart() {
  for (uint8_t i = 0; i < noticeStepCount; i++) {
    if (noticeStepShow(i)) { return true; }
  }
  noticeStepCount = 0;
  return false;
}

// The next step after the one on the panel - with `nextNotice`, the first of
// another notice - and back to the face when none is left.
void noticeAdvance(bool nextNotice) {
  uint8_t i = noticeStepAt + 1;
  while (nextNotice && i < noticeStepCount && noticeStepsSame(i, noticeStepAt)) { i++; }
  for (; i < noticeStepCount; i++) {
    if (noticeStepShow(i)) { return; }
  }
  closeScreen();   // its leaving steps clear the steps
}

// The steps for the notices not shown yet: an alert's icon, an urgent
// message's icon and then its text, a message's text. Alerts that are not
// messages and share an icon share one banner, so two warnings that came
// together do not bring the same banner twice; it shows the most severe.
//
// On the wall clock the texts go into the band instead, which takes the
// first of these notices at once, a message's text running through twice;
// the alerts' icons still come up over the face.
static void noticeStepsForNew() {
  uint8_t order[BAND_ITEMS_MAX];
  uint8_t n = noticeOrder(order, true);
  noticeStepCount = 0;
  noticeRuns = 2;
  noticeCardsRound = false;
#if BAND_ON_PANEL
  if (n) {
    bandWant = { TURN_ITEM, bandItems[order[0]].feed, bandItems[order[0]].tag };
    bandWantRuns = isMessage(bandItems[order[0]]) ? 2 : 1;
  }
#endif
  for (uint8_t k = 0; k < n; k++) {
    const BandItem &it = bandItems[order[k]];
    noticeSeenAdd(it);
    bool message = isMessage(it), alert = it.level == LEVEL_ALERT;
    if (alert && !message) {
      int8_t shared = -1, j = -1;   // the step of the banner it shares, and that step's item
      for (uint8_t s = 0; s < noticeStepCount && shared < 0; s++) {
        j = noticeItem(noticeSteps[s]);
        if (j >= 0 && !isMessage(bandItems[j]) && noticeIcon(bandItems[j]) == noticeIcon(it)) { shared = (int8_t)s; }
      }
      if (shared < 0) {
        noticeAddStep(it, PHASE_ICON);
      } else if (it.severity > bandItems[j].severity) {
        noticeSteps[shared] = { it.feed, it.tag, PHASE_ICON };
      }
      continue;
    }
    if (alert)      { noticeAddStep(it, PHASE_ICON); }
#if !BAND_ON_PANEL
    if (it.text[0]) { noticeAddStep(it, PHASE_TEXT); }
#endif
  }
}

// FN_SHOW_MESSAGES, "Show notifications": every item there is - the alerts
// first, then the messages, each in the order they came, then the
// information in the band's order: an alert's icon and then its text in the
// alert colour, a message's text, each text running through once - whoever
// asked is looking from the start - and an info card alone: the text is for
// what is urgent (Peter, 2026-10-11). Over the face only; "nothing to show"
// when there is nothing. Returns how many items it shows. On the wall
// clock the information is in the band, which starts again at its first
// item; the notices come over the face as here.
uint8_t noticesShowAgain() {
  if (screen != SCREEN_FACE) { return 0; }
  uint8_t order[BAND_ITEMS_MAX];
  uint8_t n = noticeOrder(order, false);
  noticeStepCount = 0;
  noticeRuns = 1;
  noticeCardsRound = false;
  for (uint8_t k = 0; k < n; k++) {
    const BandItem &it = bandItems[order[k]];
    noticeSeenAdd(it);
    if (it.level == LEVEL_ALERT) { noticeAddStep(it, PHASE_ICON); }
    if (it.text[0])              { noticeAddStep(it, PHASE_TEXT); }
  }
  uint8_t m = infoOrder(order, false);
#if BAND_ON_PANEL
  bandRestart();
#else
  for (uint8_t k = 0; k < m; k++) {
    const BandItem &it = bandItems[order[k]];
    noticeSeenAdd(it);
    noticeAddStep(it, PHASE_CARD);
  }
#endif
  Console.printf("Notifications: %u notices, %u information\n", n, m);
  if (!noticeStart() && m == 0) {
    showBanner("nothing", "to show", 110, 110, 110);
    return 0;
  }
  return n + m;
}

// Whether the face has shown the time's last change and moves no more for
// it: the classic face's fly-in of the new minute, the Tetris blocks, the
// muncher's walk and hour show.
bool faceTimeShown() {
  return !faceFor(settings.watchface).busy(faceMoment());
}

// On every pass: a round's minute begins, and whether its round starts now -
// the face at rest, with nothing over it, for ROUND_AFTER_FACE_MS. None on
// the wall clock, whose band shows the information all the time.
static bool infoRoundReady() {
#if BAND_ON_PANEL
  return false;
#endif
  if (minuteTrigger && feedSettings.roundMinutes && minuteNow % feedSettings.roundMinutes == 0) {
    roundMinuteAt = secondStartedAt;
    faceRestAt = 0;
  }
  if (roundMinuteAt == 0) { return false; }
  if (millisNow - roundMinuteAt > ROUND_LATEST_MS) {
    Console.println("Info round left out: the face was not free within a minute");
    roundMinuteAt = 0;
    return false;
  }
  if (screen != SCREEN_FACE || noticeStepCount || !faceTimeShown()) { faceRestAt = 0; return false; }
  if (faceRestAt == 0) { faceRestAt = millisNow; }
  return millisNow - faceRestAt >= ROUND_AFTER_FACE_MS;
}

// How long until the next round's minute begins, in ms; ~0UL when none comes.
unsigned long roundNextIn() {
  if (!feedSettings.roundMinutes || !clockIsSet()) { return ~0UL; }
  uint8_t d = 1;
  while ((minuteNow + d) % feedSettings.roundMinutes != 0) { d++; }
  long ms = ((long)d * 60L - (long)secondNow) * 1000L - (long)(millisNow - secondStartedAt);
  return ms > 0 ? (unsigned long)ms : 0;
}

// Cards for the information: all of it in the round, else what is new. They
// end before the time's next change starts, and what is new comes at the
// minute's place for it (infoSlotOpen()). False when they wait.
static bool infoCardsStart(bool round) {
  uint8_t order[BAND_ITEMS_MAX];
  uint8_t n = infoOrder(order, !round);
  if (n && !panelFitsBeforeChange(CARD_MS * n)) { return false; }
  if (n && !round && !infoSlotOpen(CARD_MS * n)) { return false; }
  if (round) { roundMinuteAt = 0; }
  if (!n) { return true; }
  noticeStepCount = 0;
  noticeRuns = 1;
  for (uint8_t k = 0; k < n; k++) {
    noticeSeenAdd(bandItems[order[k]]);
    noticeAddStep(bandItems[order[k]], PHASE_CARD);
  }
  Console.printf("Info cards: %u, %s\n", n, round ? "the round" : "new");
  if (noticeStart()) { noticeCardsRound = true; }
  return true;
}

// After a round of cards: no GIF for ROUND_GIF_GAP_MS (decorFree()), so the
// two never follow each other at once. Moving the next GIF to the end of
// that gap, as this did, made it come right after the next minute's change.
void infoCardsEnded() {
  infoCardsEndedAt = millisNow;
}

// On every pass outside the hotspot. The marker follows the notices there
// are. Notices not shown yet come up over the face, and wait while a menu,
// an editor or another banner is up; an alert ends a GIF on show, while a
// message lets it play to its end. A notice that went before it could be
// shown is not shown later. An alert that eased to information stays known,
// as information; information that turns into an alert comes up as new.
// After the notices, the information: what is new as cards at once, all of
// it in the info round; over the face only, after a GIF's end.
void noticesUpdate() {
#if defined(FACE_FRAME_LOG)
  if (testQuiet) { return; }       // a test's scenes: nothing over the face (POST /api/test quiet=1)
#endif
  bool round = infoRoundReady();   // on every pass: it keeps watch for the round's minute
  NoticeMark mark = MARK_NONE;
  bool newAlert = false, newAny = false, newInfo = false;
  for (uint8_t i = 0; i < bandItemCount; i++) {
    const BandItem &it = bandItems[i];
    int8_t s = noticeSeenIndex(it.feed, it.tag);
    if (!isNotice(it)) {
      if (s >= 0 && noticeSeen[s].notice) {   // an alert that eased: shown, now as information
        noticeSeen[s].notice = false;
        noticeSeen[s].icon = it.icon;
      }
      if (s >= 0 && noticeSeen[s].icon == it.icon) { noticeSeen[s].lastAt = millisNow; }
      else                                         { newInfo = true; }
      continue;
    }
    if (s >= 0 && !noticeSeen[s].notice) {   // information that turned into an alert
      noticeSeen[s] = noticeSeen[--noticeSeenCount];
      s = -1;
    }
    bool alert = it.level == LEVEL_ALERT;
    if (alert)                   { mark = MARK_ALERT; }
    else if (mark == MARK_NONE)  { mark = MARK_MESSAGE; }
    if (s >= 0) { noticeSeen[s].lastAt = millisNow; continue; }
    newAny = true;
    newAlert |= alert;
  }
  if (mark != noticeMark) {
    noticeMark = mark;
    panelDirty = true;   // the marker comes, goes or changes colour
  }
  for (int i = (int)noticeSeenCount - 1; i >= 0; i--) {
    if (millisNow - noticeSeen[i].lastAt < NOTICE_FORGET_MS) { continue; }
    noticeSeen[i] = noticeSeen[--noticeSeenCount];
  }
  noticesPending = false;
  if (noticeStepCount) { return; }                                // items on the panel
  if (screen != SCREEN_FACE && screen != SCREEN_GIF) { return; }  // waits for the face
  if (newAny) {
    // An alert at once, over everything but the person at the clock;
    // messages at the minute's place for them, after a GIF's end - on the
    // wall clock they go into the band, which does not cover the face.
#if BAND_ON_PANEL
    if (!newAlert && screen == SCREEN_GIF) {
#else
    if (!newAlert && (screen == SCREEN_GIF || !infoSlotOpen(noticeNewMs()))) {
#endif
      noticesPending = true;
      return;
    }
    if (screen == SCREEN_GIF) { closeScreen(); }
    noticeStepsForNew();
    noticeStart();
    return;
  }
#if BAND_ON_PANEL
  return;                                                         // the information takes turns in the band
#endif
  if (screen != SCREEN_FACE) { return; }                          // information waits for a GIF's end
  if ((newInfo || round) && !infoCardsStart(round) && newInfo) { noticesPending = true; }
}

// How long the notices not shown yet will take on the panel, about: an
// alert's icon, an urgent message's icon before its text, each text running
// through twice.
static unsigned long noticeNewMs() {
  uint8_t order[BAND_ITEMS_MAX];
  uint8_t n = noticeOrder(order, true);
  unsigned long ms = 0;
  for (uint8_t k = 0; k < n; k++) {
    const BandItem &it = bandItems[order[k]];
    if (it.level == LEVEL_ALERT) { ms += it.text[0] ? ICON_BEFORE_TEXT_MS : ALERT_BANNER_MS; }
    if (it.text[0]) { ms += 2UL * (strlen(it.text) * 6UL + matrix.width()) * 1000UL / RUN_PX_PER_S; }
  }
  return ms;
}

// Whether a face event closes a notice on the panel (the Inputs page).
bool noticeClosedBy(InputEvent ev) {
  int8_t i = faceEventIndex(ev);
  return i >= 0 && noticeCloses[i] != 0;
}

// At start-up: what closes a notice, as the Inputs page stored it, or what
// closes every other banner.
void loadNoticeKeys() {
  noticeKeysStore.begin("closes");
  for (uint8_t i = 0; i < FACE_EVENT_COUNT; i++) { noticeCloses[i] = closesBanner(FACE_EVENTS[i].event) ? 1 : 0; }
  NoticeKeys k;
  noticeKeysStore.read(k);
  if (k.magic != NOTICE_KEYS_MAGIC || k.rev != NOTICE_KEYS_REV) { return; }
  for (uint8_t i = 0; i < FACE_EVENT_COUNT && i < k.count; i++) { noticeCloses[i] = k.closes[i] ? 1 : 0; }
}

bool saveNoticeKeys() {
  NoticeKeys k;
  memset(&k, 0, sizeof(k));
  k.magic = NOTICE_KEYS_MAGIC;
  k.rev   = NOTICE_KEYS_REV;
  k.count = FACE_EVENT_COUNT;
  memcpy(k.closes, noticeCloses, FACE_EVENT_COUNT);
  return noticeKeysStore.write(k);
}

// The Inputs page's ticks, when it sent them (cset): c<i>=on for each event
// found that closes a notice; an event not found keeps what it had.
void applyNoticeKeysForm(const String &q) {
  if (!getParam(q, "cset").length()) { return; }
  for (uint8_t i = 0; i < FACE_EVENT_COUNT; i++) {
    if (!faceEventFitted(FACE_EVENTS[i].event)) { continue; }
    noticeCloses[i] = getParam(q, String("c") + i) == "on" ? 1 : 0;
  }
}

// The colours of the icons (decided on 2026-10-11): every part its own; a
// warning's level in the DWD's colours - yellow, orange, red, violet - and in
// the alert colour when none is given; rain and snow grey when they are
// unlikely, taking their colour as their chance rises.
IconRgb levelRgb(uint8_t severity) {
  switch (severity) {
    case 1:  return { 255, 230, 0 };
    case 2:  return { 255, 130, 0 };
    case 3:  return { 255, 0, 0 };
    case 4:  return { 170, 0, 255 };
    default: return { ALERT_R, ALERT_G, ALERT_B };
  }
}

// The air quality's band (airBand()), 0 good to 5 extremely poor, on the
// clock's own scale: green through yellow and orange to red and violet, as
// the warning levels go - not the European Environment Agency's colours,
// whose pages give none (looked for on 2026-10-18).
static IconRgb airRgb(uint8_t band) {
  static const IconRgb BANDS[] = {
    { 0, 210, 70 }, { 150, 220, 0 }, { 255, 220, 0 }, { 255, 130, 0 }, { 255, 0, 0 }, { 170, 0, 255 },
  };
  return BANDS[band < 6 ? band : 5];
}

static IconRgb chanceRgb(IconRgb c, uint8_t chance) {
  if (chance > 100) { return c; }   // CHANCE_UNKNOWN
  const IconRgb grey = { 70, 72, 82 };
  return { (uint8_t)(grey.r + ((int)c.r - grey.r) * chance / 100), (uint8_t)(grey.g + ((int)c.g - grey.g) * chance / 100),
           (uint8_t)(grey.b + ((int)c.b - grey.b) * chance / 100) };
}

static IconRgb roleRgb(uint8_t role, const IconLook &look) {
  switch (role) {
    case ROLE_CLOUD:     return { 205, 210, 225 };
    case ROLE_CLOUD_WET: return { 140, 148, 168 };
    case ROLE_SUN:       return { 255, 190, 0 };
    case ROLE_RAYS:      return { 255, 110, 0 };
    case ROLE_MOON:      return { 255, 225, 120 };
    case ROLE_FOG:       return { 150, 150, 165 };
    case ROLE_RAIN:      return chanceRgb({ 0, 105, 255 }, look.chance);
    case ROLE_SNOW:      return chanceRgb({ 235, 245, 255 }, look.chance);
    case ROLE_BOLT:      return { 255, 225, 0 };
    case ROLE_LEVEL:     return levelRgb(look.severity);
    case ROLE_MARK:      return { 255, 255, 255 };
    case ROLE_WIND:      return { 120, 210, 255 };
    case ROLE_GLASS:     return { 225, 235, 245 };
    case ROLE_COLD:      return { 0, 105, 255 };
    case ROLE_HOT:       return { 255, 20, 0 };
    case ROLE_WAVES:     return { 255, 110, 0 };
    case ROLE_FLAKE:     return { 120, 210, 255 };
    case ROLE_PAGE:      return { 230, 30, 30 };
    case ROLE_AIR:       return airRgb(look.severity);
    default:             return { 0, 0, 0 };
  }
}

// An icon at (x, y), its top left corner, each part in its colour: on the
// panel's canvas, or into another picture (the wall's band).
void drawItemIconOn(Adafruit_GFX &g, int16_t x, int16_t y, const IconLook &look) {
  const IconBits &pic = iconPicture(look.icon, look.intensity);
  uint16_t colour[3];
  for (uint8_t p = 0; p < 3; p++) {
    IconRgb c = roleRgb(pic.role[p], look);
    colour[p] = scaledColorVisible(c.r, c.g, c.b);
  }
  for (uint8_t r = 0; r < ICON_SIZE; r++) {
    uint32_t lo = pic.row[r].lo, hi = pic.row[r].hi;
    for (uint8_t c = 0; c < ICON_SIZE; c++) {
      uint8_t bit = ICON_SIZE - 1 - c;
      uint8_t part = (uint8_t)((((hi >> bit) & 1u) << 1) | ((lo >> bit) & 1u));
      if (part) { g.drawPixel(x + c, y + r, colour[part - 1]); }
    }
  }
}

static void drawItemIcon(int16_t x, int16_t y, const IconLook &look) { drawItemIconOn(matrix, x, y, look); }

#if defined(CLOCK_DEBUG)
// At start-up: the icon each DWD event gets (feedWarningIcon()). The first
// six are the English names Bright Sky gave on 2026-10-11 for the warnings
// in force then; the others are the words of the DWD's headlines for frost,
// black ice and heat, and events that keep the triangle.
void warningIconSelfTest() {
  struct Case { const char *event; uint8_t icon; };
  static const Case CASES[] = {
    { "near gale", ICON_WIND }, { "thunderstorms, possibly with wind gusts", ICON_THUNDER },
    { "wind gusts", ICON_WIND }, { "gale-force gusts", ICON_WIND }, { "strong wind", ICON_WIND },
    { "heavy rain", ICON_RAIN }, { "frost", ICON_FROST }, { "SIGNIFICANT ICY SURFACES", ICON_FROST },
    { "black ice", ICON_FROST }, { "strong heat", ICON_HEAT }, { "heavy snowfall", ICON_SNOW },
    { "fog", ICON_FOG }, { "UV index", ICON_WARNING }, { "", ICON_WARNING }, { nullptr, ICON_WARNING },
  };
  uint8_t wrong = 0;
  for (const Case &c : CASES) {
    uint8_t got = feedWarningIcon(c.event);
    if (got == c.icon) { continue; }
    wrong++;
    Console.printf("Warning icons: \"%s\" gave %s, not %s\n", c.event ? c.event : "(none)", ITEM_ICON_NAMES[got],
                  ITEM_ICON_NAMES[c.icon]);
  }
  Console.printf("Warning icons self-test: %s, %u cases\n", wrong ? "FAILED" : "all right",
                (unsigned)(sizeof(CASES) / sizeof(CASES[0])));
}
#endif

// An alert's banner: its icon alone, in the middle, and round the panel's
// edge a frame in the colour of its level, unless the frames are switched
// off (the web app's Info section).
void drawAlertBanner() {
  matrix.fillScreen(0);
  if (feedSettings.frames) {
    IconRgb f = levelRgb(bannerLook.severity);
    matrix.drawRect(0, 0, matrix.width(), matrix.height(), scaledColorVisible(f.r, f.g, f.b));
  }
  drawItemIcon((matrix.width() - ICON_SIZE) / 2, (matrix.height() - ICON_SIZE) / 2, bannerLook);
  drawFeedbackIndicator();
  matrix.show();
  panelDirty = false;
}

// Where an info card puts its parts (CardLayout): the lines beside the icon
// on the wide panel, below it on the upright one; their area is where a text
// without a short form runs through.
static CardLayout cardLayout() {
  int16_t w = matrix.width(), h = matrix.height();
  if (h > w) { return { (int16_t)((w - ICON_SIZE) / 2), 4, 0, w, { 34, 46 }, { 31, 49 }, 40 }; }
  return { 2, (int16_t)((h - ICON_SIZE) / 2), 26, (int16_t)(w - 26), { 7, 18 }, { 3, 21 }, (int16_t)((h - 7) / 2) };
}

// A card's line in the 5x7 font (CP437) at a size, its width in pixels: 6 a
// character and a size more for each at double size, but a degree sign stays
// small there.
static int16_t cardLineWidth(const char *s, uint8_t size) {
  int16_t w = 0;
  for (const char *p = s; *p; p++) { w += ((uint8_t)*p == 0xF8 && size > 1) ? 6 : 6 * size; }
  return w > 0 ? w - size : 0;   // no gap after the last character
}

static void drawCardLine(const char *s, int16_t x, int16_t y, uint8_t size, uint16_t colour) {
  for (const char *p = s; *p; p++) {
    uint8_t c = (uint8_t)*p;
    if (c == 0xF8 && size > 1) {   // a degree sign small and raised, as it is written
      matrix.drawChar(x, y, c, colour, colour, 1);
      x += 6;
      continue;
    }
    matrix.drawChar(x, y, c, colour, colour, size);
    x += 6 * size;
  }
}

// An info card: its icon, and its two lines or its text running through once;
// a frame in its level's colour for an item that has one (a DWD warning).
void drawCardBanner() {
  CardLayout L = cardLayout();
  int16_t w = matrix.width(), h = matrix.height();
  bool icon = bannerLook.icon != ICON_NONE;
  int16_t ax = icon ? L.areaX : 0, aw = icon ? L.areaW : w;
  int16_t x = 0;
  if (cardRuns) {
    uint32_t moved = (uint32_t)(millisNow - bannerAt) * RUN_PX_PER_S / 1000UL;
    x = moved >= (uint32_t)(runWidth + aw) ? ax - (int16_t)runWidth : ax + aw - (int16_t)moved;
    if (x == runLastX && !panelDirty) { return; }
    runLastX = x;
  }
  matrix.fillScreen(0);
  matrix.setFont();          // the built-in 5x7 font, code page 437
  matrix.setTextWrap(false);
  matrix.cp437(true);
  uint16_t white = scaledColorVisible(235, 235, 235), grey = scaledColorVisible(150, 150, 160);
  if (cardRuns) {
    matrix.setTextSize(1);
    matrix.setTextColor(white);
    matrix.setCursor(x, icon ? L.runY : (h - 7) / 2);
    matrix.print(runText);
    if (icon && L.areaX > 0) { matrix.fillRect(0, 0, L.areaX, h, 0); }   // the text goes in and out beside the icon
  } else {
    bool two = cardLine[1][0] != '\0';
    bool big = two && cardLineWidth(cardLine[0], 2) <= aw - 2;   // a first line of a few characters, the temperature
    for (uint8_t i = 0; i < (two ? 2 : 1); i++) {
      uint8_t size = (big && i == 0) ? 2 : 1;
      int16_t lw = cardLineWidth(cardLine[i], size);
      int16_t y = big ? L.bigY[i] : two ? L.lineY[i] : (L.lineY[0] + L.lineY[1]) / 2;
      if (!icon) { y += (h > w) ? -16 : 0; }   // upright without an icon: the lines in the middle
      drawCardLine(cardLine[i], ax + (aw - lw) / 2, y, size, (big && i == 1) ? grey : white);
    }
  }
  matrix.cp437(false);
  if (feedSettings.frames && bannerLook.severity) {
    IconRgb f = levelRgb(bannerLook.severity);
    matrix.drawRect(0, 0, w, h, scaledColorVisible(f.r, f.g, f.b));
  }
  if (icon) { drawItemIcon(L.iconX, L.iconY, bannerLook); }
  drawFeedbackIndicator();
  matrix.show();
  panelDirty = false;
}

// A notice's text running through the middle of the panel, in at the right
// edge and out at the left, noticeRuns times; drawn when it has moved on a
// pixel.
void drawRunBanner() {
  int16_t w = matrix.width(), h = matrix.height();
  uint32_t pass = runWidth + w;
  uint32_t moved = (uint32_t)(millisNow - bannerAt) * RUN_PX_PER_S / 1000UL;
  int16_t x = moved >= noticeRuns * pass ? -(int16_t)runWidth : w - (int16_t)(moved % pass);
  if (x == runLastX && !panelDirty) { return; }
  runLastX = x;
  matrix.fillScreen(0);
  matrix.setFont();          // the built-in 5x7 font, code page 437
  matrix.setTextSize(1);
  matrix.setTextWrap(false);
  matrix.cp437(true);        // its true code page: the classic mode moves everything from 176 up by one
  matrix.setTextColor(scaledColorVisible(runR, runG, runB));
  matrix.setCursor(x, (h - 7) / 2);
  matrix.print(runText);
  matrix.cp437(false);
  drawFeedbackIndicator();
  matrix.show();
  panelDirty = false;
}
