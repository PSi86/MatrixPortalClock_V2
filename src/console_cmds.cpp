// The console's commands, and the wall clock's watch on its panel output
// (console_cmds.h).
#include "console_cmds.h"

#include <esp_heap_caps.h>
#include "clock_state.h"
#include "console.h"
#include "feeds.h"
#include "gif_player.h"
#include "input.h"        // the console's "knock"
#include "lan_access.h"
#if BOARD_WAVESHARE_WALL
#include <mbedtls/sha256.h>    // the "crypto" test of the panel output beside the crypto DMA
#include <mbedtls/aes.h>
#include <soc/gdma_struct.h>   // and "gdma": the DMA channels' peripherals and priorities
#include <esp_timer.h>
#include "brightness.h"
#include "info_band.h"
#include "time_sync.h"

// What only this module calls, ahead of its use.
#if CLOCK_CONSOLE
static void wallCommand(const char *line);
#endif
static void wallGdmaList();
static void wallCryptoTest(bool external);
#endif

#if CLOCK_CONSOLE
// The console's commands, in builds with a console (console.h; a release
// build takes none):
// - "log 1" / "log 2": the log level until the next start (console.h).
// - "fetch" makes every source of the info band due now (weather, warnings
//   over HTTPS), to watch the panel output while they are fetched.
// - "lan on" / "lan off" switch the web app on the home network, as its
//   switch on the hotspot's page does (with the password set there).
// - "gif <name or keyword>" asks for a GIF as the web app does (gifRequest();
//   "gif! ..." with replace), "gif* ..." for all it finds, one after another
//   (gifSeriesRequest(), with replace), "gifs <name or keyword>" lists them.
// - "knock" is a knock that counts, as the sensor's would (handleInput()).
// On the wall clock also its own (wallCommand()).
static void gifCommand(const char *line);

void consoleCommands() {
  static char line[96];   // a card GIF's name has up to 63 characters
  static uint8_t len = 0;
  while (Console.available() > 0) {
    char c = (char)Console.read();
    if (c != '\n' && c != '\r') {
      if (len < sizeof(line) - 1) { line[len++] = c; }
      continue;
    }
    line[len] = '\0';
    len = 0;
    unsigned level = 0;
    if (sscanf(line, "log %u", &level) == 1 && level >= 1 && level <= 2) {
      consoleLevel = (uint8_t)level;
      Console.printf("Log level %u until the next start\n", level);
    }
    if (strcmp(line, "fetch") == 0) {
      for (uint8_t f = 0; f < FEED_COUNT; f++) { feedRuns[f].dueAt = millisNow; }
      Console.println("Info band: every source due now");
    }
    bool lanOn = strcmp(line, "lan on") == 0;
    if (lanOn || strcmp(line, "lan off") == 0) {
      lanSwitch(lanOn);
      Console.printf("Web app on the home network: %s\n", lanOn ? "on" : "off");
    }
    if (strcmp(line, "knock") == 0) {
      Console.println("Knock (from the console)");
      handleInput(EV_KNOCK, 1);
    }
    gifCommand(line);
#if BOARD_WAVESHARE_WALL
    wallCommand(line);
#endif
  }
}

