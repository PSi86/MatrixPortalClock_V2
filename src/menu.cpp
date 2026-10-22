// The on-screen menu, its editors, and the banners (menu.h).
#include "menu.h"

#include <Fonts/Picopixel.h>
#include <Fonts/TomThumb.h>    // 3x5, for the menu
#include "brightness.h"
#include "clock_state.h"
#include "clock_time.h"
#include "console.h"
#include "input.h"
#include "notices.h"
#include "settings.h"
#include "time_sync.h"
#include "time_zones.h"

// What only this module calls, ahead of its use.
enum MenuItem : uint8_t;
static void brightnessKeep();
static void dstColour(uint8_t &r, uint8_t &g, uint8_t &b);
static void showTimeBanner();
static void formatClockTime(char *buf, size_t size, time_t local);
static uint16_t uiAccent();
static void uiSmall(int16_t x, int16_t top, const char *s, uint16_t colour);
static int16_t uiTextWidth(const char *s, const GFXfont *font, uint8_t size);
static void uiTinyRight(int16_t right, int16_t top, const char *s, uint16_t colour);
static void drawList(const char *title, const char *const *labels, uint8_t count, uint8_t focus, const char *value);
static bool menuItemShown(MenuItem item);
static void menuItemValue(MenuItem item, char *buf, size_t size);
static void presentUi();
static uint16_t rawPixelIndex(int16_t x, int16_t y);
static void playSlide(int8_t dir);
static void drawBrightnessScale(int16_t y);
static void drawBrightEditor();
static void formatZoneTime(char *buf, size_t size, uint8_t zone, uint8_t dstMode);
static const char *zoneCity(uint8_t zone);
static int16_t marqueeShift(int16_t textW, int16_t boxW, unsigned long since);
static bool zoneCityScrolls();
static void drawZoneEditor();
static void drawDstEditor();
static int16_t drawTimeFieldText(int16_t x, int16_t top, const char *text, bool focused);
static void drawTimeEditor();
static MenuItem menuStep(MenuItem from, int8_t dir);
static void menuNavigate(InputFunction fn);
static void menuEnter(MenuItem item);
static void menuToggle(MenuItem item);
static void menuAction(MenuItem item);
static uint8_t zoneIndex(int32_t offset);
static uint8_t dstOptionIndex(uint8_t mode);
static void editorOpen(MenuItem item);
static void timeEditorOpen();
static int16_t daysInMonth(int16_t year, int16_t month);
static void timeEditorStep(int delta);
static void timeEditorCommit();
static void editorNavigate(InputFunction fn, uint8_t steps);
static void editorStep(int delta);
static void editorCommit();
static void toggleAutoBright();

/* ======================================================================
   On-screen menu: model
   ====================================================================== */

// One flat list. Each item names the kind of editor it uses; what the item
// changes is decided where its kind needs it (editorOpen() and friends).
enum MenuItem : uint8_t { ITEM_BRIGHT, ITEM_AUTO, ITEM_ZONE, ITEM_DST, ITEM_HOTSPOT, ITEM_TIME, ITEM_COUNT };
enum EditorKind : uint8_t {
  KIND_NUMBER,   // a value on a scale: PREV / NEXT step it, ENTER saves, BACK reverts
  KIND_TOGGLE,   // no editor: ENTER on the row flips it and saves
  KIND_ZONE,     // the timezone picker
  KIND_CHOICE,   // one of a few named options
  KIND_ACTION,   // no editor: ENTER on the row does it
  KIND_DATETIME, // several fields in turn: ENTER goes to the next and saves on the last,
                 // BACK goes to the one before and leaves without saving on the first
};
struct MenuEntry { const char *label; const char *longLabel; EditorKind kind; };
// Short labels have at most 8 characters, what a 32 px wide portrait panel
// holds in TomThumb. The long ones are for the larger wall-clock panels.
const MenuEntry MENU_ITEMS[ITEM_COUNT] = {
  { "Bright",  "Brightness",      KIND_NUMBER },
  { "Auto",    "Auto brightness", KIND_TOGGLE },
  { "Zone",    "Timezone",        KIND_ZONE   },
  { "DST",     "Daylight saving", KIND_CHOICE },
  { "Hotspot", "Config hotspot",  KIND_ACTION },
  { "Time",    "Time and date",   KIND_DATETIME },
};
static MenuItem menuFocus = ITEM_BRIGHT;

// Fields of the time editor, in the order they are set. The date is part of
// it because the daylight-saving rule depends on it.
enum TimeField : uint8_t { TF_HOUR, TF_MINUTE, TF_YEAR, TF_MONTH, TF_DAY, TF_COUNT };
const char *const TIME_FIELD_NAMES[TF_COUNT] = { "hour", "min", "year", "month", "day" };
const int16_t TIME_YEAR_MIN = 2024, TIME_YEAR_MAX = 2099;   // what the DS3231 can hold, and plausible

// Options of the daylight-saving editor, in the order shown.
const uint8_t     DST_OPTIONS[]     = { DST_AUTO, DST_SUMMER, DST_WINTER };
const char *const DST_OPTION_NAMES[] = { "Auto", "Summer", "Winter" };
const uint8_t     DST_OPTION_COUNT  = sizeof(DST_OPTIONS) / sizeof(DST_OPTIONS[0]);

