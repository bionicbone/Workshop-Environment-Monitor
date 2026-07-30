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

 // LD2410B_Presence.h

// Workshop Environment Monitor - HLK-LD2410B mmWave Presence Sensor

#ifndef _LD2410B_PRESENCE_h
#define _LD2410B_PRESENCE_h

#if defined(ARDUINO) && ARDUINO >= 100
#include "arduino.h"
#else
#include "WProgram.h"
#endif

#include <ld2410.h>
#include "Backlight.h"

// ============================================================
//  PIN DEFINITIONS
// ============================================================
#define LD2410_RX_PIN     39					// ESP RX <- sensor UART Tx
#define LD2410_TX_PIN     40					// ESP TX -> sensor UART Rx
#define LD2410_GPIO_PIN   41					// Presence GPIO OUT (HIGH = presence)
#define LD2410_BAUD       256000

// ============================================================
//  DATA (read by Graphics.cpp for display)
// ============================================================
extern bool     ld2410Detected;       // true = sensor initialised at boot
extern bool     ld2410Presence;       // true = someone present
extern bool     ld2410StillPresence;  // true = stationary presence
extern uint16_t ld2410MovingEnergy;   // 0-100 moving target energy
extern uint16_t ld2410StillEnergy;    // 0-100 still target energy
extern uint16_t ld2410Distance;       // detection distance in cm

void setupLD2410B();
void loopLD2410B();										// call every loop()

#endif