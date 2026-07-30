// SPDX-License-Identifier: AGPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Kevin Guest (BionicBone)

/**************************************************************************
 * Workshop Environment Monitor (WEM)
 * ------------------------------------------------------------------------
 * Open-source ESP32-S3 workshop air quality monitor - CO2, TVOC, HCHO,
 * particulates, temperature and humidity, published to Home Assistant
 * over MQTT.
 *
 * Project : Workshop Environment Monitor (WEM)
 * Author  : Kevin Guest (BionicBone)
 * Created : May 2026
 * Source  : https://github.com/bionicbone/Workshop-Environment-Monitor
 * License : AGPL-3.0-or-later, NO WARRANTY - see LICENSE in repo root
 *
 * Distributed WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * WHY AGPL: this firmware links against ArduinoHA
 * (home-assistant-integration), which is licensed AGPL-3.0. Linking makes
 * the compiled firmware a single combined work, so the whole project is
 * released under the same terms. Anyone who receives a compiled binary is
 * entitled to the complete corresponding source - see the repository link
 * above.
 *
 * THIRD-PARTY LIBRARIES: this project builds against third-party libraries
 * distributed under BSD, MIT, Apache-2.0, LGPL-2.1 and AGPL-3.0 terms. Full
 * copyright notices and license texts are reproduced in
 * THIRD_PARTY_LICENSES.md in the repository root.
 *
 * NOT A SAFETY DEVICE: this is a hobby air-quality monitor. It is not a
 * certified gas detector, fire alarm or life-safety product, and must not
 * be relied upon as one. See the No-Warranty and Limitation of Liability
 * sections of the license above.
 *
 * Please keep this notice intact in copies, forks and redistributions -
 * the AGPL requires copyright and license notices to be preserved, and it
 * keeps this file's provenance attached to the code.
 **************************************************************************/

// TimeSync.cpp

// Workshop Environment Monitor - Local NTP time + MQTT/HA time cross-check
// See TimeSync.h for the two-time-source design rationale.

#include "TimeSync.h"
#include "Global.h"     // debugLoop
#include "HA_OTA.h"     // displayTime (MQTT-sourced HA time)
#include "Graphics.h"   // setDirtyHeader()
#include <time.h>

volatile TimeSyncState timeSyncState = TIMESYNC_NTP_FAILED;

// ============================================================
//  SETUP - call after setupWifi()
//  configTzTime() is async: SNTP sync happens in the background over the
//  next few seconds. Safe to call even before WiFi is fully connected.
// ============================================================
void setupTimeSync() {
  configTzTime(TIMESYNC_TZ, TIMESYNC_NTP_SERVER_1, TIMESYNC_NTP_SERVER_2);
  Serial.println("TimeSync: NTP configured (GMT/BST auto), waiting for first sync...");
}

// ============================================================
//  Parse "DD/MM/YYYY HH:MM" from displayTime.
//  Returns false if the string is still the unset placeholder or malformed.
// ============================================================
static bool parseDisplayTime(int& day, int& month, int& year, int& hour, int& minute) {
  if (sscanf(displayTime, "%d/%d/%d %d:%d", &day, &month, &year, &hour, &minute) != 5) {
    return false;
  }
  return (day != 0 && month != 0 && year != 0);   // rejects "--/--/---- --:--"
}

// ============================================================
//  LOOP - call every loop().
//
//  firstCheckDone forces the very first loop() call to check immediately,
//  rather than waiting for millis() to reach the full interval - otherwise
//  the header would sit on its compiled-in default (NTP SYNC FAILED) for
//  up to 5 minutes even after NTP had actually synced. While not yet
//  TIMESYNC_OK, polling happens every 5s instead of every 5min, so the
//  header corrects itself within seconds of NTP's first sync rather than
//  waiting for the next slow steady-state tick. Once state reaches
//  TIMESYNC_OK, cadence backs off to the normal 5-minute cross-check
//  interval.
// ============================================================
void loopTimeSync() {
  static unsigned long lastCheckMs = 0;
  static bool          firstCheckDone = false;
  unsigned long now = millis();

  unsigned long interval = (timeSyncState == TIMESYNC_OK)
    ? TIMESYNC_CHECK_INTERVAL_MS
    : TIMESYNC_RETRY_INTERVAL_MS;

  if (firstCheckDone && (now - lastCheckMs < interval)) return;
  firstCheckDone = true;
  lastCheckMs = now;

  struct tm ntpTime;
  bool ntpOk = getLocalTime(&ntpTime, 0);   // non-blocking check, 0ms timeout

  TimeSyncState newState;

  if (!ntpOk) {
    newState = TIMESYNC_NTP_FAILED;
    debugLoop("NTP not synced");
    debugSpecial("NTP not synced");
  }
  else {
    int mDay, mMonth, mYear, mHour, mMin;
    if (!parseDisplayTime(mDay, mMonth, mYear, mHour, mMin)) {
      // No MQTT time received yet - nothing to cross-check against.
      newState = TIMESYNC_OK;
    }
    else {
      int nDay = ntpTime.tm_mday, nMonth = ntpTime.tm_mon + 1, nYear = ntpTime.tm_year + 1900;
      int nMinOfDay = ntpTime.tm_hour * 60 + ntpTime.tm_min;
      int mMinOfDay = mHour * 60 + mMin;

      bool sameDate = (mDay == nDay && mMonth == nMonth && mYear == nYear);
      int  diffMin = abs(nMinOfDay - mMinOfDay);

      if (!sameDate || diffMin > TIMESYNC_TOLERANCE_MIN) {
        newState = TIMESYNC_MISMATCH;
        debugLoop("MISMATCH - NTP %02d:%02d vs MQTT %02d:%02d (same date=%d)",
          ntpTime.tm_hour, ntpTime.tm_min, mHour, mMin, sameDate);
        debugSpecial("MISMATCH - NTP %02d:%02d vs MQTT %02d:%02d (same date=%d)",
          ntpTime.tm_hour, ntpTime.tm_min, mHour, mMin, sameDate);
      }
      else {
        newState = TIMESYNC_OK;
      }
    }
  }

  if (newState != timeSyncState) {
    timeSyncState = newState;
    setDirtyHeader();
  }
}