// The open edit. A value being edited lives in RAM; flash is written on ENTER
// only, and only when the value differs from what flash holds. Leaving any
// other way - BACK, HOME, the timeout - puts the stored value back.
static MenuItem      editItem       = ITEM_BRIGHT;
static uint8_t       editIndex      = 0;   // zone row or option on show
static uint8_t       editIndexSaved = 0;   // the one flash holds
static unsigned long editShownAt    = 0;   // millis() the value on show changed, where the marquee starts
static unsigned long editDrawnAt    = 0;   // millis() of the last editor frame, paces the marquee
// settings.brightness as flash holds it, while the Bright editor changes it
// live (the panel itself is the preview).
static uint8_t       brightSaved    = 0;
// The time editor: the local time and date being set, and the field on show.
// Nothing changes before ENTER on the last field.
static int16_t       editTime[TF_COUNT];
static uint8_t       editTimeField  = TF_HOUR;

// Slide between the menu list and an editor: the old picture moves out to one
// side while the new one comes in from the other, deeper to the left. Both are
// held as the panel's raw framebuffer (64x32, unrotated): the matrix is itself
// a GFXcanvas16, so a picture is a plain copy of its buffer.
const uint8_t       SLIDE_FRAMES   = 8;
const unsigned long SLIDE_FRAME_MS = 12;    // on top of the panel refresh show() waits for
static uint16_t uiFrame[64 * 32];          // the last menu picture, without the press indicator
static uint8_t  uiFrameRotation = 0xFF;    // the rotation it was drawn in; 0xFF = none yet
static uint16_t slideTo[64 * 32];          // the new picture while a slide plays
int8_t   slideDir = 0;              // the slide the next menu picture starts with: +1 deeper, -1 back, 0 none

// Banner: what it says and in which colour comes from the function that opened
// it. A time banner shows the time the clock shows now, read when it is drawn.
// A hint is text as well, but only explains what comes next, so an input on it
// is not used up (handleInput()). An alert's banner shows the alert's icon in
// its colours, framed in the colour of its level; a running one, a notice's
// text running through. Both are steps of the notices on show (noticeSteps),
// and last as long as their step.
// (BannerKind: menu.h; an alert's or a card's icon: notices.h.)
BannerKind    bannerKind = BANNER_TEXT;
static char          bannerText[2][12] = { "", "" };   // one or two lines; an empty second one is not drawn
static uint8_t       bannerR = 255, bannerG = 255, bannerB = 255;
unsigned long bannerAt = 0;      // millis() the banner came up

// Keep the brightness the Bright editor has set: written to flash if it
// differs from what flash holds.
static void brightnessKeep() {
  if (settings.brightness == brightSaved) { return; }
  brightSaved = settings.brightness;
  saveSettings();
  Console.print("Brightness set to "); Console.println(settings.brightness);
}

/* ======================================================================
   Banners: a short confirmation over the screen it confirms
   ====================================================================== */

// Daylight-saving colours, used wherever the time or the mode is confirmed:
// orange while summer time is in effect, ice-blue otherwise.
static void dstColour(uint8_t &r, uint8_t &g, uint8_t &b) {
  if (dstInEffect()) { r = 255; g = 165; b = 0; }
  else               { r = 120; g = 200; b = 255; }
}

// Put a banner up, or replace the one that is up, for BANNER_MS.
void bannerOpen() {
  bannerAt = millisNow;
  if (screen == SCREEN_BANNER) { panelDirty = true; return; }
  openScreen(SCREEN_BANNER);
}

// A banner with one or two lines of text (second = nullptr for one) in a
// colour, all chosen by the function confirming its change.
void showBanner(const char *first, const char *second, uint8_t r, uint8_t g, uint8_t b) {
  const char *lines[2] = { first, second ? second : "" };
  for (uint8_t i = 0; i < 2; i++) {
    strncpy(bannerText[i], lines[i], sizeof(bannerText[i]) - 1);
    bannerText[i][sizeof(bannerText[i]) - 1] = '\0';
  }
  bannerR = r; bannerG = g; bannerB = b;
  bannerKind = BANNER_TEXT;
  bannerOpen();
}

// FN_SHOW_HINTS: a hand has come near the gesture sensor, on the face. What a
// swipe does there, for BANNER_MS; the swipe itself closes it and goes on to
// the face.
void showHints() {
  // The gesture to show: the first found that opens the menu, else the first
  // that does anything; nothing to say, no banner.
  for (uint8_t pass = 0; pass < 2; pass++) {
    for (uint8_t i = 0; i < FACE_EVENT_COUNT; i++) {
      const FaceEvent &e = FACE_EVENTS[i];
      const FaceAction &a = FACE_ACTIONS[faceKeys[i]];
      if (!e.word[0] || !faceEventFitted(e.event) || a.function == FN_NONE) { continue; }
      if (pass == 0 && a.function != FN_OPEN_MENU) { continue; }
      showBanner(e.word, a.word, 110, 110, 110);
      bannerKind = BANNER_HINT;
      return;
    }
  }
}

