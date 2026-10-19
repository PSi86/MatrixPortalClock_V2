/* ----------------------------------------------------------------------
   The wall clock's knock, measuring round (plan of 2026-10-19, step 2a): what
   the board's QMI8658 sees of knocks on the mounted wall, and whether its tap
   engine reports them - the way the S3's LIS3DH knocks: the chip decides, its
   INT1 line (GPIO13 here) says so. Console "imu ..." (wallConsole()):

     imu                       who the chip is, what runs, the tap settings
     imu on [hz] [int]         the accelerometer at 1000/500/250/125 Hz, read
                               from the FIFO every 20 ms; with "int" the FIFO's
                               watermark on INT1, to see that the line works
     imu tap [hz]              the same with the tap engine on and its
                               interrupt on INT1 (1000 Hz unless given)
     imu tapcfg <peak> <quiet> [<peak win> <tap win> <double win>]
                               its thresholds in g^2 x 1000 and windows in
                               samples; taken at once if it runs
     imu wom <mg> [hz] [s1]    Wake on Motion instead: threshold 1-255 mg, at
                               128 Hz low power unless given (21, 125, 250, 500,
                               1000); every edge of INT1 is logged with its time,
                               and with "s1" STATUS1 is read every 20 ms (does
                               that re-arm it?)
     imu off
     imu bus <kHz>             the board bus' clock (100 or 400)

   A task on core 0 reads the chip - a FIFO read takes several I2C transfers,
   too long for the loop, which runs in step with the panel - and once a
   second hands loop() a summary to print: the largest deviation from a
   slowly following baseline in each 100 ms (the LIS3DH's knock is the same
   idea, in its high-pass filter), the gravity it reads, how long the reads
   took, how often INT1 was high, and every tap the engine reported.

   Measured on 2026-10-19 (panel bottom right): at rest 4-8 mg, knocks
   180-340 mg, firm ones up to 3.3 g; the engine with SensorLib's example
   settings at "500 Hz" (about 450 samples a second) reported none.
   ------------------------------------------------------------------------- */
#pragma once

#include "imu_qmi8658.h"
#include <freertos/queue.h>

const uint8_t IMU_INT_PIN = 13;   // the QMI8658's INT1 (schematic, 2026-10-19)

struct ImuSecond {
  uint32_t at;                 // millis() at its end
  uint16_t samples;
  uint16_t windowMg[10];       // per 100 ms: the largest deviation from the baseline, mg
  uint8_t  windows;
  uint16_t gravityMg;          // the mean vector's length
  uint16_t readAvgUs, readMaxUs;
  uint16_t reads, intHigh;     // reads, and how many found INT1 high before them
  uint32_t errors;
};
struct ImuTap {
  uint32_t at;
  uint8_t  status;             // TAP_STATUS, or STATUS1 for a Wake on Motion (wom)
  bool     int1;               // INT1 was high when it was read
  bool     wom;
};

// What the console asks for; the task carries it out (it alone talks to the chip).
struct ImuRequest {
  volatile bool     pending = false;
  volatile bool     on = false;
  volatile bool     fifoInt1 = false;
  volatile bool     tap = false;
  volatile bool     wom = false;
  volatile bool     womStatus = false;   // read STATUS1 every 20 ms meanwhile
  volatile uint8_t  womMg = 100;
  volatile uint8_t  odrCode = 4;
  volatile uint16_t odrHz = 500;
  Qmi8658Tap        tapCfg;
};

Qmi8658       imu;
ImuRequest    imuWant;
QueueHandle_t imuSeconds = nullptr;
QueueHandle_t imuTaps = nullptr;
bool          imuRunning = false;   // as the task last applied it
bool          imuTapOn = false;

// Every edge of INT1, from its interrupt: when (us) and the level after it.
struct ImuEdge {
  uint32_t us;
  uint8_t  level;
};
const uint8_t IMU_EDGES = 32;
volatile ImuEdge  imuEdge[IMU_EDGES];
volatile uint8_t  imuEdgeHead = 0, imuEdgeTail = 0;
volatile uint32_t imuEdgeLost = 0;
void IRAM_ATTR imuIntEdge() {
  uint8_t h = imuEdgeHead, next = (uint8_t)((h + 1) % IMU_EDGES);
  if (next == imuEdgeTail) { imuEdgeLost++; return; }
  imuEdge[h].us = (uint32_t)esp_timer_get_time();
  imuEdge[h].level = (uint8_t)gpio_get_level((gpio_num_t)IMU_INT_PIN);
  imuEdgeHead = next;
}

