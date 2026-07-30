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

// Global.cpp

#include "Global.h"
#include <time.h>

// TFT_eSPI
TFT_eSPI tft = TFT_eSPI();

// ============================================================
//  TIMESTAMP HELPER  (used by debugLoop/debugSpecial macros in Global.h)
//  getLocalTime(&timeinfo, 0) uses a 0ms timeout - non-blocking, safe to call
//  on every debug print without stalling the loop. Returns false until NTP
//  has completed its first sync (see TimeSync.h - setupTimeSync()).
// ============================================================
const char* getTimestamp() {
  static char buf[32];
  struct tm timeinfo;
  if (getLocalTime(&timeinfo, 0)) {
    snprintf(buf, sizeof(buf), "[%02d:%02d:%02d][%luus]",
      timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec, micros());
  }
  else {
    snprintf(buf, sizeof(buf), "[??:??:??][%luus]", micros());
  }
  return buf;
}


// ============================================================
//  I2C HELPER  (can be used to scan I2C for attached devices)
// ============================================================
void scanI2CBus(TwoWire& bus, const char* busName) {
  Serial.print("--- Scanning ");
  Serial.print(busName);
  Serial.println(" ---");

  byte error, address;
  int nDevices = 0;

  for (address = 1; address < 127; address++) {
    // The i2c_scanner uses the return value of
    // the Write.endTransmission to see if
    // a device did acknowledge to the address.
    bus.beginTransmission(address);
    error = bus.endTransmission();

    if (error == 0) {
      Serial.print("Device found at address 0x");
      if (address < 16) {
        Serial.print("0");
      }
      Serial.print(address, HEX);
      Serial.println(" !");

      nDevices++;
    }
    else if (error == 4) {
      Serial.print("Unknown error at address 0x");
      if (address < 16) {
        Serial.print("0");
      }
      Serial.println(address, HEX);
    }
  }

  if (nDevices == 0) {
    Serial.println("No I2C devices found\n");
  }
  else {
    Serial.println("Scan complete\n");
  }
}


// ============================================================
//  Oscilloscope HELPER
//  Can be used to help debugging, creates a pulse to indicate
//  what step is currently processing.
//  Used with a digitalWrite HIGH to measure timing of that code
//  to detect code that could stall for significant length of time
//  For example, used to identify network connection issue delays
//  so they could be resolved to provide a clean reconnection.
// ============================================================
void OSC_PIN_PULSE(byte times) {
  for (int i = 0; i < times; i++) {
    digitalWrite(OSC_PIN, HIGH);
    delay(1);
    digitalWrite(OSC_PIN, LOW);
    delay(1);
  }
  delay(2);
}