// The clock's time: the NTP sync, the real-time clocks and the clock's
// daylight saving (time_sync.h).
#include "time_sync.h"

#include <RTClib.h>
#include "clock_state.h"
#include "clock_time.h"
#include "sensors.h"
#include "settings.h"
#include "time_zones.h"
#include "wifi_station.h"
#if BOARD_WAVESHARE_WALL
#include "rtc_pcf85063.h"
#endif

// What only this module calls, ahead of its use.
static bool syncNeedsRadio(SyncState s);
static time_t rtcRead();
static void rtcWrite(time_t utc);
#if BOARD_WAVESHARE_WALL
static void pcfSet(time_t utc);
#endif
static String clockStatusText();
static void saveSyncRecord();
static void ntpAsk();
static void syncSchedule();

uint8_t syncTimeHour = 5, syncTimeMinute = 11;
bool dstAutoActive = false;           // DST_AUTO: whether summer time is in effect now (derived from UTC, not stored)

SyncState     syncState    = SYNC_JOINING;    // the first sync is due at start-up
unsigned long syncDueSince = 0;               // millis() when the pending sync became due
static unsigned long syncAttemptSince = 0;    // millis() the running attempt (joining and asking) began
static unsigned long syncPausedSince  = 0;    // millis() the pause began
static unsigned long syncLastAsk  = 0;        // millis() of the last ask, for the retry pace
static const unsigned long NTP_RETRY_MS  = 3000;     // between asks while a sync is due

// A clock that has a time does not keep its radio on for a sync that does not
// work out: after SYNC_ATTEMPT_MS it switches it off and tries again after
// SYNC_PAUSE_MS. Without a time it keeps trying, since it has nothing to show.
static const unsigned long SYNC_ATTEMPT_MS = 60UL * 1000UL;
static const unsigned long SYNC_PAUSE_MS   = 10UL * 60UL * 1000UL;

TimeSource timeSource = TIME_NONE;
time_t     ntpSyncedAt = 0;   // UTC of the last good NTP sync; 0 = none known

// When the last good NTP sync was, kept over a restart (NVS key "ntp"), so a
// clock with a real-time clock
// syncs by its interval and not at every start. Written on every good sync
// while a DS3231 is there; without one the clock has no time at start-up and
// syncs then anyway.
#define SYNC_RECORD_MAGIC 0x5C7D
#define SYNC_RECORD_REV   1
struct SyncRecord {
  uint16_t magic;
  uint8_t  rev;
  uint8_t  reserved;
  uint32_t syncedAt;          // UTC
};
static SettingsStore<SyncRecord> syncStore;

// Real-time clock: a DS3231 on the I2C bus (0x68), optional. It holds UTC, as
// the clock does, so the zone and daylight saving never touch it. Found at
// boot or not at all; with one, the clock has its time at power-up and the
// NTP sync needs to run only now and then (uiSettings.syncDays).
static RTC_DS3231 rtc;
bool rtcOK        = false;   // a DS3231 answered at boot
bool rtcLostPower = false;   // its oscillator had stopped, so its time is not used until it is set again
#if BOARD_WAVESHARE_WALL
// The wall clock has two places for one: a DS3231 on the GPIO header, as on
// the MatrixPortals, and the board's own PCF85063 (src/rtc_pcf85063.h). There
// rtcOK says that either answered and rtcLostPower that the one whose time
// counts (rtcChip) has none. A DS3231 with a time comes first. Only it lets
// the NTP sync wait for its interval (rtcSync): the PCF85063's crystal has no
// temperature compensation, so the clock syncs daily as without a real-time
// clock, and the PCF85063 gives it its time at start-up.
static const uint8_t RTC_CHIP_NONE = 0, RTC_CHIP_DS3231 = 1, RTC_CHIP_PCF85063 = 2;
static uint8_t  rtcChip = RTC_CHIP_NONE;
static bool     dsOK = false;       // a DS3231 answered at boot
static bool     dsLost = false;     // and has no time
bool            rtcSync = false;    // a DS3231 with a time
static Pcf85063 pcf;
// When the PCF85063 was last set (UTC), kept over a restart (NVS key "pcf"),
// so each sync can log how far it drifted since.
#define PCF_RECORD_MAGIC 0x9C63
struct PcfRecord {
  uint16_t magic;
  uint8_t  rev;
  uint8_t  reserved;
  uint32_t setAt;
};
static SettingsStore<PcfRecord> pcfStore;
#endif

