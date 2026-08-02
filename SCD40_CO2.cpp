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

// SCD40_CO2.cpp

#include "SCD40_CO2.h"

SensirionI2cScd4x scd40;

uint16_t co2;

float temperature;
float humidity;

bool scd40Detected = false;   // set true only if the sensor answers at boot
unsigned long lastSCD40OkMs = 0;

// ============================================================
//  SETUP
//
//  scd40.begin() itself does no I2C traffic - it only stores the Wire1/
//  address config. The real presence proof is the error code from
//  startPeriodicMeasurement(), so that's what's wrapped in a 3-second retry
//  window - same budget/latch-on-first-success shape as LD2410B_Presence.cpp,
//  SGP30_VOC.cpp, BME280_TEMP_HUMIDITY.cpp and SHT40_TEMP_HUMIDITY.cpp,
//  even though the underlying probe
//  mechanics differ (a 500ms stop/start command round trip, not a fast
//  tight-loop begin() call). On success scd40Detected is latched true, HA
//  entities are created, and we return immediately. If the loop falls
//  through the sensor is treated as ABSENT for the rest of the run:
//  scd40Detected stays false, no HA entities are created for it, and
//  updateSCD40_CO2() gates on the flag - an absent sensor never generates
//  I2C traffic and never publishes stale/zero data to HA.
//
//  NOTE: this is the one gated sensor that does NOT use
//  SENSOR_BEGIN_RETRY_DELAY_MS. Its probe already contains an inherent
//  500ms settle delay between stopPeriodicMeasurement() and
//  startPeriodicMeasurement(), so the loop is self-pacing at ~6 attempts
//  across the 3s window. Adding a further delay would only reduce the
//  number of attempts for no benefit.
// ============================================================
void setupSCD40_CO2() {
  scd40.begin(Wire1, 0x62);

  uint32_t timeout = millis();
  while (millis() - timeout < SENSOR_BEGIN_RETRY_MS) {
    scd40.stopPeriodicMeasurement();
    delay(500);
    uint16_t error = scd40.startPeriodicMeasurement();
    if (!error) {
      scd40Detected = true;
      Serial.println("SCD40: Periodic measurement started successfully");

      ha_scd40_co2.setName("SCD40_CO2");
      ha_scd40_co2.setUnitOfMeasurement("ppm");
      ha_scd40_co2.setIcon("mdi:molecule-co2");
      ha_scd40_co2.setStateClass("measurement");

      ha_scd40_temp.setName("SCD40_Temperature");
      ha_scd40_temp.setUnitOfMeasurement("\u00B0C");
      ha_scd40_temp.setIcon("mdi:thermometer");
      ha_scd40_temp.setStateClass("measurement");

      ha_scd40_humidity.setName("SCD40_Humidity");
      ha_scd40_humidity.setUnitOfMeasurement("%RH");
      ha_scd40_humidity.setIcon("mdi:water-percent");
      ha_scd40_humidity.setStateClass("measurement");
      return;
    }
  }
  scd40Detected = false;
  Serial.println("SCD40: Failed to start periodic measurement - check wiring on Wire1 (0x62)");
  Serial.println("SCD40: Running WITHOUT SCD40 sensor.");
}

// ============================================================
//  LOOP - call every loop()
// ============================================================
void updateSCD40_CO2() {
  // ---- Absent sensor: no I2C traffic, no HA publish, nothing else ----
  if (!scd40Detected) {
    return;
  }

  bool isDataReady = false;

  if (scd40.getDataReadyStatus(isDataReady) || !isDataReady) {
    return;
  }
  // SCD40 temp/humidity are reported to HA only. The shared display globals
  // 'temperature'/'humidity' are owned exclusively by the SHT40 (more accurate,
  // and deliberately has no fallback so the display is clear if SHT40 is
  // missing/not working), so SCD40 reads into locals and must NOT write the
  // display globals.
  float scdTemp = 0.0f, scdHum = 0.0f;
  uint16_t error = scd40.readMeasurement(co2, scdTemp, scdHum);
  if (error) {
    debugLoop("Read error");
    debugSpecial("Read error");
    return;
  }

  lastSCD40OkMs = millis();

  static unsigned long lastHAPublishMs = 0;
  if (millis() - lastHAPublishMs >= HA_PUBLISH_INTERVAL_MS) {
    lastHAPublishMs = millis();
    ha_scd40_co2.setValue(co2);
    ha_scd40_temp.setValue(scdTemp);
    ha_scd40_humidity.setValue(scdHum);
  }

  debugLoop("%u ppm | Temp: %.2f C | Humidity: %.2f %%", co2, scdTemp, scdHum);

  setDisplayDirty();
}