// A banner with the time the clock now shows and its offset from UTC, after a
// change of zone or daylight saving.
static void showTimeBanner() {
  bannerKind = BANNER_TIME;
  bannerOpen();
}

void drawBanner() {
  if (bannerKind == BANNER_ALERT) { drawAlertBanner(); return; }
  if (bannerKind == BANNER_RUN)   { drawRunBanner(); return; }
  if (bannerKind == BANNER_CARD)  { drawCardBanner(); return; }
  if (bannerKind != BANNER_TIME) {
    drawCenteredLines(bannerText[0], bannerText[1][0] ? bannerText[1] : nullptr,
                      scaledColorVisible(bannerR, bannerG, bannerB));
    return;
  }
  // Read now, not from localTime: that was computed before this loop's input
  // changed the zone.
  long offset = tzTotalOffset();
  char hhmm[6], zone[10];
  formatClockTime(hhmm, sizeof(hhmm), clockNow() + offset);
  formatOffset(zone, sizeof(zone), offset);
  uint8_t r, g, b;
  dstColour(r, g, b);
  drawCenteredLines(hhmm, zone, scaledColorVisible(r, g, b));
}

// "HH:MM" of a local time, or "--:--" while the clock has no time yet.
static void formatClockTime(char *buf, size_t size, time_t local) {
  if (!clockIsSet()) { snprintf(buf, size, "--:--"); return; }
  snprintf(buf, size, "%02d:%02d", clockHour(local), clockMinute(local));
}

/* ======================================================================
   On-screen menu: drawing (display class S, 64x32 and 32x64)

   Menus redraw on input only (panelDirty), so they cost nothing while they
   wait. Text: TomThumb (3x5) for titles, values and the portrait rows, the
   built-in 5x7 font for the landscape rows. One accent colour marks the focus
   and the focused value; secondary text is dimmed. Every colour keeps the
   FacePaint::VISIBLE_MIN floor, so the menu stays readable at night.
   ====================================================================== */

uint16_t uiInk()    { return scaledColorVisible(255, 255, 255); }
uint16_t uiQuiet()  { return scaledColorVisible(110, 110, 110); }
static uint16_t uiAccent() { return scaledColorVisible(0, 170, 255); }

// TomThumb text, placed by the top of its capitals (5 rows from `top`).
void uiTiny(int16_t x, int16_t top, const char *s, uint16_t colour) {
  matrix.setFont(&TomThumb);
  matrix.setTextSize(1);
  matrix.setTextColor(colour);
  matrix.setCursor(x, top + 5);
  matrix.print(s);
}

// Built-in 5x7 text, placed by its top row.
static void uiSmall(int16_t x, int16_t top, const char *s, uint16_t colour) {
  matrix.setFont(nullptr);
  matrix.setTextSize(1);
  matrix.setTextColor(colour);
  matrix.setCursor(x, top);
  matrix.print(s);
}

// Pixel width of a text in a font and size, from its left edge to its right edge.
static int16_t uiTextWidth(const char *s, const GFXfont *font, uint8_t size) {
  matrix.setFont(font);
  matrix.setTextSize(size);
  int16_t x1, y1; uint16_t w, h;
  matrix.getTextBounds(s, 0, 0, &x1, &y1, &w, &h);
  return (int16_t)w + x1;
}

// TomThumb text ending at column `right`.
static void uiTinyRight(int16_t right, int16_t top, const char *s, uint16_t colour) {
  uiTiny(right + 1 - uiTextWidth(s, &TomThumb, 1), top, s, colour);
}

// A list with a title: the menu itself, and the options of a choice editor.
// The focused row sits on an accent bar; `value` is shown in the title row
// in landscape and at the foot in portrait.
static void drawList(const char *title, const char *const *labels, uint8_t count, uint8_t focus, const char *value) {
  uiTiny(1, 1, title, uiQuiet());
  if (screenIsLandscape()) {
    // 64x32: the title row and three 8 px rows of 5x7 text, scrolled so the
    // focus stays visible, with a scrollbar in the last column.
    const uint8_t rows = 3;
    uiTinyRight(61, 1, value, uiAccent());
    uint8_t first = 0;
    if (count > rows) { first = (focus == 0) ? 0 : (uint8_t)min((int)focus - 1, (int)count - rows); }
    for (uint8_t r = 0; r < rows && first + r < count; r++) {
      uint8_t i = first + r;
      int16_t top = 8 + 8 * r;
      if (i == focus) { matrix.fillRect(0, top, 63, 8, uiAccent()); }
      uiSmall(1, top, labels[i], (i == focus) ? 0 : uiInk());
    }
    if (count > rows) {
      int16_t trackH = 24;
      int16_t thumbH = trackH * rows / count;
      matrix.drawFastVLine(63, 8 + trackH * first / count, thumbH, uiQuiet());
    }
  } else {
    // 32x64: all rows in TomThumb, 8 px apart, and the value at the foot.
    for (uint8_t i = 0; i < count; i++) {
      int16_t top = 8 + 8 * i;
      if (i == focus) { matrix.fillRect(0, top, matrix.width(), 7, uiAccent()); }
      uiTiny(1, top + 1, labels[i], (i == focus) ? 0 : uiInk());
    }
    uiTiny(1, 57, value, uiAccent());
  }
}