// "gif ...", "gif! ...", "gif* ..." and "gifs ..." (consoleCommands()).
static void gifCommand(const char *line) {
  if (strncmp(line, "gif* ", 5) == 0) {
    GifAnswer a = gifSeriesRequest(line + 5, true);
    Console.printf("GIF series asked for: %s (%u found, %u fit)\n", gifAskedWords(a.how), a.matches, a.fit);
    return;
  }
  if (strncmp(line, "gifs ", 5) == 0 || strcmp(line, "gifs") == 0) {
    uint16_t count = (uint16_t)gifCount();
    uint8_t *hit = count ? (uint8_t *)heap_caps_malloc(count, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) : nullptr;
    if (count && !hit) { Console.println("GIFs: no RAM to search"); return; }
    bool byName = false;
    uint32_t t0 = micros();
    uint16_t found = count ? gifFind(line[4] ? line + 5 : "", hit, byName) : 0;
    uint32_t us = micros() - t0;
    Console.printf("GIFs: %u of %u found%s in %lu us, %u in the meta index:\n", found, count, byName ? " by name" : "",
                  (unsigned long)us, gifMetaKnown());
    uint16_t shown = 0;
    for (uint16_t i = 0; i < count && shown < 40; i++) {
      if (!hit[i]) { continue; }
      uint16_t w = 0, h = 0;
      gifSizeOf((int16_t)i, w, h);
      Console.printf("  %s (%ux%u%s) %s\n", gifMetaOf((int16_t)i).field[GIF_META_NAME], w, h,
                    gifFitsNow((int16_t)i) ? "" : ", does not fit now", gifTitleOf((int16_t)i));
      shown++;
    }
    heap_caps_free(hit);
    return;
  }
  bool replace = strncmp(line, "gif! ", 5) == 0 || strcmp(line, "gif!") == 0;
  if (!replace && strncmp(line, "gif ", 4) != 0 && strcmp(line, "gif") != 0) { return; }
  const char *what = line + (replace ? 4 : 3);
  while (*what == ' ') { what++; }
  GifAnswer a = gifRequest(what, replace);
  Console.printf("GIF asked for: %s (%u found, %u fit)%s%s\n", gifAskedWords(a.how), a.matches, a.fit, a.index >= 0 ? " - " : "",
                a.index >= 0 ? gifMetaOf(a.index).field[GIF_META_NAME] : "");
}
#endif

#if BOARD_WAVESHARE_WALL
#if CLOCK_CONSOLE
// The wall clock's own commands on the console (consoleCommands()):
// - "black" switches the panels dark until the next start, as POST /api/black
//   does in every build. A flash resets the
//   chip in the middle of a refresh, and with nobody driving the panels the row
//   pair they show then may stay lit at full duty while it runs (bench rule of
//   2026-10-17: the picture black before every flash).
// - "test <panel> <brightness>" shows a test picture to measure the panels'
//   current with: that panel (1-3 the top row from the left, 4-6 the bottom
//   row; 0 none) in the clock's white at that brightness, the rest dark. The
//   clock stands still meanwhile; "test off" gives the wall back.
// - "bus <divider>" sets the panel bus to 160 MHz / divider while it runs,
//   to judge faster clocks than the driver's 13.3 MHz by eye.
// - "prio <0-5>" sets the panel output's DMA channel priority among the
//   GDMA channels (PANEL_DMA_PRIORITY from the start), until the next start.
// - "gdma" lists every GDMA channel's peripheral, priority and weight.
// - "crypto" times SHA-256 and AES-128 over blocks in internal RAM, as TLS
//   hands them to the crypto engine; "crypto ext" the same over blocks in
//   PSRAM (wallCryptoTest()).
// - "tlsmem int" / "tlsmem ext": mbedTLS's memory from internal RAM first
//   (from the start) or from PSRAM first, as the MatrixPortal S3 takes it
//   (feedTlsCalloc()), until the next start.
// - "status test" shows the band's clock status for BAND_STATUS_TEST_MS as if
//   the time sync were failing.
// - "rtc" shows what the DS3231 and the board's PCF85063 hold against the
//   clock (wallRtcReport()).
// - "imu" shows the board's motion sensor and what it gave of the knock
//   (Knock::report()).
// - "sd" shows the microSD card and its GIFs; "sd speed" reads every GIF once
//   and checks it; "sd remount" reads the card anew; "sd freq <kHz>" mounts
//   it at another clock; "sd stress" reads its largest GIFs ahead in turn
//   (gifCardCommand()).
static void wallCommand(const char *line) {
  if (strcmp(line, "black") == 0) {
    matrix.holdDark();
    Console.println("Panels dark until the next start");
  }
  unsigned panel = 0, bright = 0, div = 0, prio = 0;
  if (strcmp(line, "test off") == 0) {
    matrix.endTestPicture();
    matrix.setBrightness(panelBrightness());
    panelDirty = true;
    Console.println("Test picture off");
  } else if (sscanf(line, "test %u %u", &panel, &bright) == 2 && panel <= 6 && bright <= 255) {
    matrix.testPicture((uint8_t)panel, (uint8_t)bright);
    Console.printf("Test picture: panel %u white at brightness %u (the cap is %u), the rest dark\n",
                  panel, bright, (unsigned)PANEL_BRIGHT_CAP);
  } else if (sscanf(line, "bus %u", &div) == 1 && div >= 8 && div <= 32) {
    matrix.setBusDivider((uint8_t)div);
    Console.printf("Panel bus: 160 MHz / %u = %u kHz, refresh %lu Hz\n", div, 160000U / div,
                  (unsigned long)matrix.refreshRate());
  } else if (sscanf(line, "prio %u", &prio) == 1 && prio <= 5) {
    bool ok = matrix.setDmaPriority((uint8_t)prio);
    Console.printf("Panel output: DMA priority %u %s\n", prio, ok ? "set" : "NOT set");
  } else if (strcmp(line, "gdma") == 0) {
    wallGdmaList();
  } else if (strcmp(line, "crypto") == 0 || strcmp(line, "crypto ext") == 0) {
    wallCryptoTest(line[6] != '\0');
  } else if (strcmp(line, "rtc") == 0) {
    wallRtcReport();
  } else if (strcmp(line, "imu") == 0) {
    knock.report();
  } else if (strncmp(line, "sd", 2) == 0 && (line[2] == '\0' || line[2] == ' ')) {
    gifCardCommand(line);
  }
  if (strcmp(line, "status test") == 0) {
    bandStatusTestAt = millisNow ? millisNow : 1;
    bandLocalDone = false;   // worked out anew at once
    Console.println("Band: the clock status for 2 min, as if the time sync were failing");
  }
  bool tlsInt = strcmp(line, "tlsmem int") == 0;
  if (tlsInt || strcmp(line, "tlsmem ext") == 0) {
    feedTlsInternal() = tlsInt;
    Console.printf("TLS memory: %s first\n", tlsInt ? "internal RAM" : "PSRAM");
  }
}
#endif

