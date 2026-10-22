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
#if BOARD_WAVESHARE_WALL
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
bool gifPlaying() { return gifPlayer != nullptr; }   // a GIF has the panel (clock_state.h)
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
// catches up behind it). Until then it was dropped there.
void gifPlayNow() {
  if (screen != SCREEN_FACE) { return; }
  if (!gifStart(gifNext(), false)) { gifScheduleNext(); }
}

// The last GIF shown once more (FN_GIF_AGAIN): on the face only, as
// gifPlayNow(); a random one when none has played yet or the last no longer
// fits the panel as it is held.
void gifPlayAgain() {
  if (screen != SCREEN_FACE) { return; }
  int16_t i = (gifLast >= 0 && gifFits(gifLast)) ? gifLast : gifNext();
  if (!gifStart(i, false)) { gifScheduleNext(); }
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
  Console.printf("GIF pack: %u GIFs, %u KB; %u KB PSRAM free\n", gifPackCount, (unsigned)(h.size / 1024),
                (unsigned)(heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024));
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

// (Plain types in these signatures: the prototypes generated from this
// sketch stand on every board, where the card's types do not exist.)
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
  if (cardJobBusy || screen == SCREEN_GIF) { return; }
  if (cardRemountWanted) { gifCardRemount(); return; }
  if (!gifFromCard) { return; }
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