// Whether a menu item is offered on this clock.
static bool menuItemShown(MenuItem item) {
  if (item == ITEM_AUTO) { return luxOK; }   // nothing to switch without a light sensor
  if (item == ITEM_TIME) { return rtcOK; }   // without a real-time clock the time is set on the config page
  return true;
}

// The value a menu item shows in the list.
static void menuItemValue(MenuItem item, char *buf, size_t size) {
  switch (item) {
    case ITEM_BRIGHT:
      if (brightnessIsTrim()) { snprintf(buf, size, "%+d", brightnessTrim(settings.brightness)); }
      else { snprintf(buf, size, "%d%%", ((brightnessLevel(settings.brightness) + 1) * 100 + BRIGHT_LEVELS / 2) / BRIGHT_LEVELS); }
      break;
    case ITEM_AUTO:  snprintf(buf, size, "%s", settings.autoBright ? "On" : "Off"); break;
    case ITEM_ZONE:  formatOffset(buf, size, settings.tzOffset); break;
    case ITEM_DST:   snprintf(buf, size, "%s", DST_OPTION_NAMES[dstOptionIndex(settings.dst)]); break;
    case ITEM_TIME:  formatClockTime(buf, size, clockNow() + tzTotalOffset()); break;
    default:         buf[0] = '\0'; break;
  }
}

void drawMenu() {
  matrix.fillScreen(0);
  const char *labels[ITEM_COUNT];
  uint8_t count = 0, focus = 0;
  for (uint8_t i = 0; i < ITEM_COUNT; i++) {
    if (!menuItemShown((MenuItem)i)) { continue; }
    if (i == menuFocus) { focus = count; }
    labels[count++] = MENU_ITEMS[i].label;
  }
  char value[12];
  menuItemValue(menuFocus, value, sizeof(value));
  drawList("MENU", labels, count, focus, value);
  presentUi();
}

// Put the menu picture just drawn on the panel, with the press indicator on
// top - on the S3 sliding in from the previous one when the screen changed
// between the list and an editor.
static void presentUi() {
  // A rotation in between would slide two pictures of different shape.
  if (slideDir != 0 && uiFrameRotation == matrix.getRotation()) { playSlide(slideDir); }
  slideDir = 0;
  memcpy(uiFrame, matrix.getBuffer(), sizeof(uiFrame));
  uiFrameRotation = matrix.getRotation();
  drawFeedbackIndicator();
  matrix.show();
  panelDirty = false;
}

// Index into the panel's raw framebuffer of the pixel at (x, y) in the current
// rotation - the mapping GFXcanvas16::drawPixel() uses.
static uint16_t rawPixelIndex(int16_t x, int16_t y) {
  uint8_t rot  = matrix.getRotation();
  int16_t rawW = (rot & 1) ? matrix.height() : matrix.width();
  int16_t rawH = (rot & 1) ? matrix.width()  : matrix.height();
  int16_t t;
  switch (rot) {
    case 1: t = x; x = rawW - 1 - y; y = t; break;
    case 2: x = rawW - 1 - x; y = rawH - 1 - y; break;
    case 3: t = x; x = y; y = rawH - 1 - t; break;
  }
  return (uint16_t)(x + y * rawW);
}

// Slide from uiFrame (the old picture) to the one now in the framebuffer.
// Going deeper (dir +1) both move left and the new one comes in from the
// right; going back the other way round. Eased out, so it settles softly. It
// blocks for about SLIDE_FRAMES refreshes plus SLIDE_FRAME_MS each, some
// 130 ms, and leaves the new picture in the framebuffer.
static void playSlide(int8_t dir) {
  memcpy(slideTo, matrix.getBuffer(), sizeof(slideTo));
  uint16_t *panel = matrix.getBuffer();
  int16_t w = matrix.width(), h = matrix.height();
  for (uint8_t f = 1; f < SLIDE_FRAMES; f++) {
    float t = (float)f / SLIDE_FRAMES;
    int16_t shift = (int16_t)lroundf(w * (1.0f - (1.0f - t) * (1.0f - t)));
    for (int16_t y = 0; y < h; y++) {
      for (int16_t x = 0; x < w; x++) {
        int16_t sx = (dir > 0) ? x + shift : x - shift;   // where this pixel comes from
        uint16_t c;
        if (sx >= 0 && sx < w) { c = uiFrame[rawPixelIndex(sx, y)]; }
        else                   { c = slideTo[rawPixelIndex((dir > 0) ? sx - w : sx + w, y)]; }
        panel[rawPixelIndex(x, y)] = c;
      }
    }
    drawFeedbackIndicator();
    matrix.show();
    delay(SLIDE_FRAME_MS);
  }
  memcpy(panel, slideTo, sizeof(slideTo));
}

