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

// PMS5003_Particles.h

#ifndef _PMS5003_SENSOR_h
#define _PMS5003_SENSOR_h

#if defined(ARDUINO) && ARDUINO >= 100
#include "arduino.h"
#else
#include "WProgram.h"
#endif

#include "Global.h"
#include "Graphics.h"
#include <ArduinoHA.h>
#include <Adafruit_PM25AQI.h>

extern PM25_AQI_Data pmsData;

extern bool pms5003Detected;            // true = sensor initialised at boot (optional-sensor gate)
extern unsigned long lastPMS5003OkMs;   // millis() of last good read (staleness)

// Weight concentration (ug/m3) - Standard
extern HASensorNumber ha_pms_pm10_std;
extern HASensorNumber ha_pms_pm25_std;
extern HASensorNumber ha_pms_pm100_std;

// Weight concentration (ug/m3) - Atmospheric
extern HASensorNumber ha_pms_pm10_atm;
extern HASensorNumber ha_pms_pm25_atm;
extern HASensorNumber ha_pms_pm100_atm;

// Particle counts (per 0.1L)
extern HASensorNumber ha_pms_cnt_03;
extern HASensorNumber ha_pms_cnt_05;
extern HASensorNumber ha_pms_cnt_10;
extern HASensorNumber ha_pms_cnt_25;
extern HASensorNumber ha_pms_cnt_50;
extern HASensorNumber ha_pms_cnt_100;

void setupPMS5003();
void loopPMS5003();						// call every loop() - owns the full 120s duty cycle
bool updatePMS5003();					// true if a frame was read and published this call
void sleepPMS5003();
void wakePMS5003();
void markPMS5003Stable();			// call 30s after wake
void resetPMS5003Stable();		// call when wake command is sent

#endif