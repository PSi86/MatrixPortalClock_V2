/* -------------------------------------------------------------------------
   gif_player.h - GIFs on the panel. Now and then a GIF takes the panel in
   place of the face, for gifSettings.seconds; an input or the web app plays
   one at once. The GIFs come from the GIF pack in the flash's ffat
   partition (scripts/gif_pack.py writes it) and, on the wall clock, from
   its microSD card while one with GIFs is in (gif_card.h does the card's
   work in a task of its own). One plays only when it fits the panel as it
   is held: centred and unscaled, at 8 bits per colour through a gamma
   table, dimmed by the panel's brightness tables as the face is.
   GIFs on request (2026-10-23): one asked for by its name, or one of those a
   keyword finds in the GIF library's meta index, which the pack and the
   card carry (gif_find.h); or all of them, one after another (2026-10-24).

   A module of its own since 2026-10-22 (code review of 2026-10-14, point 10).
   ------------------------------------------------------------------------- */
#ifndef GIF_PLAYER_H
#define GIF_PLAYER_H

#include <Arduino.h>
#include <esp_partition.h>
#include "board_hal.h"
#include "settings.h"    // GIF_SECONDS_MAX
#include "gif_find.h"    // GifMeta
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

bool    gifPlaying();        // a GIF has the panel, its decoder up
void    gifGammaInit();      // at start-up: the gamma table
void    gifScheduleNext();   // the next GIF of its own accord, at a random time between the settings
void    gifUpdate();         // on every pass: a GIF of its own accord when one is due
void    drawGif();           // the GIF's next frame, when it is due (SCREEN_GIF)
void    gifEnd();            // its screen closed: the GIF goes, its RAM with it
void    gifPlayNow();        // one at once (an input, the web app); now or not at all
void    gifPlayAgain();      // the last one again
int16_t gifCount();          // the GIFs there are to play
void    applyGifParams(const String &q);   // the web app's GIF settings (/api/save)

// ---- GIFs on request ---------------------------------------------------------------
// A GIF asked for by its name, or one of those a keyword finds (gif_find.h),
// from the web app, Home Assistant or the console: over the face, now or not
// at all - also into the time's change, as every GIF the user asks for. With
// replace it takes the place of a GIF on show; without, a GIF on show stays
// and the request goes (Peter, 2026-10-23). The wall clock reads a card GIF
// first that is not read ahead: it plays once it is read, if the face is
// free then.
enum GifAsked : uint8_t {
  GIF_ASKED_PLAYS,        // on the panel now
  GIF_ASKED_READING,      // the wall: being read from the card, plays when it is
  GIF_ASKED_NOT_ON_FACE,  // the menu, a notice, the hotspot ...: not now
  GIF_ASKED_GIF_ON_SHOW,  // a GIF on show, and no replace
  GIF_ASKED_NO_MATCH,     // the request names or finds no GIF
  GIF_ASKED_NONE_FITS,    // it finds some, none fits the panel as it is held
  GIF_ASKED_NOT_OPENED,   // the one chosen did not open this moment (no RAM, the card's next not read yet)
  GIF_ASKED_NO_SEARCH,    // a series asked for without a search: it would be all GIFs
};
struct GifAnswer {
  GifAsked how;
  uint16_t matches, fit;  // GIFs the request finds, and of them those that fit the panel as it is held
  int16_t  index;         // the GIF chosen, -1 none
  bool     byName;        // the request was the name of the GIFs it found
};
GifAnswer gifRequest(const char *request, bool replace);   // "" asks for any GIF
// A series (Peter, 2026-10-24): every GIF a request finds that fits, one
// after another in a shuffled order, each once, cut hard from one to the
// next without the face between them; into the time's change, as every GIF
// the user asks for. An input or another GIF asked for ends it. matches and
// fit as in gifRequest() - fit is the series' length -, index its first GIF.
GifAnswer gifSeriesRequest(const char *request, bool replace);
GifAnswer gifAgain(bool replace);   // the last GIF shown once more (a random one when there is none)
const char *gifAskedWords(GifAsked how);   // what came of it, in words for the console
// Marks in hit (gifCount() bytes) the GIFs a request finds; an empty one finds
// every GIF. Returns how many; byName as in GifAnswer.
uint16_t       gifFind(const char *request, uint8_t *hit, bool &byName);
// The GIFs a request finds, sorted by title, into order (room for gifCount()
// of them); fit gets how many fit the panel as it is held. Returns how many,
// or -1 without RAM to search.
int32_t        gifFindByTitle(const char *request, int16_t *order, uint16_t &fit, bool &byName);
uint32_t       gifListGeneration();     // counts up whenever the GIFs to play change (a pack, the card)
const GifMeta &gifMetaOf(int16_t i);    // GIF i's name, meta fields and search text
const char    *gifTitleOf(int16_t i);   // its title, or its name without the meta index
bool           gifFitsNow(int16_t i);   // it fits the panel as it is held
void           gifSizeOf(int16_t i, uint16_t &w, uint16_t &h);   // 0 x 0 for a card GIF not read yet
uint16_t       gifMetaKnown();          // GIFs the meta index has a line for

// ---- The GIF pack ----------------------------------------------------------------
// The GIFs in the ffat partition. scripts/gif_pack.py writes them and
// describes the layout; all numbers little-endian.
struct GifPackHeader {
  char     magic[8];          // "CLKGPACK"
  uint16_t version;           // 1
  uint16_t count;             // entries in the index right after the header
  uint32_t indexCrc;          // CRC-32 of the index
  uint32_t size;              // header, index, GIFs and meta index, in bytes
  uint32_t metaOffset;        // the meta index after the GIFs, from the start; 0 = none
  uint32_t metaSize;          // its bytes (once 12 bytes zero: a pack from before it has none)
  uint32_t metaCrc;           // CRC-32 of them
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
void gifPackForget();                // no pack: its index and meta index go (before a new one is written)
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
bool gifCardSettle(unsigned long maxMs); // a card job under way ended and taken, waiting up to maxMs; false if it still runs
#endif

#endif  // GIF_PLAYER_H
