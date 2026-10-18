# ESP32-HUB75-MatrixPanel-DMA 3.0.15, wall clock copy

The panel driver [ESP32-HUB75-MatrixPanel-DMA](https://github.com/mrcodetastic/ESP32-HUB75-MatrixPanel-DMA)
by MrCodetastic, release 3.0.15 as the PlatformIO registry ships it (`src/` and `LICENSE.txt`, MIT), for
the wall clock build only (`env:waveshare_wallclock*` in `platformio.ini`). The MatrixPortal S3 keeps the
unchanged registry release, and its envs ignore this copy.

The wall clock keeps the driver's frame buffer in PSRAM (`SPIRAM_DMA_BUFFER`): at 8 bits one buffer for
six 64x64 panels takes 192 KB, which the internal RAM cannot spare next to the WiFi (measured on
2026-10-18: 56 KB left, the hotspot crashed, the home WiFi did not join). With the buffer in PSRAM the
CPU's cache has to be written back before the DMA reads a change. Release 3.0.15 does so after every bit
of every pixel in its pixel writer (about 13 us a pixel) and not at all in its line writers.

Added, each marked "MatrixPortalClock_V2 wall clock" in the source:

- `writeBackRow(row)`: writes one row's buffer back in one go, for a picture written with
  `drawFastHLine()` or `writeRowRGB()`;
- `writeRowRGB(x, y, n, rgb, reversed)`: a run of pixels of one panel row, each its own colour, one pass
  per bit plane (no rotation, no write-back);
- `rowsPerFrame()`: the rows the buffer holds (the panel height halved);
- `lsbMsbTransition()`: how many low bit planes the driver shows once each, weighted by their OE time
  alone; the clock reckons each plane's light from it.

Added: `PSRAM_LCD_DIV_NUM` (in `platforms/esp32s3/gdma_lcd_parallel16.cpp`) sets the bus clock's divider
with the buffer in PSRAM, where the release fixes 12 (160 MHz / 12 = 13.3 MHz). On the wall clock board
10 (16 MHz, 160 Hz) ran clean beside the WiFi and looked better in motion; 8 (20 MHz) stopped the
output until a restart (tested live on 2026-10-18).

Changed: with `SPIRAM_DMA_BUFFER` the release switches its line and rectangle writers off
(`NO_FAST_FUNCTIONS` in `platforms/platform_detect.hpp`, "I don't understand the interaction with DMA
PSRAM and the CPU->DMA->SPIRAM Cache"). This copy keeps them, since the clock writes the rows back with
`writeBackRow()` after drawing its lines. Nothing else is changed. Release 3.1.0 (2026-10-12) writes the cache back in its line writer too, one
word at a time.
