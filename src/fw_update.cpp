// A new firmware over the web, and its trial (fw_update.h).
#include "fw_update.h"

#include <ArduinoJson.h>
#include <esp_ota_ops.h>
#include <esp_image_format.h>
#include <esp_app_desc.h>
#include "board_hal.h"      // BOARD_NAME
#include "clock_state.h"    // panelDirty, fwVersion()
#include "console.h"
#include "hotspot.h"        // apActive
#include "web_api.h"        // uploadRead(), uploadDrain(), uploadSettle(), drawUploadProgress()
#include "web_server.h"     // sendJson(), sendJsonError()
#include "wifi_station.h"   // stationUp()

// What only this module calls, ahead of its use.
static void fwConfirm(const char *why);

bool fwOnTrial = false;
bool fwGaveWay = false;

// The core confirms a new firmware as it starts (initArduino(), built with
// CONFIG_APP_ROLLBACK_ENABLE) unless this says the sketch does it later:
// fwTrialUpdate() does. Declared weak in the core's C code, hence C linkage.
extern "C" bool verifyRollbackLater() { return true; }

// The mark every firmware of this clock carries, one per board. An upload
// has to carry the running board's, so the other board's firmware.bin is
// refused. The search below reads it, so the linker keeps it in the image.
static const char FW_BOARD_TAG[] = "\x02" "MatrixClock firmware for " BOARD_NAME "\x03";
static const size_t FW_TAG_LEN = sizeof(FW_BOARD_TAG) - 1;
static_assert(FW_TAG_LEN < 256, "the tag's search table holds uint8_t");

// The start of an image: its header, the first segment's, and the app
// description that segment begins with - read before anything is written.
static const size_t FW_HEAD = sizeof(esp_image_header_t) + sizeof(esp_image_segment_header_t) + sizeof(esp_app_desc_t);
static const size_t FW_CHUNK = 4096;
static const uint32_t FW_SHOW_EVERY = 65536;   // the panel's progress, every this many bytes

// Looks for FW_BOARD_TAG in the image as it arrives, chunk by chunk
// (Knuth-Morris-Pratt, so a tag split across two chunks is found too).
struct TagSearch {
  uint8_t fail[FW_TAG_LEN];
  size_t at = 0;
  bool found = false;

  TagSearch() {
    fail[0] = 0;
    for (size_t i = 1, k = 0; i < FW_TAG_LEN; i++) {
      while (k && FW_BOARD_TAG[i] != FW_BOARD_TAG[k]) { k = fail[k - 1]; }
      if (FW_BOARD_TAG[i] == FW_BOARD_TAG[k]) { k++; }
      fail[i] = (uint8_t)k;
    }
  }
  void feed(const uint8_t *p, size_t n) {
    for (size_t i = 0; i < n && !found; i++) {
      while (at && p[i] != (uint8_t)FW_BOARD_TAG[at]) { at = fail[at - 1]; }
      if (p[i] == (uint8_t)FW_BOARD_TAG[at] && ++at == FW_TAG_LEN) { found = true; }
    }
  }
};

/* ---- The trial ------------------------------------------------------------ */

// A firmware the bootloader starts for the first time after an update is
// pending: confirmed, it stays; not, the next start goes back to the one
// before - and that one finds this slot marked invalid (fwGaveWay).
void fwTrialBegin() {
  const esp_partition_t *run = esp_ota_get_running_partition();
  esp_ota_img_states_t state;
  fwOnTrial = run && esp_ota_get_state_partition(run, &state) == ESP_OK && state == ESP_OTA_IMG_PENDING_VERIFY;
  fwGaveWay = esp_ota_get_last_invalid_partition() != nullptr;
  Console.printf("Firmware %s in %s%s%s\n", fwVersion(), run ? run->label : "?",
                 fwOnTrial ? " - new, on trial until it has run a minute with the network" : "",
                 fwGaveWay ? " - an update before it did not prove itself" : "");
#if defined(FW_TEST_CRASH)
  // Test builds: a firmware that dies on its trial start, to see the
  // bootloader go back to the one before. Flashed over USB it is not on
  // trial and runs.
  if (fwOnTrial) { Console.println("Firmware: FW_TEST_CRASH - dying on trial"); delay(100); abort(); }
#endif
}

// Proven: a minute's run, and the home WiFi joined or the hotspot open, so
// the web app reaches it for the next update. A clock whose station is off
// by its settings (no home-network access, only the daily sync) proves it at
// its next sync.
void fwTrialUpdate() {
#if defined(FW_TEST_NO_CONFIRM)
  return;   // test builds: one that never proves itself, to see it give way at the next start
#endif
  if (!fwOnTrial || millis() < FW_TRIAL_MS) { return; }
  if (!stationUp() && !apActive) { return; }
  fwConfirm(stationUp() ? "ran a minute, the home WiFi joined" : "ran a minute, the hotspot open");
}

static void fwConfirm(const char *why) {
  esp_err_t err = esp_ota_mark_app_valid_cancel_rollback();
  if (err != ESP_OK) {
    static bool said = false;
    if (!said) { said = true; Console.printf("Firmware: could not confirm it (%s)\n", esp_err_to_name(err)); }
    return;
  }
  fwOnTrial = false;
  Console.printf("Firmware confirmed: %s\n", why);
}

uint32_t fwUpdateMax() {
  const esp_partition_t *part = esp_ota_get_next_update_partition(nullptr);
  return part ? part->size : 0;
}

/* ---- The upload ----------------------------------------------------------- */