// The brightness scale, one pixel tall across the panel at row y. Manual: a
// bar from the left as long as the level. Auto: a bar from the middle (the
// sensor's own value) to the trim, left darker, right brighter.
static void drawBrightnessScale(int16_t y) {
  int16_t w = matrix.width();
  matrix.drawFastHLine(0, y, w, uiQuiet());
  if (brightnessIsTrim()) {
    int16_t mid = w / 2;
    int16_t len = (int16_t)((long)brightnessTrim(settings.brightness) * mid / BRIGHT_TRIM_MAX);
    if (len > 0) { matrix.drawFastHLine(mid, y, len, uiAccent()); }
    if (len < 0) { matrix.drawFastHLine(mid + len, y, -len, uiAccent()); }
    matrix.drawPixel(mid, y, uiInk());
  } else {
    int16_t len = (int16_t)((long)(brightnessLevel(settings.brightness) + 1) * w / BRIGHT_LEVELS);
    matrix.drawFastHLine(0, y, len, uiAccent());
  }
}

// Editor of a number (Bright): the value at twice the size, the scale under it,
// and "auto" when the value is a trim around the light sensor.
static void drawBrightEditor() {
  char value[8];
  menuItemValue(ITEM_BRIGHT, value, sizeof(value));
  bool land = screenIsLandscape();
  uiTiny(1, 1, MENU_ITEMS[ITEM_BRIGHT].label, uiQuiet());
  if (brightnessIsTrim()) {
    if (land) { uiTinyRight(62, 1, "auto", uiQuiet()); } else { uiTiny(1, 57, "auto", uiQuiet()); }
  }
  int16_t valueTop = land ? 11 : 22;
  matrix.setFont(&TomThumb);
  matrix.setTextSize(2);
  matrix.setTextColor(uiInk());
  matrix.setCursor((matrix.width() - uiTextWidth(value, &TomThumb, 2)) / 2, valueTop + 10);
  matrix.print(value);
  matrix.setTextSize(1);
  drawBrightnessScale(land ? 27 : 38);
}

// The time a zone row and daylight-saving mode would show now, as "HH:MM".
static void formatZoneTime(char *buf, size_t size, uint8_t zone, uint8_t dstMode) {
  time_t utc = clockNow();
  bool summer = (dstMode == DST_AUTO) ? dstActiveAt(TZONES[zone].rule, utc) : (dstMode == DST_SUMMER);
  formatClockTime(buf, size, utc + TZONES[zone].off + (summer ? 3600 : 0));
}

// The place names of a zone row, without the "(UTC+01:00) " in front.
static const char *zoneCity(uint8_t zone) {
  const char *s = strstr(TZONES[zone].label, ") ");
  return s ? s + 2 : TZONES[zone].label;
}

// Marquee for a text wider than its box: it stands at the start, moves left a
// pixel per step until its end is in view, stands again, and starts over.
const unsigned long MARQUEE_STEP_MS  = 80;
const unsigned long MARQUEE_PAUSE_MS = 1200;
static int16_t marqueeShift(int16_t textW, int16_t boxW, unsigned long since) {
  if (textW <= boxW) { return 0; }
  unsigned long travel = (unsigned long)(textW - boxW);
  unsigned long t = (millisNow - since) % (2 * MARQUEE_PAUSE_MS + travel * MARQUEE_STEP_MS);
  if (t < MARQUEE_PAUSE_MS) { return 0; }
  t -= MARQUEE_PAUSE_MS;
  return (int16_t)min(t / MARQUEE_STEP_MS, travel);
}

// Whether the zone row's place names are too wide for the panel.
static bool zoneCityScrolls() {
  return uiTextWidth(zoneCity(editIndex), &TomThumb, 1) > matrix.width() - 2;
}

// Editor of the zone: the offset, the time this zone would show now, and its
// place names, scrolled when they do not fit.
static void drawZoneEditor() {
  char offset[10], hhmm[6];
  formatOffset(offset, sizeof(offset), TZONES[editIndex].off);
  formatZoneTime(hhmm, sizeof(hhmm), editIndex, settings.dst);
  const char *city = zoneCity(editIndex);
  int16_t shift = marqueeShift(uiTextWidth(city, &TomThumb, 1), matrix.width() - 2, editShownAt);
  uiTiny(1, 1, MENU_ITEMS[ITEM_ZONE].label, uiQuiet());
  if (screenIsLandscape()) {
    uiTinyRight(62, 1, hhmm, uiAccent());
    uiSmall(1, 10, offset, uiInk());
    uiTiny(1 - shift, 22, city, uiInk());
  } else {
    // "UTC+5:30" is 48 px in 5x7, so the 32 px column gets the offset in
    // TomThumb and the time in 5x7.
    uiTiny(1, 10, offset, uiInk());
    uiSmall(1, 19, hhmm, uiAccent());
    uiTiny(1 - shift, 31, city, uiInk());
  }
}

// Editor of a choice (DST): the options as a list, with the time the focused
// one would show.
static void drawDstEditor() {
  char hhmm[6];
  formatZoneTime(hhmm, sizeof(hhmm), zoneIndex(settings.tzOffset), DST_OPTIONS[editIndex]);
  drawList(MENU_ITEMS[ITEM_DST].label, DST_OPTION_NAMES, DST_OPTION_COUNT, editIndex, hhmm);
}

