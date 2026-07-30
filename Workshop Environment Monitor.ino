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

// Workshop Environment Monitor.ino

// ============================================================
//  SENSOR TIMING SUMMARY
//  SFA40  HCHO    : every loop()   UART-driven, must never be delayed
//  SGP30  TVOC    : every 1 second   Sensirion algorithm requires 1s ticks
//  SCD40  CO2     : every 5 seconds   matches sensor internal cycle
//  BME280 Temp etc: every 5 seconds   fast sensor, 5s is adequate
//  SHT40  Temp/Hum: every 5 seconds   primary display source
//                   I2C address 0x44, Wire1   no conflict with existing sensors
//  PMS5003 PM     : 2-minute sleep/wake cycle, owned by loopPMS5003()
//                   (PMS5003_Particles.cpp) - wake at 1.5 min (90s),
//                   read+sleep at 2 min (120s), 30s stabilisation gap
//                   handled by timer difference (non-blocking)
//  SGP30 baseline : saved to flash every 1 hour
// ============================================================

#include "NetworkCanary.h"
#include "TimeSync.h"
#include <Update.h>             // Leave in - solves VS compile issues
#include <WiFi.h>               // WiFi.status()/WL_CONNECTED - used directly for the splash WiFi line
#include "Buzzer.h"
#include "Global.h"
#include "Backlight.h"
#include "Graphics.h"
#include "PMS5003_Particles.h"
#include "SGP30_VOC.h"
#include "SCD40_CO2.h"
#include "BME280_TEMP_HUMIDITY.h"
#include "HA_OTA.h"
#include "SFA40_HCHO_Sensor.h"
#include "LD2410B_Presence.h"   // after Global.h - needs Arduino core types

// ============================================================
//  SHT40   primary display temperature/humidity source
//  I2C address 0x44, Wire1   no conflicts with other sensors.
//  Graphics.cpp reads temperature/humidity from SHT40 only, deliberately
//  no fallback to SCD40 or BME280   see Schematic Reference for the
//  "SHT40 display override" rationale.
// ============================================================
#include "SHT40_TEMP_HUMIDITY.h"

#include "TimeSync.h"


void setup() {
  pinMode(OSC_PIN, OUTPUT);

  Serial.begin(115200);
  debugLoop("Started...");
  debugSpecial("Started...");

  Serial.printf("\nName        : Workshop Environment Monitor\nCreated     : May 2026\nAuthor      : Kevin Guest (BionicBone)\n");
  Serial.printf("Program     : %s\n", TOP_MENU_PROGRAM_VERSION);
  Serial.printf("License     : AGPL-3.0-or-later, NO WARRANTY\n");
  Serial.printf("Source      : https://github.com/bionicbone/Workshop-Environment-Monitor\n");
  Serial.printf("ESP-IDF     : %s\n", esp_get_idf_version());
  Serial.printf("Arduino Core: v%d.%d.%d\n", ESP_ARDUINO_VERSION_MAJOR, ESP_ARDUINO_VERSION_MINOR, ESP_ARDUINO_VERSION_PATCH);

  // Tested cores: v2.0.17 and v3.3.10. LEDC API is version-gated in
  // Buzzer.cpp/Buzzer.h to compile+run on both - see Schematic Reference,
  // "Arduino IDE Compiler Settings" for the full tested-version record.
  bool coreTested = (ESP_ARDUINO_VERSION == ESP_ARDUINO_VERSION_VAL(2, 0, 17)) ||
    (ESP_ARDUINO_VERSION == ESP_ARDUINO_VERSION_VAL(3, 3, 10));
  if (!coreTested) {
    Serial.printf("\n\nThis ESP32 Arduino Core version has not been tested\n");
    Serial.printf("Tested versions: v2.0.17, v3.3.10 - see Schematic Reference for detail\n");
    Serial.printf("***!!!*** PROCEED WITH CAUTION ***!!!***\n");
  }

  // ---- I2C first: the display is optional and detected over Wire1 ----
  // setupTFT() probes the FT5x06 touch controller at 0x38 to decide whether
  // a display is fitted at all (see "OPTIONAL DISPLAY GATE" in Graphics.h),
  // so the bus MUST be up before it is called. This is why Wire1.begin() and
  // the bus scan now run first - setupTFT() used to be the very first call
  // in setup().
  bool wire1Init = Wire1.begin(I2C1_SDA, I2C1_SCL, 100000);
  if (!wire1Init) {
    Serial.println("Failed to initialize Wire1 (Bus 1)");
  }

  scanI2CBus(Wire1, "Wire1 (Bus 1)");
  // Expected devices:
  // 0x38 - TFT Capacitive Touch  (absent = no display fitted, see setupTFT())
  // 0x44 - SHT40
  // 0x58 - SGP30
  // 0x62 - SCD40
  // 0x76 - BME280

  // Ordering below is fixed and load-bearing:
  //   setupTFT()       latches displayDetected - must run before the two
  //                    calls that gate on it.
  //   setupBacklight() gates on displayDetected.
  //   setupBuzzer()    must follow setupTFT() because TFT_eSPI initialises
  //                    LEDC internally - see the "LEDC API" note in Buzzer.h
  //                    for what changes when no display is fitted.
  setupTFT();
  setupBacklight();
  setupBuzzer();
  setupLD2410B();
  splashLine("LD2410B Presence", ld2410Detected);

  setupWifi();
  splashLine("WiFi", WiFi.status() == WL_CONNECTED);
  setupTimeSync();          // start NTP sync ASAP - async, doesn't block on WiFi
  setupOTA();

  // Set up all sensors BEFORE setting up Home Assistant MQTT
  // SFA40 has no detection gate (passive UART, no probe possible - see
  // Schematic Reference) and deliberately has no splash line for the same
  // reason: there is nothing true to report yet at this point in setup().
  setupSFA40_HCHO();
  setupBME280();
  splashLine("BME280", bme280Detected);
  setupSCD40_CO2();
  splashLine("SCD40 CO2", scd40Detected);
  setupSGP30_VOC();
  splashLine("SGP30 VOC", sgp30Detected);
  restoreSGP30Baseline();   // Restore SGP30 calibration baseline from flash
  setupPMS5003();
  splashLine("PMS5003", pms5003Detected);

  // SHT40   primary display temperature/humidity (overwrites SCD40 display globals)
  setupSHT40();
  splashLine("SHT40", sht40Detected);

  // Leave at the very bottom
  setupHA();
  setupNetworkCanary();     // self-contained network-health probe - see NetworkCanary.h
  setupVentSubscription();

  // Splash screen hand-off: hold the completed progress list briefly so the
  // last line or two is actually readable, then switch to the normal
  // dashboard. No-op on a headless build (displayDetected false).
  if (displayDetected) {
    delay(SPLASH_HOLD_MS);
    drawStaticChrome();
  }

  debugLoop("Completed, running the main loop");
  debugSpecial("Completed, running the main loop");
}


