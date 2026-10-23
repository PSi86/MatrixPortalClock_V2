// GIFs on the panel: playing them, the GIF pack in the flash, and the wall
// clock's microSD card (gif_player.h).
#include "gif_player.h"

#include <AnimatedGIF.h>
#include <new>                 // new (std::nothrow) for the decoder
#include <esp_heap_caps.h>     // PSRAM for the pack's index and an inflated GIF
#include <esp_rom_crc.h>       // CRC-32 in ROM, to check the pack
#include <miniz.h>             // inflate in ROM (tinfl), for the pack's deflated GIFs
#include "brightness.h"
#include "clock_state.h"
#include "web_server.h"
#include "console_cmds.h"   // wallOutputStops
#include "notices.h"
#include "panel_rules.h"
#include "settings.h"

// What only this module calls, ahead of its use.
static bool gifFits(int16_t i);
static int16_t gifPick();
static void gifDrawLine(GIFDRAW *d);
static bool gifStart(int16_t i, bool ownAccord);
static int16_t gifNext();
static void gifSourceClose(uint8_t how);
static void gifDispose();
static void gifRender();
static uint32_t gifLoopMs(const uint8_t *d, uint32_t n, uint16_t &frames);
static bool gifPackMappable(const GifPackEntry &e);
static bool gifPackOpen(const GifPackEntry &e, const uint8_t *&data, uint32_t &size);
static void gifPackClose();
static void gifPackMetaFree();
static void gifPackMetaLoad(const GifPackHeader &h, uint32_t dataFrom);
static uint16_t gifFindQuery(const GifQuery &q, uint8_t *hit, bool &byName);
#if BOARD_WAVESHARE_WALL
static GifAsked gifCardWant(int16_t i);
static void gifCardWantUpdate();
static void gifCardSend(uint8_t kind, int16_t index);
static void gifCardFreeSlot(uint8_t s);
static int16_t gifCardNext();
static bool gifCardOpen(int16_t i, const uint8_t *&data, uint32_t &size);
static void gifCardClose(uint8_t how);
static void gifCardRemount();
static void gifCardResult();
#endif

// GIF playback state (the section "GIF playback" below).
#if BOARD_WAVESHARE_WALL
static const int GIF_CANVAS_MAX = WALL_W * WALL_H;   // GIFs play in the wall's own LEDs, up to 192x128
#else
static const int GIF_CANVAS_MAX = 64 * 32;      // pixels of the largest GIF that fits the 64x32 panel either way round
#endif
// The decoder and the canvas only while a GIF plays: 30 KB of RAM the rest of
// the time, the hotspot's time included, has better uses.
struct GifPlayer { AnimatedGIF decoder; uint8_t canvas[GIF_CANVAS_MAX * 3]; };
static GifPlayer *gifPlayer = nullptr;
bool gifPlaying() { return gifPlayer != nullptr; }   // a GIF has the panel (gif_player.h)
static uint8_t       gifGamma[256];
static int16_t       gifW = 0, gifH = 0;
static int16_t       gifIndex = -1;                 // the GIF on show
int16_t              gifLast = -1;                  // the one before
static uint8_t       gifRotation = 0;               // the rotation it started in
unsigned long        gifShownAt = 0;
static unsigned long gifNextFrameAt = 0, gifDueAt = 0;
static bool          gifRestartDue = false;         // its last frame is on the panel: start over with the next
static uint16_t      gifLoops = 0;                  // whole loops of it shown so far
static uint16_t      gifLoopsPlanned = 1;           // as many as fit into gifSettings.seconds, at least one
static uint32_t      gifLoopTime = 0;               // one loop by its frame delays, 0 if unknown
static uint16_t      gifFrames = 0, gifFrame = 0;   // frames of a loop (0 if unknown), of this loop shown so far
struct GifArea { int16_t x, y, w, h; uint8_t disposal; bool valid; };
static GifArea       gifPrev;                       // the last frame's area, for its disposal

// The GIF pack (section "GIF pack" below; its layout in gif_player.h).
static const uint32_t GIF_PACK_GIF_MAX = 614400;    // the largest GIF it may inflate to
GifPackState  gifPackState = GIF_PACK_NONE;
const esp_partition_t *gifPackPart = nullptr;
GifPackEntry *gifPack = nullptr;             // the index, in PSRAM when there is some
uint16_t      gifPackCount = 0;
// Its meta index: per GIF its name and fields and its search text (GIFs on
// request), in PSRAM; without one, or one that does not check out, every GIF
// has its name alone.
static const uint32_t GIF_PACK_META_MAX = 1UL << 20;   // the most a meta index may take
static GifMeta *gifPackMeta = nullptr;
static char    *gifPackMetaFile = nullptr, *gifPackMetaTexts = nullptr;   // what it points into
static uint16_t gifPackMetaKnown = 0;                  // GIFs it has a line for
// The pack GIF on show: its bytes mapped from flash, or read or inflated into PSRAM.
static esp_partition_mmap_handle_t gifPackMap;
static bool          gifPackMapped = false;
static uint8_t      *gifPackCopy = nullptr;
// The core's cache maps flash only below 16 MB (24-bit addresses; it was
// built without CONFIG_BOOTLOADER_CACHE_32BIT_ADDR_OCTAL_FLASH). The wall
// clock's pack reaches up to 32 MB: GIFs above the line are read instead of
// mapped, which the flash driver does at any address (2026-10-18: three GIFs
// read back from above 16 MB had their CRCs).
static const uint32_t GIF_PACK_MAP_LIMIT = 0x1000000;

// How the GIF on show ended (gifSourceClose()): played to its end, not now
// (it would run into the time's change, or no RAM), or broken.
static const uint8_t GIF_CLOSE_PLAYED = 0, GIF_CLOSE_NOT_NOW = 1, GIF_CLOSE_BROKEN = 2;

#if BOARD_WAVESHARE_WALL
// The wall clock's microSD card (src/gif_card.h; section "GIFs from the
// card" below). A card with GIFs is the GIFs' source while it is in
// (Peter, 2026-10-19), the pack in the flash otherwise. The source changes
// only while no GIF plays.
CardList      cardList;
bool          cardMounted = false;
bool          gifFromCard = false;
bool          cardJobBusy = false;           // a job is with the card's task
bool          cardRemountWanted = false;     // POST /api/card, the console, or reads that keep failing
static uint8_t  cardFetchFails = 0;          // in a row
static uint32_t cardFreqKhz = CARD_FREQ_KHZ;
uint32_t      cardMb = 0, cardFreeMb = 0;
static uint32_t cardRealKhz = 0;
static uint32_t cardSpeedStopsBefore = 0;
// Two GIFs of the card's in PSRAM: the next one, read ahead so a GIF starts
// at once ("now or not at all"), and the last one shown, for "GIF again".
static const uint8_t CARD_SLOT_EMPTY = 0, CARD_SLOT_FILLING = 1, CARD_SLOT_READY = 2;
struct CardSlot { uint8_t *data; uint32_t size; int16_t index; uint8_t state; };
static CardSlot cardSlot[2] = { { nullptr, 0, -1, CARD_SLOT_EMPTY }, { nullptr, 0, -1, CARD_SLOT_EMPTY } };
static int8_t   cardPlaying = -1, cardLastSlot = -1;
static CardResult cardResult;                // the task's last result, taken in gifCardUpdate()
// Console "sd stress": the card's largest GIFs read ahead in turn, instead of
// random ones, to watch the wall play them.
static int16_t  cardStress[16];
static uint8_t  cardStressCount = 0, cardStressAt = 0;
// A card GIF's one loop must leave room in a minute: the faces' changes take
// up to about 30 s of it (the Muncher's and the Aliens' from about :30).
static const uint32_t CARD_LOOP_MAX_MS = 25000;
// A card GIF asked for (gifRequest()) that is being read for it: it plays
// once it is read, if the face is free then; a later request takes its
// place, and after CARD_WANT_MS it goes.
static int16_t       cardWant = -1;
static unsigned long cardWantAt = 0;
static const unsigned long CARD_WANT_MS = 3000;
#endif

