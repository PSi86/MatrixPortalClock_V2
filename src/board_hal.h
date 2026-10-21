/* ----------------------------------------------------------------------
   Hardware abstraction for the supported clock boards:

     Adafruit MatrixPortal S3   ESP32-S3, WiFi on the main MCU
     Waveshare ESP32-S3-RGB-Matrix, the wall clock: ESP32-S3 with six 64x64
                                panels as one 192x128 wall

   Everything that genuinely differs between them lives here: matrix and button
   pins, the WiFi / NTP calls, the settings storage backend and the reset
   instruction. The sketch itself stays board independent.
   ------------------------------------------------------------------------- */
#pragma once

#include <Arduino.h>
#include "console.h"   // the console: Serial in debug and test builds, nothing in release

/* ======================================================================
   Board detection
   ====================================================================== */
// The MatrixPortal S3 and the wall clock run the same ESP32-S3 code: WiFi,
// settings, console, panel driver. Only their pins and panels differ. (The
// MatrixPortal M4 was dropped on 2026-10-21; the last commit that builds for
// it is ae18faf, README "MatrixPortal M4".)
#if defined(ARDUINO_ADAFRUIT_MATRIXPORTAL_ESP32S3)
  #define BOARD_MATRIXPORTAL_S3 1
  #define BOARD_WAVESHARE_WALL  0
#elif defined(ARDUINO_WAVESHARE_ESP32S3_RGB_MATRIX)
  #define BOARD_MATRIXPORTAL_S3 0
  #define BOARD_WAVESHARE_WALL  1
#else
  #error "Unsupported board - build env:adafruit_matrixportal_s3 or env:waveshare_wallclock"
#endif

// The board and its panel in LEDs, as the web app and Home Assistant name them.
#if BOARD_WAVESHARE_WALL
  #define BOARD_NAME "Waveshare ESP32-S3-RGB-Matrix"
  #define BOARD_PANEL_W 192
  #define BOARD_PANEL_H 128
#else
  #define BOARD_NAME "MatrixPortal S3"
  #define BOARD_PANEL_W 64
  #define BOARD_PANEL_H 32
#endif

/* ======================================================================
   Pins
   ====================================================================== */
#if BOARD_MATRIXPORTAL_S3

  // HUB75 pins of the MatrixPortal S3 (R1 G1 B1 R2 G2 B2; A B C D E). The DMA
  // driver drives them through the ESP32-S3 LCD_CAM peripheral, which MUXes
  // freely, so the order is arbitrary. A 64x32 panel needs no E line.
  #define MATRIX_RGB_PINS   {42, 41, 40, 38, 39, 37}
  #define MATRIX_ADDR_PINS  {45, 36, 48, 35, 21}
  #define MATRIX_CLOCK_PIN  2
  #define MATRIX_LATCH_PIN  47
  #define MATRIX_OE_PIN     14

  // UP and DOWN buttons. CAUTION: D2 is the matrix CLOCK line on this board.
  // Neither button has an external pull-up; pressing pulls the input low,
  // hence INPUT_PULLUP.
  #define UP_BUTTON_PIN     PIN_BUTTON_UP     // GPIO6
  #define DOWN_BUTTON_PIN   PIN_BUTTON_DOWN   // GPIO7
  #define BOARD_BUTTON_COUNT 2

#elif BOARD_WAVESHARE_WALL

  // HUB75 pins of the Waveshare ESP32-S3-RGB-Matrix, from Waveshare's example
  // code (esp32s3-default-pins.hpp) and board support package (config.h),
  // checked against the schematic; the wall clock spike drove six 64x64 panels
  // with them (2026-10-16/17). The 64x64 panels need the E line.
  #define MATRIX_RGB_PINS   {4, 5, 6, 7, 15, 16}
  #define MATRIX_ADDR_PINS  {18, 8, 3, 42, 9}
  #define MATRIX_CLOCK_PIN  41
  #define MATRIX_LATCH_PIN  40
  #define MATRIX_OE_PIN     2

  // No buttons to operate it: the board has RESET and BOOT only, and no GPIO is
  // left for UP and DOWN. Its input is the gesture sensor (decided 2026-10-05).
  #define BOARD_BUTTON_COUNT 0

#endif

#if BOARD_WAVESHARE_WALL
  // No user LED on the wall clock board.
  #define BOARD_HAS_FEEDBACK_LED 0
  // No LIS3DH: the board's own motion sensor is a QMI8658, which gives the
  // knock only (src/imu_qmi8658.h); the wall clock hangs in landscape and does
  // not turn (decided 2026-10-18).
  #define BOARD_HAS_ACCEL 0
  // The rotation the clock draws in without an accelerometer: landscape.
  #define BOARD_DEFAULT_ROTATION 0
#else
  // Button feedback LED: the red user LED on D13.
  #define BOARD_HAS_FEEDBACK_LED 1
  #define FEEDBACK_LED_PIN LED_BUILTIN
  #define BOARD_HAS_ACCEL 1
  // Without an accelerometer the MatrixPortals stay in portrait.
  #define BOARD_DEFAULT_ROTATION 3
#endif

// Onboard LIS3DH accelerometer: same part and same (non-standard) I2C address
// on both MatrixPortals. Wire.begin() picks up each variant's default SDA/SCL,
// on the wall clock the GPIO header's IO45/IO46 (its own variant).
#define ACCEL_I2C_ADDR 0x19

// Gesture sensor (PAJ7620U2, optional, I2C 0x73, e.g. on STEMMA QT): its INT
// line, if it is wired to a free pin - STEMMA QT carries none - for instance
// with -D GESTURE_INT_PIN=A0 in build_flags on a MatrixPortal. With it the
// sensor is read only when the line says a gesture is waiting; without it (-1)
// the sketch asks the sensor over I2C now and then. On the wall clock A0 is
// GPIO1, the microSD card's clock line, and the GPIO header has only IO45 and
// IO46, the I2C bus: there the sensor is asked.
#ifndef GESTURE_INT_PIN
  #define GESTURE_INT_PIN -1
#endif

#if BOARD_WAVESHARE_WALL
#include <Wire.h>
// The wall clock board's own I2C bus (schematic, read on 2026-10-19): IO47 and
// IO48 run at 1.8 V and reach the 3.3 V side through a level shifter
// (NDC7002N, 4.7 kOhm pull-ups on both sides). On it: the PCF85063 real-time
// clock (0x51), the QMI8658 motion sensor (0x6B), the SHTC3 (0x70) and the
// audio codecs ES8311 (0x18) and ES7210 (0x40). It is Wire1; Wire is the GPIO
// header's bus (IO45/IO46) for the clock's own sensors.
#define BOARD_I2C_SDA 47
#define BOARD_I2C_SCL 48
#define BOARD_I2C_HZ  100000

// One lock over a whole transfer on the board's bus - the address written,
// the bytes read and taken out of TwoWire's buffer - for more than one task
// may use it. TwoWire locks only its own calls, and read() not at all.
inline SemaphoreHandle_t &boardBusMutex() {
  static SemaphoreHandle_t m = nullptr;
  return m;
}
struct BoardBusLock {
  BoardBusLock()  { xSemaphoreTake(boardBusMutex(), portMAX_DELAY); }
  ~BoardBusLock() { xSemaphoreGive(boardBusMutex()); }
};
inline bool boardBusBegin() {
  if (!boardBusMutex()) { boardBusMutex() = xSemaphoreCreateMutex(); }
  return Wire1.begin(BOARD_I2C_SDA, BOARD_I2C_SCL, BOARD_I2C_HZ);
}
#endif

/* ======================================================================
   Address of the config access point (and of its web UI), both boards
   ====================================================================== */
// Deliberately NOT a private address (same choice as WLED). Our captive-portal
// DNS answers every lookup with this address, including Android's check host
// connectivitycheck.gstatic.com. If that answer is private (10/8, 172.16/12,
// 192.168/16), Android's NetworkMonitor skips its HTTP check altogether (the
// PRIVATE_IP rule in sendDnsAndHttpProbes()) and reports "connected, no
// internet" instead of "sign in to network", so the portal never pops up.
// The address only exists inside the clock's own isolated AP.
#define AP_IP_ADDR 4, 3, 2, 1

