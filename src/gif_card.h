/* ----------------------------------------------------------------------
   The wall clock's GIFs from its microSD card (plan of 2026-10-19, step 3).

   The card sits in the Waveshare board's slot, wired for 1-bit SD (schematic,
   read on 2026-10-19): CLK GPIO1, CMD GPIO44, D0 GPIO17, D3 GPIO14 pulled up,
   no card detect, the card's supply always on. It is mounted through ESP-IDF
   directly (the core's SD_MMC does not hand the host back cleanly when it
   unmounts), FAT32, read only: the clock never writes to the card, so a power
   cut cannot damage it. The GIFs are single files in /gifs, put there by the
   GIF library's card.py from a card reader on the PC.

   A task on core 0 does all the card's work, as the band's fetch task does:
   - mount: the card, its index /gifs/.index (card.py writes it: size, width,
     height, one loop in ms, frames, SHA-1 and name per GIF), and one pass over
     the folder with FatFs's own readdir, which gives every file's size without
     opening it. A GIF the index does not name (put there by hand) is taken as
     well; its size and loop are found when it is first read.
   - fetch: one GIF into PSRAM, through a 32 KB buffer in internal RAM. The
     card's DMA so never touches PSRAM - DMA into PSRAM by the crypto engine
     made the panel output stop on 2026-10-18 - and the copy into PSRAM is the
     CPU's. Its SHA-1 is checked against the index.
   - speed: every GIF read once and checked, for the console.
   loop() sends it jobs and takes its results (gifCardUpdate() in the sketch);
   it never waits for the card.
   ------------------------------------------------------------------------- */
#pragma once

#include <Arduino.h>
#include <esp_vfs_fat.h>
#include <sdmmc_cmd.h>
#include <driver/sdmmc_host.h>
#include <diskio_sdmmc.h>
#include <ff.h>
#include <mbedtls/sha1.h>
#include <freertos/queue.h>
#include <esp_heap_caps.h>

const uint32_t CARD_GIF_MAX     = 4UL << 20;   // bytes; the largest GIF of the library that fits the wall has 1.8 MB
const uint16_t CARD_COUNT_MAX   = 8000;
// 1-bit SD. Measured on the wall on 2026-10-20 with a 32 GB SDHC card, every
// one of 966 GIFs (143 MB) read and checked by SHA-1: 20 MHz 1.13 MB/s, 40 MHz
// 1.74 MB/s, both without a read error, a wrong SHA-1 or the panel output
// stopping; ten mounts in a row at 40 MHz, each about 370 ms.
const uint32_t CARD_FREQ_KHZ    = 40000;
const size_t   CARD_CHUNK       = 32 * 1024;   // the internal buffer the card's DMA reads into
const char    *const CARD_MOUNT = "/sd";

struct CardGif {
  const char *name;           // in CardList's name pool
  uint32_t bytes;
  uint32_t loopMs;            // one loop as the clock plays it; 0 = not known yet
  uint16_t width, height;     // 0 = not known yet (no line in the index)
  uint16_t frames;
  uint8_t  sha1[20];
  bool     hasSha;            // from the index
  bool     bad;               // could not be read, or is no GIF the wall plays
};

// What the mount found. Built by the task, handed to loop() whole; after
// that only loop() changes it (bad, and what a fetch found out).
struct CardList {
  CardGif *gifs = nullptr;
  uint16_t count = 0;
  char    *names = nullptr;   // the name pool
  size_t   namesUsed = 0, namesSize = 0;
};

enum CardJobKind : uint8_t { CARD_MOUNT_JOB, CARD_FETCH_JOB, CARD_SPEED_JOB };
struct CardJob {
  CardJobKind kind;
  int16_t     index;          // fetch: which GIF of the list
  uint32_t    freqKhz;        // mount
  const CardGif *gif;         // fetch: the entry (read only in the task)
};
struct CardResult {
  CardJobKind kind;
  bool        ok;
  int16_t     index;
  uint8_t    *data;           // fetch: the GIF in PSRAM (loop() frees it)
  uint32_t    size;
  uint16_t    width, height;  // fetch: from the GIF's header
  uint32_t    ms;
  CardList    list;           // mount
  uint16_t    fromIndex, byHand, skipped;
  uint32_t    cardMb, freeMb, freqKhz;
  uint32_t    bytes, shaWrong, readErrors;
  char        text[96];
};

// The task's state; volatile what loop() looks at.
inline sdmmc_card_t *&cardHandle() { static sdmmc_card_t *c = nullptr; return c; }
inline volatile bool &cardReading() { static volatile bool r = false; return r; }
inline QueueHandle_t &cardJobs()    { static QueueHandle_t q = nullptr; return q; }
inline QueueHandle_t &cardResults() { static QueueHandle_t q = nullptr; return q; }
inline uint8_t *&cardChunk()        { static uint8_t *b = nullptr; return b; }