void imuTestTask(void *) {
  ImuSecond sec = {};
  float base[3] = {0, 0, 0};
  bool  baseSet = false;
  float sum[3] = {0, 0, 0};
  float windowMax = 0;
  uint32_t secondFrom = millis(), windowFrom = millis();
  uint32_t readUsSum = 0;
  bool on = false, wom = false;
  for (;;) {
    if (imuWant.pending) {
      imuWant.pending = false;
      imu.begin(Wire1);                         // a reset: whatever ran before is gone
      on = imuWant.on;
      wom = on && imuWant.wom;
      imuTapOn = false;
      if (wom) {
        on = imu.womStart(imuWant.odrCode, imuWant.womMg, 16);
      } else {
        if (on && imuWant.tap) { imuTapOn = imu.tapConfigure(imuWant.tapCfg, true); }
        if (on) { on = imu.accelStart(imuWant.odrCode, 10, imuWant.fifoInt1, imuTapOn); }
      }
      imuRunning = on;
      baseSet = false;
      sec = {};
      sum[0] = sum[1] = sum[2] = 0;
      windowMax = 0;
      readUsSum = 0;
      secondFrom = windowFrom = millis();
    }
    if (!on) { vTaskDelay(pdMS_TO_TICKS(50)); continue; }
    if (wom) {                                  // no data to read; INT1's edges come by interrupt
      if (imuWant.womStatus) {
        bool intHigh = digitalRead(IMU_INT_PIN) == HIGH;
        uint8_t s1 = imu.status1();
        if (s1 & 0x04) {
          ImuTap ev = { millis(), s1, intHigh, true };
          xQueueSend(imuTaps, &ev, 0);
        }
      }
      vTaskDelay(pdMS_TO_TICKS(20));
      continue;
    }

    int16_t xyz[Qmi8658::SAMPLES_PER_READ][3];
    bool intHigh = digitalRead(IMU_INT_PIN) == HIGH;
    if (imuTapOn) {                             // before the FIFO read: INT1 as the tap left it
      uint8_t t = imu.tapRead();
      if (t) {
        ImuTap tap = { millis(), t, intHigh, false };
        xQueueSend(imuTaps, &tap, 0);
      }
    }
    uint32_t t0 = micros();
    int n = imu.fifoRead(xyz);
    uint32_t us = micros() - t0;
    sec.reads++;
    if (intHigh) { sec.intHigh++; }
    readUsSum += us;
    if (us > sec.readMaxUs) { sec.readMaxUs = (uint16_t)min<uint32_t>(us, 65535); }
    for (int i = 0; i < n; i++) {
      if (!baseSet) {
        for (uint8_t a = 0; a < 3; a++) { base[a] = xyz[i][a]; }
        baseSet = true;
      }
      float dev2 = 0;
      for (uint8_t a = 0; a < 3; a++) {
        float d = xyz[i][a] - base[a];
        base[a] += d / 16.0f;                   // follows within about 32 samples
        dev2 += d * d;
        sum[a] += xyz[i][a];
      }
      if (dev2 > windowMax) { windowMax = dev2; }
      sec.samples++;
    }
    uint32_t now = millis();
    if (now - windowFrom >= 100) {
      if (sec.windows < 10) { sec.windowMg[sec.windows++] = (uint16_t)(sqrtf(windowMax) * 1000.0f / 8192.0f); }
      windowMax = 0;
      windowFrom = now;
    }
    if (now - secondFrom >= 1000) {
      if (sec.samples) {
        float m[3] = { sum[0] / sec.samples, sum[1] / sec.samples, sum[2] / sec.samples };
        sec.gravityMg = (uint16_t)(sqrtf(m[0] * m[0] + m[1] * m[1] + m[2] * m[2]) * 1000.0f / 8192.0f);
      }
      sec.readAvgUs = sec.reads ? (uint16_t)min<uint32_t>(readUsSum / sec.reads, 65535) : 0;
      sec.errors = imu.busErrors();
      sec.at = now;
      xQueueSend(imuSeconds, &sec, 0);
      sec = {};
      sum[0] = sum[1] = sum[2] = 0;
      readUsSum = 0;
      secondFrom = now;
    }
    vTaskDelay(pdMS_TO_TICKS(20));
  }
}

