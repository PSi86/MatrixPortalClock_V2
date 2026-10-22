// The wall clock's knock interrupt (knock.h): it counts the QMI8658's INT1
// edges. In a file of its own, since a function in IRAM cannot be inline -
// its literals would land elsewhere, and the linker refuses that - and so is
// defined once, whatever includes knock.h.
#include "knock.h"

#if BOARD_WAVESHARE_WALL
volatile uint32_t KnockIsr::edges = 0;
volatile unsigned long KnockIsr::edgeAt = 0;
void IRAM_ATTR KnockIsr::edge() {
  edges = edges + 1;
  edgeAt = millis();
}
#endif