/* ======================================================================
   GIF playback

   Now and then a GIF takes the panel in place of the face, for
   gifSettings.seconds; holding UP on the face plays one at once. The GIFs
   come from the GIF pack (below); none is built into the firmware any more
   (flash layout of 2026-10-09). One plays only when it fits the panel as it
   is held: centred and unscaled. The frames are
   decoded into an RGB canvas here, so transparency and disposal work the same
   whatever the GIF does, and drawn at 8 bits per colour straight into the
   panel driver through a gamma table: GIF colours are made for screens, while
   the light of an LED is linear in its on-time. The panel's tables for the
   brightness (PanelCanvas::panelValue) dim a GIF as they dim the face.
   ====================================================================== */

// The GIF section of the config page (/save).
void applyGifParams(const String &q) {
  settingsApply(gifTable, [&q](const char *key) { return getParam(q, key); }, false);   // GIF_PARAMS
  gifSettingsCheck();
  matrix.setBrightness(panelBrightness());     // a GIF on show takes the new lowest at once
}

void gifGammaInit() {
  for (int v = 0; v < 256; v++) { gifGamma[v] = (uint8_t)lroundf(powf(v / 255.0f, 2.2f) * 255.0f); }
}

// The next GIF of its own accord: at a random time between the two settings,
// drawn anew when it falls into the slot around a change of the time.
void gifScheduleNext() {
  uint32_t lo = gifSettings.minMinutes * 60000UL, hi = gifSettings.maxMinutes * 60000UL;
  gifDueAt = panelRandomAt(lo, hi, (unsigned long)gifSettings.seconds * 1000UL);
}

// All GIFs: the pack's, none without a pack - on the wall clock the card's
// while it is the source.
int16_t gifCount() {
#if BOARD_WAVESHARE_WALL
  if (gifFromCard) { return cardList.count; }
#endif
  return gifPackCount;
}

bool gifFits(int16_t i) {
#if BOARD_WAVESHARE_WALL
  if (gifFromCard) {
    // A card GIF the index did not describe (put there by hand) counts until
    // it has been read; then its size and loop are known.
    const CardGif &g = cardList.gifs[i];
    return !g.bad && g.width <= WALL_W && g.height <= WALL_H && (g.loopMs == 0 || g.loopMs <= CARD_LOOP_MAX_MS);
  }
#endif
  uint16_t w = gifPack[i].width, h = gifPack[i].height;
#if BOARD_WAVESHARE_WALL
  return w <= WALL_W && h <= WALL_H;   // the wall's LEDs, not the clock's 64x32 picture
#else
  return w <= matrix.width() && h <= matrix.height() && (int32_t)w * h <= GIF_CANVAS_MAX;
#endif
}

// A random GIF that fits the panel as it is held, not the last one shown while
// another fits; -1 when none fits.
int16_t gifPick() {
  uint16_t choices = 0;
  for (int16_t i = 0; i < gifCount(); i++) { if (gifFits(i) && i != gifLast) { choices++; } }
  if (choices == 0) { return (gifLast >= 0 && gifFits(gifLast)) ? gifLast : -1; }
  uint16_t n = esp_random() % choices;
  for (int16_t i = 0; i < gifCount(); i++) {
    if (gifFits(i) && i != gifLast) { if (n == 0) { return i; } n--; }
  }
  return -1;
}

// One line of a frame into the canvas: transparent pixels keep what is there.
void gifDrawLine(GIFDRAW *d) {
  int16_t y = d->iY + d->y;
  if (y < 0 || y >= gifH) { return; }
  const uint8_t *pal = d->pPalette24;
  for (int16_t i = 0; i < d->iWidth; i++) {
    int16_t x = d->iX + i;
    if (x < 0 || x >= gifW) { continue; }
    uint8_t idx = d->pPixels[i];
    if (d->ucHasTransparency && idx == d->ucTransparent) { continue; }
    uint8_t *c = &gifPlayer->canvas[(y * gifW + x) * 3];
    c[0] = pal[idx * 3]; c[1] = pal[idx * 3 + 1]; c[2] = pal[idx * 3 + 2];
  }
  if (d->y == 0) {
    gifPrev = { (int16_t)d->iX, (int16_t)d->iY, (int16_t)d->iWidth, (int16_t)d->iHeight, d->ucDisposalMethod, true };
  }
}

// Shows GIF i over the face; false when it does not open.
bool gifStart(int16_t i, bool ownAccord) {
  if (i < 0 || i >= gifCount()) { return false; }
  const char *name;
  uint16_t w, h;
  const uint8_t *data;
  uint32_t size;
#if BOARD_WAVESHARE_WALL
  if (gifFromCard) {
    if (!gifCardOpen(i, data, size)) { return false; }   // read ahead, or not at all
    const CardGif &g = cardList.gifs[i];
    name = g.name;
    w = g.width;
    h = g.height;
  } else
#endif
  {
    const GifPackEntry &e = gifPack[i];
    name = e.name;
    w = e.width;
    h = e.height;
    if (!gifPackOpen(e, data, size)) { return false; }
  }
  gifPlayer = new (std::nothrow) GifPlayer;
  if (!gifPlayer) { Console.println("GIF: no RAM for the decoder"); gifSourceClose(GIF_CLOSE_NOT_NOW); return false; }
  gifPlayer->decoder.begin(GIF_PALETTE_RGB888);   // gifDrawLine() reads the palette as RGB888
  if (!gifPlayer->decoder.open((uint8_t *)data, size, gifDrawLine)) {
    Console.printf("GIF %s: does not open (%d)\n", name, gifPlayer->decoder.getLastError());
    delete gifPlayer;
    gifPlayer = nullptr;
    gifSourceClose(GIF_CLOSE_BROKEN);
    return false;
  }
  gifLoopTime = gifLoopMs(data, size, gifFrames);
  uint32_t playMs = gifSettings.seconds * 1000UL;
  gifLoopsPlanned = (gifLoopTime > 0 && gifLoopTime <= playMs) ? (uint16_t)(playMs / gifLoopTime) : 1;
  // Of its own accord, its whole loops end before the time's next change and
  // ROUND_GIF_GAP_MS before an info round, or it does not start. Checked
  // once, against what it plays (K6 of the face refactor: until 2026-10-20 a
  // GIF of its own accord was checked against the seconds set first, which
  // let one through whose loops ran longer, and held one back whose loops
  // were shorter). One the user asked for plays whole even into the change -
  // the user may miss it, or see the rest of it after (Peter, 2026-10-22;
  // until then it had to end before the change too).
  uint32_t lenMs = gifLoopTime > 0 ? min<uint32_t>((uint32_t)gifLoopsPlanned * gifLoopTime, GIF_PLAY_MAX_MS) : playMs;
  if (ownAccord && (!panelFitsBeforeChange(lenMs) || roundNextIn() < lenMs + ROUND_GIF_GAP_MS)) {
    Console.printf("GIF %s: its %lu ms have no room before the time's change, dropped\n", name, (unsigned long)lenMs);
    gifPlayer->decoder.close();
    delete gifPlayer;
    gifPlayer = nullptr;
    gifSourceClose(GIF_CLOSE_NOT_NOW);
    return false;
  }
  gifIndex = i;
  gifW = w;
  gifH = h;
  matrix.setBrightness(panelBrightness());     // a GIF may run brighter than the clock
  memset(gifPlayer->canvas, 0, sizeof(gifPlayer->canvas));
  gifPrev.valid = false;
  gifRestartDue = false;
  gifLoops = 0;
  gifFrame = 0;
  gifRotation = matrix.getRotation();
  gifShownAt = millisNow;
  gifNextFrameAt = millisNow;
  Console.printf("GIF: %s (%ux%u), %u x %lu ms, brightness %u (clock %u)\n", name, w, h,
                gifLoopsPlanned, (unsigned long)gifLoopTime, panelBrightness(), effectiveBrightness);
  openScreen(SCREEN_GIF);
  return true;
}