/* ======================================================================
   Daylight saving
   ====================================================================== */

// The clock's daylight saving: the mode chosen and, in DST_AUTO, whether
// summer time is in effect now. The zones and their rules: time_zones.h.
bool dstInEffect() {
  if (settings.dst == DST_AUTO) { return dstAutoActive; }
  return settings.dst == DST_SUMMER;
}

// Switch the daylight-saving mode. The clock holds UTC, so the time on the panel
// follows by itself (by one hour, or not at all); only the automatic mode needs
// to know at once whether summer time is in effect.
void setDstMode(uint8_t mode) {
  settings.dst = mode;
  if (mode == DST_AUTO) { dstAutoActive = clockIsSet() && dstActiveAt(tzRule(settings.tzOffset), clockNow()); }
}

// DST_AUTO, once a minute: follow the rule's changes (EU: 01:00 UTC on the last
// Sunday of March and October). Decided on UTC, so the hour repeated in autumn
// does not switch back again.
void updateAutoDst() {
  if (settings.dst != DST_AUTO || !clockIsSet()) { return; }
  bool active = dstActiveAt(tzRule(settings.tzOffset), clockNow());
  if (active == dstAutoActive) { return; }
  dstAutoActive = active;
  time_t local = clockNow() + tzTotalOffset();
  Console.print(active ? "DST auto -> summer, clock " : "DST auto -> winter, clock ");
  Console.print(clockHour(local)); Console.print(':'); Console.println(clockMinute(local));
}

// Effective timezone offset in seconds, including daylight saving.
long tzTotalOffset() {
  return (long)settings.tzOffset + (dstInEffect() ? 3600L : 0L);
}

// The one way to change the sync state. A sync becomes due when it is entered
// from a synced state; asking again after an empty answer keeps that moment, so
// syncOverdue() measures from when the sync was first wanted.
void setSyncState(SyncState next) {
  if (next == syncState) { return; }
  if (syncStateDue(next) && !syncStateDue(syncState)) { syncDueSince = millisNow; }
  if (syncAttempting(next) && !syncAttempting(syncState)) {
    syncAttemptSince = millisNow;
    netNtpRequestFresh();   // the station may be up already, for the info band: a new answer is wanted
  }
  if (syncNeedsRadio(next)) { radioUsers |= RADIO_SYNC; } else { radioUsers &= (uint8_t)~RADIO_SYNC; }
  if (next == SYNC_PAUSED) { syncPausedSince = millisNow; }
  DEBUG_LOG("sync %u -> %u\n", (unsigned)syncState, (unsigned)next);
  syncState = next;
}


// Whether a sync is due in this state: wanted, and not done yet.
bool syncStateDue(SyncState s) {
  return s == SYNC_JOINING || s == SYNC_FETCHING || s == SYNC_RETRY_WAIT || s == SYNC_PAUSED;
}

// Whether this state is part of an attempt, with the radio on: joining, then
// asking NTP until it answers.
bool syncAttempting(SyncState s) {
  return s == SYNC_JOINING || s == SYNC_FETCHING || s == SYNC_RETRY_WAIT;
}

// Whether the sync needs the station in this state: during an attempt, and a
// minute early so it has joined by the sync time.
bool syncNeedsRadio(SyncState s) {
  return s == SYNC_WAKING || syncAttempting(s);
}

// The one way to set the clock: UTC, and where it came from. The automatic
// daylight saving is decided again at once for the new time. A real-time
// clock keeps every time the clock is given; a time read from it is not
// written back, since every write starts its second afresh.
void setClockTo(time_t utc, TimeSource source) {
  clockSet(utc);
  timeSource = source;
  if (source != TIME_RTC) { rtcWrite(utc); }
  if (settings.dst == DST_AUTO) { dstAutoActive = dstActiveAt(tzRule(settings.tzOffset), utc); }
  panelDirty = true;
}