// One field of the time editor in 5x7 text: the one being set in the accent
// colour, the others in ink. Returns the column after it.
static int16_t drawTimeFieldText(int16_t x, int16_t top, const char *text, bool focused) {
  uiSmall(x, top, text, focused ? uiAccent() : uiInk());
  return x + 6 * (int16_t)strlen(text);
}

// Editor of the time and date: HH:MM, then the date, the field being set in
// the accent colour and named in the title row (landscape) or at the foot
// (portrait).
static void drawTimeEditor() {
  char f[TF_COUNT][7];   // room for any int16_t, though every field is 2 or 4 digits
  snprintf(f[TF_HOUR],   sizeof(f[0]), "%02d", editTime[TF_HOUR]);
  snprintf(f[TF_MINUTE], sizeof(f[0]), "%02d", editTime[TF_MINUTE]);
  snprintf(f[TF_YEAR],   sizeof(f[0]), "%04d", editTime[TF_YEAR]);
  snprintf(f[TF_MONTH],  sizeof(f[0]), "%02d", editTime[TF_MONTH]);
  snprintf(f[TF_DAY],    sizeof(f[0]), "%02d", editTime[TF_DAY]);
  uiTiny(1, 1, MENU_ITEMS[ITEM_TIME].label, uiQuiet());
  bool land = screenIsLandscape();
  if (land) { uiTinyRight(62, 1, TIME_FIELD_NAMES[editTimeField], uiAccent()); }
  else      { uiTiny(1, 57, TIME_FIELD_NAMES[editTimeField], uiAccent()); }
  // Landscape: "HH:MM" and "YYYY-MM-DD" (60 px) in two rows; portrait (32 px
  // wide): "HH:MM", then "YYYY" and "MM-DD".
  int16_t x = drawTimeFieldText(1, land ? 10 : 12, f[TF_HOUR], editTimeField == TF_HOUR);
  x = drawTimeFieldText(x, land ? 10 : 12, ":", false);
  drawTimeFieldText(x, land ? 10 : 12, f[TF_MINUTE], editTimeField == TF_MINUTE);
  x = drawTimeFieldText(1, land ? 21 : 26, f[TF_YEAR], editTimeField == TF_YEAR);
  int16_t dateTop = land ? 21 : 36;
  if (land) { x = drawTimeFieldText(x, dateTop, "-", false); } else { x = 1; }
  x = drawTimeFieldText(x, dateTop, f[TF_MONTH], editTimeField == TF_MONTH);
  x = drawTimeFieldText(x, dateTop, "-", false);
  drawTimeFieldText(x, dateTop, f[TF_DAY], editTimeField == TF_DAY);
}

// Whether the editor has to be redrawn without an input: a scrolling name, at
// its own pace.
bool editorMoving() {
  if (editItem != ITEM_ZONE || !zoneCityScrolls()) { return false; }
  return millisNow - editDrawnAt >= MARQUEE_STEP_MS;
}

void drawEditor() {
  matrix.fillScreen(0);
  switch (editItem) {
    case ITEM_BRIGHT: drawBrightEditor(); break;
    case ITEM_ZONE:   drawZoneEditor();   break;
    case ITEM_DST:    drawDstEditor();    break;
    case ITEM_TIME:   drawTimeEditor();   break;
    default:          break;
  }
  presentUi();
  editDrawnAt = millisNow;
}

/* ======================================================================
   On-screen menu: what the functions do there
   ====================================================================== */

// FN_OPEN_MENU: the list, on top of the face, focus on the first item.
void menuOpen() {
  menuFocus = ITEM_BRIGHT;
  openScreen(SCREEN_MENU);
}

// PREV / NEXT / ENTER / BACK / HOME mean what the screen on top makes of them.
void navigate(InputFunction fn, uint8_t steps) {
  switch (screen) {
    case SCREEN_MENU:
      menuNavigate(fn);
      break;
    case SCREEN_EDITOR:
      editorNavigate(fn, steps);
      break;
    case SCREEN_HOTSPOT_INFO:
    case SCREEN_HOTSPOT_PREVIEW:
      if (fn == FN_BACK || fn == FN_HOME) { stopAPMode(); }   // closes the hotspot, back to the face
      break;
    default:
      break;
  }
}

// The next item shown in the list from `from`, in direction dir, wrapping
// round at either end.
static MenuItem menuStep(MenuItem from, int8_t dir) {
  uint8_t i = from;
  do { i = (uint8_t)((i + ITEM_COUNT + dir) % ITEM_COUNT); } while (!menuItemShown((MenuItem)i));
  return (MenuItem)i;
}

static void menuNavigate(InputFunction fn) {
  switch (fn) {
    case FN_PREV:  menuFocus = menuStep(menuFocus, -1); panelDirty = true; break;
    case FN_NEXT:  menuFocus = menuStep(menuFocus, +1); panelDirty = true; break;
    case FN_ENTER: menuEnter(menuFocus); break;
    case FN_BACK:  closeScreen(); break;            // BACK at the top leaves the menu
    case FN_HOME:  setScreen(SCREEN_FACE); break;
    default:       break;
  }
}