// The GIF to play next: a random one of the pack's; of the card's the one
// read ahead (gifCardUpdate()), -1 while none is ready.
int16_t gifNext() {
#if BOARD_WAVESHARE_WALL
  if (gifFromCard) { return gifCardNext(); }
#endif
  return gifPick();
}

// A GIF now (FN_PLAY_GIF): on the face only, now or not at all (Peter,
// 2026-10-12) - also during the face's change or a scene of it, and into the
// next change (Peter, 2026-10-22: what the user asks for is granted; the face
// catches up behind it). Until then it was dropped there. Any GIF, as a
// request for none in particular (gifRequest()).
void gifPlayNow() {
  gifRequest("", false);
}

// The last GIF shown once more (FN_GIF_AGAIN): on the face only, as
// gifPlayNow().
void gifPlayAgain() {
  gifAgain(false);
}

// The last GIF shown once more - with replace the one on show, from its
// start - or a random one when none has played yet or the last no longer
// fits the panel as it is held. Over the face, now or not at all, as
// gifRequest().
GifAnswer gifAgain(bool replace) {
  GifAnswer a = { GIF_ASKED_NO_MATCH, 0, 0, -1, false };
  bool onShow = screen == SCREEN_GIF;
  if (screen != SCREEN_FACE && !onShow) { a.how = GIF_ASKED_NOT_ON_FACE; return a; }
  if (onShow && !replace) { a.how = GIF_ASKED_GIF_ON_SHOW; return a; }
  if (gifCount() == 0) { return a; }
  if (onShow) { closeScreen(); }   // the GIF on show becomes the last one
  if (screen != SCREEN_FACE) { a.how = GIF_ASKED_NOT_ON_FACE; return a; }
  bool last = gifLast >= 0 && gifFits(gifLast);
  a.index = last ? gifLast : gifNext();
  a.matches = a.fit = 1;
  a.byName = last;
  if (gifStart(a.index, false)) {
    a.how = GIF_ASKED_PLAYS;
  } else {
    a.how = GIF_ASKED_NOT_OPENED;
    gifScheduleNext();
  }
  return a;
}

/* ----------------------------------------------------------------------
   GIFs on request (plan of 2026-10-23): a GIF by its name, or one of those
   a keyword finds in the meta index (gif_find.h has the rule), from the web
   app, Home Assistant or the console. The same search gives the web app its
   list, so what the list shows is what a request draws from.
   ---------------------------------------------------------------------- */

const char *gifAskedWords(GifAsked how) {
  static const char *const WORDS[] = { "plays", "being read from the card", "not over the face", "a GIF on show",
                                       "no GIF found", "none fits the panel as it is held", "could not be opened now" };
  return how < sizeof(WORDS) / sizeof(WORDS[0]) ? WORDS[how] : "?";
}

const GifMeta &gifMetaOf(int16_t i) {
  static GifMeta bare;   // a GIF of a list without meta (no RAM for it): its name, no search text
  const GifMeta *m = nullptr;
  const char *name = "";
#if BOARD_WAVESHARE_WALL
  if (gifFromCard) {
    m = cardList.meta ? &cardList.meta[i] : nullptr;
    name = cardList.gifs[i].name;
  } else
#endif
  {
    m = gifPackMeta ? &gifPackMeta[i] : nullptr;
    name = gifPack[i].name;
  }
  if (m) { return *m; }
  for (uint8_t k = 0; k < GIF_META_FIELDS; k++) { bare.field[k] = ""; }
  bare.field[GIF_META_NAME] = name;
  bare.text = "";
  bare.starts = 0;
  bare.cut = false;
  return bare;
}

const char *gifTitleOf(int16_t i) {
  const GifMeta &m = gifMetaOf(i);
  return m.field[GIF_META_TITLE][0] ? m.field[GIF_META_TITLE] : m.field[GIF_META_NAME];
}

bool gifFitsNow(int16_t i) { return gifFits(i); }

void gifSizeOf(int16_t i, uint16_t &w, uint16_t &h) {
#if BOARD_WAVESHARE_WALL
  if (gifFromCard) { w = cardList.gifs[i].width; h = cardList.gifs[i].height; return; }
#endif
  w = gifPack[i].width;
  h = gifPack[i].height;
}

uint16_t gifMetaKnown() {
#if BOARD_WAVESHARE_WALL
  if (gifFromCard) { return cardList.metaKnown; }
#endif
  return gifPackMetaKnown;
}

// The GIFs a request taken apart finds (hit as in gifFind()); one for none in
// particular finds them all.
uint16_t gifFindQuery(const GifQuery &q, uint8_t *hit, bool &byName) {
  uint16_t count = (uint16_t)gifCount();
  byName = false;
  if (!q.name[0] && !q.keys) {
    memset(hit, 1, count);
    return count;
  }
  return gifFindAll(q, count, [](uint16_t i) {
    const GifMeta &m = gifMetaOf((int16_t)i);
    return GifFindItem{ m.text, m.cut, m.starts };
  }, hit, byName);
}

uint16_t gifFind(const char *request, uint8_t *hit, bool &byName) {
  GifQuery q;
  gifQueryParse(request, q);
  return gifFindQuery(q, hit, byName);
}

int32_t gifFindByTitle(const char *request, int16_t *order, uint16_t &fit, bool &byName) {
  uint16_t count = (uint16_t)gifCount(), n = 0;
  fit = 0;
  byName = false;
  if (!count) { return 0; }
  uint8_t *hit = (uint8_t *)heap_caps_malloc(count, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!hit) { return -1; }
  gifFind(request, hit, byName);
  for (uint16_t i = 0; i < count; i++) {
    if (!hit[i]) { continue; }
    order[n++] = (int16_t)i;
    fit += gifFits((int16_t)i);
  }
  heap_caps_free(hit);
  qsort(order, n, sizeof(int16_t), [](const void *a, const void *b) {
    int16_t x = *(const int16_t *)a, y = *(const int16_t *)b;
    int r = strcasecmp(gifTitleOf(x), gifTitleOf(y));
    return r ? r : strcmp(gifMetaOf(x).field[GIF_META_NAME], gifMetaOf(y).field[GIF_META_NAME]);
  });
  return n;
}

// Counts up whenever the GIFs to play change: Home Assistant's matches are
// sent anew then.
static uint32_t gifGeneration = 0;
uint32_t gifListGeneration() { return gifGeneration; }

