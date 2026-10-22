/* -------------------------------------------------------------------------
   gif_player.h - GIFs on the panel. Now and then a GIF takes the panel in
   place of the face, for gifSettings.seconds; an input or the web app plays
   one at once. The GIFs come from the GIF pack in the flash's ffat
   partition (scripts/gif_pack.py writes it) and, on the wall clock, from
   its microSD card while one with GIFs is in (gif_card.h does the card's
   work in a task of its own). One plays only when it fits the panel as it
   is held: centred and unscaled, at 8 bits per colour through a gamma
   table, dimmed by the panel's brightness tables as the face is.

   A module of its own since 2026-10-22 (code review of 2026-10-14, point 10).
   ------------------------------------------------------------------------- */
#ifndef GIF_PLAYER_H
#define GIF_PLAYER_H

#include <Arduino.h>
#include <esp_partition.h>
#include "board_hal.h"
#include "settings.h"    // GIF_SECONDS_MAX
#if BOARD_WAVESHARE_WALL
#include "gif_card.h"    // CardList
#endif

// ---- Playing -------------------------------------------------------------------
extern int16_t       gifLast;       // the GIF shown before the one on show (GIF again)
extern unsigned long gifShownAt;    // millis() the GIF on show began
// A GIF ends at the end of its last loop (drawGif()); one whose single loop
// runs longer than the longest setting is ended there anyway, so a very long
// GIF cannot hide the clock for minutes.
const unsigned long GIF_PLAY_MAX_MS = GIF_SECONDS_MAX * 1000UL;

void    gifGammaInit();      // at start-up: the gamma table
void    gifScheduleNext();   // the next GIF of its own accord, at a random time between the settings
void    gifUpdate();         // on every pass: a GIF of its own accord when one is due
void    drawGif();           // the GIF's next frame, when it is due (SCREEN_GIF)
void    gifEnd();            // its screen closed: the GIF goes, its RAM with it
void    gifPlayNow();        // one at once (an input, the web app); now or not at all
void    gifPlayAgain();      // the last one again
int16_t gifCount();          // the GIFs there are to play
void    applyGifParams(const String &q);   // the web app's GIF settings (/api/save)

// ---- The GIF pack ----------------------------------------------------------------
// The GIFs in the ffat partition. scripts/gif_pack.py writes them and
// describes the layout; all numbers little-endian.
struct GifPackHeader {
  char     magic[8];          // "CLKGPACK"
  uint16_t version;           // 1
  uint16_t count;             // entries in the index right after the header
  uint32_t indexCrc;          // CRC-32 of the index
  uint32_t size;              // header, index and GIFs, in bytes
  uint8_t  reserved[12];
};
struct GifPackEntry {
  char     name[32];          // NUL-terminated
  uint16_t width, height;
  uint8_t  codec;             // GIF_PACK_STORED or GIF_PACK_DEFLATED
  uint8_t  reserved[3];
  uint32_t offset;            // from the start of the partition
  uint32_t size;              // bytes stored
  uint32_t gifSize;           // bytes of the GIF itself
  uint32_t crc;               // CRC-32 of the bytes stored
};
static_assert(sizeof(GifPackHeader) == 32 && sizeof(GifPackEntry) == 56, "the layout scripts/gif_pack.py writes");
const uint8_t  GIF_PACK_STORED = 0, GIF_PACK_DEFLATED = 1;
const uint16_t GIF_PACK_COUNT_MAX = 4000;
enum GifPackState : uint8_t { GIF_PACK_NONE, GIF_PACK_BAD, GIF_PACK_OK };
extern GifPackState  gifPackState;
extern const esp_partition_t *gifPackPart;
extern GifPackEntry *gifPack;        // the index, in PSRAM when there is some
extern uint16_t      gifPackCount;
void gifPackLoad();                  // what the partition holds: a pack, a damaged one, or none
bool gifPackCrcOk(const GifPackEntry &e);

#if BOARD_WAVESHARE_WALL
// ---- The wall clock's microSD card -------------------------------------------------
extern CardList cardList;
extern bool     cardMounted;
extern bool     gifFromCard;         // the card's GIFs play, the pack stands by
extern bool     cardJobBusy;         // a job is with the card's task
extern bool     cardRemountWanted;   // POST /api/card, the console, or reads that keep failing
extern uint32_t cardMb, cardFreeMb;
void gifCardBegin();                 // at start-up: the card's task
void gifCardUpdate();                // on every pass: its results, and the next GIF read ahead
void gifCardCommand(const char *line);   // the console's "sd ..."
#endif

#endif  // GIF_PLAYER_H
