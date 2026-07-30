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

// TimeSync.h

// Workshop Environment Monitor - Local NTP time + MQTT/HA time cross-check
//
// Two independent time sources exist in this firmware:
//   1. Local NTP (this module) - synced via configTzTime(), used purely to
//      power getTimestamp() (Global.cpp) for debug/serial timestamp logging.
//      Starts syncing early in setup(), independent of MQTT/HA availability.
//   2. MQTT "workshop/time" (HA_OTA.cpp, displayTime) - the value shown on
//      the TFT header, sourced from Home Assistant. Minute resolution, no
//      seconds, published on minute-change.
//
// This module does NOT replace either source. It periodically cross-checks
// them and exposes a state Graphics.cpp uses to warn on the header if they
// disagree - which would otherwise silently undermine the point of adding
// NTP timestamps in the first place (correlating serial logs against HA
// history).

#ifndef _TIMESYNC_h
#define _TIMESYNC_h

#if defined(ARDUINO) && ARDUINO >= 100
#include "arduino.h"
#else
#include "WProgram.h"
#endif

// ============================================================
//  TIMEZONE
//  POSIX TZ string for the UK - configTzTime() handles GMT/BST transitions
//  automatically from this, no manual offset logic needed.
// ============================================================
#define TIMESYNC_TZ  "GMT0BST,M3.5.0/1,M10.5.0"

// ============================================================
//  NTP SERVERS
// ============================================================
#define TIMESYNC_NTP_SERVER_1  "uk.pool.ntp.org"
#define TIMESYNC_NTP_SERVER_2  "pool.ntp.org"

// ============================================================
//  CHECK INTERVAL / TOLERANCE
//  MQTT time has minute resolution only (no seconds), so a 1-minute
//  tolerance absorbs normal rounding at a minute boundary without masking
//  a genuine mismatch (clock drift, wrong timezone, stuck retained message).
// ============================================================
#define TIMESYNC_CHECK_INTERVAL_MS  (5UL * 60UL * 1000UL)   // 5 minutes - steady-state cross-check
#define TIMESYNC_RETRY_INTERVAL_MS  (5UL * 1000UL)          // 5 seconds - while not yet synced
#define TIMESYNC_TOLERANCE_MIN      1

// ============================================================
//  SYNC STATE  (read by Graphics.cpp updateHeader())
// ============================================================
enum TimeSyncState {
  TIMESYNC_OK,            // NTP synced; MQTT time (if present) agrees within tolerance
  TIMESYNC_NTP_FAILED,    // local NTP has never synced
  TIMESYNC_MISMATCH       // both sources present but disagree beyond tolerance
};

extern volatile TimeSyncState timeSyncState;

void setupTimeSync();
void loopTimeSync();   // call every loop() - internally gated to TIMESYNC_CHECK_INTERVAL_MS

#endif