// A request: which GIFs it finds and which of them fit, one of those drawn at
// random (not the last one shown while another fits), and that one started -
// or, for none in particular, the one gifNext() has (on the wall the card's
// GIF read ahead, so it starts at once).
GifAnswer gifRequest(const char *request, bool replace) {
  GifAnswer a = { GIF_ASKED_NO_MATCH, 0, 0, -1, false };
  bool onShow = screen == SCREEN_GIF;
  if (screen != SCREEN_FACE && !onShow) { a.how = GIF_ASKED_NOT_ON_FACE; return a; }
  if (onShow && !replace) {
    Console.printf("GIF request \"%s\": a GIF is on show, and no replace - dropped\n", request);
    a.how = GIF_ASKED_GIF_ON_SHOW;
    return a;
  }
  GifQuery q;
  gifQueryParse(request, q);
  bool any = !q.name[0] && !q.keys;
  uint16_t count = (uint16_t)gifCount();
  uint8_t *hit = nullptr;
  if (count) {
    hit = (uint8_t *)heap_caps_malloc(count, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!hit) { hit = (uint8_t *)malloc(count); }
    if (!hit) { Console.println("GIF request: no RAM to search"); a.how = GIF_ASKED_NOT_OPENED; return a; }
  }
  uint32_t t0 = micros();
  a.matches = count ? gifFindQuery(q, hit, a.byName) : 0;
  // The one shown last: with replace the one on show, which becomes the last
  // only when it closes (2026-10-23 on the S3: before, 20 requests in a row
  // brought the same GIF twice in a row twice).
  int16_t last = onShow ? gifIndex : gifLast;
  uint16_t others = 0;   // fitting finds but the last one shown
  for (uint16_t i = 0; i < count; i++) {
    if (hit[i] && gifFits((int16_t)i)) { a.fit++; others += (int16_t)i != last; }
  }
  int16_t pick = -1;
  if (a.fit && !any) {
    pick = last;   // the only one that fits, when no other does
    if (others) {
      uint16_t n = esp_random() % others;
      for (uint16_t i = 0; i < count; i++) {
        if (hit[i] && gifFits((int16_t)i) && (int16_t)i != last) {
          if (n == 0) { pick = (int16_t)i; break; }
          n--;
        }
      }
    }
  }
  free(hit);
  if (!any) {
    Console.printf("GIF request \"%s\"%s: %u found%s, %u fit, %lu us\n", request, replace ? " (replace)" : "",
                  a.matches, a.byName ? " by name" : "", a.fit, (unsigned long)(micros() - t0));
  }
  if (!a.matches) { a.how = GIF_ASKED_NO_MATCH; return a; }
  if (!a.fit) { a.how = GIF_ASKED_NONE_FITS; return a; }
  if (onShow) { closeScreen(); }                      // replace: the GIF on show goes
  if (screen != SCREEN_FACE) { a.how = GIF_ASKED_NOT_ON_FACE; return a; }
  if (any) { pick = gifNext(); }
  a.index = pick;
#if BOARD_WAVESHARE_WALL
  if (gifFromCard && !any) {
    a.how = gifCardWant(pick);
    if (a.how == GIF_ASKED_NOT_OPENED) { gifScheduleNext(); }
    return a;
  }
#endif
  if (gifStart(pick, false)) {
    a.how = GIF_ASKED_PLAYS;
  } else {
    a.how = GIF_ASKED_NOT_OPENED;
    gifScheduleNext();
  }
  return a;
}

// A GIF of its own accord once it is due: now, or not at all when the panel
// is not free for it (decorFree()) - it does not wait (Peter, 2026-10-12);
// GIFs that waited all came at once when the wait ended, right after the
// time's change. What does not depend on its length is looked at before it
// is opened; its whole loops when they are known (gifStart()).
void gifUpdate() {
  if (screen == SCREEN_GIF) { return; }   // one on show; the next is drawn at its end
  if (!gifSettings.randomOn || gifCount() == 0) { return; }
  if ((long)(millisNow - gifDueAt) < 0) { return; }
  if (screen != SCREEN_FACE || !decorFree(0, true)) {
    Console.println("GIF: due, the panel not free for it - dropped");
    gifScheduleNext();
    return;
  }
  if (!gifStart(gifNext(), true)) { gifScheduleNext(); }
}

// Lets go of the bytes of the GIF that was on show, or was about to be.
void gifSourceClose(uint8_t how) {
#if BOARD_WAVESHARE_WALL
  if (gifFromCard) { gifCardClose(how); return; }
#endif
  (void)how;
  gifPackClose();
}

// The GIF screen goes: the decoder closes, the face draws itself afresh, and
// the next GIF is scheduled.
void gifEnd() {
  if (gifPlayer) {
    gifPlayer->decoder.close();
    delete gifPlayer;
    gifPlayer = nullptr;
  }
  gifSourceClose(GIF_CLOSE_PLAYED);
  matrix.setBrightness(panelBrightness());     // back to the clock's
  gifLast = gifIndex;
  gifIndex = -1;
  matrix.forgetHeld();
  gifScheduleNext();
}

// The area of the frame before asked to go back to the background.
void gifDispose() {
  if (!gifPrev.valid || gifPrev.disposal != 2) { return; }
  for (int16_t y = max<int16_t>(gifPrev.y, 0); y < gifPrev.y + gifPrev.h && y < gifH; y++) {
    for (int16_t x = max<int16_t>(gifPrev.x, 0); x < gifPrev.x + gifPrev.w && x < gifW; x++) {
      memset(&gifPlayer->canvas[(y * gifW + x) * 3], 0, 3);
    }
  }
}

#if BOARD_WAVESHARE_WALL
// The canvas onto the wall in its own LEDs, centred, the rest dark, through
// the gamma table and the panel's table for the brightness; the wall writes
// only what changed since the frame before (PanelCanvas::wallRow()).
void gifRender() {
  int16_t x0 = (WALL_W - gifW) / 2, y0 = (WALL_H - gifH) / 2;
  static uint8_t row[WALL_W * 3];
  static const uint8_t dark[WALL_W * 3] = {};
  memset(row, 0, sizeof(row));   // left and right of the GIF stays dark
  matrix.waitForBackBuffer();
#if defined(CLOCK_DEBUG)
  // Debug build: how long a frame takes onto the wall, every 2 s while GIFs play.
  static uint32_t frames = 0, sumUs = 0, maxUs = 0, since = 0;
  uint32_t t0 = micros();
#endif
  for (int16_t y = 0; y < WALL_H; y++) {
    int16_t gy = y - y0;
    if (gy < 0 || gy >= gifH) { matrix.wallRow(y, dark); continue; }
    const uint8_t *c = &gifPlayer->canvas[gy * gifW * 3];
    uint8_t *o = row + x0 * 3;
    for (int16_t gx = 0; gx < gifW; gx++, c += 3, o += 3) {
      o[0] = matrix.panelValue(0, gifGamma[c[0]]);
      o[1] = matrix.panelValue(1, gifGamma[c[1]]);
      o[2] = matrix.panelValue(2, gifGamma[c[2]]);
    }
    matrix.wallRow(y, row);
  }
  matrix.endWallFrame();
#if defined(CLOCK_DEBUG)
  uint32_t us = micros() - t0;
  frames++; sumUs += us; if (us > maxUs) { maxUs = us; }
  if (millis() - since >= 2000) {
    Console.printf("GIF onto the wall: %lu frames, avg %lu max %lu us\n", (unsigned long)frames,
                  (unsigned long)(sumUs / frames), (unsigned long)maxUs);
    frames = sumUs = maxUs = 0;
    since = millis();
  }
#endif
}
#else
// The canvas onto the panel, centred, the rest dark, through the gamma table
// and the panel's table for the brightness.
void gifRender() {
  int16_t lw = matrix.width(), lh = matrix.height();
  int16_t x0 = (lw - gifW) / 2, y0 = (lh - gifH) / 2;
  MatrixPanel_I2S_DMA *panel = matrix.driver();
  matrix.waitForBackBuffer();
  for (int16_t y = 0; y < lh; y++) {
    for (int16_t x = 0; x < lw; x++) {
      uint8_t r = 0, g = 0, b = 0;
      int16_t gx = x - x0, gy = y - y0;
      if (gx >= 0 && gy >= 0 && gx < gifW && gy < gifH) {
        const uint8_t *c = &gifPlayer->canvas[(gy * gifW + gx) * 3];
        r = matrix.panelValue(0, gifGamma[c[0]]);
        g = matrix.panelValue(1, gifGamma[c[1]]);
        b = matrix.panelValue(2, gifGamma[c[2]]);
      }
      int16_t px = x, py = y;
      matrix.panelXY(px, py);
      panel->drawPixelRGB888(px, py, r, g, b);
    }
  }
  matrix.flip();
  matrix.forgetHeld();
}
#endif

