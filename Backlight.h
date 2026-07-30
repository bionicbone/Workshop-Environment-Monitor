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

 // Backlight.h

// Workshop Environment Monitor - Backlight Manager
//
// Presence-based control via LD2410B mmWave sensor:
//   Presence detected  -> backlight on, reset inactivity timer
//   No presence + timeout -> backlight off
//
// Touch-to-wake: with an LD2410B fitted, presence keeps the display alive and
// touch is used only for alarm snooze (owned by Buzzer.cpp). But if NO LD2410B
// is fitted there is no presence signal, so a timed-out display would stay off
// forever. To cover that case Buzzer.cpp calls wakeBacklight() on a touch when
// the display is off. Touch reads still live entirely in Buzzer.cpp - this
// module just exposes the wake action and the on/off state.
//
// setPresenceDetected() is called by LD2410B_Presence.cpp on every
// loop() where presence is confirmed - it resets the timer and
// wakes the backlight if it was off.
//
// OPTIONAL DISPLAY: the TFT is optional (see "OPTIONAL DISPLAY GATE" in
// Graphics.h). With no display fitted every function in this module is a
// no-op: BACKLIGHT_PIN is never configured or driven, the HA backlight
// entity is never named and so never published, and isBacklightOn() reports
// false. Callers do not need to check anything - setPresenceDetected() in
// particular still fires from LD2410B_Presence.cpp on a headless unit with a
// presence sensor fitted, and is harmless. setupBacklight() must be called
// AFTER setupTFT(), which is what latches the detection flag.

#ifndef _BACKLIGHT_h
#define _BACKLIGHT_h

#if defined(ARDUINO) && ARDUINO >= 100
#include "arduino.h"
#else
#include "WProgram.h"
#endif

#include <ArduinoHA.h>
#include "HA_OTA.h"   // for ha_backlight extern

// ============================================================
//  TIMEOUT
// ============================================================
#define BACKLIGHT_TIMEOUT_MS  (5UL * 60UL * 1000UL)   // 5 minutes

void setupBacklight();
void loopBacklight();
void setPresenceDetected();   // called by LD2410B_Presence.cpp
void wakeBacklight();         // manual wake (touch) - called by Buzzer.cpp
bool isBacklightOn();         // current on/off state - read by Buzzer.cpp
void publishBacklightStateHA();

#endif