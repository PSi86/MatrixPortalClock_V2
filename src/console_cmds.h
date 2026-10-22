/* -------------------------------------------------------------------------
   console_cmds.h - the console's commands, in builds with a console
   (console.h; a release build takes none): the log level, a fetch of every
   info band source now, the web app on the home network on or off, and on
   the wall clock its own - dark panels, test pictures, the panel bus and its
   DMA priority, the crypto test, the clocks, the motion sensor, the microSD
   card. Also the wall clock's watch on its panel output, on every pass in
   every build: when the output has stopped it is logged and started again.

   A module of its own since 2026-10-22 (code review of 2026-10-14, point 10).
   ------------------------------------------------------------------------- */
#ifndef CONSOLE_CMDS_H
#define CONSOLE_CMDS_H

#include <Arduino.h>
#include "board_hal.h"
#include "console.h"   // CLOCK_CONSOLE

#if CLOCK_CONSOLE
void consoleCommands();   // on every pass: a command line that has come in
#endif

#if BOARD_WAVESHARE_WALL
extern uint32_t wallOutputStops;   // the panel output's stops since the start
void wallOutputWatch();            // on every pass: the panel output started again when it has stopped
#endif

#endif  // CONSOLE_CMDS_H
