/* ----------------------------------------------------------------------
   The clock's console: the USB serial, in debug and test builds only.

   Release builds write nothing and take no commands (Peter, 2026-10-21):
   debugging and development run the debug build. Console stands for Serial
   where the console is on; in a release build it is an object whose calls do
   nothing, and the compiler drops them. A release build with -DCLOCK_CONSOLE=1
   has the console all the same, for a look at one.

   Log levels, in builds with a console (CLOCK_LOG, 1 unless built otherwise;
   the console's "log 1" or "log 2" changes it until the next start):
     1  the start, the sensors, errors and changes of state - the WiFi, the
        time, Home Assistant, fetches, GIFs, inputs
     2  and the running figures: the light every second, the frames and the
        face's passes every second or two, the RAM every 30 s
     3  and the core's own WiFi and HTTP lines, which the core builds in or
        leaves out as it is compiled: the ..._verbose environments
        (platformio.ini; CORE_DEBUG_LEVEL=4, CLOCK_LOG=2)
   ---------------------------------------------------------------------- */
#ifndef CONSOLE_H
#define CONSOLE_H

#include <Arduino.h>

#ifndef CLOCK_CONSOLE
  #if defined(CLOCK_DEBUG) || defined(FACE_FRAME_LOG) || defined(SENSOR_FAIL_FIRST) || defined(SENSOR_HIDE)
    #define CLOCK_CONSOLE 1
  #else
    #define CLOCK_CONSOLE 0
  #endif
#endif

#ifndef CLOCK_LOG
  #define CLOCK_LOG 1
#endif

#if CLOCK_CONSOLE
static decltype(Serial) &Console = Serial;
#else
struct NoConsole {
  template <typename... A> size_t print(A &&...) { return 0; }
  template <typename... A> size_t println(A &&...) { return 0; }
  template <typename... A> size_t printf(const char *, A &&...) { return 0; }
  template <typename... A> size_t write(A &&...) { return 0; }
  int available() { return 0; }
  int read() { return -1; }
  void flush() {}
};
static NoConsole Console;
#endif

// The log level now (above); 0 without a console.
static uint8_t consoleLevel = CLOCK_CONSOLE ? CLOCK_LOG : 0;

#endif  // CONSOLE_H