/* ======================================================================
   Panel refresh and frame pacing
   ====================================================================== */
// The panel driver is ESP32-HUB75-MatrixPanel-DMA (PanelCanvas below), 8 bits
// per colour, dimmed through the OE time. Protomatter, which the S3 used
// before, draws into RGB565 and dims by scaling the colours, which costs levels.
#if BOARD_WAVESHARE_WALL
  // The refresh rate of the wall's six panels at 8 bits with the bus at 16 MHz
  // (show() reckons it from the driver at the start), and the rate the pacing assumes.
  #define PANEL_HZ_TYPICAL 160
#else
  // The refresh rate the driver calculates for the 64x32 panel at 8 bits, and
  // the rate the pacing below assumes.
  #define PANEL_HZ_TYPICAL 200
#endif
// Drawing and handing over a frame takes a few ms, less than one panel
// refresh. So the loop runs in step with the panel: show() waits out a refresh
// period after the previous frame, and the animation moves one pixel every
// whole number of refreshes, which keeps every step equally long. A fixed
// millisecond loop drifts against the refresh and shows steps for 1, 2 or 3
// refreshes instead.
#if BOARD_WAVESHARE_WALL
  // No LIS3DH, so no INT1 line of its: the LIS3DH's knock code is not built
  // here. The knock comes from the QMI8658's INT1 on GPIO13 (knockSensorBegin()).
  #define ACCEL_INT_PIN -1
#else
  // The accelerometer's INT1 line. Not in Adafruit's pinout page - taken from
  // the board schematic (Adafruit-MatrixPortal-S3-PCB, "Adafruit MatrixPortal
  // S3.sch"), where the net list has U4 (the LIS3DH) pin INT1 on the net INT
  // together with U3.IO15, U3 being the ESP32-S3. INT2 is on no net at all.
  #define ACCEL_INT_PIN 15
#endif

/* ======================================================================
   Panel driver

   The clock draws into a 16-bit canvas, as it did with Protomatter, and
   show() hands every finished frame to ESP32-HUB75-MatrixPanel-DMA, which
   keeps its own frame buffer at 8 bits per colour. getBuffer(), setRotation()
   and the rest of the canvas behave as before.

   Brightness goes through the colour values, at the full OE time. Dimming
   through the OE time, as the driver does, cuts the LEDs' on-time, and the
   panel's LED drivers need a minimum on-time per colour, longest for blue:
   measured 2026-10-07 with a BH1750 in front of this panel, blue was down to
   a fifth of red at OE brightness 64 and gone at 48, green gone at 28, so
   white turned yellow and then red. With the values turned down instead the
   three stayed together down to about 5 % (blue 0.84 of red at 5 %, 0.95 at
   13 %), so that is as dark as the clock goes (PANEL_BRIGHT_FLOOR).
   The same measurement gave the light of each bit plane per colour
   (PANEL_BIT_LIGHT): the driver's planes are not weighted in powers of two
   (bit 7 gives 46 % instead of 50 %, so 127 is brighter than 128). For each
   colour value and brightness, show() hands the driver the value whose
   measured light comes nearest, from a table per colour (_lut) built when
   the brightness changes. A canvas colour is a share of the panel's light:
   255 is the colour at the current brightness, 128 half of that.
   ====================================================================== */
#include <Adafruit_GFX.h>
#include <ESP32-HUB75-MatrixPanel-I2S-DMA.h>

#if BOARD_WAVESHARE_WALL
#include <soc/lcd_cam_struct.h>   // the bus clock the driver really set

// The wall: six 64x64 panels in one chain, three across and two down. The
// board feeds the bottom right panel, the chain runs left along the bottom
// row and back right along the top row, whose panels hang upside down
// (Peter's cabling, confirmed on the panels 2026-10-17). The driver holds
// the chain as one row of 384 x 64 LEDs: the top row of the wall in its
// first 192, turned round, the bottom row in the second 192 (wallLine()).
const int16_t WALL_W = 192, WALL_H = 128;
const uint8_t WALL_PANEL = 64;           // a panel's edge in LEDs
const uint8_t WALL_CHAIN = 6;
// The clock's 64x32 picture goes onto the wall three times its size, at the
// top; the 192x32 below it is the info band's (concept doc, wall clock B).
const uint8_t WALL_SCALE = 3;
const int16_t WALL_CANVAS_X = 0, WALL_CANVAS_Y = 0;
const int16_t WALL_BAND_Y = 96, WALL_BAND_H = 32;   // the band's rows, below the canvas (showBand())
// The panels' supply gives 7.5 A at most (Peter, 2026-10-18). Measured on its
// 5 V side the same day (bus at 16 MHz, buffer in PSRAM): the wall dark 555 mA,
// one panel in white 564-567 mA more at brightness 160 and 451 mA at 128, so
// the current goes with the brightness and the whole wall white would draw
// about 3.9 A at 160 and 6.0 A at 255. Capped at 230: about 5.4 A at most,
// 72 % of the supply. Brightness is light here (show() turns it into values).
const uint8_t PANEL_BRIGHT_CAP = 230;
// The output's DMA channel priority, 0 (as allocated) to 5 (the ESP32-S3's
// highest), set in begin().
const uint8_t PANEL_DMA_PRIORITY = 5;
// The driver's frame buffer is in PSRAM (SPIRAM_DMA_BUFFER, set by the wall
// clock's envs; the driver is the copy in lib/hub75-dma-wall). At 8 bits one
// buffer for the six panels takes 192 KB: in internal RAM that left 56 KB
// beside it, the hotspot then crashed in the WiFi driver and the home WiFi
// did not join (2026-10-18). In PSRAM the driver holds the bus at 13.3 MHz,
// in this build at 16 MHz (160 Hz, PSRAM_LCD_DIV_NUM), and every change has
// to go from the CPU's cache to the PSRAM before the DMA reads it: show()
// writes the rows it touched back at the end.
// The light of each bit plane is reckoned from how the driver shows them
// (reckonPlaneLight()), not measured yet.
#else
// Light of each bit plane at the full OE time, per colour (red, green, blue),
// out of 65535 for the value 255: fitted to the BH1750 readings of 2026-10-07
// (17 values per colour on the MatrixPortal S3's 64x32 panel). Bits that give
// a colour almost no light (blue below bit 3) are about as dim as the sensor
// could tell; the two it could not tell from nothing count 1, so that no two
// values have the same light (255 would otherwise go out as 254).
const uint16_t PANEL_BIT_LIGHT[3][8] = {
  {   259,   237,   733,  3403,  5383,  8867, 16428, 30225 },   // red
  {     1,   185,   680,  3324,  5260,  8956, 16563, 30566 },   // green
  {     1,    22,   390,  3051,  5175,  8773, 16702, 31421 },   // blue
};
#endif
const uint8_t PANEL_BRIGHT_FLOOR = 13;   // brightness 1..13 all give 13/255, about 5 %
const uint8_t PANEL_OE_FULL = 255;       // the driver's brightness, kept at full

class PanelCanvas : public GFXcanvas16 {
 public:
  PanelCanvas(uint16_t w, uint16_t h) : GFXcanvas16(w, h) {}