// The panel output stops for good when the DMA cannot read the frame buffer
// out of PSRAM fast enough: the clock runs on and the wall stays dark (bus at
// 20 MHz, 2026-10-18; the same day twice at the start of a weather fetch).
// Watched on every pass: once it has stopped it is logged, with what the
// fetch task was doing, and started again (at most every 50 ms). Only after
// it has been seen running: should the flag read 0 all along, that says so
// once and nothing is restarted.
uint32_t wallOutputStops = 0;   // since the start
void wallOutputWatch() {
  uint32_t &stops = wallOutputStops;
  static bool seenRunning = false, toldNever = false;
  static unsigned long restartedAt = 0;
  if (matrix.outputRunning()) { seenRunning = true; return; }
  if (!seenRunning) {
    if (!toldNever && millisNow > 15000) {
      toldNever = true;
      Console.println("Panel output: never seen running - the watch does nothing");
    }
    return;
  }
  if (restartedAt != 0 && millisNow - restartedAt < 50) { return; }
  restartedAt = millisNow ? millisNow : 1;
  stops++;
  if (stops <= 20 || stops % 100 == 0) {
    if (feedBusy) {
      Console.printf("Panel output stopped (%lu since the start), a fetch running for %lu ms; started again\n",
                    (unsigned long)stops, millisNow - feedSentAt);
    } else {
      Console.printf("Panel output stopped (%lu since the start), no fetch running (the last %lu ms ago); started again\n",
                    (unsigned long)stops, millisNow - feedSentAt);
    }
    if (cardReading()) { Console.println("Panel output stopped while the card was being read"); }
  }
  matrix.restartOutput();
}

