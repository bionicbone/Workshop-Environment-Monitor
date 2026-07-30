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

// NetworkCanary.cpp

#include "NetworkCanary.h"
#include "Global.h"
#include "HA_OTA.h"     // for NetHealthState / netHealthState - see NetworkCanary.h for why this is an acceptable dependency
#include "secrets.h"    // BROKER_ADDR, MQTT_USERNAME, MQTT_PASSWORD

WiFiClient canaryWifiClient;
PubSubClient canaryClient(canaryWifiClient);

void setupNetworkCanary() {
  canaryClient.setServer(BROKER_ADDR, 1883);
  canaryClient.setSocketTimeout(CANARY_SOCKET_TIMEOUT_S);
}

static bool connectCanary() {
  if (canaryClient.connect(CANARY_CLIENT_ID, MQTT_USERNAME, MQTT_PASSWORD)) {
    // Retained heartbeat - not required for the canary's own gating logic
    // (connect() succeeding is already the proof), but gives anyone
    // watching in MQTT Explorer a visible, WEM-owned topic to confirm
    // reachability against, rather than needing to know this client's
    // internal ID.
    canaryClient.publish(CANARY_TOPIC, "online", true);
    return true;
  }
  return false;
}

void loopNetworkCanary() {
  // Mirrors the original ventClient-as-canary gating: don't burn reconnect
  // attempts during a total WiFi outage. Only NET_DOWN stops it - it must
  // still be able to probe during NET_PROBATION, since a successful probe
  // there is precisely what promotes PROBATION -> HEALTHY.
  if (netHealthState == NET_DOWN) return;

  if (!canaryClient.connected()) {
    static unsigned long lastReconnectAttemptMs = 0;
    static unsigned long currentBackoffMs = CANARY_RECONNECT_INTERVAL_MS;
    unsigned long now = millis();

    // Exponential backoff (doubling, capped at CANARY_RECONNECT_BACKOFF_MAX_MS)
    // instead of a flat interval - keeps early retries fast for the common
    // case (a brief blip) while backing off hard during a genuinely
    // prolonged outage. Resets to the base interval the moment a connect
    // succeeds.
    if (now - lastReconnectAttemptMs >= currentBackoffMs) {
      lastReconnectAttemptMs = now;
      if (connectCanary()) {
        currentBackoffMs = CANARY_RECONNECT_INTERVAL_MS;   // reset on success
        debugLoop("Network canary connected");
        debugSpecial("Network canary connected");
      }
      else {
        currentBackoffMs = min(currentBackoffMs * 2, CANARY_RECONNECT_BACKOFF_MAX_MS);
        debugLoop("Network canary reconnect failed - next attempt in %lus", currentBackoffMs / 1000UL);
        debugSpecial("Network canary reconnect failed - next attempt in %lus", currentBackoffMs / 1000UL);
      }
    }
    return;
  }
  canaryClient.loop();
}

void disconnectNetworkCanary() {
  canaryClient.disconnect();
}

bool isNetworkCanaryConnected() {
  return canaryClient.connected();
}