/* ---- Real-time clock (DS3231) -------------------------------------------- */

#if BOARD_WAVESHARE_WALL
// The name of the real-time clock whose time counts, for the web app and the
// console; "" without one.
const char *rtcChipName() {
  return rtcChip == RTC_CHIP_DS3231 ? "DS3231" : rtcChip == RTC_CHIP_PCF85063 ? "PCF85063" : "";
}

// At boot: which real-time clocks answer - a DS3231 on the GPIO header, the
// board's PCF85063 - and the time of the first that has one, the DS3231
// before the PCF85063. One without a time (its oscillator stopped, or the
// PCF85063 never set by this clock) gives none, so the clock stays without
// one until NTP or the user sets it - the face never shows a time it does
// not have.
void rtcBegin() {
  pcfStore.begin("pcf");
  sensorTries[SENSOR_RTC] = sensorBegin(SENSOR_RTC, []() { return rtc.begin(&Wire); });
  dsOK = sensorTries[SENSOR_RTC] != 0;
  time_t dsUtc = 0;
  if (dsOK) {
    dsLost = rtc.lostPower();
    if (!dsLost) { dsUtc = rtcRead(); dsLost = (dsUtc == 0); }
    Console.println(dsLost ? "DS3231 found, but it has no time" : "DS3231 found");
  } else {
    Console.println("DS3231 not found");
  }
  time_t pcfUtc = 0;
  sensorTries[SENSOR_BOARD_RTC] = sensorBegin(SENSOR_BOARD_RTC, []() { return pcf.begin(Wire1); });
  if (sensorTries[SENSOR_BOARD_RTC]) {
    Console.printf("PCF85063 found: Control_1 0x%02X, Control_2 0x%02X, Offset %d, %s\n", pcf.control1(),
                  pcf.control2(), pcf.offset(), pcf.kept() ? "holds a time of this clock's" : "holds no time of this clock's");
    if (pcf.kept()) { pcfUtc = pcf.read(); }
    if (pcfUtc == 0) {
      // Lost power, or never set by this clock: started afresh, set with the next time.
      if (!pcf.reset()) { Console.println("PCF85063: the reset failed"); }
    } else if (!pcf.clkoutOff()) {
      Console.println("PCF85063: CLKOUT did not switch off");
    }
  } else {
    Console.println("PCF85063 not found");
  }
  rtcOK = dsOK || pcf.found();
  if (dsUtc != 0) {
    rtcChip = RTC_CHIP_DS3231;
    setClockTo(dsUtc, TIME_RTC);
    if (pcf.found() && pcfUtc == 0) { pcfSet(dsUtc); }   // the PCF85063 takes the DS3231's
    Console.print("Clock set from the DS3231: UTC "); Console.println((long)dsUtc);
  } else if (pcfUtc != 0) {
    rtcChip = RTC_CHIP_PCF85063;
    setClockTo(pcfUtc, TIME_RTC);
    Console.print("Clock set from the PCF85063: UTC "); Console.println((long)pcfUtc);
  } else {
    rtcChip = dsOK ? RTC_CHIP_DS3231 : pcf.found() ? RTC_CHIP_PCF85063 : RTC_CHIP_NONE;
    if (rtcOK) { Console.println("No real-time clock has a time - none until NTP or a time set by hand"); }
  }
  rtcLostPower = rtcOK && !clockIsSet();
  rtcSync = dsOK && !dsLost;
}