// The GIF screen: the next frame when it is due. Turning the panel ends it,
// since the GIF no longer stands upright, or may no longer fit.
void drawGif() {
  if (!gifPlayer || matrix.getRotation() != gifRotation) { closeScreen(); return; }
  if ((long)(millisNow - gifNextFrameAt) < 0) { return; }
  if (gifRestartDue) {
    // A whole loop is over; the next only while the loops worked out at the
    // start (gifStart()) are not done, so a GIF is never cut off in the middle.
    gifLoops++;
    if (gifLoops >= gifLoopsPlanned) {
      Console.printf("GIF: %u whole loops of %lu ms in %lu ms\n", gifLoops, (unsigned long)gifLoopTime,
                    millisNow - gifShownAt);
      closeScreen();
      return;
    }
    gifPlayer->decoder.reset();
    memset(gifPlayer->canvas, 0, sizeof(gifPlayer->canvas));
    gifPrev.valid = false;
    gifRestartDue = false;
    gifFrame = 0;
  } else {
    gifDispose();
  }
  int delayMs = 0;
  int more = gifPlayer->decoder.playFrame(false, &delayMs);   // 0: this was the last frame, it still gets shown
  if (more < 0) { Console.printf("GIF decode error %d\n", gifPlayer->decoder.getLastError()); }
  // The last frame is the last one gifLoopMs() found: with more blocks after
  // it (a comment, other data) AnimatedGIF reports more frames and then hands
  // out an empty one, which would stand for 100 ms with the last frame's
  // disposal already done.
  if (more <= 0 || ++gifFrame == gifFrames) { gifRestartDue = true; }
  if (delayMs < 20) { delayMs = 100; }         // what browsers make of 0 and 10 ms
  // On the file's own timeline: the next frame is due its delay after this one
  // was due, not after it was drawn, so frames drawn a few ms late do not add
  // up over the loops. Far behind - a long pause of the loop - it goes on from
  // now instead of rushing through the frames it missed.
  gifNextFrameAt += delayMs;
  if ((long)(millisNow - gifNextFrameAt) > 250) { gifNextFrameAt = millisNow + delayMs; }
  gifRender();
}

// One loop of a GIF in memory, as drawGif() times it: each frame's delay from
// the Graphic Control Extension before it (in 10 ms), and 100 ms for a frame
// with less than 20 ms or without one, as AnimatedGIF and drawGif() make it;
// frames gets the number of frames. Both 0 when the file cannot be walked.
uint32_t gifLoopMs(const uint8_t *d, uint32_t n, uint16_t &frames) {
  frames = 0;
  if (n < 13 || memcmp(d, "GIF", 3) != 0) { return 0; }
  uint32_t i = 13, total = 0, delay = 0;
  if (d[10] & 0x80) { i += 3u << ((d[10] & 7) + 1); }   // the global colour table
  while (i < n) {
    if (d[i] == 0x21) {                                  // an extension
      if (i + 1 >= n) { frames = 0; return 0; }
      if (d[i + 1] == 0xF9 && i + 5 < n && d[i + 2] == 4) { delay = (uint32_t)(d[i + 4] | (d[i + 5] << 8)) * 10; }
      i += 2;
      while (i < n && d[i]) { i += d[i] + 1; }           // its sub-blocks
      i++;
    } else if (d[i] == 0x2C) {                           // an image: one frame
      if (i + 10 > n) { frames = 0; return 0; }
      uint8_t flags = d[i + 9];
      i += 10;
      if (flags & 0x80) { i += 3u << ((flags & 7) + 1); }   // a local colour table
      i++;                                               // the LZW code size
      while (i < n && d[i]) { i += d[i] + 1; }           // the image data
      i++;
      total += delay < 20 ? 100 : delay;
      frames++;
      delay = 0;                                         // the extension was for this frame only
    } else if (d[i] == 0x3B || n - i < 32) {             // the trailer, or stray bytes at the end as
      break;                                             // AnimatedGIF takes them
    } else {
      frames = 0;
      return 0;
    }
  }
  return total;
}

/* ----------------------------------------------------------------------
   GIF pack

   All the GIFs the clock has, in the ffat partition (4544 KB in the flash
   layout of 2026-10-09), which the clock uses for nothing else:
   scripts/gif_pack.py builds the pack, "pio run -t sendgifs" sends it over
   the home network and "pio run -t uploadgifs" writes it through the ROM
   bootloader. A firmware upload leaves it alone. The index is read and checked at start-up; a
   GIF's bytes are checked each time it is about to play, straight from flash,
   and a deflated one is inflated into PSRAM by the inflate in the chip's ROM.
   ---------------------------------------------------------------------- */

void gifPackLoad() {
  gifPackMetaFree();   // an old pack's, should there be one
  gifGeneration++;
  gifPackPart = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_FAT, "ffat");
  GifPackHeader h;
  if (!gifPackPart || esp_partition_read(gifPackPart, 0, &h, sizeof(h)) != ESP_OK
      || memcmp(h.magic, "CLKGPACK", sizeof(h.magic)) != 0) {
    Console.println("GIF pack: none");
    return;
  }
  gifPackState = GIF_PACK_BAD;
  uint32_t indexBytes = (uint32_t)h.count * sizeof(GifPackEntry);
  uint32_t dataFrom = sizeof(h) + indexBytes;
  if (h.version != 1 || h.count == 0 || h.count > GIF_PACK_COUNT_MAX || h.size > gifPackPart->size || dataFrom > h.size) {
    Console.println("GIF pack: damaged header");
    return;
  }
  GifPackEntry *index = (GifPackEntry *)heap_caps_malloc(indexBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!index) { index = (GifPackEntry *)malloc(indexBytes); }
  if (!index) {
    Console.println("GIF pack: no RAM for the index");
    return;
  }
  if (esp_partition_read(gifPackPart, sizeof(h), index, indexBytes) != ESP_OK
      || esp_rom_crc32_le(0, (const uint8_t *)index, indexBytes) != h.indexCrc) {
    Console.println("GIF pack: damaged index");
    free(index);
    return;
  }
  for (uint16_t n = 0; n < h.count; n++) {
    GifPackEntry &e = index[n];
    e.name[sizeof(e.name) - 1] = '\0';
    bool codecOK = e.codec == GIF_PACK_DEFLATED || (e.codec == GIF_PACK_STORED && e.gifSize == e.size);
    if (!codecOK || e.size == 0 || e.offset < dataFrom || e.offset > h.size || e.size > h.size - e.offset
        || e.gifSize > GIF_PACK_GIF_MAX) {
      Console.printf("GIF pack: damaged entry %u\n", n);
      free(index);
      return;
    }
  }
  gifPack = index;
  gifPackCount = h.count;
  gifPackState = GIF_PACK_OK;
  gifPackMetaLoad(h, dataFrom);
  Console.printf("GIF pack: %u GIFs, %u KB, %u in the meta index; %u KB PSRAM free\n", gifPackCount,
                (unsigned)(h.size / 1024), gifPackMetaKnown, (unsigned)(heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024));
}