// POST /api/firmware: a firmware.bin, sent whole as the request body, into
// the slot the clock does not run from. Its start is checked before anything
// is written; the rest is written as it arrives, erased a sector ahead
// (OTA_WITH_SEQUENTIAL_WRITES), while the board's tag is looked for in it.
// esp_ota_end() then checks the whole image against its own SHA-256. Only
// then does the clock choose the new slot and restart. Anything before that
// fails leaves the slot it runs from, and the choice, as they were. The loop
// waits for it - about as long as the upload takes - and the panel shows how
// far it is.
void apiFirmwareUpload(WiFiClient &client, Print &out, long length, bool &restart) {
  const esp_partition_t *part = esp_ota_get_next_update_partition(nullptr);
  if (!part) {
    uploadDrain(client, length);
    sendJsonError(out, 500, "This clock has no second program slot for an update.");
    return;
  }
  if (length < (long)FW_HEAD || (uint32_t)length > part->size) {
    uploadDrain(client, length);
    String e = "Not a firmware this clock takes: its program slot holds up to " + String(part->size / 1024) + " KB.";
    sendJsonError(out, 400, e.c_str());
    return;
  }
  uint8_t *buf = (uint8_t *)malloc(FW_CHUNK);
  if (!buf) {
    uploadDrain(client, length);
    sendJsonError(out, 500, "The clock has no memory free for the upload; nothing was written.");
    return;
  }
  unsigned long started = millis();
  size_t n = uploadRead(client, buf, FW_HEAD);
  if (n != FW_HEAD) {
    free(buf);
    sendJsonError(out, 400, "The upload broke off at its start; the clock keeps its firmware.");
    return;
  }
  const esp_image_header_t *image = (const esp_image_header_t *)buf;
  const esp_app_desc_t *app = (const esp_app_desc_t *)(buf + sizeof(esp_image_header_t) + sizeof(esp_image_segment_header_t));
  if (image->magic != ESP_IMAGE_HEADER_MAGIC || image->chip_id != ESP_CHIP_ID_ESP32S3 || app->magic_word != ESP_APP_DESC_MAGIC_WORD) {
    free(buf);
    uploadDrain(client, length - (long)n);
    sendJsonError(out, 400, "Not a firmware.bin for this clock (a full image or a GIF pack?); nothing was written.");
    return;
  }
  // IDF refuses to write while the running firmware is on trial. It has just
  // taken an upload over the web, which is what the trial waits to see.
  if (fwOnTrial) { fwConfirm("an upload reached it"); }
  uploadSettle("Firmware");

  esp_ota_handle_t ota = 0;
  esp_err_t err = esp_ota_begin(part, OTA_WITH_SEQUENTIAL_WRITES, &ota);
  if (err != ESP_OK) {
    free(buf);
    uploadDrain(client, length - (long)n);
    String e = String("The clock could not start writing (") + esp_err_to_name(err) + "); it keeps its firmware.";
    sendJsonError(out, 500, e.c_str());
    return;
  }
  Console.printf("Firmware: receiving %ld KB into %s\n", length / 1024, part->label);
  TagSearch tag;
  uint32_t at = 0, shownAt = 0, total = (uint32_t)length;
  bool brokeOff = false;
  drawUploadProgress("UPDATE", 0, total);
  while (true) {
    tag.feed(buf, n);
    err = esp_ota_write(ota, buf, n);
    if (err != ESP_OK) { break; }
    at += n;
    if (at - shownAt >= FW_SHOW_EVERY || at == total) { drawUploadProgress("UPDATE", at, total); shownAt = at; }
    if (at >= total) { break; }
    delay(1);   // the rest of the loop is waiting; let the idle task in
    n = uploadRead(client, buf, min(total - at, (uint32_t)FW_CHUNK));
    if (n == 0) { brokeOff = true; break; }
  }
  free(buf);
  panelDirty = true;   // the progress is still on the panel
  String e;
  if (brokeOff) {
    e = "The upload broke off after " + String(at / 1024) + " of " + String(total / 1024) + " KB";
  } else if (err != ESP_OK) {
    uploadDrain(client, (long)(total - at - n));
    e = String("Writing failed after ") + String(at / 1024) + " KB (" + esp_err_to_name(err) + ")";
  } else if (!tag.found) {
    e = "This firmware is not for this board, the " BOARD_NAME;
  }
  if (e.length()) {
    esp_ota_abort(ota);
    e += "; the clock keeps its firmware.";
    Console.println("Firmware: " + e);
    sendJsonError(out, 400, e.c_str());
    return;
  }
  err = esp_ota_end(ota);   // the whole image checked, its SHA-256 too
  if (err != ESP_OK) {
    e = err == ESP_ERR_OTA_VALIDATE_FAILED ? String("The firmware arrived damaged: its check failed")
                                           : String("The firmware could not be closed (") + esp_err_to_name(err) + ")";
    e += "; the clock keeps its firmware.";
    Console.println("Firmware: " + e);
    sendJsonError(out, 400, e.c_str());
    return;
  }
  err = esp_ota_set_boot_partition(part);
  if (err != ESP_OK) {
    e = String("The clock could not choose the new firmware (") + esp_err_to_name(err) + "); it keeps its own.";
    Console.println("Firmware: " + e);
    sendJsonError(out, 500, e.c_str());
    return;
  }
  unsigned long ms = millis() - started;
  Console.printf("Firmware: %u KB written to %s in %lu ms, the board's tag found - restarting with it\n",
                 (unsigned)(total / 1024), part->label, ms);
  JsonDocument d;
  d["ok"] = true;
  d["kb"] = total / 1024;
  d["ms"] = ms;
  d["slot"] = part->label;
  d["trialS"] = FW_TRIAL_MS / 1000;
  sendJson(out, d);
  restart = true;   // once the answer is out (web_server.cpp, webAfter())
}