// Every real-time clock found takes the new time; that also clears the
// DS3231's lost-power flag. From then on the DS3231's time counts when there
// is one, else the PCF85063's.
void rtcWrite(time_t utc) {
  if (!rtcOK || (int64_t)utc < UTC_PLAUSIBLE_FROM || (int64_t)utc > UTC_PLAUSIBLE_TO) { return; }
  if (dsOK) {
    rtc.adjust(DateTime((uint32_t)utc));
    dsLost = false;
    Console.println("DS3231 set");
  }
  if (pcf.found()) { pcfSet(utc); }
  rtcChip = dsOK ? RTC_CHIP_DS3231 : RTC_CHIP_PCF85063;
  rtcLostPower = false;
  rtcSync = dsOK;
}

// The PCF85063 takes a new time, and says how far it had drifted since it was
// last set: plus is ahead. From NTP within PCF_KEEP_S it is left running
// instead, so its drift adds up over the days and the log shows its real rate
// (ppm) - with a write every day it would show one day at one-second steps.
const long PCF_KEEP_S = 2;
void pcfSet(time_t utc) {
  time_t had = pcf.kept() ? pcf.read() : 0;
  if (had != 0) {
    long apart = (long)(had - utc);
    PcfRecord rec;
    pcfStore.read(rec);
    if (rec.magic == PCF_RECORD_MAGIC && (int64_t)rec.setAt >= UTC_PLAUSIBLE_FROM && (time_t)rec.setAt < utc) {
      long since = (long)(utc - (time_t)rec.setAt);
      Console.printf("PCF85063: %+ld s from the new time, %.1f h after it was set (%+.0f ppm)\n", apart,
                    since / 3600.0f, apart * 1e6f / since);
    } else {
      Console.printf("PCF85063: %+ld s from the new time\n", apart);
    }
    if (timeSource == TIME_NTP && apart >= -PCF_KEEP_S && apart <= PCF_KEEP_S) {
      Console.println("PCF85063 left running");
      return;
    }
  }
  if (!pcf.write(utc)) { Console.println("PCF85063: not set, the bus failed"); return; }
  PcfRecord rec = { PCF_RECORD_MAGIC, 1, 0, (uint32_t)utc };
  pcfStore.write(rec);
  Console.println("PCF85063 set");
}

// Once a minute, half way through it: the clock counts on millis(), whose
// crystal drifts far more than the DS3231's 2 ppm, so it is pulled back to the
// DS3231 once they are two seconds apart (one second can be the moment of
// reading alone). Not to the PCF85063: its crystal is no better than the
// ESP32's.
void rtcFollow() {
  if (!dsOK || dsLost || !clockIsSet()) { return; }
  time_t utc = rtcRead();
  if (utc == 0) { return; }
  long apart = (long)(utc - clockNow());
  if (apart >= -1 && apart <= 1) { return; }
  setClockTo(utc, TIME_RTC);
  Console.print("Clock pulled back to the DS3231 by "); Console.print(apart); Console.println(" s");
}

// Console "rtc": what each real-time clock holds against the clock.
void wallRtcReport() {
  time_t now = clockIsSet() ? clockNow() : 0;
  if (dsOK) {
    time_t t = dsLost ? 0 : rtcRead();
    Console.printf("DS3231: %s\n", t ? (String("UTC ") + (long)t + ", " + (long)(t - now) + " s from the clock").c_str()
                                    : "no time");
  } else {
    Console.println("DS3231: not found");
  }
  if (pcf.found()) {
    time_t t = pcf.kept() ? pcf.read() : 0;
    Console.printf("PCF85063: %s; Control_1 0x%02X, Control_2 0x%02X\n",
                  t ? (String("UTC ") + (long)t + ", " + (long)(t - now) + " s from the clock").c_str() : "no time",
                  pcf.control1(), pcf.control2());
  } else {
    Console.println("PCF85063: not found");
  }
  Console.printf("Time counts from: %s; NTP waits for its interval: %s\n", rtcOK ? rtcChipName() : "none",
                rtcSync ? "yes" : "no, daily");
}
#else
// At boot: whether a DS3231 answers, and its time if it has one. One whose
// oscillator has stopped (the coin cell ran empty, or it never had one) has
// no time worth showing, so the clock stays without one until NTP or the user
// sets it - the face never shows a time it does not have.
void rtcBegin() {
  sensorTries[SENSOR_RTC] = sensorBegin(SENSOR_RTC, []() { return rtc.begin(&Wire); });
  rtcOK = sensorTries[SENSOR_RTC] != 0;
  if (!rtcOK) { Console.println("DS3231 not found - no real-time clock"); return; }
  if (rtc.lostPower()) {
    rtcLostPower = true;
    Console.println("DS3231 found, but it lost power - no time until NTP or a time set by hand");
    return;
  }
  time_t utc = rtcRead();
  if (utc == 0) {
    rtcLostPower = true;   // running, but not with a time of this century: as good as none
    Console.println("DS3231 found, but its time is not plausible");
    return;
  }
  setClockTo(utc, TIME_RTC);
  Console.print("DS3231 found, clock set from it: UTC "); Console.println((long)utc);
}
#endif