void gifPackForget() {
  gifGeneration++;
  gifPackState = GIF_PACK_NONE;
  free(gifPack);
  gifPack = nullptr;
  gifPackCount = 0;
  gifPackMetaFree();
}

void gifPackMetaFree() {
  heap_caps_free(gifPackMeta);
  heap_caps_free(gifPackMetaFile);
  heap_caps_free(gifPackMetaTexts);
  gifPackMeta = nullptr;
  gifPackMetaFile = gifPackMetaTexts = nullptr;
  gifPackMetaKnown = 0;
}

// The pack's meta index (h.metaOffset, scripts/gif_pack.py): read, checked
// and taken apart, a line per GIF in the order of the index, and every GIF's
// search text. One out of place, damaged or with another count of lines is
// left out: the GIFs keep their names.
void gifPackMetaLoad(const GifPackHeader &h, uint32_t dataFrom) {
  gifPackMeta = (GifMeta *)heap_caps_calloc(gifPackCount, sizeof(GifMeta), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!gifPackMeta) { Console.println("GIF pack: no RAM for the meta index - GIFs on request find nothing"); return; }
  auto byName = []() {
    for (uint16_t i = 0; i < gifPackCount; i++) {
      GifMeta &m = gifPackMeta[i];
      for (uint8_t k = 0; k < GIF_META_FIELDS; k++) { m.field[k] = ""; }
      m.field[GIF_META_NAME] = gifPack[i].name;
      m.cut = strlen(gifPack[i].name) == sizeof(gifPack[i].name) - 1;   // as gif_pack.py cuts it
    }
  };
  byName();
  if (h.metaSize) {
    const char *why = nullptr;
    if (h.metaOffset < dataFrom || h.metaOffset > h.size || h.metaSize > h.size - h.metaOffset || h.metaSize > GIF_PACK_META_MAX) {
      why = "lies out of place";
    } else if (!(gifPackMetaFile = (char *)heap_caps_malloc(h.metaSize + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT))) {
      why = "has no RAM";
    } else if (esp_partition_read(gifPackPart, h.metaOffset, gifPackMetaFile, h.metaSize) != ESP_OK) {
      why = "cannot be read";
    } else if (esp_rom_crc32_le(0, (const uint8_t *)gifPackMetaFile, h.metaSize) != h.metaCrc) {
      why = "is damaged";
    } else {
      gifPackMetaFile[h.metaSize] = '\0';
      uint16_t n = 0;
      long lines = gifMetaLines(gifPackMetaFile, h.metaSize, [&n](const char *const *f) {
        if (n < gifPackCount) {
          memcpy(gifPackMeta[n].field, f, sizeof(gifPackMeta[n].field));
          gifPackMeta[n].cut = false;
        }
        n++;
      });
      if (lines == gifPackCount) {
        gifPackMetaKnown = gifPackCount;
      } else {
        why = lines < 0 ? "is cut off or of another kind" : "has another count of lines";
        byName();
      }
    }
    if (why) {
      Console.printf("GIF pack: its meta index %s - GIFs on request find them by name\n", why);
      heap_caps_free(gifPackMetaFile);
      gifPackMetaFile = nullptr;
    }
  }
  gifPackMetaTexts = gifMetaTexts(gifPackMeta, gifPackCount,
                                  [](size_t n) { return (char *)heap_caps_malloc(n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT); });
  if (!gifPackMetaTexts) {
    Console.println("GIF pack: no RAM for the search texts - GIFs on request find nothing");
    gifPackMetaFree();
  }
}

// Whether pack GIF e lies where the cache can map it (GIF_PACK_MAP_LIMIT).
bool gifPackMappable(const GifPackEntry &e) {
  return (uint64_t)gifPackPart->address + e.offset + e.size <= GIF_PACK_MAP_LIMIT;
}

// Whether the bytes of pack GIF e still have their CRC: mapped, or read a
// piece at a time where the cache does not reach.
bool gifPackCrcOk(const GifPackEntry &e) {
  if (gifPackMappable(e)) {
    const void *mapped = nullptr;
    esp_partition_mmap_handle_t map;
    if (esp_partition_mmap(gifPackPart, e.offset, e.size, ESP_PARTITION_MMAP_DATA, &mapped, &map) != ESP_OK) {
      return false;
    }
    bool ok = esp_rom_crc32_le(0, (const uint8_t *)mapped, e.size) == e.crc;
    esp_partition_munmap(map);
    return ok;
  }
  static uint8_t piece[4096];
  uint32_t crc = 0;
  for (uint32_t at = 0; at < e.size; at += sizeof(piece)) {
    uint32_t n = min<uint32_t>(sizeof(piece), e.size - at);
    if (esp_partition_read(gifPackPart, e.offset + at, piece, n) != ESP_OK) { return false; }
    crc = esp_rom_crc32_le(crc, piece, n);
  }
  return crc == e.crc;
}