// Console "gdma": the five GDMA channels both ways - the peripheral each is
// connected to, its priority and weight - and whether the priorities count
// at all. Shows whom the panel output (LCD_CAM, out) shares the DMA with.
static void wallGdmaList() {
  static const char *const PERI[] = { "SPI2", "SPI3", "UHCI0", "I2S0", "I2S1", "LCD_CAM", "AES", "SHA",
                                      "ADC_DAC", "RMT" };
  for (uint8_t i = 0; i < 5; i++) {
    unsigned o = GDMA.channel[i].out.peri_sel.sel, n = GDMA.channel[i].in.peri_sel.sel;
    Console.printf("GDMA %u: out %-7s priority %u weight %2u, L2 FIFO code %u | in %-7s priority %u weight %2u\n", i,
                  o < 10 ? PERI[o] : "-", (unsigned)GDMA.channel[i].out.pri.tx_pri,
                  (unsigned)GDMA.channel[i].out.weight.tx_weight, (unsigned)GDMA.sram_size[i].out.out_size,
                  n < 10 ? PERI[n] : "-", (unsigned)GDMA.channel[i].in.pri.rx_pri,
                  (unsigned)GDMA.channel[i].in.weight.rx_weight);
  }
  Console.printf("GDMA: priority arbitration %s\n", GDMA.misc_conf.arb_pri_dis ? "OFF" : "on");
}

// Console "crypto": what the crypto engine does during a TLS handshake, on
// its own - SHA-256 and AES-128-CTR, ten times over 16 KB each, in internal
// RAM (the TLS buffers are there, CONFIG_MBEDTLS_INTERNAL_MEM_ALLOC; the
// ESP32-S3 feeds both engines by GDMA) or, with external, in PSRAM (where
// larger application buffers go). Prints how long each took and how often
// the panel output stopped meanwhile (watched between the blocks), so the DMA
// priority can be judged on both sides: the panel's output and the crypto's
// speed. On 2026-10-18 internal blocks stopped the output not once, even at
// priority 0, while real handshakes did.
static void wallCryptoTest(bool external) {
  const size_t N = 16384;
  const uint8_t ROUNDS = 10;
  uint32_t caps = external ? MALLOC_CAP_SPIRAM : (MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
  uint8_t *in = (uint8_t *)heap_caps_aligned_alloc(64, N, caps);   // whole cache lines
  uint8_t *out = (uint8_t *)heap_caps_aligned_alloc(64, N, caps);
  if (!in || !out) {
    heap_caps_free(in);
    heap_caps_free(out);
    Console.println("Crypto test: not enough RAM");
    return;
  }
  for (size_t i = 0; i < N; i++) { in[i] = (uint8_t)(i * 7); }
  uint32_t stopsBefore = wallOutputStops;
  uint8_t digest[32];
  int64_t t0 = esp_timer_get_time();
  for (uint8_t r = 0; r < ROUNDS; r++) {
    mbedtls_sha256(in, N, digest, 0);
    wallOutputWatch();
  }
  int64_t t1 = esp_timer_get_time();
  static const uint8_t KEY[16] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16 };
  mbedtls_aes_context aes;
  mbedtls_aes_init(&aes);
  mbedtls_aes_setkey_enc(&aes, KEY, 128);
  for (uint8_t r = 0; r < ROUNDS; r++) {
    size_t off = 0;
    uint8_t nonce[16] = {}, block[16];
    mbedtls_aes_crypt_ctr(&aes, N, &off, nonce, block, in, out);
    wallOutputWatch();
  }
  int64_t t2 = esp_timer_get_time();
  mbedtls_aes_free(&aes);
  heap_caps_free(in);
  heap_caps_free(out);
  double mb = (double)N * ROUNDS / 1e6;
  Console.printf("Crypto test (%s): SHA-256 %lu us (%.2f MB/s), AES-128-CTR %lu us (%.2f MB/s), %u x 16 KB each; "
                "panel output stopped %lu times meanwhile\n", external ? "PSRAM" : "internal RAM",
                (unsigned long)(t1 - t0), mb / ((t1 - t0) / 1e6), (unsigned long)(t2 - t1), mb / ((t2 - t1) / 1e6),
                (unsigned)ROUNDS, (unsigned long)(wallOutputStops - stopsBefore));
  wallGdmaList();   // the crypto engines' channels, should they be given back after use
}
#endif