// The DS3231's time as UTC, or 0 when it is outside the plausible range (a read
// that went wrong reads as garbage).
time_t rtcRead() {
  uint32_t utc = rtc.now().unixtime();
  if ((int64_t)utc < UTC_PLAUSIBLE_FROM || (int64_t)utc > UTC_PLAUSIBLE_TO) { return 0; }
  return (time_t)utc;
}

#if !BOARD_WAVESHARE_WALL
// Set the RTC; that also clears its lost-power flag.
void rtcWrite(time_t utc) {
  if (!rtcOK || (int64_t)utc < UTC_PLAUSIBLE_FROM || (int64_t)utc > UTC_PLAUSIBLE_TO) { return; }
  rtc.adjust(DateTime((uint32_t)utc));
  rtcLostPower = false;
  Console.println("DS3231 set");
}

// Once a minute, half way through it: the clock counts on millis(), whose
// crystal drifts far more than the DS3231's 2 ppm, so it is pulled back to the
// RTC once they are two seconds apart. One second apart can be the moment of
// reading alone - the RTC's second has a phase of its own - and is left be.
void rtcFollow() {
  if (!rtcOK || rtcLostPower || !clockIsSet()) { return; }
  time_t utc = rtcRead();
  if (utc == 0) { return; }
  long apart = (long)(utc - clockNow());
  if (apart >= -1 && apart <= 1) { return; }
  setClockTo(utc, TIME_RTC);
  Console.print("Clock pulled back to the DS3231 by "); Console.print(apart); Console.println(" s");
}
#endif

// Whether the clock syncs with NTP at all. Without a real-time clock always,
// daily as it always has; with one, unless the interval is set to none. On
// the wall clock only a DS3231 with a time counts here (rtcSync): with the
// PCF85063 alone the clock syncs as without a real-time clock.
bool ntpSyncOn() {
  return !rtcSync || uiSettings.syncDays > 0;
}

// Whether the sync time at `when` (UTC) brings a sync. Without a real-time
// clock (rtcSync, as above) every day. With one, once the interval has run
// out: counted in the clock's own calendar days since the last good sync, so
// the sync time of the day it runs out counts, whatever time of day that sync
// was made.
bool syncDueAt(time_t when) {
  if (!rtcSync) { return true; }
  if (uiSettings.syncDays == 0) { return false; }
  if (ntpSyncedAt == 0) { return true; }
  long offset = tzTotalOffset();
  long days = (long)((when + offset) / 86400) - (long)((ntpSyncedAt + offset) / 86400);
  return days >= (long)uiSettings.syncDays;
}

// The UTC instant of a wall-clock time in the clock's zone and daylight-saving
// mode. In the automatic mode summer time counts if it is in effect an hour
// before the standard-time reading; in the hour that repeats in autumn that
// picks the summer-time one.
time_t localToUtc(time_t local) {
  time_t utc = local - settings.tzOffset;
  bool summer = (settings.dst == DST_AUTO) ? dstActiveAt(tzRule(settings.tzOffset), utc - 3600)
                                           : (settings.dst == DST_SUMMER);
  return summer ? utc - 3600 : utc;
}

