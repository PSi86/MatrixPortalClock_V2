/* -------------------------------------------------------------------------
   notices.h - the info band's items on the 64x32 clock, which has no room
   for the band (concept doc, "Info for every clock"). The notices - the
   items at alert level and the messages from Home Assistant - come up as
   banners over the face: an alert's icon, a message's text running through,
   an urgent message as both; the face carries a marker while there are any.
   The other items, the information, come as info cards: once when they are
   new, and all of them in the info round. "Show notifications" shows
   everything once more. On the wall clock the band shows the texts and the
   information; the alerts' icons still come up over the face.

   Also here: how an item's icon is drawn and coloured, which the wall
   clock's band shares, and which face events close a notice (the Inputs
   page, a blob of its own).

   A module of its own since 2026-10-22 (code review of 2026-10-14, point 10).
   ------------------------------------------------------------------------- */
#ifndef NOTICES_H
#define NOTICES_H

#include <Arduino.h>
#include <Adafruit_GFX.h>
#include "board_hal.h"
#include "band_data.h"   // BandItem
#include "input_map.h"   // InputEvent, FACE_EVENT_COUNT

// ---- Icons ----------------------------------------------------------------------
// An icon and how it looks: the ItemIcon, then its item's severity,
// intensity and chance (BandItem).
struct IconLook { uint8_t icon, severity, intensity, chance; };
struct IconRgb { uint8_t r, g, b; };             // a colour of an icon's part (roleRgb())
IconRgb levelRgb(uint8_t severity);              // a warning level's colour
void drawItemIconOn(Adafruit_GFX &g, int16_t x, int16_t y, const IconLook &look);
uint8_t noticeIcon(const BandItem &it);          // its own, the warning sign for one without
#if defined(CLOCK_DEBUG)
void warningIconSelfTest();
#endif

// ---- Texts ----------------------------------------------------------------------
const uint16_t RUN_PX_PER_S = 30;   // a text running through: about five characters a second
void textToCp437(char *dst, const char *src, size_t size);   // UTF-8 into the font's code page
void panelSeparators(char *s);      // " | " shown as a middle dot

// ---- The notices and the info cards -----------------------------------------------
const uint8_t ALERT_R = 255, ALERT_G = 0, ALERT_B = 0;   // the alert colour
enum NoticeMark : uint8_t { MARK_NONE, MARK_MESSAGE, MARK_ALERT };
extern NoticeMark    noticeMark;         // what the face's corner shows
extern uint8_t       noticeStepCount;    // the steps on the panel; 0: none
extern unsigned long noticeStepMs;       // how long the step on the panel stays
extern bool          noticeCardsRound;   // the steps on the panel are cards of their own accord: an input ends them
extern bool          cardRuns;           // the card on show has no short form: its text runs
extern bool          noticesPending;     // messages or new cards wait for the minute's place for them
extern unsigned long roundMinuteAt;      // millis() the info round's minute began; 0 = no round waits
extern unsigned long infoCardsEndedAt;   // a GIF keeps ROUND_GIF_GAP_MS after info cards

void noticesUpdate();              // on every pass: new notices and cards come up, the round keeps watch
void noticeAdvance(bool nextNotice);   // the next step, or back to the face
uint8_t noticesShowAgain();        // "Show notifications": how many items it shows
void infoCardsEnded();             // the cards' screen closed
bool faceTimeShown();              // the face has shown the new time and is not busy
unsigned long roundNextIn();       // ms to the next info round
void noticeForget(const BandItem &it);   // an item that is over is new again when it comes back
uint8_t infoOrder(uint8_t *order, bool newOnly);     // the information, in the band's order
uint8_t noticeOrder(uint8_t *order, bool newOnly);   // the notices, alerts first
const char *itemShowsHere(const BandItem &it);       // for the web app's list
void drawAlertBanner();
void drawRunBanner();
void drawCardBanner();

// ---- Which face events close a notice ------------------------------------------------
extern uint8_t noticeCloses[FACE_EVENT_COUNT];   // 1: this face event closes a notice, in FACE_EVENTS order
bool noticeClosedBy(InputEvent ev);
void loadNoticeKeys();
bool saveNoticeKeys();
void applyNoticeKeysForm(const String &q);

#endif  // NOTICES_H
