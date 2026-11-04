/* -------------------------------------------------------------------------
   fw_update.h - a new firmware over the web: the web app sends a
   firmware.bin (POST /api/firmware), the clock writes it into its other
   program slot and restarts with it.

   Both boards have two program slots of the same size and the slot choice
   in otadata (partitions_clock_s3.csv, partitions_wallclock.csv). The
   running firmware stays untouched while the new one is written, so an
   upload that breaks off, a file that is no firmware or one for the other
   board changes nothing.

   A new firmware starts on trial (the core's app rollback, which the
   bootloader has: CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE). It confirms itself
   once it has run FW_TRIAL_MS with the home WiFi joined or the hotspot open,
   so the web app can reach it for the next update. One that does not get
   that far - a crash, a watchdog, no network - gives way to the firmware
   before it at the next start. The core would confirm every new firmware as
   it starts; verifyRollbackLater() (fw_update.cpp) leaves that to the clock.
   ------------------------------------------------------------------------- */
#ifndef FW_UPDATE_H
#define FW_UPDATE_H

#include <Arduino.h>
#include <WiFi.h>

const unsigned long FW_TRIAL_MS = 60000;   // a new firmware's run, with the network up, before it confirms itself

extern bool fwOnTrial;   // the running firmware came by an update and is not confirmed yet
extern bool fwGaveWay;   // an update after this firmware did not prove itself; the clock went back to this one

void fwTrialBegin();    // at start-up: whether this firmware is on trial
void fwTrialUpdate();   // on every pass: confirmed once it has proven itself
// POST /api/firmware: the body read at once into the other slot, the panel
// showing how far; restart is set when the clock is to start the new one.
void apiFirmwareUpload(WiFiClient &client, Print &out, long length, bool &restart);
// The slot a new firmware goes to, its size; 0 when there is none.
uint32_t fwUpdateMax();

#endif  // FW_UPDATE_H