  // Starts the driver; false when it gets neither its memory nor its pins.
  bool begin() {
    const uint8_t rgb[] = MATRIX_RGB_PINS;
    const uint8_t addr[] = MATRIX_ADDR_PINS;
#if BOARD_WAVESHARE_WALL
    HUB75_I2S_CFG::i2s_pins pins = {
      (int8_t)rgb[0], (int8_t)rgb[1], (int8_t)rgb[2], (int8_t)rgb[3], (int8_t)rgb[4], (int8_t)rgb[5],
      (int8_t)addr[0], (int8_t)addr[1], (int8_t)addr[2], (int8_t)addr[3], (int8_t)addr[4],
      MATRIX_LATCH_PIN, MATRIX_OE_PIN, MATRIX_CLOCK_PIN };
    // As the wall clock spike ran the six panels on the bench (2026-10-16/17).
    HUB75_I2S_CFG cfg(WALL_PANEL, WALL_PANEL, WALL_CHAIN, pins);
    // One buffer: the picture changes while the panels show it; the clock
    // writes only what changed, which takes a fraction of a refresh.
    cfg.double_buff = false;
    // 160 MHz / 7 = 22.9 MHz on the bus with the buffer in internal RAM; with
    // it in PSRAM the driver sets its own (PSRAM_LCD_DIV_NUM: 16 MHz).
    cfg.i2sspeed = HUB75_I2S_CFG::HZ_20M;
    cfg.driver = HUB75_I2S_CFG::FM6126A;    // how Waveshare's example starts its panels
    cfg.latch_blanking = 2;
    cfg.clkphase = false;
    cfg.min_refresh_rate = 120;
    _held[0] = (uint16_t *)calloc((size_t)WIDTH * HEIGHT, sizeof(uint16_t));
    if (!_held[0]) { return false; }
    // Without it native pictures are written whole every frame (wallRow()).
    _shown = (uint8_t *)heap_caps_malloc((size_t)WALL_W * WALL_H * 3, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    // And without this the band (showBand()).
    _bandHeld = (uint16_t *)heap_caps_malloc((size_t)WALL_W * WALL_BAND_H * 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
#else
    HUB75_I2S_CFG::i2s_pins pins = {
      (int8_t)rgb[0], (int8_t)rgb[1], (int8_t)rgb[2], (int8_t)rgb[3], (int8_t)rgb[4], (int8_t)rgb[5],
      (int8_t)addr[0], (int8_t)addr[1], (int8_t)addr[2], (int8_t)addr[3],
      (int8_t)(HEIGHT > 32 ? addr[4] : -1),
      MATRIX_LATCH_PIN, MATRIX_OE_PIN, MATRIX_CLOCK_PIN };
    HUB75_I2S_CFG cfg(WIDTH, HEIGHT, 1, pins);
    cfg.double_buff = true;
    // With the driver's default clock phase the picture sits one column to the
    // left on this panel and the right column shows stale data (tested on the
    // MatrixPortal S3 with its 64x32 panel).
    cfg.clkphase = false;
    cfg.min_refresh_rate = 120;
    _held[0] = (uint16_t *)calloc((size_t)WIDTH * HEIGHT, sizeof(uint16_t));
    _held[1] = (uint16_t *)calloc((size_t)WIDTH * HEIGHT, sizeof(uint16_t));
    if (!_held[0] || !_held[1]) { return false; }
#endif
    forgetHeld();   // whatever the driver's buffers start with, the first shows write all
#if !BOARD_WAVESHARE_WALL
    buildLight();
#endif
    _dma = new MatrixPanel_I2S_DMA(cfg);
    if (!_dma->begin()) { return false; }
#if BOARD_WAVESHARE_WALL
    reckonPlaneLight();   // from the planes the driver has just chosen
    buildLight();
    // The output's DMA channel first among the GDMA channels. It shares them
    // with the crypto engine (AES), all at priority 0 as allocated, and lost
    // out to it during TLS handshakes: the output stopped, a row flashed
    // bright until wallOutputWatch() restarted it. Measured 2026-10-18 at
    // 16 MHz, weather + warnings fetched six times each way: 20 stops at
    // priority 0, 1 at 5; the crypto engine as fast at 5 as at 0.
    if (!_dma->setDmaPriority(PANEL_DMA_PRIORITY)) { Console.println("Panel: DMA priority NOT set"); }
#endif
    _dma->setBrightness8(0);   // dark until the sketch sets a brightness
    // The driver clocks the panel bus without a pause and sets its pins to the
    // strongest drive; those edges disturb the WiFi. Measured on the
    // MatrixPortal S3 with 30 TCP connections to the router: 0 failed before
    // the driver started, 20 failed with it running (panel lit or dark), none
    // at drive strength 0. The level shifters between the S3 and the panel are
    // all these pins drive, so the weakest strength is enough.
    for (uint8_t i = 0; i < 6; i++) { gpio_set_drive_capability((gpio_num_t)rgb[i], GPIO_DRIVE_CAP_0); }
#if BOARD_WAVESHARE_WALL
    for (uint8_t i = 0; i < 5; i++) { gpio_set_drive_capability((gpio_num_t)addr[i], GPIO_DRIVE_CAP_0); }
#else
    for (uint8_t i = 0; i < (HEIGHT > 32 ? 5 : 4); i++) { gpio_set_drive_capability((gpio_num_t)addr[i], GPIO_DRIVE_CAP_0); }
#endif
    gpio_set_drive_capability((gpio_num_t)MATRIX_CLOCK_PIN, GPIO_DRIVE_CAP_0);
    gpio_set_drive_capability((gpio_num_t)MATRIX_LATCH_PIN, GPIO_DRIVE_CAP_0);
    gpio_set_drive_capability((gpio_num_t)MATRIX_OE_PIN, GPIO_DRIVE_CAP_0);
    if (_dma->calculated_refresh_rate > 0) { _rate = _dma->calculated_refresh_rate; }
#if BOARD_WAVESHARE_WALL
    // The driver reckons its rate with the clock in its settings (the one
    // asked for, or 8 MHz with its buffer in PSRAM), while the bus runs at
    // 160 MHz divided by the whole number it set: 147 Hz in its figure at
    // 20 MHz asked, 169 Hz measured on the panels' E line.
    uint32_t div = LCD_CAM.lcd_clock.lcd_clkm_div_num;
    uint32_t reckoned = (uint32_t)_dma->getCfg().i2sspeed;
    if (div > 0 && reckoned > 0) { _rate = (uint32_t)((uint64_t)_rate * (160000000UL / div) / reckoned); }
    Console.printf("Panel: bus %lu kHz, refresh %lu Hz\n", (unsigned long)(160000UL / (div ? div : 1)),
                  (unsigned long)_rate);
#endif
    _periodUs = 1000000UL / _rate;
    _countFrom = micros();
    _instance = this;
    esp_register_shutdown_handler(blankOnRestart);
    return true;
  }

  // A software restart leaves the panel with whatever it was sent last, and
  // with nobody driving it while the chip restarts that row pair may light at
  // full duty. So black frames go out first: both buffers cleared, each shown
  // for a refresh, and the panel switched dark.
  static void blankOnRestart() {
    PanelCanvas *p = _instance;
    if (!p || !p->_dma) { return; }
    for (int i = 0; i < 2; i++) {
      p->_dma->clearScreen();
      p->flip();
      delayMicroseconds(p->_periodUs + 1000);
    }
    p->_dma->setBrightness8(0);
  }

#if BOARD_WAVESHARE_WALL
  // The canvas onto the wall, each canvas pixel as a WALL_SCALE x WALL_SCALE
  // block. The driver has one buffer, which the panels show while it is
  // written; waiting out a refresh period since the last show() keeps the loop
  // in step with the refresh, as on the MatrixPortal. Only the pixels that
  // changed since then are written, a run of neighbours at a time, and the
  // rows they are in then go from the CPU's cache to the PSRAM the DMA reads.
  void show() {
    if (!_dma) { return; }
    waitForBackBuffer();
    if (_testing) { flip(); return; }   // a test picture stands (testPicture())
#if defined(CLOCK_DEBUG)
    uint32_t pushStart = micros(), pushed = 0;
#endif
    if (_native) { endNative(); }
    const uint16_t *src = getBuffer();
    uint16_t *held = _held[0];
    static uint8_t run[WALL_W * 3];   // one wall row of the run, red green blue
    for (int16_t y = 0; y < HEIGHT; y++) {
      int16_t x = 0;
      while (x < WIDTH) {
        int i = y * WIDTH + x;
        if (src[i] == held[i] && !_stale[0]) { x++; continue; }
        int16_t from = x;
        uint8_t *o = run;
        while (x < WIDTH && (src[y * WIDTH + x] != held[y * WIDTH + x] || _stale[0])) {
          uint16_t c = src[y * WIDTH + x];
          uint8_t r = c >> 11, g = (c >> 5) & 0x3F, b = c & 0x1F;
          uint8_t rv = _lut[0][(r << 3) | (r >> 2)], gv = _lut[1][(g << 2) | (g >> 4)], bv = _lut[2][(b << 3) | (b >> 2)];
          for (uint8_t k = 0; k < WALL_SCALE; k++) { *o++ = rv; *o++ = gv; *o++ = bv; }
          held[y * WIDTH + x] = c;
          x++;
#if defined(CLOCK_DEBUG)
          pushed++;
#endif
        }
        int16_t wx = WALL_CANVAS_X + from * WALL_SCALE, n = (x - from) * WALL_SCALE;
        for (uint8_t k = 0; k < WALL_SCALE; k++) { wallSpan(wx, WALL_CANVAS_Y + y * WALL_SCALE + k, n, run); }
      }
    }
    _stale[0] = false;
    _shownValid = false;   // the canvas wrote over what a native picture left
    writeBackRows();
#if defined(CLOCK_DEBUG)
    debugShow(micros() - pushStart, pushed);
#endif
    flip();
  }

  // The driver's rows written since writeBackRows() last ran go from the CPU's
  // cache to the PSRAM, one row (both panel halves, all bit planes) at a time.
  void writeBackRows() {
    for (uint8_t row = 0; _touched != 0 && row < 32; row++) {
      if (_touched & (1UL << row)) { _dma->writeBackRow(row); _touched &= ~(1UL << row); }
    }
  }

  // Wall row y from wall column x to the right: n pixels at driver values (3
  // bytes a pixel). In the driver's row of 384 the top row of the wall comes
  // first, turned round (wall x 0..191 is 191..0, wall y 0..63 is 63..0), then
  // the bottom row as it is (wall x 0..191 is 192..383, wall y 64..127 is 0..63).
  void wallSpan(int16_t x, int16_t y, int16_t n, const uint8_t *rgb) {
    bool top = y < WALL_PANEL;
    int16_t dx = top ? WALL_W - x - n : WALL_W + x;
    int16_t dy = top ? WALL_PANEL - 1 - y : y - WALL_PANEL;
    _dma->writeRowRGB(dx, dy, n, rgb, top);
    _touched |= 1UL << (dy % (WALL_PANEL / 2));   // a driver row holds panel rows y and y + 32
  }

  // n LEDs of wall row y from wall column x in one colour (driver values).
  void wallLine(int16_t x, int16_t y, int16_t n, uint8_t r, uint8_t g, uint8_t b) {
    int16_t dx = (y < WALL_PANEL) ? WALL_W - x - n : WALL_W + x;
    int16_t dy = (y < WALL_PANEL) ? WALL_PANEL - 1 - y : y - WALL_PANEL;
    _dma->drawFastHLine(dx, dy, n, r, g, b);
    _touched |= 1UL << (dy % (WALL_PANEL / 2));
  }

  // Pictures in the wall's own LEDs (GIFs; the info band later), one whole
  // wall row at a time at driver values, then endWallFrame(). Only what
  // changed since the last such frame is written, as long as nothing else
  // wrote over it. The next show() gives the wall back to the canvas and
  // darkens the rows below it.
  void wallRow(int16_t y, const uint8_t *rgb) {
    if (!_dma || y < 0 || y >= WALL_H) { return; }
    _native = true;
    uint8_t *shown = _shown ? _shown + (size_t)y * WALL_W * 3 : nullptr;
    if (!shown || !_shownValid) {
      wallSpan(0, y, WALL_W, rgb);
      if (shown) { memcpy(shown, rgb, WALL_W * 3); }
      return;
    }
    int16_t x = 0;
    while (x < WALL_W) {
      if (memcmp(rgb + 3 * x, shown + 3 * x, 3) == 0) { x++; continue; }
      int16_t from = x;
      while (x < WALL_W && memcmp(rgb + 3 * x, shown + 3 * x, 3) != 0) { x++; }
      wallSpan(from, y, x - from, rgb + 3 * from);
      memcpy(shown + 3 * from, rgb + 3 * from, (size_t)(x - from) * 3);
    }
  }
  void endWallFrame() {
    writeBackRows();
    _shownValid = _shown != nullptr;
    flip();
    forgetHeld();   // the canvas is no longer what the wall shows
  }

  // After a native picture: the rows below the canvas dark again (the canvas
  // rows are written whole, forgetHeld()), until showBand() fills the band's.
  void endNative() {
    static uint8_t dark[WALL_W * 3] = {};
    for (int16_t y = WALL_CANVAS_Y + HEIGHT * WALL_SCALE; y < WALL_H; y++) { wallSpan(0, y, WALL_W, dark); }
    _native = false;
    _shownValid = false;
    _bandStale = true;
  }

  // The info band in the wall's own LEDs below the canvas: a picture of
  // WALL_W x WALL_BAND_H in canvas colours, turned into driver values through
  // the same tables as the canvas. Only what changed since the last call is
  // written, a run of neighbours at a time - all of it after anything else
  // wrote over its rows (a GIF, a test picture, another brightness). Nothing
  // while a GIF or a test picture holds the wall.
  void showBand(const uint16_t *src) {
    if (!_dma || _testing || _native || !src) { _bandStale = true; return; }
    static uint8_t run[WALL_W * 3];
    for (int16_t y = 0; y < WALL_BAND_H; y++) {
      const uint16_t *s = src + y * WALL_W;
      uint16_t *held = _bandHeld ? _bandHeld + y * WALL_W : nullptr;
      int16_t x = 0;
      while (x < WALL_W) {
        if (held && !_bandStale && s[x] == held[x]) { x++; continue; }
        int16_t from = x;
        uint8_t *o = run;
        while (x < WALL_W && (!held || _bandStale || s[x] != held[x])) {
          uint16_t c = s[x];
          uint8_t r = c >> 11, g = (c >> 5) & 0x3F, b = c & 0x1F;
          *o++ = _lut[0][(r << 3) | (r >> 2)];
          *o++ = _lut[1][(g << 2) | (g >> 4)];
          *o++ = _lut[2][(b << 3) | (b >> 2)];
          if (held) { held[x] = c; }
          x++;
        }
        wallSpan(from, WALL_BAND_Y + y, x - from, run);
      }
    }
    _bandStale = false;
    writeBackRows();
  }
  bool bandStale() const { return _bandStale; }
  // The light of each bit plane, out of 65535 for the value 255, reckoned from
  // how the driver (3.0.15) shows them: each plane once at the start of a
  // row, then the planes above its lsbMsbTransition() again 2^(i - t - 1)
  // times. The panel lights the data of one pass while the next pass shifts
  // in, for that pass's OE time, which the driver shortens for the first
  // planes (setBrightnessOE(): 1/2^rightshift of a pass). Reckoned so for the
  // MatrixPortal S3's settings this matches its BH1750 table (planes 4-7 within
  // 6 %), and for the wall at 169 Hz it gives the colour steps at every multiple
  // of 32 that the ramps showed on the bench (2026-10-17).
  void reckonPlaneLight() {
    const int depth = 8;
    const int t = _dma->lsbMsbTransition();
    uint8_t seq[160];
    int n = 0;
    for (int i = 0; i < depth; i++) { seq[n++] = (uint8_t)i; }
    for (int i = t + 1; i < depth; i++) {
      for (int k = 0; k < (1 << (i - t - 1)) && n < (int)sizeof(seq); k++) { seq[n++] = (uint8_t)i; }
    }
    float light[depth] = {};
    float total = 0;
    for (int j = 0; j < n; j++) {
      int shown = seq[(j + n - 1) % n];   // the data latched at the end of the pass before
      int bitplane = (2 * depth - seq[j]) % depth;
      int rightshift = bitplane - ((depth - t - 1) >> 1) - 2;
      float part = 1.0f / (float)(1 << (rightshift > 0 ? rightshift : 0));
      light[shown] += part;
      total += part;
    }
    uint32_t sum = 0;
    for (int i = 0; i < depth; i++) {
      _planeLight[i] = (uint16_t)lroundf(light[i] * 65535.0f / total);
      sum += _planeLight[i];
    }
    _planeLight[depth - 1] += (uint16_t)(65535 - sum);   // rounding: 255 gives exactly 65535
    Console.printf("Panel: planes 0..%d shown once each; light per plane %u %u %u %u %u %u %u %u\n", t,
                  _planeLight[0], _planeLight[1], _planeLight[2], _planeLight[3],
                  _planeLight[4], _planeLight[5], _planeLight[6], _planeLight[7]);
  }
#else
  // The canvas onto the panel. The buffer it writes into was on the panel
  // until the last flip, and the driver only switches buffers at the end of a
  // refresh, so it first waits out one refresh period since that flip. That
  // also paces the loop to the refresh, as Protomatter's show() did.
  //
  // The driver spreads every pixel over its 8 bit planes, which makes a whole
  // frame slow to hand over (the loop, and with it the config page, would stall
  // on it). So it keeps a copy of what each of its two buffers holds and writes
  // only the pixels that differ.
  void show() {
    if (!_dma) { return; }
    waitForBackBuffer();
#if defined(CLOCK_DEBUG)
    uint32_t pushStart = micros(), pushed = 0;
#endif
    const uint16_t *src = getBuffer();
    uint16_t *held = _held[_back];
    for (int16_t y = 0; y < HEIGHT; y++) {
      for (int16_t x = 0; x < WIDTH; x++) {
        int i = y * WIDTH + x;
        if (src[i] == held[i] && !_stale[_back]) { continue; }
        uint16_t c = src[i];
        uint8_t r = c >> 11, g = (c >> 5) & 0x3F, b = c & 0x1F;
        _dma->drawPixelRGB888(x, y, _lut[0][(r << 3) | (r >> 2)], _lut[1][(g << 2) | (g >> 4)],
                              _lut[2][(b << 3) | (b >> 2)]);
        held[i] = src[i];
#if defined(CLOCK_DEBUG)
        pushed++;
#endif
      }
    }
    _stale[_back] = false;
#if defined(CLOCK_DEBUG)
    debugShow(micros() - pushStart, pushed);
#endif
    flip();
  }
#endif

  // After a picture was drawn straight into the driver (driver() below), the
  // copies no longer tell what its buffers hold: the next shows write all.
  void forgetHeld() {
    _stale[0] = _stale[1] = true;
#if BOARD_WAVESHARE_WALL
    _bandStale = true;   // and the band's next showBand()
#endif
  }

  // Brightness of the whole panel, 0..255: 0 switches it dark (through the OE
  // time), anything else sets the light the colour values give (see above),
  // at least PANEL_BRIGHT_FLOOR. Another brightness rebuilds the tables, and
  // the next shows write every pixel.
  void setBrightness(uint8_t b) {
    if (!_dma) { return; }
#if BOARD_WAVESHARE_WALL
    if (_heldDark) { return; }
#endif
    if (b == 0) {
      if (_bright != 0) { _dma->setBrightness8(0); _bright = 0; }
      return;
    }
    if (b < PANEL_BRIGHT_FLOOR) { b = PANEL_BRIGHT_FLOOR; }
#if BOARD_WAVESHARE_WALL
    if (b > PANEL_BRIGHT_CAP) { b = PANEL_BRIGHT_CAP; }
#endif
    if (b == _bright) { return; }
    if (_bright == 0) { _dma->setBrightness8(PANEL_OE_FULL); }
    _bright = b;
    buildLuts();
    forgetHeld();
  }

  // The driver's value for colour c (0 red, 1 green, 2 blue) of the canvas
  // value v at the current brightness, for pictures drawn straight into the
  // driver.
  uint8_t panelValue(uint8_t c, uint8_t v) const { return _lut[c][v]; }

  // Refreshes since the last call, from the rate the driver calculated (it
  // counts none itself).
  uint32_t getFrameCount() {
    uint32_t n = (uint32_t)((uint64_t)(uint32_t)(micros() - _countFrom) * _rate / 1000000UL);
    _countFrom += (uint32_t)((uint64_t)n * 1000000UL / _rate);
    return n;
  }

  static uint16_t color565(uint8_t r, uint8_t g, uint8_t b) {
    return ((uint16_t)(r & 0xF8) << 8) | ((uint16_t)(g & 0xFC) << 3) | (b >> 3);
  }

  // For pictures with 8 bits per colour, drawn straight into the driver:
  // waitForBackBuffer(), driver()->drawPixelRGB888(...) at panelXY(), flip(),
  // forgetHeld().
  MatrixPanel_I2S_DMA *driver() { return _dma; }

  // A point of the canvas in its current rotation, as the driver addresses it
  // (unrotated), the same way GFXcanvas16::drawPixel() turns it.
  void panelXY(int16_t &x, int16_t &y) const {
    int16_t t;
    switch (getRotation()) {
      case 1: t = x; x = WIDTH - 1 - y; y = t; break;
      case 2: x = WIDTH - 1 - x; y = HEIGHT - 1 - y; break;
      case 3: t = x; x = y; y = HEIGHT - 1 - t; break;
      default: break;
    }
  }
  void waitForBackBuffer() {
    uint32_t waited = micros() - _flippedAt;
    if (waited >= _periodUs) { return; }
    uint32_t rest = _periodUs - waited;
    if (rest >= 1000) { delay(rest / 1000); }   // lets the WiFi tasks run meanwhile
    delayMicroseconds(rest % 1000);
  }
#if BOARD_WAVESHARE_WALL
  // One buffer: nothing to switch, only the time the frame went out.
  void flip() { _flippedAt = micros(); }

  // The panels dark (through the OE time) until the next start, whatever
  // brightness the clock asks for afterwards: before a flash.
  void holdDark() {
    if (!_dma) { return; }
    _dma->setBrightness8(0);
    _bright = 0;
    _heldDark = true;
  }

  // A test picture to measure the panels' current with: wall panel n lit in
  // the clock's white at brightness b (capped as always), everything else
  // dark; n counts the top row 1-3 from the left, then the bottom row 4-6, and
  // 0 leaves the whole wall dark. show() writes nothing while it stands;
  // endTestPicture() gives the wall back to the clock.
  void testPicture(uint8_t n, uint8_t b) {
    if (!_dma) { return; }
    _testing = true;
    _shownValid = false;
    setBrightness(b);
    int16_t px = (n >= 1 && n <= 6) ? ((n - 1) % 3) * WALL_PANEL : -1;
    int16_t py = (n >= 1 && n <= 6) ? ((n - 1) / 3) * WALL_PANEL : -1;
    for (int16_t y = 0; y < WALL_H; y++) {
      wallLine(0, y, WALL_W, 0, 0, 0);
      if (px >= 0 && y >= py && y < py + WALL_PANEL) {
        wallLine(px, y, WALL_PANEL, _lut[0][255], _lut[1][255], _lut[2][255]);
      }
    }
    writeBackRows();
  }
  void endTestPicture() {
    if (!_testing) { return; }
    _testing = false;
    for (int16_t y = 0; y < WALL_H; y++) { wallLine(0, y, WALL_W, 0, 0, 0); }
    forgetHeld();   // the next show() and showBand() write the whole picture again
  }
  bool testing() const { return _testing; }

  // Test of the bus clock (console "bus"): 160 MHz divided by div, set while
  // the driver runs (with its buffer in PSRAM it sets PSRAM_LCD_DIV_NUM). The
  // refresh and the loop's pace follow it. For a look at the panels only; a
  // restart brings the driver's clock back.
  void setBusDivider(uint8_t div) {
    if (!_dma || div < 8 || div > 32) { return; }
    uint32_t old = LCD_CAM.lcd_clock.lcd_clkm_div_num;
    LCD_CAM.lcd_clock.lcd_clkm_div_num = div;
    LCD_CAM.lcd_user.lcd_update = 1;
    if (old > 0) { _rate = _rate * old / div; }
    _periodUs = 1000000UL / _rate;
  }
  uint32_t refreshRate() const { return _rate; }
  // Whether the driver still sends the buffer to the panels, and a fresh start
  // of that once it has stopped (wallOutputWatch() in the sketch).
  bool outputRunning() const { return !_dma || _dma->outputRunning(); }
  void restartOutput() { if (_dma) { _dma->restartOutput(); } }
  // Test (console "prio"): the output's DMA channel priority, 0 to 5.
  bool setDmaPriority(uint8_t priority) { return _dma && _dma->setDmaPriority(priority); }
#else
  void flip() { _dma->flipDMABuffer(); _flippedAt = micros(); _back ^= 1; }
#endif

 private:
  // The light of every value per colour (out of 65535, from PANEL_BIT_LIGHT;
  // on the wall from reckonPlaneLight()) and the values sorted by it, the
  // smaller value first among equals.
  void buildLight() {
    for (uint8_t c = 0; c < 3; c++) {
      uint16_t *light = _light[c];
      uint8_t *order = _order[c];
      for (uint16_t v = 0; v < 256; v++) {
        uint32_t sum = 0;
#if BOARD_WAVESHARE_WALL
        for (uint8_t bit = 0; bit < 8; bit++) { if ((v >> bit) & 1) { sum += _planeLight[bit]; } }
#else
        for (uint8_t bit = 0; bit < 8; bit++) { if ((v >> bit) & 1) { sum += PANEL_BIT_LIGHT[c][bit]; } }
#endif
        light[v] = (uint16_t)sum;
        order[v] = (uint8_t)v;
      }
      for (int16_t i = 1; i < 256; i++) {   // insertion sort, once at start-up
        uint8_t v = order[i];
        int16_t j = i - 1;
        while (j >= 0 && (light[order[j]] > light[v] || (light[order[j]] == light[v] && order[j] > v))) {
          order[j + 1] = order[j];
          j--;
        }
        order[j + 1] = v;
      }
    }
  }

  // The value of colour c whose light is nearest to want, searched from
  // position k of its order on (k only grows while want does).
  uint8_t nearest(uint8_t c, uint32_t want, uint16_t &k) const {
    const uint16_t *light = _light[c];
    const uint8_t *order = _order[c];
    while (k < 255 && light[order[k]] < want) { k++; }
    uint32_t above = light[order[k]];
    uint8_t pick = order[k];
    if (k > 0) {
      uint32_t below = light[order[k - 1]];
      uint32_t offAbove = above >= want ? above - want : want - above;
      if (want - below < offAbove) { pick = order[k - 1]; }
    }
    return pick;
  }

  // For the brightness: per colour, the value whose light is nearest to the
  // canvas value's share of it. Blue steps the coarsest at the dark end (8.5 %
  // of full is followed by 12.6 %), so the light blue really gives for 255 is
  // the full share all three aim at: white stays white (measured: blue at 0.94 to
  // 1.07 of red, green at 1.00 to 1.13), and the brightness keeps to blue's steps
  // there (up to 21 % off the asked one) instead of the colours drifting apart.
  void buildLuts() {
    uint16_t k = 0;
    uint32_t full = _light[2][nearest(2, (uint32_t)_bright * 65535UL / 255UL, k)];
    for (uint8_t c = 0; c < 3; c++) {
      k = 0;
      for (uint16_t v = 0; v < 256; v++) { _lut[c][v] = nearest(c, (uint32_t)v * full / 255UL, k); }
    }
  }

#if defined(CLOCK_DEBUG)
  // Debug build: how long handing frames over takes, printed every 2 s.
  void debugShow(uint32_t us, uint32_t pixels) {
    _dbgCalls++; _dbgUs += us; _dbgPixels += pixels;
    if (us > _dbgMaxUs) { _dbgMaxUs = us; }
    if (millis() - _dbgSince < 2000) { return; }
    if (consoleLevel >= 2) {   // a running figure (console.h)
      Console.printf("show: %lu frames, %lu px avg, push avg %lu max %lu us\n",
                    (unsigned long)_dbgCalls, (unsigned long)(_dbgPixels / _dbgCalls),
                    (unsigned long)(_dbgUs / _dbgCalls), (unsigned long)_dbgMaxUs);
    }
    _dbgCalls = _dbgUs = _dbgPixels = _dbgMaxUs = 0;
    _dbgSince = millis();
  }
  uint32_t _dbgCalls = 0, _dbgUs = 0, _dbgPixels = 0, _dbgMaxUs = 0, _dbgSince = 0;
#endif
  inline static PanelCanvas *_instance = nullptr;   // for blankOnRestart()
  MatrixPanel_I2S_DMA *_dma = nullptr;
  uint32_t _rate = PANEL_HZ_TYPICAL;
  uint32_t _periodUs = 1000000UL / PANEL_HZ_TYPICAL;
  uint32_t _flippedAt = 0;
  uint32_t _countFrom = 0;
  // What the buffer written at every other show() holds; flip() alternates.
  uint16_t *_held[2] = { nullptr, nullptr };
  bool      _stale[2] = { true, true };
  uint8_t   _back = 0;
  uint8_t   _bright = 0;          // 0 = dark
#if BOARD_WAVESHARE_WALL
  bool      _heldDark = false;    // holdDark(): dark until the next start
  bool      _testing = false;     // testPicture() stands
  bool      _native = false;      // wallRow() drew since the last show()
  uint8_t  *_shown = nullptr;     // what wallRow() left on the wall, 3 bytes an LED (PSRAM)
  bool      _shownValid = false;  // ... and nothing wrote over it since
  uint32_t  _touched = 0;         // driver rows written since the last writeBackRows(), a bit each
  uint16_t  _planeLight[8] = {};  // reckonPlaneLight()
  uint16_t *_bandHeld = nullptr;  // what showBand() left in the band's rows, canvas colours (PSRAM)
  bool      _bandStale = true;    // ... and something wrote over them since
#endif
  uint16_t  _light[3][256];
  uint8_t   _order[3][256];
  uint8_t   _lut[3][256] = {};    // canvas value -> driver value, per colour
};

/* ======================================================================
   Settings persistence

   NVS, through the Preferences library. The nvs partition sits at 0x9000 and
   is not part of the images esptool writes on an upload (bootloader, partition
   table, otadata, app, UF2 bootloader), so the settings survive a re-flash.
   ====================================================================== */

#include <Preferences.h>

// The key lets a build keep more than one blob side by side in the same
// namespace. It defaults to the one the clock has always used, so existing
// settings stay exactly where they are. A blob of bytes; SettingsStore<T>
// below is the same for a struct, and the faces' settings (face.h) use this
// one through their tables.
class SettingsBlobStore {
 public:
  void begin(const char *key = "settings") {
    _key = key;
    _prefs.begin("matrixclock", false);
  }

  // Reads the stored blob. A blob SHORTER than `size` is accepted and the
  // rest left zeroed - that is a layout from an earlier firmware, which the
  // loader then migrates by revision. Anything longer comes from a NEWER
  // firmware and is refused, leaving the destination zeroed so the caller's
  // magic check falls back to the factory defaults.
  bool read(void *dst, size_t size) {
    memset(dst, 0, size);
    size_t stored = _prefs.getBytesLength(_key);
    if (stored == 0 || stored > size) { return false; }
    if (_prefs.getBytes(_key, dst, size) != stored) {
      memset(dst, 0, size);
      return false;
    }
    return true;
  }

  void write(const void *src, size_t size) { _prefs.putBytes(_key, src, size); }

 private:
  Preferences _prefs;
  const char *_key = "settings";
};

template <typename T>
class SettingsStore : public SettingsBlobStore {
 public:
  bool read(T &dst) { return SettingsBlobStore::read(&dst, sizeof(T)); }
  void write(T &src) { SettingsBlobStore::write(&src, sizeof(T)); }
};


/* ======================================================================
   Serial console
   ====================================================================== */
// Debug-build output (env:adafruit_matrixportal_s3_debug sets CLOCK_DEBUG).
// Both compile to nothing in the normal builds. BOOT_TRACE pushes its line out
// before continuing so it survives a reset right after it.
#if defined(CLOCK_DEBUG)
  #define BOOT_TRACE(msg) do { Console.println(msg); Console.flush(); } while (0)
  #define DEBUG_LOG(...)  do { Console.printf(__VA_ARGS__); } while (0)
#else
  #define BOOT_TRACE(msg) do { } while (0)
  #define DEBUG_LOG(...)  do { } while (0)
#endif

inline void boardSerialBegin(unsigned long baud) {
#if !CLOCK_CONSOLE
  // A release build has no console (console.h): nothing is started on the
  // USB serial. The S3's USB-Serial-JTAG still takes uploads, in hardware.
  (void)baud;
#if ARDUINO_USB_CDC_ON_BOOT && !ARDUINO_USB_MODE
  Serial.enableReboot(false);   // TinyUSB's CDC: as below
#endif
#else
#if defined(BOOT_WAIT_FOR_SERIAL_MS) && ARDUINO_USB_CDC_ON_BOOT && ARDUINO_USB_MODE
  // The debug build writes much more; with the TX timeout at 0 below, what the
  // reader has not taken yet waits here instead of being dropped.
  Serial.setTxBufferSize(8192);
#endif
  Serial.begin(baud);
#if defined(BOOT_WAIT_FOR_SERIAL_MS)
  // Debug aid (env:adafruit_matrixportal_s3_debug): hold the boot until a serial
  // monitor is attached or the time runs out, so the first messages - and the
  // last one before an early crash - are not lost.
  for (unsigned long t0 = millis(); !Serial && millis() - t0 < BOOT_WAIT_FOR_SERIAL_MS; ) { delay(10); }
  Serial.setDebugOutput(true);
  Serial.printf("boot: serial attached after %lu ms\n", millis());
  // Why did the previous run end? (1 power-on, 2 reset pin, 3 software,
  // 4 panic, 5 int. watchdog, 6 task watchdog, 7 other watchdog, 9 brownout)
  Serial.printf("boot: reset reason %d\n", (int)esp_reset_reason());
  // TinyUF2's bootloader charges an RC element on GPIO1 during its 0.5 s
  // detection window and reads any reset while it is still charged as a double
  // reset -> UF2 mode. Staying here a while lets a crash further on reboot into
  // the app again, so its reset reason gets printed above.
  while (millis() < 3000) { delay(10); }
  Serial.flush();
#if ARDUINO_USB_CDC_ON_BOOT
  // As in the release build (below): without it every write waited up to
  // 100 ms once the console's reader had closed while the USB stayed plugged
  // in, and the wall clock ran in slow motion (2026-10-20).
  Serial.setTxTimeoutMs(0);
#endif
#elif ARDUINO_USB_CDC_ON_BOOT
  // The S3 console is native USB CDC. Without this, every write blocks for up
  // to 100 ms whenever the port is enumerated but nothing is reading it, which
  // would stutter the animation as soon as the clock is plugged into a PC.
  // (Built with -D ARDUINO_USB_CDC_ON_BOOT=0 the console is UART0 instead and
  // the board brings up no USB device at all.)
  Serial.setTxTimeoutMs(0);
#if !ARDUINO_USB_MODE
  // TinyUSB's CDC: a host that sets 1200 baud, or toggles DTR and RTS in
  // esptool's order (a serial tool did as it closed the port), would restart
  // the clock into its bootloader, where nothing drives the panel and the last
  // row pair it was sent stays lit at full duty. Uploads went by UF2 or the ROM
  // bootloader, so that path was switched off.
  Serial.enableReboot(false);
#else
  // The chip's own USB-Serial-JTAG (the clock's flash layout of 2026-10-09):
  // esptool's DTR/RTS order resets the chip into its ROM bootloader in
  // hardware, which is how uploads go now; no software can switch that off.
  // A tool that only reads the console opens the port with DTR and RTS low.
#endif
#endif
#endif
}

/* ======================================================================
   Why did the last run end?
   ====================================================================== */
// Short label for the reset cause, meant to be readable on the panel when the
// clock runs without a serial console (e.g. on a power supply). "POWER" is a
// normal power-up, "BUTTON" the reset button; "BROWN" (supply voltage dipped),
// "PANIC" (crash) and the watchdogs point at a real problem.
inline const char *boardResetReasonText() {
  switch (esp_reset_reason()) {
    case ESP_RST_POWERON:  return "POWER";
    case ESP_RST_EXT:      return "BUTTON";
    case ESP_RST_SW:       return "SOFT";
    case ESP_RST_PANIC:    return "PANIC";
    case ESP_RST_INT_WDT:  return "IWDT";
    case ESP_RST_TASK_WDT: return "TWDT";
    case ESP_RST_WDT:      return "WDT";
    case ESP_RST_BROWNOUT: return "BROWN";
    case ESP_RST_DEEPSLEEP: return "SLEEP";
    default:               return "OTHER";
  }
}

/* ======================================================================
   Boot breadcrumb
   ====================================================================== */
// How far the boot got, kept in flash so it survives a crash AND the trip
// through the UF2 bootloader (which swallows the reset reason). The clock
// writes a stage while starting and clears it once it has run for a while;
// a value left behind therefore names the step the previous run died in.
inline void boardBootStageWrite(uint8_t stage) {
  Preferences p;
  if (p.begin("matrixclock", false)) { p.putUChar("bootstage", stage); p.end(); }
}

inline uint8_t boardBootStageRead() {
  Preferences p;
  if (!p.begin("matrixclock", true)) { return 0; }
  uint8_t stage = p.getUChar("bootstage", 0);
  p.end();
  return stage;
}

// True for the two harmless causes, so a normal start shows no extra screen.
inline bool boardResetWasNormal() {
  const char *r = boardResetReasonText();
  return (strcmp(r, "POWER") == 0) || (strcmp(r, "BUTTON") == 0);
}

/* ======================================================================
   Reboot
   ====================================================================== */
inline void boardReset() {
  ESP.restart();
}

/* ======================================================================
   WiFi / NTP

   lwIP runs on the ESP32-S3 itself: SNTP is started explicitly and a sync
   notification tells us when a *fresh* timestamp landed.
   (Asking whether time() merely looks plausible would not do - the system clock
   keeps running across a radio power cycle, so it would happily hand back a
   day-old, drifted value instead of a new reading.)
   ====================================================================== */

#include <WiFi.h>
#include <WiFiUdp.h>
#include <esp_sntp.h>
#include <esp_netif.h>
#include <esp_mac.h>                 // esp_read_mac(), for the clock's name on the network
#include <dhcpserver/dhcpserver.h>   // OFFER_DNS

// Time sources for the S3's own SNTP client. Point the first one at a server on
// the LAN (e.g. a Fritz!Box at 192.168.2.1) if the clock has no internet access.
#ifndef NTP_SERVER_1
  #define NTP_SERVER_1 "pool.ntp.org"
#endif
#ifndef NTP_SERVER_2
  #define NTP_SERVER_2 "time.nist.gov"
#endif

// State of the SNTP client. Held in function-local statics so this header stays
// self-contained (one shared instance no matter how often it is included).
inline volatile bool &netNtpFresh()   { static volatile bool fresh = false;   return fresh; }
inline bool          &netSntpRunning(){ static bool          running = false; return running; }

// Called from the SNTP task once a server reply has been applied to the clock.
inline void netNtpSyncCallback(struct timeval *) { netNtpFresh() = true; }

// Discard the last sync so netNtpEpoch() only reports a genuinely new one.
inline void netNtpRequestFresh() {
  netNtpFresh()     = false;
  netSntpRunning()  = false;   // SNTP is (re)started on the next poll
}

// Radio transmit power, set after every mode change because a mode change resets
// it. Transmit bursts are the clock's highest current draw: at the default
// 19.5 dBm the board reset while joining the WiFi on every power supply tried
// (boot breadcrumb DIED3, sometimes reset reason BROWN), while it ran through
// at 11 dBm; on a PC USB port both work. 11 dBm roughly halves the peak and is
// plenty for a home network (measured -56 dBm there).
#ifndef WIFI_TX_POWER
  #define WIFI_TX_POWER WIFI_POWER_11dBm
#endif

inline void netRadioInit() {
  WiFi.persistent(false);   // don't rewrite the stored credentials on every boot
  BOOT_TRACE("net: WiFi.mode(WIFI_STA) ...");
  WiFi.mode(WIFI_STA);
  WiFi.setTxPower(WIFI_TX_POWER);
  BOOT_TRACE("net: WiFi.mode(WIFI_STA) done");
}

// The clock's name on the home network, for DHCP and mDNS: "matrixclock-"
// and the last three bytes of its MAC, e.g. matrixclock-06c16c.
inline const char *netHostname() {
  static char name[20] = "";
  if (!name[0]) {
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    snprintf(name, sizeof(name), "matrixclock-%02x%02x%02x", mac[3], mac[4], mac[5]);
  }
  return name;
}

inline void netStaBegin(const char *ssid, const char *pass) {
  WiFi.persistent(false);   // also here: the station can be started without netRadioInit()
  WiFi.setHostname(netHostname());   // before the mode: the station's netif takes it when it is made
  WiFi.mode(WIFI_STA);
  WiFi.setTxPower(WIFI_TX_POWER);
  WiFi.begin(ssid, pass);   // non-blocking on the ESP32
  netNtpRequestFresh();
}

inline bool netStaConnected() { return WiFi.status() == WL_CONNECTED; }

// Once as the station comes up, before anything that looks names up may
// start. The core's NetworkManager::hostByName() clears lwIP's DNS cache on
// its first call after the addresses changed, without lwIP's lock: a lookup
// in flight then - the Home Assistant link's, SNTP's - is ended on the wrong
// task, and lwIP stops the clock ("assert failed: udp_remove ... Required to
// lock TCPIP core functionality!"; on the S3 2026-10-20 at every start of the
// debug build, the info band's first fetch against the link's lookup of
// homeassistant.local). Called here, the core sees the new address while no
// lookup runs, and the lookups after it find nothing changed. lwIP answers
// "localhost" itself (its loopback), so this does not wait for the network.
inline void netDnsSettle() {
  IPAddress ip;
#if defined(CLOCK_DEBUG)
  unsigned long t0 = micros();
  Network.hostByName("localhost", ip);
  Console.printf("DNS: settled for the new address in %lu us\n", (unsigned long)(micros() - t0));
#else
  Network.hostByName("localhost", ip);
#endif
}

// Stop the SNTP client after a sync while the station stays joined for
// something else; the next sync starts it afresh (netNtpEpoch()).
inline void netSntpStop() {
#if ESP_IDF_VERSION_MAJOR >= 5
  esp_sntp_stop();
#else
  sntp_stop();
#endif
  netSntpRunning() = false;
}

inline void netRadioOff() {
#if ESP_IDF_VERSION_MAJOR >= 5
  esp_sntp_stop();          // ESP-IDF 5 renamed it (Arduino-ESP32 3.x)
#else
  sntp_stop();              // ESP-IDF 4.4 has no esp_ alias for it
#endif
  netSntpRunning() = false;
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
}

// Returns a UTC epoch once a fresh SNTP sync has completed, 0 while it has not.
inline uint32_t netNtpEpoch() {
  if (!netStaConnected()) { return 0; }
  if (!netSntpRunning()) {
    sntp_set_time_sync_notification_cb(netNtpSyncCallback);
    configTime(0, 0, NTP_SERVER_1, NTP_SERVER_2); // TZ = UTC; the sketch adds the offset
    netSntpRunning() = true;
  }
  if (!netNtpFresh()) { return 0; }
  return (uint32_t)time(nullptr);
}

// Explicitly hand our own address out as the DNS server in every DHCP lease, so
// the phone's lookups - including its captive-portal check - reach our DNS
// hijack no matter what the softAP DHCP server would advertise by default.
// (Verified on hardware: the S24 then queries us and the portal pops up.)
inline void netApOfferSelfAsDns() {
  esp_netif_t *ap = esp_netif_get_handle_from_ifkey("WIFI_AP_DEF");
  if (ap == nullptr) { return; }
  esp_netif_ip_info_t ipInfo;
  if (esp_netif_get_ip_info(ap, &ipInfo) != ESP_OK) { return; }

  esp_netif_dns_info_t dns = {};
  dns.ip.type = ESP_IPADDR_TYPE_V4;
  dns.ip.u_addr.ip4.addr = ipInfo.ip.addr;
  uint8_t offer = OFFER_DNS;

  // The option can only be changed while the DHCP server is stopped. No client
  // can be connected yet at this point, so nobody loses a lease over it.
  esp_err_t eStop  = esp_netif_dhcps_stop(ap);
  esp_err_t eDns   = esp_netif_set_dns_info(ap, ESP_NETIF_DNS_MAIN, &dns);
  esp_err_t eOpt   = esp_netif_dhcps_option(ap, ESP_NETIF_OP_SET, ESP_NETIF_DOMAIN_NAME_SERVER, &offer, sizeof(offer));
  esp_err_t eStart = esp_netif_dhcps_start(ap);
  DEBUG_LOG("ap: DHCP offers DNS %s (stop 0x%x, set_dns 0x%x, option 0x%x, start 0x%x)\n",
            IPAddress(ipInfo.ip.addr).toString().c_str(), eStop, eDns, eOpt, eStart);
  (void)eStop; (void)eDns; (void)eOpt; (void)eStart;
}

inline bool netApBegin(const char *ssid, const char *pass) {
  WiFi.persistent(false);   // also needed here: the recovery AP starts before netRadioInit()
  WiFi.disconnect(true);    // stop the station side from retrying underneath us
  WiFi.mode(WIFI_AP);
  WiFi.setTxPower(WIFI_TX_POWER);
  const IPAddress apAddr(AP_IP_ADDR);
  bool cfgOk = WiFi.softAPConfig(apAddr, apAddr, IPAddress(255, 255, 255, 0));
  DEBUG_LOG("ap: softAPConfig(%s) %s\n", apAddr.toString().c_str(), cfgOk ? "ok" : "FAILED");
  (void)cfgOk;
  bool ok = WiFi.softAP(ssid, pass);
  if (ok) { netApOfferSelfAsDns(); }
  return ok;
}

// Drop the softAP. Whether the station joins again or the radio goes off is
// the caller's choice (netStaBegin() or netRadioOff()).
inline void netApEnd() {
  WiFi.softAPdisconnect(true);
}

// Scan for networks in range, with the radio as a station. Blocks: an active
// scan of 120 ms per channel, about 1.6 s. Returns how many it found (< 0 on
// a failure); netScanResult() reads them, netScanDone() frees the list.
inline int netScan() {
  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  WiFi.setTxPower(WIFI_TX_POWER);
  // A join under way makes the driver refuse the scan at once (seen: the
  // hotspot opened while the clock was retrying a WiFi, and the scan came back
  // empty within the same second), so stop it first.
  WiFi.disconnect();
  return WiFi.scanNetworks(false, false, false, 120);   // not async, no hidden ones, active
}
inline void netScanResult(int i, char *ssid, size_t size, int8_t &rssi, bool &open) {
  strncpy(ssid, WiFi.SSID(i).c_str(), size - 1);
  ssid[size - 1] = '\0';
  rssi = (int8_t)WiFi.RSSI(i);
  open = (WiFi.encryptionType(i) == WIFI_AUTH_OPEN);
}
inline void netScanDone() { WiFi.scanDelete(); }

// The station's state (wl_status_t: WL_NO_SSID_AVAIL, WL_CONNECT_FAILED, ...)
// and, once connected, its signal.
inline uint8_t netStaStatus() { return (uint8_t)WiFi.status(); }
inline int8_t  netStaRssi()   { return (int8_t)WiFi.RSSI(); }

inline bool netApHasStation() { return WiFi.softAPgetStationNum() > 0; }
inline IPAddress netApIP()    { return WiFi.softAPIP(); }

// Next pending HTTP connection. accept() is the 3.x name (available() is
// deprecated there); in 2.x both are the same function.
inline WiFiClient netServerAccept(WiFiServer &s) { return s.accept(); }

inline void netPrintRadioInfo() {
  Console.print("ESP-IDF SDK: ");
  Console.println(ESP.getSdkVersion());
}