inline void cardDrive(char *out) {   // "0:" - FatFs's name of the mounted card
  BYTE pdrv = cardHandle() ? ff_diskio_get_pdrv_card(cardHandle()) : 0;
  out[0] = (char)('0' + pdrv);
  out[1] = ':';
  out[2] = '\0';
}

inline void cardListFree(CardList &l) {
  heap_caps_free(l.gifs);
  heap_caps_free(l.names);
  l = CardList();
}

inline const char *cardListName(CardList &l, const char *name) {
  size_t n = strlen(name) + 1;
  if (l.namesUsed + n > l.namesSize) {
    size_t grow = max<size_t>(l.namesSize * 2, 64 * 1024);
    char *p = (char *)heap_caps_realloc(l.names, grow, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!p) { return nullptr; }
    // The entries point into the pool: move them along.
    for (uint16_t i = 0; i < l.count; i++) { l.gifs[i].name = p + (l.gifs[i].name - l.names); }
    l.names = p;
    l.namesSize = grow;
  }
  char *at = l.names + l.namesUsed;
  memcpy(at, name, n);
  l.namesUsed += n;
  return at;
}

inline bool cardHex(const char *s, uint8_t *out) {
  for (uint8_t i = 0; i < 20; i++) {
    uint8_t v = 0;
    for (uint8_t k = 0; k < 2; k++) {
      char c = s[i * 2 + k];
      v = (uint8_t)(v << 4 | (c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : 0xFF));
      if (!isxdigit((unsigned char)c)) { return false; }
    }
    out[i] = v;
  }
  return true;
}