// ENTER on a row: a toggle flips in place, an action runs, everything else
// opens its editor.
static void menuEnter(MenuItem item) {
  switch (MENU_ITEMS[item].kind) {
    case KIND_TOGGLE: menuToggle(item); break;
    case KIND_ACTION: menuAction(item); break;
    default:          editorOpen(item); break;
  }
}

// A toggle needs no confirmation: the row shows its new value.
static void menuToggle(MenuItem item) {
  if (item == ITEM_AUTO) { toggleAutoBright(); }
  panelDirty = true;
}

static void menuAction(MenuItem item) {
  if (item == ITEM_HOTSPOT) { startAPMode(); }   // its screen replaces the menu
}

// Row of TZONES with this base offset, or the nearest one.
static uint8_t zoneIndex(int32_t offset) {
  uint8_t best = 0;
  for (uint8_t i = 0; i < ZONE_COUNT; i++) {
    if (labs((long)TZONES[i].off - offset) < labs((long)TZONES[best].off - offset)) { best = i; }
  }
  return best;
}

// Position of a daylight-saving mode in DST_OPTIONS.
static uint8_t dstOptionIndex(uint8_t mode) {
  for (uint8_t i = 0; i < DST_OPTION_COUNT; i++) {
    if (DST_OPTIONS[i] == mode) { return i; }
  }
  return 0;
}

static void editorOpen(MenuItem item) {
  editItem = item;
  switch (item) {
    case ITEM_BRIGHT: brightSaved = settings.brightness; break;
    case ITEM_ZONE:   editIndex = zoneIndex(settings.tzOffset); break;
    case ITEM_DST:    editIndex = dstOptionIndex(settings.dst); break;
    case ITEM_TIME:   timeEditorOpen(); break;
    default:          break;
  }
  editIndexSaved = editIndex;
  editShownAt = millisNow;
  openScreen(SCREEN_EDITOR);
}

// The time editor starts from the time the clock shows, or, while it has
// none, from noon on the day the firmware was built.
static void timeEditorOpen() {
  editTimeField = TF_HOUR;
  if (clockIsSet()) {
    time_t local = clockNow() + tzTotalOffset();
    struct tm t;
    gmtime_r(&local, &t);
    editTime[TF_HOUR] = t.tm_hour;  editTime[TF_MINUTE] = t.tm_min;
    editTime[TF_YEAR] = t.tm_year + 1900;  editTime[TF_MONTH] = t.tm_mon + 1;  editTime[TF_DAY] = t.tm_mday;
  } else {
    // __DATE__ is "Mmm dd yyyy".
    static const char MONTHS[] = "JanFebMarAprMayJunJulAugSepOctNovDec";
    const char *built = __DATE__;
    char mon[4] = { built[0], built[1], built[2], '\0' };
    const char *at = strstr(MONTHS, mon);
    editTime[TF_HOUR] = 12;  editTime[TF_MINUTE] = 0;
    editTime[TF_YEAR] = (int16_t)atoi(built + 7);
    editTime[TF_MONTH] = at ? (int16_t)((at - MONTHS) / 3 + 1) : 1;
    editTime[TF_DAY] = (int16_t)atoi(built + 4);
  }
  editTime[TF_YEAR] = constrain(editTime[TF_YEAR], TIME_YEAR_MIN, TIME_YEAR_MAX);
}

