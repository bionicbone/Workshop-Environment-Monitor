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

// Backlight.cpp

#include "Backlight.h"
#include "Global.h"    // BACKLIGHT_PIN pin

// ============================================================
//  OPTIONAL DISPLAY GATE
//  Owned by Graphics.cpp, latched in setupTFT() - see "OPTIONAL DISPLAY
//  GATE" in Graphics.h for the full rationale.
//
//  Declared directly here rather than by including Graphics.h. Graphics.h
//  pulls in most of the project's headers, several of which sit upstream of
//  Backlight.h, so including it from this module would risk a circular
//  include chain for the sake of one bool. Same precedent as sht40Detected's
//  direct extern in Graphics.h.
// ============================================================
extern bool displayDetected;

// ============================================================
//  STATE
// ============================================================
static bool          blOn = true;
static unsigned long lastActivityMs = 0;

// Single point that touches BACKLIGHT_PIN and the HA entity, so the display
// gate lives here as well as at the entry points below - anything that
// reaches this function with no display fitted is a no-op rather than a
// stray GPIO write or a meaningless HA publish.
static void setBacklight(bool on) {
  if (!displayDetected) {
    return;
  }
  blOn = on;
  digitalWrite(BACKLIGHT_PIN, on ? HIGH : LOW);
  ha_backlight.setValue(on ? "ON" : "OFF");
  if (on) debugSpecial("ON");
  else    debugSpecial("OFF (timeout)");
}

static void resetTimer() {
  lastActivityMs = millis();
  if (!blOn) setBacklight(true);
}

void setPresenceDetected() {
  resetTimer();
}

void wakeBacklight() {
  resetTimer();
}

// blOn defaults to true, which is only meaningful once a display exists -
// fold the gate into the answer rather than leaving callers to remember it.
bool isBacklightOn() {
  return displayDetected && blOn;
}

// Must be called AFTER setupTFT(), which is what latches displayDetected.
// With no display fitted this returns before touching BACKLIGHT_PIN and
// before naming the HA entity - matching the optional-sensor pattern, where
// HA entities are configured only on the success branch. An unconfigured
// entity is simply never published, so nothing appears in Home Assistant.
void setupBacklight() {
  if (!displayDetected) {
    return;
  }

  pinMode(BACKLIGHT_PIN, OUTPUT);

  ha_backlight.setName("Display Backlight");
  ha_backlight.setIcon("mdi:monitor");

  setBacklight(true);
  lastActivityMs = millis();
  Serial.printf("[Backlight] Timeout: %lu min\n",
    BACKLIGHT_TIMEOUT_MS / 60000UL);
}

void loopBacklight() {
  if (!displayDetected) {
    return;
  }

  if (blOn && (millis() - lastActivityMs >= BACKLIGHT_TIMEOUT_MS)) {
    setBacklight(false);
  }
}

void publishBacklightStateHA() {
  if (!displayDetected) {
    return;
  }
  ha_backlight.setValue(blOn ? "ON" : "OFF");
}