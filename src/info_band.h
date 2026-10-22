/* -------------------------------------------------------------------------
   info_band.h - the info band on the wall clock: the 192x32 below the face
   (concept doc, section "Info band"). One turn after the other: the items
   (alerts, messages, information) with their icons and texts, and the
   clock's own turns - its status, the clock change, the date, the sun, the
   moon and the room. A new notice takes the band at once. The 64x32 clock
   has no band; its items come as notices and cards (notices.h).

   A module of its own since 2026-10-22 (code review of 2026-10-14, point 10).
   ------------------------------------------------------------------------- */
#ifndef INFO_BAND_H
#define INFO_BAND_H

#include <Arduino.h>
#include "board_hal.h"   // BAND_ON_PANEL

#if BAND_ON_PANEL
// A turn: an item by source and tag, or one of the clock's own.
enum BandTurnKind : uint8_t { TURN_NONE, TURN_ITEM, TURN_STATUS, TURN_SHIFT, TURN_DATE, TURN_SUN, TURN_MOON, TURN_ROOM };
struct BandTurn { uint8_t kind, feed; uint32_t tag; };
extern BandTurn      bandWant;           // one that takes the band at once (a new notice)
extern uint8_t       bandWantRuns;       // how often its text runs through
extern bool          bandLocalDone;      // false: the clock's own turns are worked out anew (bandLocalUpdate())
extern unsigned long bandStatusTestAt;   // millis() the console showed the clock status as a test; 0 = none

void bandUpdate();    // on every pass outside the hotspot: the turn on show, the next when it is due
void bandRestart();   // the band starts again at its first turn
void bandHide();      // while the hotspot has the panel: the band dark, once
#if defined(CLOCK_DEBUG)
void bandSelfTest();
#endif
#endif  // BAND_ON_PANEL

#endif  // INFO_BAND_H
