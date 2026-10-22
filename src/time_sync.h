/* -------------------------------------------------------------------------
   time_sync.h - where the clock's time comes from: NTP over the station,
   a real-time clock (a DS3231, on the wall clock also the board's
   PCF85063), a phone or the user; when it syncs; and the zone and daylight
   saving it is shown in (the zones themselves: time_zones.h).

   One sync state instead of the flags ntpSuccess, ntpRequestActive and
   wifiEnabled. ntpSuccess used to do two jobs - colour the status pixel
   and, cleared at the sync minute, ask for the daily sync - so the pixel
   turned red every day without anything having failed. The state changes
   only through setSyncState(). Nothing waits for the WiFi: setup() only
   starts the join, and the clock is on the panel and operable while the
   join and the sync run here.

   A module of its own since 2026-10-22 (code review of 2026-10-14, point 10).
   ------------------------------------------------------------------------- */
#ifndef TIME_SYNC_H
#define TIME_SYNC_H

#include <Arduino.h>
#include <time.h>
#include "board_hal.h"

// ---- The NTP sync --------------------------------------------------------------
enum SyncState : uint8_t {
  SYNC_IDLE,        // synced, radio off, waiting for the next sync time
  SYNC_WAKING,      // radio on one minute early, so it has joined by the sync time
  SYNC_JOINING,     // a sync is due and the station is not connected: re-join every JOIN_RETRY_MS
  SYNC_FETCHING,    // a sync is due and the station is connected: ask for the time on this pass
  SYNC_RETRY_WAIT,  // the last ask came back empty: wait NTP_RETRY_MS, then ask again
  SYNC_PAUSED,      // an attempt failed although the clock has a time: radio off, try again after SYNC_PAUSE_MS
};
extern SyncState     syncState;
extern unsigned long syncDueSince;    // millis() when the pending sync became due
extern uint8_t syncTimeHour, syncTimeMinute;   // the daily sync's time (applySettings())
extern time_t  ntpSyncedAt;           // UTC of the last good NTP sync; 0 = none known

// A due sync that has not succeeded for this long is an error, shown on the
// status pixel. The hour covers the retries, so a sync in progress never shows.
const unsigned long SYNC_LATE_MS = 60UL * 60UL * 1000UL;

// The one way to change the sync state. A sync becomes due when it is entered
// from a synced state; asking again after an empty answer keeps that moment, so
// syncOverdue() measures from when the sync was first wanted.
void setSyncState(SyncState next);
bool syncStateDue(SyncState s);     // a sync is due in this state: wanted, and not done yet
bool syncAttempting(SyncState s);   // part of an attempt, with the radio on
bool ntpSyncOn();                   // the clock syncs with NTP at all
bool syncDueAt(time_t when);        // the sync time at `when` (UTC) brings a sync
// At start-up, after the DS3231 was read: when the last good sync was.
void loadSyncRecord();
// Keeps the clock (UTC) in step with NTP, with the station wanted only while
// a sync needs it. Runs on every pass and never waits.
void timeSync_WifiLib();

// ---- Setting the clock -----------------------------------------------------------
// Where the clock's time came from, for the config page.
enum TimeSource : uint8_t { TIME_NONE, TIME_NTP, TIME_PHONE, TIME_MANUAL, TIME_RTC };
extern TimeSource timeSource;

// The range a time has to lie in to be taken: anything before 2024 is a phone
// or a real-time clock without a time, and the DS3231 counts only to 2099.
// 64 bits: the end of 2099 is past a 32-bit long, which both boards have, and
// as a long it turned negative, so every time was refused (2026-10-10).
const int64_t UTC_PLAUSIBLE_FROM = 1704067200LL;   // 2024-01-01
const int64_t UTC_PLAUSIBLE_TO   = 4102444799LL;   // 2099-12-31 23:59:59

// The one way to set the clock: UTC, and where it came from.
void setClockTo(time_t utc, TimeSource source);
// The UTC instant of a wall-clock time in the clock's zone and daylight-saving mode.
time_t localToUtc(time_t local);
// The config page's status of the time: the clock's line, and what keeps its
// time over a power loss.
String timeStatusText();

// ---- The real-time clocks ----------------------------------------------------------
extern bool rtcOK;          // a real-time clock answered at boot
extern bool rtcLostPower;   // the one whose time counts has none until it is set again
#if BOARD_WAVESHARE_WALL
extern bool rtcSync;        // a DS3231 with a time: only it lets the NTP sync wait for its interval
void wallRtcReport();       // the console's "rtc"
#else
#define rtcSync rtcOK       // on the MatrixPortals the DS3231 is the only real-time clock
#endif
const char *rtcChipName();  // the one whose time counts, for the web app and the console; "" without one
void rtcBegin();            // at boot: which answer, and the time of the first that has one
void rtcFollow();           // once a minute: the clock pulled back to the DS3231 when they are apart

// ---- The clock's daylight saving ------------------------------------------------------
extern bool dstAutoActive;  // DST_AUTO: whether summer time is in effect now (derived from UTC, not stored)
bool dstInEffect();
void setDstMode(uint8_t mode);   // switch the daylight-saving mode
void updateAutoDst();            // DST_AUTO, once a minute: follow the rule's changes
long tzTotalOffset();            // the effective offset from UTC in seconds, with daylight saving

#endif  // TIME_SYNC_H
