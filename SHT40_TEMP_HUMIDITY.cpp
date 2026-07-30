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

// SHT40_TEMP_HUMIDITY.cpp

// Workshop Environment Monitor - Sensirion SHT40 Temperature & Humidity

#include "SHT40_TEMP_HUMIDITY.h"
#include "SCD40_CO2.h"

Adafruit_SHT4x sht40;

bool sht40Detected = false;   // set true only if the sensor answers at boot
unsigned long lastSHT40OkMs = 0;

// ============================================================
//  SETUP
//
//  A 3-second retry loop gives the sensor time to come up on I2C, before we
//  write it off for the session - same pattern as LD2410B_Presence.cpp,
//  SGP30_VOC.cpp and BME280_TEMP_HUMIDITY.cpp. On success sht40Detected is
//  latched true, precision/heater are configured, HA entities are created,
//  and we return immediately. If the
//  loop falls through the sensor is treated as ABSENT for the rest of the
//  run: sht40Detected stays false, no HA entities are created for it, and
//  updateSHT40() gates on the flag - an absent sensor never generates I2C
//  traffic and the shared temperature/humidity display globals (see
//  SCD40_CO2.h) simply never get written, which Graphics.cpp already
//  correctly reads as permanently stale via lastSHT40OkMs staying 0.
// ============================================================
void setupSHT40() {
  uint32_t timeout = millis();
  while (millis() - timeout < SENSOR_BEGIN_RETRY_MS) {
    if (sht40.begin(&Wire1)) {
      sht40Detected = true;
      Serial.println("SHT40: Found and initialised");

      sht40.setPrecision(SHT4X_HIGH_PRECISION);
      sht40.setHeater(SHT4X_NO_HEATER);

      ha_sht40_temp.setName("SHT40_Temperature");
      ha_sht40_temp.setUnitOfMeasurement("\u00B0C");
      ha_sht40_temp.setIcon("mdi:thermometer");

      ha_sht40_humidity.setName("SHT40_Humidity");
      ha_sht40_humidity.setUnitOfMeasurement("%RH");
      ha_sht40_humidity.setIcon("mdi:water-percent");
      return;
    }
    delay(SENSOR_BEGIN_RETRY_DELAY_MS);
  }
  sht40Detected = false;
  Serial.println("SHT40: Sensor not found at 0x44!");
  Serial.println("SHT40: Running WITHOUT SHT40 sensor.");
}

// ============================================================
//  LOOP - call every loop()
// ============================================================
void updateSHT40() {
  // ---- Absent sensor: no I2C traffic, no HA publish, nothing else ----
  if (!sht40Detected) {
    return;
  }

  sensors_event_t humEvent, tempEvent;

  // TIMING INSTRUMENTATION. getEvent() is a BLOCKING call: it sends the
  // measurement command, then hard-delay()s for the precision-dependent
  // measurement time (~8.2ms at SHT4X_HIGH_PRECISION per Sensirion/Adafruit)
  // before reading the result back. Nothing else in loop() - including an
  // SGP30 read due at the same moment - can run during that window. This
  // print exists to see exactly when that window falls, so it can be lined
  // up against SGP30's own timing prints (see SGP30_VOC.cpp) if a
  // blocking-window collision between the two is ever suspected again.
  // debugSpecial (not debugLoop) - useful for diagnosing blocking-related
  // faults, not needed in day-to-day operation.
  unsigned long sht40StartUs = micros();
  bool ok = sht40.getEvent(&humEvent, &tempEvent);
  unsigned long sht40DurationUs = micros() - sht40StartUs;

  if (!ok) {
    debugLoop("Read failed (blocked %lu us)", sht40DurationUs);
    debugSpecial("Read failed (blocked %lu us)", sht40DurationUs);
    return;
  }

  float temp = tempEvent.temperature;
  float hum = humEvent.relative_humidity;

  lastSHT40OkMs = millis();

  static unsigned long lastHAPublishMs = 0;
  if (millis() - lastHAPublishMs >= HA_PUBLISH_INTERVAL_MS) {
    lastHAPublishMs = millis();
    ha_sht40_temp.setValue(temp);
    ha_sht40_humidity.setValue(hum);
  }

  temperature = temp;
  humidity = hum;

  debugLoop("Read OK -> %.2f C | %.2f %%RH (blocked %lu us)", temp, hum, sht40DurationUs);

  setDisplayDirty();
}