// At start-up, once the board bus runs: who the chip is. Its task waits for
// "imu on". The bus at 400 kHz: a FIFO read takes about 3 ms then instead of
// 9, and no transfer failed through the level shifter (2026-10-19).
void imuTestBegin() {
  pinMode(IMU_INT_PIN, INPUT);
  attachInterrupt(digitalPinToInterrupt(IMU_INT_PIN), imuIntEdge, CHANGE);
  {
    BoardBusLock lock;
    Wire1.setClock(400000);
  }
  // Wider windows than SensorLib's example, for about 900 samples a second,
  // and thresholds far below the knocks measured: whether the engine reports
  // anything at all comes first.
  imuWant.tapCfg.peakWindow = 30;
  imuWant.tapCfg.tapWindow = 100;
  imuWant.tapCfg.dTapWindow = 250;
  imuWant.tapCfg.peak = 20;      // 0.02 g^2: about 0.14 g
  imuWant.tapCfg.quiet = 2000;   // 2 g^2
  if (!imu.begin(Wire1)) {
    Serial.printf("QMI8658 not found (WHO_AM_I 0x%02X)\n", imu.whoAmI());
    return;
  }
  Serial.printf("QMI8658 found: WHO_AM_I 0x%02X, revision 0x%02X, reset result 0x%02X\n", imu.whoAmI(),
                imu.revision(), imu.resetResult());
  imuSeconds = xQueueCreate(4, sizeof(ImuSecond));
  imuTaps = xQueueCreate(8, sizeof(ImuTap));
  if (!imuSeconds || !imuTaps ||
      xTaskCreatePinnedToCore(imuTestTask, "imu", 4096, nullptr, 1, nullptr, 0) != pdPASS) {
    Serial.println("QMI8658: no task for it");
  }
}

// What the task handed over since the last pass, onto the console.
void imuTestPrint() {
  if (!imuSeconds) { return; }
  ImuSecond s;
  while (xQueueReceive(imuSeconds, &s, 0) == pdTRUE) {
    char w[64];
    int len = 0;
    for (uint8_t i = 0; i < s.windows && len < (int)sizeof(w) - 6; i++) {
      len += snprintf(w + len, sizeof(w) - len, " %u", s.windowMg[i]);
    }
    Serial.printf("IMU: %u samples, g %u mg, deviation per 100 ms (mg):%s; read avg %u max %u us, %u reads, "
                  "INT1 high before %u; bus errors %lu\n", s.samples, s.gravityMg, w, s.readAvgUs, s.readMaxUs,
                  s.reads, s.intHigh, (unsigned long)s.errors);
  }
  // INT1's edges: each one in Wake on Motion, otherwise only how many there
  // were (the FIFO's watermark would fill the console).
  static uint32_t lastEdgeUs = 0;
  uint32_t edges = 0;
  while (imuEdgeTail != imuEdgeHead) {
    ImuEdge e = { imuEdge[imuEdgeTail].us, imuEdge[imuEdgeTail].level };
    imuEdgeTail = (uint8_t)((imuEdgeTail + 1) % IMU_EDGES);
    edges++;
    if (imuWant.wom && imuRunning) {
      Serial.printf("IMU INT1: %s, %lu ms after the edge before\n", e.level ? "rose" : "fell",
                    (unsigned long)((e.us - lastEdgeUs) / 1000));
    }
    lastEdgeUs = e.us;
  }
  if (imuEdgeLost) {
    Serial.printf("IMU INT1: %lu edges not kept\n", (unsigned long)imuEdgeLost);
    imuEdgeLost = 0;
  }
  ImuTap t;
  while (xQueueReceive(imuTaps, &t, 0) == pdTRUE) {
    if (t.wom) {
      Serial.printf("IMU Wake on Motion: STATUS1 0x%02X, INT1 %s before it was read\n", t.status,
                    t.int1 ? "high" : "low");
      continue;
    }
    static const char *const AXIS[] = { "?", "X", "Y", "Z" };
    uint8_t count = t.status & 0x03, axis = (t.status >> 4) & 0x03;
    Serial.printf("IMU tap: %s on %s%s (TAP_STATUS 0x%02X), INT1 %s\n",
                  count == 2 ? "double" : count == 1 ? "single" : "?", AXIS[axis], (t.status & 0x80) ? "-" : "+",
                  t.status, t.int1 ? "high" : "low");
  }
}