// One line for the config page: the clock's date and time, its offset from
// UTC, and where the time came from - or that it has none.
String clockStatusText() {
  if (!clockIsSet()) { return "The clock has no time yet."; }
  long offset = tzTotalOffset();
  time_t local = clockNow() + offset;
  struct tm t;
  gmtime_r(&local, &t);
  char line[64], zone[10];
  formatOffset(zone, sizeof(zone), offset);
  const char *source = (timeSource == TIME_NTP)   ? "from the internet (NTP)"
                     : (timeSource == TIME_PHONE) ? "set from a phone"
                     : (timeSource == TIME_RTC)   ? "kept by the real-time clock" : "set by hand";
  snprintf(line, sizeof(line), "Clock: %04d-%02d-%02d %02d:%02d %s, %s",
           t.tm_year + 1900, t.tm_mon + 1, t.tm_mday, t.tm_hour, t.tm_min, zone, source);
  return String(line);
}

// The clock's line, and what keeps its time over a power loss: the config
// page's status of the time, sent again whenever the time is set there.
String timeStatusText() {
  String s = clockStatusText();
  if (clockIsSet()) { s += "."; }   // the line about the clock ends here; the one about the RTC follows
#if BOARD_WAVESHARE_WALL
  if (!rtcOK) { s += " No real-time clock: a time set here lasts until the next power loss."; }
  else if (rtcLostPower) {
    s += " The real-time clock ("; s += rtcChipName();
    s += ") had lost power and has no time until one is set here or comes from the internet.";
  }
  else if (rtcChip == RTC_CHIP_PCF85063) {
    s += " The board's real-time clock (PCF85063) keeps the time over a restart, and over a power loss with a coin cell in its socket.";
  }
  else { s += " The real-time clock (DS3231) keeps the time without power."; }
#else
  if (!rtcOK)            { s += " No real-time clock: a time set here lasts until the next power loss."; }
  else if (rtcLostPower) { s += " The real-time clock (DS3231) had lost power and has no time until one is set here or comes from the internet."; }
  else                   { s += " The real-time clock (DS3231) keeps the time without power."; }
#endif
  return s;
}


// At start-up, after the DS3231 was read: when the last good sync was, if one
// is stored and the clock has its time from the RTC. A stored sync later than
// that time is not taken - an RTC set back would otherwise put the next sync
// off by as much.
void loadSyncRecord() {
  syncStore.begin("ntp");
  if (!rtcOK || rtcLostPower || !clockIsSet()) { return; }
  SyncRecord rec;
  syncStore.read(rec);
  if (rec.magic != SYNC_RECORD_MAGIC) { return; }
  if ((int64_t)rec.syncedAt < UTC_PLAUSIBLE_FROM || (int64_t)rec.syncedAt > (int64_t)clockNow() + 60) { return; }
  ntpSyncedAt = (time_t)rec.syncedAt;
}

void saveSyncRecord() {
  SyncRecord rec = { SYNC_RECORD_MAGIC, SYNC_RECORD_REV, 0, (uint32_t)ntpSyncedAt };
  syncStore.write(rec);
}

// Ask NTP for the time once (board_hal.h: lwIP's SNTP client). netNtpEpoch()
// returns 0 until SNTP has an answer, which takes
// a few seconds after joining.
void ntpAsk() {
  syncLastAsk = millisNow;
  time_t utc = netNtpEpoch();
  if (utc == 0) {
    Console.println("NTP failed");
    setSyncState(SYNC_RETRY_WAIT);
    return;
  }
#if defined(CLOCK_DEBUG) && defined(DST_TEST_UTC)
  utc = DST_TEST_UTC;       // debug test: start just before a daylight-saving change
  settings.dst = DST_AUTO;  // in RAM only
#endif
  // How far the clock had drifted from NTP, in seconds: positive when the clock
  // was slow, negative when it was fast. Only logged.
  long drift = clockIsSet() ? (long)(utc - clockNow()) : 0;
  setClockTo(utc, TIME_NTP); // UTC as delivered; zone and daylight saving are added for the panel
  ntpSyncedAt = utc;
  if (rtcOK) { saveSyncRecord(); }
  if (wifiStored() && !wifiCreds.verified) {   // the first sync over this WiFi: a known-good config now
    wifiCreds.verified = 1;
    saveWifiCreds();
    Console.println("WiFi: config works, stored as known good");
  }
  Console.println("NTP success");
  Console.print("NTP offset: ");
  Console.println(drift);
  netSntpStop();
  setSyncState(SYNC_IDLE);   // the radio goes off unless the info band still needs it
}