// Days in a month of the Gregorian calendar.
static int16_t daysInMonth(int16_t year, int16_t month) {
  static const uint8_t DAYS[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
  bool leap = (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
  return (month == 2 && leap) ? 29 : DAYS[(month - 1) % 12];
}

// PREV / NEXT on a time field: up is later, as on a scale. Hours, minutes,
// months and days go round; the year stops at its ends. A day past the end of
// a shorter month moves back to its last.
static void timeEditorStep(int delta) {
  int16_t &v = editTime[editTimeField];
  switch (editTimeField) {
    case TF_HOUR:   v = (int16_t)(((v + delta) % 24 + 24) % 24); break;
    case TF_MINUTE: v = (int16_t)(((v + delta) % 60 + 60) % 60); break;
    case TF_YEAR:   v = (int16_t)constrain(v + delta, TIME_YEAR_MIN, TIME_YEAR_MAX); break;
    case TF_MONTH:  v = (int16_t)(((v - 1 + delta) % 12 + 12) % 12 + 1); break;
    case TF_DAY: {
      int16_t dim = daysInMonth(editTime[TF_YEAR], editTime[TF_MONTH]);
      v = (int16_t)(((v - 1 + delta) % dim + dim) % dim + 1);
      break;
    }
    default: break;
  }
  int16_t dim = daysInMonth(editTime[TF_YEAR], editTime[TF_MONTH]);
  if (editTime[TF_DAY] > dim) { editTime[TF_DAY] = dim; }
}

// ENTER on the last field: the local time entered, with the seconds at zero,
// becomes the clock's time (and the real-time clock's), converted to UTC with
// the zone and its daylight-saving rule. A banner shows the time the clock now
// shows.
static void timeEditorCommit() {
  time_t local = (time_t)daysFromCivil(editTime[TF_YEAR], editTime[TF_MONTH], editTime[TF_DAY]) * 86400
               + editTime[TF_HOUR] * 3600L + editTime[TF_MINUTE] * 60L;
  setClockTo(localToUtc(local), TIME_MANUAL);
  Console.println("Time set in the menu");
  closeScreen();
  showTimeBanner();
}

static void editorNavigate(InputFunction fn, uint8_t steps) {
  if (editItem == ITEM_TIME) {
    switch (fn) {
      case FN_ENTER:
        if (editTimeField + 1 < TF_COUNT) { editTimeField++; panelDirty = true; } else { timeEditorCommit(); }
        return;
      case FN_BACK:
        if (editTimeField > 0) { editTimeField--; panelDirty = true; } else { closeScreen(); }
        return;
      default:
        break;   // PREV, NEXT and HOME as in every editor
    }
  }
  switch (fn) {
    case FN_PREV:  editorStep(-(int)steps); break;
    case FN_NEXT:  editorStep(steps); break;
    case FN_ENTER: editorCommit(); break;
    case FN_BACK:  closeScreen(); break;            // leaving reverts, see leaveScreen()
    case FN_HOME:  setScreen(SCREEN_FACE); break;
    default:       break;
  }
}

// PREV / NEXT in an editor. Lists (zones, options) move with the list:
// PREV goes up the list, towards the west for zones. A scale moves the other
// way, so that UP - mapped to PREV - is brighter.
static void editorStep(int delta) {
  switch (editItem) {
    case ITEM_BRIGHT: brightnessStep(-delta); break;   // applied at once: the panel is the preview
    case ITEM_ZONE:   editIndex = (uint8_t)constrain((int)editIndex + delta, 0, (int)ZONE_COUNT - 1); break;
    case ITEM_DST:    editIndex = (uint8_t)constrain((int)editIndex + delta, 0, (int)DST_OPTION_COUNT - 1); break;
    case ITEM_TIME:   timeEditorStep(-delta); break;   // UP (PREV) is later
    default:          break;
  }
  editShownAt = millisNow;
  panelDirty = true;
}

// ENTER in an editor: save what is on show if it differs from what flash
// holds, close the editor, and confirm a change the menu list does not show -
// a new zone or daylight-saving mode changes the time on the clock.
static void editorCommit() {
  bool changed = (editIndex != editIndexSaved);
  editIndexSaved = editIndex;   // whatever happens now, there is nothing to revert
  switch (editItem) {
    case ITEM_BRIGHT:
      brightnessKeep();
      closeScreen();
      break;
    case ITEM_ZONE:
      closeScreen();
      if (!changed) { break; }
      settings.tzOffset = TZONES[editIndex].off;
      setDstMode(settings.dst);   // Auto follows the new zone's rule at once
      saveSettings();
      Console.print("Timezone -> "); Console.println(settings.tzOffset);
      showTimeBanner();
      break;
    case ITEM_DST:
      closeScreen();
      if (!changed) { break; }
      setDstMode(DST_OPTIONS[editIndex]);
      saveSettings();
      Console.print("DST mode -> "); Console.println(settings.dst);
      showTimeBanner();
      break;
    default:
      closeScreen();
      break;
  }
}

// What an editor did without ENTER goes back. Only the brightness is applied
// while it is being edited; zone and daylight saving only ever change on ENTER.
void editorRevert() {
  if (editItem == ITEM_BRIGHT) { settings.brightness = brightSaved; }
}


// FN_CYCLE_DST (Classic: 1x on the face): cycle the daylight-saving mode
// automatic -> summer -> winter -> automatic; the time on the panel follows,
// and a banner names the new mode.
void cycleDstMode() {
  uint8_t next = (settings.dst == DST_AUTO)   ? DST_SUMMER
               : (settings.dst == DST_SUMMER) ? DST_WINTER : DST_AUTO;
  setDstMode(next);
  saveSettings();
  uint8_t r, g, b;
  dstColour(r, g, b);
  showBanner((settings.dst == DST_AUTO) ? "auto" : (settings.dst == DST_SUMMER) ? "summer" : "winter", nullptr, r, g, b);
  Console.print("DST mode -> "); Console.println(settings.dst);
}

// Toggle the BH1750 auto-brightness on/off and save it.
static void toggleAutoBright() {
  settings.autoBright = settings.autoBright ? 0 : 1;
  saveSettings();
  Console.print("Auto-brightness -> "); Console.println(settings.autoBright);
}

// FN_TOGGLE_AUTO (Classic: 2x on the face), where nothing on the screen shows
// the mode, so a banner says it. Without a light sensor there is nothing to
// switch, and the banner says that instead.
void toggleAutoBrightWithBanner() {
  if (!luxOK) { showBanner("no", "sensor", 255, 255, 255); return; }
  toggleAutoBright();
  showBanner(settings.autoBright ? "auto" : "manual", nullptr, 255, 255, 255);
}
