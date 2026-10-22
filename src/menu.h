/* -------------------------------------------------------------------------
   menu.h - the on-screen menu (display class S, 64x32 and 32x64): one flat
   list of items, each with its editor - a number, a toggle, the zone, a
   choice, an action, the time and date - and a slide between the list and an
   editor. A value being edited lives in RAM; flash is written on ENTER only.
   Also the banners: a short confirmation over the screen it confirms, a hint
   of what a gesture does, and the steps of the notices on show (notices.h).

   A module of its own since 2026-10-22 (code review of 2026-10-14, point 10).
   ------------------------------------------------------------------------- */
#ifndef MENU_H
#define MENU_H

#include <Arduino.h>
#include "input_map.h"   // InputFunction

// ---- The menu ----------------------------------------------------------------------
void menuOpen();                                  // FN_OPEN_MENU: the list, over the face
void navigate(InputFunction fn, uint8_t steps);   // PREV, NEXT, ENTER, BACK, HOME in the menu or an editor
void drawMenu();
void drawEditor();
bool editorMoving();    // the editor has to be redrawn without an input (a scrolling name)
void editorRevert();    // what ENTER did not save goes back
extern int8_t slideDir; // the slide the next menu picture starts with: +1 deeper, -1 back, 0 none
// What a function does on the face (the profile's or the shortcuts' rows).
void cycleDstMode();
void toggleAutoBrightWithBanner();
// The menu's look: its colours and its 3x5 font.
uint16_t uiInk();
uint16_t uiQuiet();
void uiTiny(int16_t x, int16_t top, const char *s, uint16_t colour);

// ---- Banners ---------------------------------------------------------------------
// What a banner shows: text, the time, a hint, or a step of the notices.
enum BannerKind : uint8_t { BANNER_TEXT, BANNER_TIME, BANNER_HINT, BANNER_ALERT, BANNER_RUN, BANNER_CARD };
extern BannerKind    bannerKind;
extern unsigned long bannerAt;   // millis() the banner came up
void bannerOpen();               // put a banner up, or replace the one that is up
void showBanner(const char *first, const char *second, uint8_t r, uint8_t g, uint8_t b);
void showHints();                // FN_SHOW_HINTS: what a swipe does on the face
void drawBanner();

#endif  // MENU_H
