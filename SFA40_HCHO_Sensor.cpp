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

// SFA40_HCHO_Sensor.cpp

#include "SFA40_HCHO_Sensor.h"

uint16_t hcho_ppb = 0;

unsigned long lastSFA40OkMs = 0;

void setupSFA40_HCHO() {
  Serial1.begin(9600, SERIAL_8N1, SFA40_RX_PIN, SFA40_TX_PIN);
  Serial.println("SFA40: Found and initialised");

  ha_sfa40_hcho.setName("SFA40_HCHO");
  ha_sfa40_hcho.setUnitOfMeasurement("ppb");
  ha_sfa40_hcho.setIcon("mdi:biohazard");

  ha_sfa40_temp.setName("SFA40_Temperature");
  ha_sfa40_temp.setUnitOfMeasurement("\u00B0C");
  ha_sfa40_temp.setIcon("mdi:thermometer");

  ha_sfa40_humidity.setName("SFA40_Humidity");
  ha_sfa40_humidity.setUnitOfMeasurement("%RH");
  ha_sfa40_humidity.setIcon("mdi:water-percent");
}


void updateSFA40_HCHO() {
  static uint8_t buffer[14];
  static uint8_t index = 0;

  static unsigned long lastHAPublishMs = 0;

  while (Serial1.available()) {
    uint8_t incomingByte = Serial1.read();

    if (index == 0 && incomingByte != 0xFF) {
      continue;
    }

    buffer[index++] = incomingByte;

    if (index == 14) {
      uint8_t checksumTarget = 0;
      for (int i = 1; i < 13; i++) {
        checksumTarget += buffer[i];
      }
      checksumTarget = (uint8_t)(~checksumTarget + 1);

      if (checksumTarget == buffer[13]) {
        hcho_ppb = (buffer[4] << 8) | buffer[5];

        float sfa_temp = buffer[9] + (buffer[10] / 100.0f);
        if (buffer[8] == 1) sfa_temp = -sfa_temp;

        float sfa_hum = buffer[11] + (buffer[12] / 100.0f);

        lastSFA40OkMs = millis();

        unsigned long now = millis();
        if (now - lastHAPublishMs >= HA_PUBLISH_INTERVAL_MS) {
          lastHAPublishMs = now;
          ha_sfa40_hcho.setValue(hcho_ppb);
          ha_sfa40_temp.setValue(sfa_temp);
          ha_sfa40_humidity.setValue(sfa_hum);
        }

        debugLoop("%u ppb | Temp: %.2f C | Humidity: %.2f %%", hcho_ppb, sfa_temp, sfa_hum);

        setDisplayDirty();
      }
      else {
        debugLoop("Warning: Frame corrupted, checksum validation failed.");
        debugSpecial("Warning: Frame corrupted, checksum validation failed.");
      }

      index = 0;
    }
  }
}