// The console's "imu ..."; false for a line that is not one.
bool imuTestCommand(const char *line) {
  if (strncmp(line, "imu", 3) != 0 || (line[3] != '\0' && line[3] != ' ')) { return false; }
  unsigned khz = 0, hz = 0, a = 0, b = 0, c = 0, d = 0, e = 0;
  const Qmi8658Tap &tc = imuWant.tapCfg;
  if (strcmp(line, "imu") == 0) {
    Serial.printf("QMI8658: %s, WHO_AM_I 0x%02X, revision 0x%02X; %s, %u Hz%s%s; tap: peak %u, quiet %u, windows "
                  "%u/%u/%u; bus errors %lu\n", imu.found() ? "found" : "not found", imu.whoAmI(), imu.revision(),
                  imuRunning ? "running" : "off", imuWant.odrHz, imuWant.fifoInt1 ? ", FIFO watermark on INT1" : "",
                  imuWant.wom ? ", Wake on Motion" : imuTapOn ? ", tap engine on" : "", tc.peak, tc.quiet,
                  tc.peakWindow, tc.tapWindow, tc.dTapWindow, (unsigned long)imu.busErrors());
    if (imuWant.wom) {
      Serial.printf("QMI8658 Wake on Motion: over %u mg%s; INT1 now %s\n", imuWant.womMg,
                    imuWant.womStatus ? ", STATUS1 read every 20 ms" : "",
                    digitalRead(IMU_INT_PIN) == HIGH ? "high" : "low");
    }
    return true;
  }
  if (!imu.found()) { Serial.println("QMI8658 not found"); return true; }
  if (sscanf(line, "imu bus %u", &khz) == 1 && (khz == 100 || khz == 400)) {
    BoardBusLock lock;
    Wire1.setClock(khz * 1000UL);
    Serial.printf("Board I2C bus: %u kHz\n", khz);
    return true;
  }
  if (strcmp(line, "imu off") == 0) {
    imuWant.on = false;
    imuWant.tap = false;
    imuWant.wom = false;
    imuWant.pending = true;
    Serial.println("QMI8658: off");
    return true;
  }
  unsigned mg = 0;
  if (sscanf(line, "imu wom %u", &mg) == 1 && mg >= 1 && mg <= 255) {
    const char *rest = line + 8;
    while (*rest && *rest != ' ') { rest++; }   // past the threshold
    if (sscanf(rest, " %u", &hz) != 1) { hz = 128; }
    uint8_t code = hz == 128 ? 12 : hz == 21 ? 13 : hz >= 1000 ? 3 : hz >= 500 ? 4 : hz >= 250 ? 5 : 6;
    imuWant.odrCode = code;
    imuWant.odrHz = code == 12 ? 128 : code == 13 ? 21 : code == 3 ? 1000 : code == 4 ? 500 : code == 5 ? 250 : 125;
    imuWant.womMg = (uint8_t)mg;
    imuWant.womStatus = strstr(line, " s1") != nullptr;
    imuWant.wom = true;
    imuWant.tap = false;
    imuWant.fifoInt1 = false;
    imuWant.on = true;
    imuWant.pending = true;
    Serial.printf("QMI8658: Wake on Motion over %u mg at %u Hz%s\n", mg, imuWant.odrHz,
                  imuWant.womStatus ? ", STATUS1 read every 20 ms" : "");
    return true;
  }
  int got = sscanf(line, "imu tapcfg %u %u %u %u %u", &a, &b, &c, &d, &e);
  if (got == 2 || got == 5) {
    imuWant.tapCfg.peak = (uint16_t)a;
    imuWant.tapCfg.quiet = (uint16_t)b;
    if (got == 5) {
      imuWant.tapCfg.peakWindow = (uint8_t)min(c, 255u);
      imuWant.tapCfg.tapWindow = (uint16_t)d;
      imuWant.tapCfg.dTapWindow = (uint16_t)e;
    }
    if (imuWant.tap && imuWant.on) { imuWant.pending = true; }
    Serial.printf("QMI8658 tap: peak %u, quiet %u (g^2 x 1000), windows %u/%u/%u%s\n", tc.peak, tc.quiet,
                  tc.peakWindow, tc.tapWindow, tc.dTapWindow, imuWant.pending ? ", taken now" : "");
    return true;
  }
  bool tap = strncmp(line, "imu tap", 7) == 0 && (line[7] == '\0' || line[7] == ' ');
  if (tap || strncmp(line, "imu on", 6) == 0) {
    if (sscanf(line + (tap ? 7 : 6), " %u", &hz) != 1) { hz = tap ? 1000 : 500; }
    uint8_t code = hz >= 1000 ? 3 : hz >= 500 ? 4 : hz >= 250 ? 5 : 6;
    imuWant.odrCode = code;
    imuWant.odrHz = code == 3 ? 1000 : code == 4 ? 500 : code == 5 ? 250 : 125;
    imuWant.fifoInt1 = !tap && strstr(line, " int") != nullptr;
    imuWant.tap = tap;
    imuWant.wom = false;
    imuWant.on = true;
    imuWant.pending = true;
    Serial.printf("QMI8658: on, %u Hz%s%s\n", imuWant.odrHz, imuWant.fifoInt1 ? ", FIFO watermark on INT1" : "",
                  tap ? ", tap engine on INT1" : "");
    return true;
  }
  Serial.println("imu: imu | imu on [hz] [int] | imu tap [hz] | imu tapcfg <peak> <quiet> [<pw> <tw> <dw>] | "
                 "imu wom <mg> [hz] [s1] | imu off | imu bus 100|400");
  return true;
}