// Once a minute: wake the radio one minute before the sync time, and make the
// sync due at the sync time. Counted in minutes since midnight so it wraps
// across the hour and midnight (sync 05:00 -> 04:59, 00:00 -> 23:59);
// "syncTimeMinute-1" alone is -1 for minute 00 and never matched, which once
// stopped the daily resync for good. With a real-time clock only the sync
// times syncDueAt() picks count; the wake-up asks about the minute to come.
void syncSchedule() {
  const uint16_t minutesPerDay = 24 * 60;
  uint16_t nowMinute  = hourNow * 60 + minuteNow;
  uint16_t syncMinute = syncTimeHour * 60 + syncTimeMinute;
  if (syncState == SYNC_IDLE && nowMinute == (syncMinute + minutesPerDay - 1) % minutesPerDay &&
      syncDueAt(clockNow() + 60)) {
    setSyncState(SYNC_WAKING);
  }
  if ((syncState == SYNC_IDLE || syncState == SYNC_WAKING) && nowMinute == syncMinute) {
    if (!syncDueAt(clockNow())) {
      // Woken for a sync that is no longer wanted (the interval was changed
      // in the minute between): back to sleep.
      if (syncState == SYNC_WAKING) { setSyncState(SYNC_IDLE); }
      return;
    }
    // Still idle means the early wake-up was missed, e.g. because the config AP
    // was up a minute ago; the station joins now instead of failing until tomorrow.
    Console.println("Renew NTP sync");
    setSyncState(SYNC_JOINING);
  }
}

// Keeps the clock (UTC) in step with NTP, with the station wanted only while
// a sync needs it (setSyncState()). Runs on every pass and never waits: the
// clock stays on the panel and operable while the station joins.
void timeSync_WifiLib() {
  // An attempt that has not worked out within SYNC_ATTEMPT_MS: with a time on
  // the clock it is not worth keeping the radio on, so pause and try later.
  if (syncAttempting(syncState) && clockIsSet() && millisNow - syncAttemptSince >= SYNC_ATTEMPT_MS) {
    Console.println("NTP sync did not work out - trying again in 10 min");
    setSyncState(SYNC_PAUSED);
  }
  switch (syncState) {
    case SYNC_PAUSED:
      if (millisNow - syncPausedSince >= SYNC_PAUSE_MS) { setSyncState(SYNC_JOINING); }
      break;
    case SYNC_JOINING:
      // No WiFi stored: the station stays off, and the sync stays due - after
      // SYNC_LATE_MS the status pixel says so.
      if (stationUp()) { setSyncState(SYNC_FETCHING); }
      break;
    case SYNC_FETCHING:
      ntpAsk();
      break;
    case SYNC_RETRY_WAIT:
      // Retried on its own pace, not on the minute change: gating the retry
      // behind minuteTrigger once left the clock at 1970 for up to a minute
      // after boot although the WiFi was connected. A station that has dropped
      // out has to join again first.
      if (millisNow - syncLastAsk > NTP_RETRY_MS) {
        Console.println("NTP Retry");
        setSyncState(stationUp() ? SYNC_FETCHING : SYNC_JOINING);
      }
      break;
    default:
      break;
  }
  if (minuteTrigger) { syncSchedule(); }
}

#if !BOARD_WAVESHARE_WALL
// The real-time clock's name for the web app: the MatrixPortal S3 has a place
// for a DS3231 only. (The wall clock's is with its real-time clocks.)
const char *rtcChipName() {
  return rtcOK ? "DS3231" : "";
}
#endif
