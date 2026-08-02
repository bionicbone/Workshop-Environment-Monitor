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

// BME280_TEMP_HUMIDITY.cpp

#include "BME280_TEMP_HUMIDITY.h"

Adafruit_BME280 bme;

bool bme280Detected = false;   // set true only if the sensor answers at boot
unsigned long lastBME280OkMs = 0;

// ============================================================
//  SETUP
//
//  A 3-second retry loop gives the sensor time to come up on I2C, before we
//  write it off for the session - same pattern as LD2410B_Presence.cpp and
//  SGP30_VOC.cpp. On success bme280Detected
//  is latched true, HA entities are created, and we return immediately. If
//  the loop falls through the sensor is treated as ABSENT for the rest of
//  the run: bme280Detected stays false, no HA entities are created for it,
//  and updateBME280() gates on the flag - an absent sensor never generates
//  I2C traffic and never publishes stale/zero data to HA.
// ============================================================
void setupBME280() {
  uint32_t timeout = millis();
  while (millis() - timeout < SENSOR_BEGIN_RETRY_MS) {
    if (bme.begin(0x76, &Wire1)) {
      bme280Detected = true;
      Serial.println("BME280: Found and initialised");

      ha_bme280_temp.setName("BME280_Temperature");
      ha_bme280_temp.setUnitOfMeasurement("\u00B0C");
      ha_bme280_temp.setIcon("mdi:thermometer");
      ha_bme280_temp.setStateClass("measurement");

      ha_bme280_humidity.setName("BME280_Humidity");
      ha_bme280_humidity.setUnitOfMeasurement("%RH");
      ha_bme280_humidity.setIcon("mdi:water-percent");
      ha_bme280_humidity.setStateClass("measurement");

      ha_bme280_pressure.setName("BME280_Pressure");
      ha_bme280_pressure.setUnitOfMeasurement("hPa");
      ha_bme280_pressure.setIcon("mdi:gauge");
      ha_bme280_pressure.setStateClass("measurement");
      return;
    }
    delay(SENSOR_BEGIN_RETRY_DELAY_MS);
  }
  bme280Detected = false;
  Serial.println("BME280: Sensor not found - check wiring on Wire1 (0x76)");
  Serial.println("BME280: Running WITHOUT BME280 sensor.");
}

// ============================================================
//  LOOP - call every loop()
// ============================================================
void updateBME280() {
  // ---- Absent sensor: no I2C traffic, no HA publish, nothing else ----
  if (!bme280Detected) {
    return;
  }

  float temp = bme.readTemperature();
  float pressure = bme.readPressure() / 100.0F;  // hPa
  float humidity = bme.readHumidity();

  if (isnan(temp) || isnan(pressure) || isnan(humidity)) {
    debugLoop("Read failed (NaN)");
    debugSpecial("Read failed (NaN)");
    return;
  }

  lastBME280OkMs = millis();

  static unsigned long lastHAPublishMs = 0;
  if (millis() - lastHAPublishMs >= HA_PUBLISH_INTERVAL_MS) {
    lastHAPublishMs = millis();
    ha_bme280_temp.setValue(temp);
    ha_bme280_humidity.setValue(humidity);
    ha_bme280_pressure.setValue(pressure);
  }

  debugLoop("Temp: %.2f C | Pressure: %.2f hPa | Humidity: %.2f %%", temp, pressure, humidity);

  // NOTE: BME280 is HA-only — it is not shown on the TFT, so it does NOT
  // mark the display dirty (doing so forced a needless full gauge redraw
  // every 5s). If BME280 is ever added to the display, set its dirty flag here.
}