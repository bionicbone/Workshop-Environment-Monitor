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

// SHT40_TEMP_HUMIDITY.h

// Workshop Environment Monitor - Sensirion SHT40 Temperature & Humidity
//
// The SHT40 is the primary temperature and humidity source for the display.
// It overwrites the extern float temperature and humidity globals (declared
// in SCD40_CO2.h) so Graphics.cpp requires no changes.
// The SCD40 continues to report its own temp/humidity to HA independently.
//
// NOTE: Graphics.cpp reads temperature/humidity from SHT40 only, with no
// fallback to SCD40 or BME280, and stales them purely off lastSHT40OkMs.

#ifndef _SHT40_TEMP_HUMIDITY_h
#define _SHT40_TEMP_HUMIDITY_h

#if defined(ARDUINO) && ARDUINO >= 100
#include "arduino.h"
#else
#include "WProgram.h"
#endif

#include <Adafruit_SHT4x.h>
#include <ArduinoHA.h>
#include "Graphics.h"

// HA entities - declared here, defined in HA_OTA.cpp
extern HASensorNumber ha_sht40_temp;
extern HASensorNumber ha_sht40_humidity;

extern bool sht40Detected;            // true = sensor initialised at boot (optional-sensor gate)
extern unsigned long lastSHT40OkMs;   // millis() of last good read (staleness)

void setupSHT40();
void updateSHT40();

#endif