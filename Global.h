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

// Global.h

#ifndef _GLOBAL_h
#define _GLOBAL_h

#if defined(ARDUINO) && ARDUINO >= 100
#include "arduino.h"
#else
#include "WProgram.h"
#endif

// Debugging Options (ESP32 Version)
// DEBUG 0 = Debugging Serial Messages are switched off
// DEBUG 1 = Debugging Serial Messages are switched on
// DEBUG 2 = Dedugging Only Special Serial Messages are switch on

#define DEBUG 0

// ============================================================
//  TIMESTAMP HELPER  (used by debugLoop/debugSpecial below)
//  Defined in Global.cpp. Returns "[HH:MM:SS][123456us]" - wall clock once
//  NTP has synced (see TimeSync.h - setupTimeSync() starts the sync early in
//  setup()), falling back to "[??:??:??][123456us]" before that (e.g. during
//  early boot, before WiFi/NTP are up) so prints still carry a usable
//  relative timestamp via micros().
// ============================================================
const char* getTimestamp();

#if DEBUG == 1
#define debugLoop(fmt, ...) Serial.printf("%s %s: " fmt "\r\n", getTimestamp(), __func__, ##__VA_ARGS__)		// Serial Debugging On
#else
#define debugLoop(fmt, ...)			// Serial Debugging Off
#endif
#if DEBUG == 2
#define debugSpecial(fmt, ...) Serial.printf("%s %s: " fmt "\r\n", getTimestamp(), __func__, ##__VA_ARGS__) // Serial Debugging On
#else
#define debugSpecial(fmt, ...)  // Serial Debugging Off
#endif

// ============================================================
//  OPTIONAL-SENSOR DETECTION GATE
//  Shared by every gated sensor's setup function - LD2410B, SGP30, BME280,
//  SHT40, SCD40, PMS5003. Single source of truth for the retry timing, so
//  every gated sensor uses the same window and pacing.
//
//  SENSOR_BEGIN_RETRY_MS - total window a sensor gets to answer at boot
//  before it is written off for the whole session. Detecting a fitted sensor
//  correctly matters more than boot speed: the display and backlight are
//  already up before sensor init runs, so a slow boot is not user-visible.
//
//  SENSOR_BEGIN_RETRY_DELAY_MS - pause between attempts. Without it, an
//  absent I2C sensor produces ~3 seconds of continuous back-to-back failed
//  transactions. A sensor that needs seconds to wake does not care about
//  100ms granularity, so this costs nothing in detection terms while cutting
//  pointless bus traffic ~30x during the noisiest part of boot - which is
//  also when SGP30 begin()/read failures have been observed to cluster.
//
//  NOTE: SCD40 does not use SENSOR_BEGIN_RETRY_DELAY_MS. Its probe is a
//  stop/start command pair with an inherent 500ms settle delay, so it is
//  already self-pacing - see setupSCD40_CO2().
// ============================================================
#define SENSOR_BEGIN_RETRY_MS        3000UL
#define SENSOR_BEGIN_RETRY_DELAY_MS  100UL

#include <Wire.h>

// TFT_eSPI Library
#include <SPI.h>
#include <TFT_eSPI.h>
#include <SPIFFS.h>
#include <vfs_api.h>
#include <FSImpl.h>
#include <FS.h>
//TFT_eSPI tft = TFT_eSPI();
extern TFT_eSPI tft;

// Constants
#define I2C1_SDA 1												// ESP32-S3 GPIO pin number		
#define I2C1_SCL 4												// ESP32-S3 GPIO pin number		
#define I2C_FT5206  0x38									// Touch Controller I2C address 
#define SDA_FT5206  I2C1_SDA
#define SCL_FT5206  I2C1_SCL
#define INT_FT5206  2											// ESP32-S3 GPIO pin number		

#define PMS5003_RX_PIN 21									// ESP32-S3 GPIO pin number		
#define PMS5003_TX_PIN 35									// ESP32-S3 GPIO pin number		
#define COLOUR_BACKGROUND TFT_GREEN
#define COLOUR_FOREGROUND TFT_WHITE
#define TOP_MENU_PROGRAM_VERSION "v1.0.0" // Will be displayed at the top of the display
#define OSC_PIN 36												// Oscilloscope Test Pin used for program flow debugging	
#define BACKLIGHT_PIN 10									// ESP32-S3 GPIO pin number																																																																																						// Define pins for I2C Bus 0 (Wire) - All I2C Sensors

// User Functions
void scanI2CBus(TwoWire& bus, const char* busName);
void OSC_PIN_PULSE(byte times);

#if (USER_SETUP_ID != 50)
#error "Incorrect TFT_eSPI Display Setup - should be 50"
#endif 

#endif