void loop() {
  OSC_PIN_PULSE(1);
  // --- OTA: Listen for over-the-air updates ---
  ArduinoOTA.handle();

  // --- Backlight: schedule ---
  loopBacklight();

  // --- Buzzer for Warnings - Touch to silence for a preiod of time ---
  loopBuzzer();

  // --- LD2410B mmWave: presence detection ---
  loopLD2410B();

  // --- WiFi: actively monitor + recover connection (built-in auto-reconnect
  //     is not reliable after a genuine AP/router power-cycle) - must run
  //     before loopHA()/loopVentSubscription() so they see current status ---
  loopWifi();

  // --- MQTT: Keep connections alive   must run every loop ---
  loopHA();
  loopNetworkCanary();    // drives netHealthState PROBATION -> HEALTHY promotion - see NetworkCanary.h
  loopVentSubscription();
  loopTimeSync();         // periodic NTP vs MQTT cross-check (gated internally)

  // ---- SFA40 HCHO: UART-driven   must run every loop ----
  // Buffers incoming serial bytes; misses data if called on a timer
  updateSFA40_HCHO();

  unsigned long currentMillis = millis();

  // ---- SGP30 TVOC: 1-second timer ----
  // Sensirion requires IAQmeasure() called every 1 second for the
  // on-chip baseline compensation algorithm to function correctly.
  // Running less frequently degrades TVOC accuracy over time.
  static unsigned long lastSGP30Update = 0;
  if (currentMillis - lastSGP30Update >= 1000) {
    updateSGP30_VOC();
    lastSGP30Update = currentMillis;
  }

  // ---- 5-second sensors: SCD40, BME280, SHT40 ----
  static unsigned long lastSlowUpdate = 0;
  if (currentMillis - lastSlowUpdate >= 5000) {
    updateSCD40_CO2();
    updateBME280();
    updateSHT40();   // runs AFTER SCD40 so SHT40 values win for display globals
    lastSlowUpdate = currentMillis;
  }

  // ---- PMS5003: 120s sleep/wake cycle ----
  // Cycle timing now owned by PMS5003_Particles.cpp (loopPMS5003()) rather
  // than tracked here, matching loopBacklight()/loopLD2410B()/loopWifi()/
  // loopBuzzer() - see PMS5003_Particles.cpp for the full cycle timeline
  // and the wakePMS5003()-must-not-block note.
  loopPMS5003();

  // ---- Header: refresh every 60 seconds for WiFi status ----
  // The header also updates immediately when MQTT time arrives (via setDirtyHeader
  // in HA_OTA.cpp), so this is just a fallback for WiFi status changes.
  static unsigned long lastHeaderRefresh = 0;
  if (currentMillis - lastHeaderRefresh >= 60000UL) {
    setDirtyHeader();
    lastHeaderRefresh = currentMillis;
  }
  // Baseline is used on next boot to restore calibration immediately.
  static unsigned long lastBaselineSave = 0;
  if (currentMillis - lastBaselineSave >= 3600000UL) {
    lastBaselineSave = currentMillis;
    saveSGP30Baseline();
  }

  // ---- Display update: respects dirty flag and minimum interval ----
  updateDisplay();
}