// The bytes of pack GIF e for the decoder; false when they cannot be had.
bool gifPackOpen(const GifPackEntry &e, const uint8_t *&data, uint32_t &size) {
  const uint8_t *stored = nullptr;
  uint8_t *read = nullptr;   // the stored bytes, read into PSRAM above GIF_PACK_MAP_LIMIT
  if (gifPackMappable(e)) {
    const void *mapped = nullptr;
    if (esp_partition_mmap(gifPackPart, e.offset, e.size, ESP_PARTITION_MMAP_DATA, &mapped, &gifPackMap) != ESP_OK) {
      Console.printf("GIF %s: cannot map it\n", e.name);
      return false;
    }
    gifPackMapped = true;
    stored = (const uint8_t *)mapped;
  } else {
    read = (uint8_t *)heap_caps_malloc(e.size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!read || esp_partition_read(gifPackPart, e.offset, read, e.size) != ESP_OK) {
      Console.printf("GIF %s: cannot read it\n", e.name);
      free(read);
      return false;
    }
    stored = read;
  }
  if (esp_rom_crc32_le(0, stored, e.size) != e.crc) {
    Console.printf("GIF %s: damaged in the pack\n", e.name);
    free(read);
    gifPackClose();
    return false;
  }
  if (e.codec == GIF_PACK_STORED) {
    if (read) { gifPackCopy = read; }   // freed by gifPackClose()
    data = stored;
    size = e.size;
    return true;
  }
  gifPackCopy = (uint8_t *)heap_caps_malloc(e.gifSize, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  tinfl_decompressor *inflator = (tinfl_decompressor *)malloc(sizeof(tinfl_decompressor));
  bool ok = gifPackCopy && inflator;
  if (ok) {
    unsigned long started = millis();
    size_t in = e.size, out = e.gifSize;
    tinfl_init(inflator);
    tinfl_status status = tinfl_decompress(inflator, stored, &in, gifPackCopy, gifPackCopy, &out,
                                           TINFL_FLAG_PARSE_ZLIB_HEADER | TINFL_FLAG_USING_NON_WRAPPING_OUTPUT_BUF);
    ok = status == TINFL_STATUS_DONE && out == e.gifSize;
    if (ok) {
      Console.printf("GIF %s: inflated %u -> %u bytes in %lu ms\n", e.name, (unsigned)e.size, (unsigned)e.gifSize,
                    millis() - started);
    } else {
      Console.printf("GIF %s: does not inflate (%d)\n", e.name, (int)status);
    }
  } else {
    Console.printf("GIF %s: no PSRAM to inflate it\n", e.name);
  }
  free(inflator);
  if (gifPackMapped) {   // the stored bytes are done with
    esp_partition_munmap(gifPackMap);
    gifPackMapped = false;
  }
  free(read);
  if (!ok) {
    gifPackClose();
    return false;
  }
  data = gifPackCopy;
  size = e.gifSize;
  return true;
}

// Lets go of the pack GIF's bytes once it no longer plays.
void gifPackClose() {
  if (gifPackMapped) {
    esp_partition_munmap(gifPackMap);
    gifPackMapped = false;
  }
  free(gifPackCopy);
  gifPackCopy = nullptr;
}

#if BOARD_WAVESHARE_WALL
/* ----------------------------------------------------------------------
   GIFs from the card (the wall clock; src/gif_card.h does the card's work in
   a task of its own). loop() only sends it jobs and takes the results, so
   the face never waits for the card. The next GIF is read into PSRAM ahead
   of time, while no GIF plays; a GIF due, or asked for, starts from there at
   once, or not at all when it is not ready yet. The last one shown stays in
   PSRAM for "GIF again". A card GIF that cannot be read, has the wrong SHA-1
   or does not open is left out until the card is read again.
   ---------------------------------------------------------------------- */

// At start-up: the card's task, and the card read for the first time.
void gifCardBegin() {
  if (!cardBegin()) { Console.println("Card: no RAM for its task"); return; }
  cardRemountWanted = true;
}

// (Plain types in these signatures: from the .ino, whose generated
// prototypes stood on every board, where the card's types do not exist.)
void gifCardSend(uint8_t kind, int16_t index) {
  CardJob job = { (CardJobKind)kind, index, cardFreqKhz, index >= 0 ? &cardList.gifs[index] : nullptr };
  if (xQueueSend(cardJobs(), &job, 0) == pdTRUE) { cardJobBusy = true; }
}

void gifCardFreeSlot(uint8_t s) {
  heap_caps_free(cardSlot[s].data);
  cardSlot[s] = { nullptr, 0, -1, CARD_SLOT_EMPTY };
  if (cardLastSlot == s) { cardLastSlot = -1; }
}

// The card GIF read ahead to come next, -1 while none is ready.
int16_t gifCardNext() {
  for (uint8_t s = 0; s < 2; s++) {
    if (cardSlot[s].state == CARD_SLOT_READY && s != cardLastSlot) { return cardSlot[s].index; }
  }
  Console.println("GIF: the card's next one is not read yet");
  return -1;
}

// Card GIF i asked for (gifRequest()): at once when it is read ahead or was
// the last one shown; else it is read for it, in place of the GIF read
// ahead, and plays once it is read (gifCardWantUpdate()).
GifAsked gifCardWant(int16_t i) {
  for (uint8_t s = 0; s < 2; s++) {
    if (cardSlot[s].state == CARD_SLOT_READY && cardSlot[s].index == i) {
      cardWant = -1;
      return gifStart(i, false) ? GIF_ASKED_PLAYS : GIF_ASKED_NOT_OPENED;
    }
  }
  // The GIF read ahead makes room, so no GIF of its own accord takes it meanwhile.
  for (uint8_t s = 0; s < 2; s++) {
    if (cardSlot[s].state == CARD_SLOT_READY && s != cardLastSlot) { gifCardFreeSlot(s); }
  }
  cardWant = i;
  cardWantAt = millisNow;
  Console.printf("Card: %s asked for - reading it\n", cardList.gifs[i].name);
  return GIF_ASKED_READING;
}

// On every pass: the GIF asked for, once it is read - over the face, else it
// goes - or it goes when it cannot be played or is not read in CARD_WANT_MS.
void gifCardWantUpdate() {
  if (cardWant < 0) { return; }
  if (!gifFromCard || cardWant >= (int16_t)cardList.count) { cardWant = -1; return; }
  const CardGif &g = cardList.gifs[cardWant];
  for (uint8_t s = 0; s < 2; s++) {
    if (cardSlot[s].state == CARD_SLOT_READY && cardSlot[s].index == cardWant) {
      int16_t i = cardWant;
      cardWant = -1;
      if (screen != SCREEN_FACE) {
        Console.printf("Card: %s read, the face is not free now - dropped\n", g.name);
      } else if (!gifStart(i, false)) {
        gifScheduleNext();
      }
      return;
    }
  }
  if (g.bad) {
    Console.printf("Card: %s asked for does not play - dropped\n", g.name);
    cardWant = -1;
  } else if ((long)(millisNow - cardWantAt) > (long)CARD_WANT_MS) {
    Console.printf("Card: %s not read in %lu ms - dropped\n", g.name, CARD_WANT_MS);
    cardWant = -1;
  }
}

// Card GIF i's bytes, from the slot holding it; false when none does.
bool gifCardOpen(int16_t i, const uint8_t *&data, uint32_t &size) {
  for (uint8_t s = 0; s < 2; s++) {
    if (cardSlot[s].state == CARD_SLOT_READY && cardSlot[s].index == i) {
      cardPlaying = (int8_t)s;
      data = cardSlot[s].data;
      size = cardSlot[s].size;
      return true;
    }
  }
  return false;
}

// The card GIF on show is done with: played, it becomes the last one (and
// the one before goes); not now, it stays ready; broken, it is left out.
void gifCardClose(uint8_t how) {
  if (cardPlaying < 0) { return; }
  uint8_t s = (uint8_t)cardPlaying;
  cardPlaying = -1;
  if (how == GIF_CLOSE_BROKEN) {
    cardList.gifs[cardSlot[s].index].bad = true;
    gifCardFreeSlot(s);
  } else if (how == GIF_CLOSE_PLAYED) {
    if (cardLastSlot >= 0 && cardLastSlot != s) { gifCardFreeSlot((uint8_t)cardLastSlot); }
    cardLastSlot = (int8_t)s;
  }
}

// The card is read anew (at start-up, POST /api/card, the console, or when
// reads keep failing): the pack is the source until it is done.
void gifCardRemount() {
  gifGeneration++;
  for (uint8_t s = 0; s < 2; s++) { gifCardFreeSlot(s); }
  if (gifFromCard) { gifLast = -1; }
  gifFromCard = false;
  cardMounted = false;
  cardListFree(cardList);
  cardRemountWanted = false;
  cardFetchFails = 0;
  gifCardSend(CARD_MOUNT_JOB, -1);
}

void gifCardResult() {
  CardResult &r = cardResult;
  cardJobBusy = false;
  if (r.kind == CARD_MOUNT_JOB) {
    gifGeneration++;
    cardMounted = cardHandle() != nullptr;
    cardList = r.list;
    cardMb = r.cardMb;
    cardFreeMb = r.freeMb;
    cardRealKhz = r.freqKhz;
    gifFromCard = r.ok;
    if (gifFromCard) { gifLast = -1; }   // it was the pack's
    Console.printf("Card: %s, %lu ms%s\n", r.text, (unsigned long)r.ms,
                  gifFromCard ? " - the GIFs come from the card" : cardMounted ? " - no GIFs on it, the pack plays" : "");
    if (cardMounted) {
      Console.printf("Card: %lu MB, %lu MB free, %lu kHz\n", (unsigned long)cardMb, (unsigned long)cardFreeMb,
                    (unsigned long)cardRealKhz);
    }
  } else if (r.kind == CARD_FETCH_JOB) {
    int8_t s = cardSlot[0].state == CARD_SLOT_FILLING ? 0 : cardSlot[1].state == CARD_SLOT_FILLING ? 1 : -1;
    if (s < 0 || r.index < 0 || r.index >= cardList.count) { heap_caps_free(r.data); return; }
    CardGif &g = cardList.gifs[r.index];
    if (!r.ok) {
      Console.printf("Card: %s %s - left out\n", g.name, r.text);
      g.bad = true;
      gifCardFreeSlot((uint8_t)s);
      if (++cardFetchFails >= 3) { cardRemountWanted = true; }   // the card may be gone
      return;
    }
    cardFetchFails = 0;
    cardSlot[s] = { r.data, r.size, r.index, CARD_SLOT_READY };
    g.width = r.width;
    g.height = r.height;
    Console.printf("Card: %s read ahead, %lu KB in %lu ms\n", g.name, (unsigned long)(r.size / 1024),
                  (unsigned long)r.ms);
    if (g.loopMs == 0) { g.loopMs = gifLoopMs(r.data, r.size, g.frames); }
    if (!gifFits(r.index)) {   // put there by hand: too large, or its loop too long
      Console.printf("Card: %s (%ux%u, %lu ms) does not fit the wall - left out\n", g.name, g.width, g.height,
                    (unsigned long)g.loopMs);
      g.bad = true;
      gifCardFreeSlot((uint8_t)s);
    }
  } else {
    double mb = r.bytes / 1e6;
    Console.printf("Card: read %.1f MB in %lu ms, %.2f MB/s; %lu read errors, %lu not as the index says; "
                  "the panel output stopped %lu times meanwhile\n", mb, (unsigned long)r.ms,
                  r.ms ? mb / (r.ms / 1000.0) : 0.0, (unsigned long)r.readErrors, (unsigned long)r.shaWrong,
                  (unsigned long)(wallOutputStops - cardSpeedStopsBefore));
  }
}

// Every pass: the task's result, and the next job - only while no GIF plays,
// so the card's reads do not come on top of a GIF's frames.
void gifCardUpdate() {
  if (!cardResults()) { return; }
  if (cardJobBusy && xQueueReceive(cardResults(), &cardResult, 0) == pdTRUE) { gifCardResult(); }
  gifCardWantUpdate();
  if (cardJobBusy || screen == SCREEN_GIF) { return; }
  if (cardRemountWanted) { cardWant = -1; gifCardRemount(); return; }
  if (!gifFromCard) { return; }
  if (cardWant >= 0) {
    // The GIF asked for, in place of one read ahead meanwhile.
    for (uint8_t s = 0; s < 2; s++) {
      if (cardSlot[s].state == CARD_SLOT_READY && s != cardLastSlot && cardSlot[s].index != cardWant) { gifCardFreeSlot(s); }
    }
    for (uint8_t s = 0; s < 2; s++) {
      if (cardSlot[s].state == CARD_SLOT_EMPTY) {
        cardSlot[s] = { nullptr, 0, cardWant, CARD_SLOT_FILLING };
        gifCardSend(CARD_FETCH_JOB, cardWant);
        break;
      }
    }
    return;
  }
  bool ready = false;
  int8_t empty = -1;
  for (uint8_t s = 0; s < 2; s++) {
    if (cardSlot[s].state == CARD_SLOT_READY && s != cardLastSlot) { ready = true; }
    if (cardSlot[s].state == CARD_SLOT_EMPTY && empty < 0) { empty = (int8_t)s; }
  }
  if (ready || empty < 0) { return; }
  int16_t i = cardStressAt < cardStressCount ? cardStress[cardStressAt++] : gifPick();
  if (i < 0 || (cardLastSlot >= 0 && cardSlot[cardLastSlot].index == i)) { return; }   // nothing new to read
  cardSlot[empty] = { nullptr, 0, i, CARD_SLOT_FILLING };
  gifCardSend(CARD_FETCH_JOB, i);
}

// Console "sd ...".
void gifCardCommand(const char *line) {
  unsigned khz = 0;
  if (strcmp(line, "sd") == 0) {
    uint16_t bad = 0, byHand = 0;
    for (uint16_t i = 0; i < cardList.count; i++) {
      if (cardList.gifs[i].bad) { bad++; }
      if (!cardList.gifs[i].hasSha) { byHand++; }
    }
    Console.printf("Card: %s, %u GIFs (%u not in the index, %u left out), %lu MB, %lu MB free, %lu kHz; source %s; "
                  "slots %d/%d (index %d/%d), last %d\n", cardMounted ? "mounted" : "none", cardList.count, byHand, bad,
                  (unsigned long)cardMb, (unsigned long)cardFreeMb, (unsigned long)cardRealKhz,
                  gifFromCard ? "the card" : "the pack", cardSlot[0].state, cardSlot[1].state, cardSlot[0].index,
                  cardSlot[1].index, cardLastSlot);
  } else if (strcmp(line, "sd remount") == 0) {
    cardRemountWanted = true;
    Console.println("Card: read anew as soon as no GIF plays");
  } else if (sscanf(line, "sd freq %u", &khz) == 1 && khz >= 400 && khz <= 40000) {
    cardFreqKhz = khz;
    cardRemountWanted = true;
    Console.printf("Card: %u kHz from the next mount (now)\n", khz);
  } else if (strcmp(line, "sd stress") == 0) {
    // The 10 largest files, then the 4 largest 192x128 not among them.
    if (!gifFromCard || cardJobBusy || screen == SCREEN_GIF) { Console.println("Card: not now (no card, busy, or a GIF plays)"); return; }
    cardStressCount = 0;
    for (uint8_t pass = 0; pass < 2; pass++) {
      uint8_t want = pass == 0 ? 10 : 4;
      for (uint8_t k = 0; k < want && cardStressCount < sizeof(cardStress) / sizeof(cardStress[0]); k++) {
        int16_t best = -1;
        for (uint16_t i = 0; i < cardList.count; i++) {
          const CardGif &g = cardList.gifs[i];
          if (g.bad || (pass == 1 && (g.width != WALL_W || g.height != WALL_H))) { continue; }
          bool taken = false;
          for (uint8_t t = 0; t < cardStressCount; t++) { if (cardStress[t] == (int16_t)i) { taken = true; } }
          if (!taken && (best < 0 || g.bytes > cardList.gifs[best].bytes)) { best = (int16_t)i; }
        }
        if (best < 0) { break; }
        cardStress[cardStressCount++] = best;
      }
    }
    cardStressAt = 0;
    for (uint8_t s = 0; s < 2; s++) { if (cardSlot[s].state == CARD_SLOT_READY && s != cardLastSlot) { gifCardFreeSlot(s); } }
    Console.printf("Card: the next %u GIFs read ahead are the largest:", cardStressCount);
    for (uint8_t t = 0; t < cardStressCount; t++) { Console.printf(" %s", cardList.gifs[cardStress[t]].name); }
    Console.println();
  } else if (strcmp(line, "sd speed") == 0) {
    if (!cardMounted || cardJobBusy || screen == SCREEN_GIF) { Console.println("Card: not now (no card, busy, or a GIF plays)"); return; }
    cardSpeedList() = &cardList;
    cardSpeedStopsBefore = wallOutputStops;
    gifCardSend(CARD_SPEED_JOB, -1);
    Console.println("Card: reading every GIF once ...");
  }
}
#endif
