/* -------------------------------------------------------------------------
   panel_rules.h - who takes the panel, and when (concept doc, tab "Panel
   priorities"; panel_rules.cpp has the rules in full). The time's change is
   protected from what the clock plans itself; messages and information
   come at a fixed place in the minute; decoration - the faces' extras and
   GIFs of their own accord - takes the panel only when it is free, and is
   dropped when it is not. Also here: when the faces' extras play.

   A module of its own since 2026-10-22 (code review of 2026-10-14, point 10).
   ------------------------------------------------------------------------- */
#ifndef PANEL_RULES_H
#define PANEL_RULES_H

#include <Arduino.h>

const unsigned long ROUND_GIF_GAP_MS = 60000;   // at least this between an info round and a GIF

extern unsigned long panelFaceSince;   // millis() the face came back on top
extern unsigned long extrasDueAt;      // the next extra of a face; 0 = not drawn yet

bool panelFitsBeforeChange(unsigned long ms);   // something this long ends before the face's next change
bool infoSlotOpen(unsigned long ms);            // messages and new cards this long may come up now
bool decorFree(unsigned long ms, bool gif);     // decoration of its own accord may take the panel now
unsigned long panelRandomAt(unsigned long lo, unsigned long hi, unsigned long ms);   // a random time outside the changes' slots
void panelWatch();                              // on every pass: when the face's change or scene ends
void extrasUpdate();                            // on every pass outside the hotspot: an extra when one is due
void applyDecorParams(const String &q);         // the web app's extras and quiet (/api/save)

#endif  // PANEL_RULES_H