// A whole file of the card (path below its root) into PSRAM, NUL-terminated,
// through the internal buffer; nullptr when it is not there, larger than
// `limit` or cannot be read.
inline char *cardReadAll(const char *file, uint32_t limit) {
  char drv[3], path[300];
  cardDrive(drv);
  snprintf(path, sizeof(path), "%s/%s", drv, file);
  FIL *f = (FIL *)heap_caps_malloc(sizeof(FIL), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  if (!f) { return nullptr; }
  char *out = nullptr;
  if (f_open(f, path, FA_READ) == FR_OK) {
    uint32_t size = (uint32_t)f_size(f);
    out = size <= limit ? (char *)heap_caps_malloc(size + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) : nullptr;
    for (uint32_t at = 0; out && at < size; ) {
      UINT got = 0, want = (UINT)min<uint32_t>(CARD_CHUNK, size - at);
      if (f_read(f, cardChunk(), want, &got) != FR_OK || got != want) { heap_caps_free(out); out = nullptr; break; }
      memcpy(out + at, cardChunk(), got);
      at += got;
    }
    if (out) { out[size] = '\0'; }
    f_close(f);
  }
  heap_caps_free(f);
  return out;
}

// Mounts the card and makes its list: the index's lines for the files that
// are there with that size, the rest of the folder's .gif files as found.
inline void cardMount(uint32_t freqKhz, CardResult &r) {
  if (cardHandle()) {
    esp_vfs_fat_sdcard_unmount(CARD_MOUNT, cardHandle());
    cardHandle() = nullptr;
  }
  sdmmc_host_t host = SDMMC_HOST_DEFAULT();
  host.flags = SDMMC_HOST_FLAG_1BIT | SDMMC_HOST_FLAG_DEINIT_ARG;   // 1 bit; deinit with its slot
  host.max_freq_khz = (int)freqKhz;
  sdmmc_slot_config_t slot = SDMMC_SLOT_CONFIG_DEFAULT();
  slot.width = 1;
  slot.clk = GPIO_NUM_1;
  slot.cmd = GPIO_NUM_44;
  slot.d0  = GPIO_NUM_17;
  slot.d1 = slot.d2 = slot.d3 = GPIO_NUM_NC;   // D3 (GPIO14) stays high by its pull-up: the card in SD mode
  slot.cd = SDMMC_SLOT_NO_CD;
  slot.wp = SDMMC_SLOT_NO_WP;
  esp_vfs_fat_sdmmc_mount_config_t mc = VFS_FAT_MOUNT_DEFAULT_CONFIG();
  mc.max_files = 2;
  sdmmc_card_t *card = nullptr;
  esp_err_t err = esp_vfs_fat_sdmmc_mount(CARD_MOUNT, &host, &slot, &mc, &card);
  if (err != ESP_OK) {
    snprintf(r.text, sizeof(r.text), "no card (%s)", esp_err_to_name(err));
    return;
  }
  cardHandle() = card;
  r.freqKhz = (uint32_t)card->real_freq_khz;
  r.cardMb = (uint32_t)((uint64_t)card->csd.capacity * card->csd.sector_size / 1000000ULL);
  char drv[3], path[300];
  cardDrive(drv);
  FATFS *fs = nullptr;
  DWORD freeClusters = 0;
  if (f_getfree(drv, &freeClusters, &fs) == FR_OK && fs) {
    r.freeMb = (uint32_t)((uint64_t)freeClusters * fs->csize * 512 / 1000000ULL);
  }

  // The index: name -> line, sorted by name as card.py writes it. Read whole
  // into PSRAM (the core has no f_gets) and taken apart there.
  struct Line { char name[64]; uint32_t bytes, loop; uint16_t w, h, frames; uint8_t sha[20]; };
  Line *lines = nullptr;
  uint16_t nLines = 0;
  char *text = cardReadAll("gifs/.index", 2UL << 20);
  if (text && strncmp(text, "MATRIXCLOCK GIF INDEX 1\n", 24) == 0) {
    lines = (Line *)heap_caps_malloc(sizeof(Line) * CARD_COUNT_MAX, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    bool whole = false;
    for (char *line = text + 24, *next; lines && *line; line = next) {
      next = strchr(line, '\n');
      if (!next) { break; }   // a last line without its end: cut off
      *next++ = '\0';
      unsigned count = 0;
      if (sscanf(line, "END %u", &count) == 1) { whole = (count == nLines); break; }
      if (nLines >= CARD_COUNT_MAX) { break; }
      Line &l = lines[nLines];
      unsigned long size, loop;
      unsigned w, h, frames;
      char sha[41], name[256];
      if (sscanf(line, "%lu\t%u\t%u\t%lu\t%u\t%40s\t%255[^\r\n]", &size, &w, &h, &loop, &frames, sha, name) == 7
          && strlen(name) < sizeof(l.name) && cardHex(sha, l.sha)) {
        strcpy(l.name, name);
        l.bytes = size; l.loop = loop; l.w = (uint16_t)w; l.h = (uint16_t)h; l.frames = (uint16_t)frames;
        nLines++;
      }
    }
    if (!whole) { nLines = 0; }   // cut off or written by something else: the folder alone counts
  }
  heap_caps_free(text);

  // One pass over the folder.
  CardList &list = r.list;
  list.gifs = (CardGif *)heap_caps_calloc(CARD_COUNT_MAX, sizeof(CardGif), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  FF_DIR *dir = (FF_DIR *)heap_caps_malloc(sizeof(FF_DIR), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  FILINFO *fi = (FILINFO *)heap_caps_malloc(sizeof(FILINFO), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  snprintf(path, sizeof(path), "%s/gifs", drv);
  if (list.gifs && dir && fi && f_opendir(dir, path) == FR_OK) {
    while (f_readdir(dir, fi) == FR_OK && fi->fname[0]) {
      const char *name = fi->fname;
      size_t n = strlen(name);
      if ((fi->fattrib & AM_DIR) || name[0] == '.' || n < 5 || strcasecmp(name + n - 4, ".gif") != 0) { continue; }
      if (list.count >= CARD_COUNT_MAX || fi->fsize == 0 || fi->fsize > CARD_GIF_MAX) { r.skipped++; continue; }
      CardGif &g = list.gifs[list.count];
      g.name = cardListName(list, name);
      if (!g.name) { r.skipped++; continue; }
      g.bytes = (uint32_t)fi->fsize;
      int lo = 0, hi = (int)nLines - 1;   // the index line, by binary search
      while (lo <= hi) {
        int mid = (lo + hi) / 2, c = strcmp(lines[mid].name, name);
        if (c == 0) {
          if (lines[mid].bytes == g.bytes) {
            g.width = lines[mid].w; g.height = lines[mid].h; g.loopMs = lines[mid].loop;
            g.frames = lines[mid].frames; memcpy(g.sha1, lines[mid].sha, 20); g.hasSha = true;
          }
          break;
        }
        if (c < 0) { lo = mid + 1; } else { hi = mid - 1; }
      }
      if (g.hasSha) { r.fromIndex++; } else { r.byHand++; }
      list.count++;
    }
    f_closedir(dir);
  }
  heap_caps_free(dir);
  heap_caps_free(fi);
  heap_caps_free(lines);
  r.ok = list.count > 0;
  snprintf(r.text, sizeof(r.text), "%u GIFs (%u from the index, %u not in it, %u left out)", list.count,
           r.fromIndex, r.byHand, r.skipped);
}

// Reads one file through the internal buffer: into `into` (PSRAM) when given,
// the SHA-1 always. false on a read error or a short file.
inline bool cardRead(const char *name, uint32_t bytes, uint8_t *into, uint8_t sha[20]) {
  char drv[3], path[300];
  cardDrive(drv);
  snprintf(path, sizeof(path), "%s/gifs/%s", drv, name);
  FIL *f = (FIL *)heap_caps_malloc(sizeof(FIL), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  if (!f) { return false; }
  bool ok = f_open(f, path, FA_READ) == FR_OK;
  mbedtls_sha1_context ctx;
  mbedtls_sha1_init(&ctx);
  mbedtls_sha1_starts(&ctx);
  uint32_t at = 0;
  cardReading() = true;
  while (ok && at < bytes) {
    UINT got = 0;
    UINT want = (UINT)min<uint32_t>(CARD_CHUNK, bytes - at);
    ok = f_read(f, cardChunk(), want, &got) == FR_OK && got == want;
    if (!ok) { break; }
    mbedtls_sha1_update(&ctx, cardChunk(), got);
    if (into) { memcpy(into + at, cardChunk(), got); }
    at += got;
  }
  cardReading() = false;
  mbedtls_sha1_finish(&ctx, sha);
  mbedtls_sha1_free(&ctx);
  f_close(f);
  heap_caps_free(f);
  return ok;
}

inline void cardFetch(const CardJob &job, CardResult &r) {
  const CardGif &g = *job.gif;
  r.index = job.index;
  r.data = (uint8_t *)heap_caps_malloc(g.bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!r.data) { snprintf(r.text, sizeof(r.text), "no PSRAM for %lu bytes", (unsigned long)g.bytes); return; }
  uint8_t sha[20];
  bool ok = cardRead(g.name, g.bytes, r.data, sha);
  if (!ok) { ok = cardRead(g.name, g.bytes, r.data, sha); r.readErrors = 1; }   // once more
  if (!ok) {
    snprintf(r.text, sizeof(r.text), "cannot be read");
  } else if (g.hasSha && memcmp(sha, g.sha1, 20) != 0) {
    ok = false;
    r.shaWrong = 1;
    snprintf(r.text, sizeof(r.text), "its SHA-1 is not the index's");
  } else if (g.bytes < 10 || memcmp(r.data, "GIF8", 4) != 0) {
    ok = false;
    snprintf(r.text, sizeof(r.text), "is no GIF");
  }
  if (!ok) {
    heap_caps_free(r.data);
    r.data = nullptr;
    return;
  }
  r.size = g.bytes;
  r.width  = (uint16_t)(r.data[6] | (r.data[7] << 8));
  r.height = (uint16_t)(r.data[8] | (r.data[9] << 8));
  r.ok = true;
}

// Every GIF of the list read once, timed and checked; for the console.
inline void cardSpeed(const CardList *list, CardResult &r) {
  for (uint16_t i = 0; list && i < list->count; i++) {
    const CardGif &g = list->gifs[i];
    uint8_t sha[20];
    if (!cardRead(g.name, g.bytes, nullptr, sha)) { r.readErrors++; continue; }
    if (g.hasSha && memcmp(sha, g.sha1, 20) != 0) { r.shaWrong++; }
    r.bytes += g.bytes;
  }
  r.ok = r.readErrors == 0 && r.shaWrong == 0;
}

inline const CardList *&cardSpeedList() { static const CardList *l = nullptr; return l; }

inline void cardTask(void *) {
  static CardResult r;
  CardJob job;
  for (;;) {
    if (xQueueReceive(cardJobs(), &job, portMAX_DELAY) != pdTRUE) { continue; }
    memset(&r, 0, sizeof(r));
    r.kind = job.kind;
    r.index = -1;
    uint32_t t0 = millis();
    if (job.kind == CARD_MOUNT_JOB) { cardMount(job.freqKhz, r); }
    else if (job.kind == CARD_FETCH_JOB) { cardFetch(job, r); }
    else { cardSpeed(cardSpeedList(), r); }
    r.ms = millis() - t0;
    xQueueSend(cardResults(), &r, portMAX_DELAY);
  }
}

// Starts the card's task: on core 0, its stack and the chunk buffer in
// internal RAM (the card's DMA reads into the buffer). false without RAM.
inline bool cardBegin() {
  if (cardJobs()) { return true; }
  cardChunk() = (uint8_t *)heap_caps_aligned_alloc(64, CARD_CHUNK, MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
  cardJobs()    = xQueueCreate(1, sizeof(CardJob));
  cardResults() = xQueueCreate(1, sizeof(CardResult));
  if (!cardChunk() || !cardJobs() || !cardResults()) { return false; }
  return xTaskCreatePinnedToCore(cardTask, "card", 6144, nullptr, 1, nullptr, 0) == pdPASS;
}
