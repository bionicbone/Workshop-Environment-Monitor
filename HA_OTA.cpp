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

// HA_OTA.cpp

#include "HA_OTA.h"
#include "Graphics.h"        // for setDirtyHeader(), setDirtyOutside()
#include "NetworkCanary.h"   // network-health probe - see NetworkCanary.h; no longer ventClient's job

// The following must remain in this order to ensure correct runtime flow
WiFiClient client;
HADevice device("workshop_environment_monitor");
HAMqtt mqtt(client, device, 35);   // raise device-type limit (default 24); 35 gives headroom

// ============================================================
//  HA ENTITY COUNT — keep this tally current
//  Every HASensor/HASensorNumber/HASwitch/etc. instance below counts as one
//  device type against the HAMqtt limit above. Update this count whenever
//  an entity is added or removed — exceeding the limit fails SILENTLY (the
//  extra entity is just dropped, no error, no discovery message), so this
//  comment is the only warning a future addition gets.
//
//  Current count: 28   Limit: 35   Headroom: 7
//  (3 SFA40 + 3 BME280 + 2 SHT40 + 3 SCD40 + 4 SGP30 (2 live + 2 baseline)
//   + 6 PMS5003 weight/atm + 6 PMS5003 counts + 1 backlight)
// ============================================================

// --- HA MQTT sensor entities ---
HASensorNumber ha_sfa40_hcho("SFA40_HCHO", HASensorNumber::PrecisionP0);
HASensorNumber ha_sfa40_temp("SFA40_Temperature", HASensorNumber::PrecisionP1);
HASensorNumber ha_sfa40_humidity("SFA40_Humidity", HASensorNumber::PrecisionP0);

HASensorNumber ha_bme280_temp("BME280_Temperature", HASensorNumber::PrecisionP1);
HASensorNumber ha_bme280_humidity("BME280_Humidity", HASensorNumber::PrecisionP0);
HASensorNumber ha_bme280_pressure("BME280_Pressure", HASensorNumber::PrecisionP0);

HASensorNumber ha_sht40_temp("SHT40_Temperature", HASensorNumber::PrecisionP1);
HASensorNumber ha_sht40_humidity("SHT40_Humidity", HASensorNumber::PrecisionP0);

HASensorNumber ha_scd40_co2("SCD40_CO2", HASensorNumber::PrecisionP0);
HASensorNumber ha_scd40_temp("SCD40_Temperature", HASensorNumber::PrecisionP1);
HASensorNumber ha_scd40_humidity("SCD40_Humidity", HASensorNumber::PrecisionP0);

HASensorNumber ha_sgp30_eco2("SGP30_eCO2", HASensorNumber::PrecisionP0);
HASensorNumber ha_sgp30_tvoc("SGP30_TVOC", HASensorNumber::PrecisionP0);
HASensorNumber ha_sgp30_eco2_baseline("SGP30_eCO2_Baseline", HASensorNumber::PrecisionP0);
HASensorNumber ha_sgp30_tvoc_baseline("SGP30_TVOC_Baseline", HASensorNumber::PrecisionP0);

// PMS5003 - Weight concentration (ug/m3) Standard
HASensorNumber ha_pms_pm10_std("PMS5003_PM10_Std", HASensorNumber::PrecisionP0);
HASensorNumber ha_pms_pm25_std("PMS5003_PM25_Std", HASensorNumber::PrecisionP0);
HASensorNumber ha_pms_pm100_std("PMS5003_PM100_Std", HASensorNumber::PrecisionP0);

// PMS5003 - Weight concentration (ug/m3) Atmospheric
HASensorNumber ha_pms_pm10_atm("PMS5003_PM10_Atm", HASensorNumber::PrecisionP0);
HASensorNumber ha_pms_pm25_atm("PMS5003_PM25_Atm", HASensorNumber::PrecisionP0);
HASensorNumber ha_pms_pm100_atm("PMS5003_PM100_Atm", HASensorNumber::PrecisionP0);

// PMS5003 - Particle counts (per 0.1L)
HASensorNumber ha_pms_cnt_03("PMS5003_Cnt_03um", HASensorNumber::PrecisionP0);
HASensorNumber ha_pms_cnt_05("PMS5003_Cnt_05um", HASensorNumber::PrecisionP0);
HASensorNumber ha_pms_cnt_10("PMS5003_Cnt_10um", HASensorNumber::PrecisionP0);
HASensorNumber ha_pms_cnt_25("PMS5003_Cnt_25um", HASensorNumber::PrecisionP0);
HASensorNumber ha_pms_cnt_50("PMS5003_Cnt_50um", HASensorNumber::PrecisionP0);
HASensorNumber ha_pms_cnt_100("PMS5003_Cnt_100um", HASensorNumber::PrecisionP0);

// Display backlight
HASensor ha_backlight("Display_Backlight");

// Vent controller
WiFiClient ventWifiClient;
PubSubClient ventClient(ventWifiClient);
volatile int   ventFanRPM = 0;
volatile float ventChamberTemp = 0;
volatile float ventAmbientTemp = 0;
volatile float ventDeltaTemp = 0;
volatile bool  ventFlagOpen = 0;
volatile int   ventTriggerTimer = 0;

// MQTT time string
char displayTime[20] = "--/--/---- --:--";

// ============================================================
//  OUTDOOR SENSOR GLOBALS
//  Populated by ventClient MQTT callback on rtl_433 wildcard topics.
//  lastOutdoorUpdate = 0 until first valid message received.
//  Graphics.cpp uses lastOutdoorUpdate for stale detection.
// ============================================================
volatile float outdoorTempC = 0.0f;
volatile float outdoorHumidity = 0.0f;
volatile unsigned long lastOutdoorUpdate = 0;


void setupWifi() {
  Serial.begin(115200);
  delay(100);

  WiFi.setHostname(WIFI_HOSTNAME);
  delay(1000);

  bool connected = false;

  for (uint8_t attempt = 1; attempt <= WIFI_CONNECT_RETRIES && !connected; attempt++) {
    Serial.printf("Connecting to WiFi (attempt %u/%u)...", attempt, WIFI_CONNECT_RETRIES);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

    unsigned long timerMillis = millis();
    unsigned long currentMillis = millis();
    while (WiFi.status() != WL_CONNECTED && currentMillis - timerMillis <= WIFI_CONNECT_TIMEOUT_MS) {
      delay(500);
      Serial.print(".");
      currentMillis = millis();
    }

    connected = (WiFi.status() == WL_CONNECTED);

    if (!connected && attempt < WIFI_CONNECT_RETRIES) {
      Serial.printf("\nWiFi attempt %u failed - retrying in %lus...\n",
        attempt, WIFI_RETRY_DELAY_MS / 1000UL);
      WiFi.disconnect(true);   // clear any half-associated state before retrying
      delay(WIFI_RETRY_DELAY_MS);
    }
  }

  if (connected) {
    Serial.println("\nWiFi Connected!");
    Serial.print("Waiting for local IP assignment...");
    while (WiFi.localIP() == IPAddress(0, 0, 0, 0)) {
      delay(100);
      Serial.print(".");
    }
    Serial.println("\nIP Address Bound Successfully!");
    Serial.print("IP: ");
    Serial.println(WiFi.localIP());
    // NOTE: mDNS is deliberately NOT started here - see loopWifi() below.
    // Starting it only on a successful setup()-time connect means it would
    // never start at all for the rest of the session on any boot that began
    // disconnected, even after loopWifi() later reconnects. Centralising it
    // in loopWifi() covers both the setup()-time connect and the recovery
    // path with one code path instead of two.
  }
  else {
    Serial.printf("\nWiFi Failed to Connect after %u attempts\n", WIFI_CONNECT_RETRIES);
  }
}


// ============================================================
//  NETWORK HEALTH STATE MACHINE  (design rationale in HA_OTA.h)
//  loopWifi() owns netHealthState. All network consumers (loopHA,
//  loopVentSubscription, loopNetworkCanary) check the state rather than
//  WiFi.status() directly - "has the path proven itself" beats "is it up
//  this instant". Promotion itself is driven by the network canary
//  (NetworkCanary.h/.cpp), not by ventClient - see HA_OTA.h for why.
// ============================================================
volatile NetHealthState netHealthState = NET_PROBATION;   // boot: WiFi may already be up from setupWifi(), but make it prove stability first

void loopWifi() {
  static unsigned long disconnectedSinceMs = 0;
  static unsigned long lastReconnectAttemptMs = 0;
  static unsigned long wifiUpSinceMs = 0;

  bool wifiUp = (WiFi.status() == WL_CONNECTED);
  unsigned long now = millis();

  // ---- WiFi loss handles identically from any state ----
  if (!wifiUp) {
    if (netHealthState != NET_DOWN) {
      // Force-disconnect all three clients NOW, while we know the connection
      // state is dirty. This prevents the zombie-socket condition: without
      // it, a client can keep reporting "connected" on a dead TCP socket and
      // its next loop()/read blocks indefinitely. These calls act on local
      // state + a best-effort packet; they do not wait on the peer.
      netHealthState = NET_DOWN;
      disconnectedSinceMs = now;
      wifiUpSinceMs = 0;
      mqtt.disconnect();
      ventClient.disconnect();
      disconnectNetworkCanary();
      debugLoop("WiFi lost - MQTT clients force-disconnected, network DOWN");
      debugSpecial("WiFi lost - MQTT clients force-disconnected, network DOWN");
    }

    // Drive recovery. The ESP32 core's built-in auto-reconnect is unreliable
    // after a genuine AP power-cycle (confirmed against the mesh's own
    // device list), so force a full fresh association periodically.
    if (now - disconnectedSinceMs >= WIFI_RECONNECT_GRACE_MS &&
      now - lastReconnectAttemptMs >= WIFI_RECONNECT_RETRY_MS) {
      lastReconnectAttemptMs = now;
      debugLoop("WiFi reconnect attempt (down for %lus)", (now - disconnectedSinceMs) / 1000UL);
      debugSpecial("WiFi reconnect attempt (down for %lus)", (now - disconnectedSinceMs) / 1000UL);
      WiFi.disconnect();
      WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    }
    return;
  }

  // ---- WiFi is associated from here on ----
  if (wifiUpSinceMs == 0) wifiUpSinceMs = now;   // start/restart the settle timer

  // ---- mDNS: start once, the first time WiFi is genuinely up ----
  // Covers both a successful setupWifi() connect AND a later recovery via
  // this loop - previously mDNS only started on the former, so a boot that
  // began disconnected never got it for the rest of the session even after
  // WiFi came back. Single-shot per session, matching the original
  // behaviour (no retry if MDNS.begin() itself fails).
  static bool mdnsStarted = false;
  if (!mdnsStarted) {
    if (MDNS.begin(WIFI_HOSTNAME)) {
      Serial.println("mDNS responder started");
    }
    mdnsStarted = true;
  }

  switch (netHealthState) {

  case NET_DOWN:
    // Wait out the settle window to absorb association flapping, then move
    // to PROBATION so the network canary can start probing the actual path.
    if (now - wifiUpSinceMs >= NET_WIFI_SETTLE_MS) {
      netHealthState = NET_PROBATION;
      debugLoop("WiFi settled - probing path via network canary (PROBATION)");
      debugSpecial("WiFi settled - probing path via network canary (PROBATION)");
    }
    break;

  case NET_PROBATION:
    // Promotion requires a REAL end-to-end success, not elapsed time.
    // loopNetworkCanary() is permitted to attempt its bounded, throttled
    // connect while in this state; the moment it succeeds the path is
    // proven, and the heavier HAMqtt is allowed to run.
    if (isNetworkCanaryConnected()) {
      netHealthState = NET_HEALTHY;
      debugLoop("Path proven by network canary - network HEALTHY, MQTT permitted");
      debugSpecial("Path proven by network canary - network HEALTHY, MQTT permitted");
    }
    break;

  case NET_HEALTHY:
    // If the canary drops, the path is no longer proven. Demote so
    // mqtt.loop() stops before it can meet a degraded path, and let the
    // canary re-prove reachability first.
    if (!isNetworkCanaryConnected()) {
      netHealthState = NET_PROBATION;
      debugLoop("Network canary dropped - path unproven, back to PROBATION");
      debugSpecial("Network canary dropped - path unproven, back to PROBATION");
    }
    break;
  }
}


void setupOTA() {
  Serial.println("Initializing ArduinoOTA...");
  ArduinoOTA.setHostname(WIFI_HOSTNAME);

  ArduinoOTA.onStart([]() {
    Serial.println("OTA Update Started...");
    });
  ArduinoOTA.onEnd([]() {
    Serial.println("\nOTA Update Finished!");
    });
  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("OTA Error[%u]: ", error);
    });

  ArduinoOTA.begin();
  Serial.println("ArduinoOTA Started Safely!");
}


void setupHA() {
  device.setName("Workshop Environment Monitor");
  device.setSoftwareVersion("1.0.0");
  device.setManufacturer("TheBionicBone");
  device.setModel("ESP32-S3-Dev");
  device.enableSharedAvailability();
  device.enableLastWill();

  // NOTE: per-entity setup (setName/setUnitOfMeasurement/setIcon/
  // setStateClass) lives in each sensor's own setup...() function
  // (SFA40_HCHO_Sensor.cpp, BME280_TEMP_HUMIDITY.cpp, SCD40_CO2.cpp,
  // SGP30_VOC.cpp, SHT40_TEMP_HUMIDITY.cpp, PMS5003_Particles.cpp) -
  // deliberately NOT duplicated here. Each entity's identity has exactly
  // one place it's configured; see those files for state_class/units.

  mqtt.begin(BROKER_ADDR, 1883, MQTT_USERNAME, MQTT_PASSWORD);
}

void loopHA() {
  // Gated on the network health state machine, not raw WiFi.status().
  // WiFi.status() stays WL_CONNECTED during marginal-signal zombie-socket
  // conditions - exactly when mqtt.loop() can block loop() indefinitely.
  // NET_HEALTHY means the association has held stable through probation AND
  // all clients were force-disconnected on the last outage, so mqtt.loop()
  // here always operates on either a genuinely live connection or a clean
  // disconnected state it can rebuild from - never a zombie.
  if (netHealthState != NET_HEALTHY) return;

  mqtt.loop();

  static bool          prevConnected = false;
  static unsigned long disconnectedSinceMs = 0;
  static unsigned long lastForcedRebuildMs = 0;
  unsigned long now = millis();
  bool nowConnected = mqtt.isConnected();

  // ---- Connection state transition logging ----
  // Both transitions are logged so an MQTT outage is visible in the serial
  // log rather than only showing up as HA entities sitting unavailable.
  if (nowConnected && !prevConnected) {
    debugLoop("MQTT connected");
    debugSpecial("MQTT connected");
    publishBacklightStateHA();   // (re)assert true state after every (re)connect
  }
  else if (!nowConnected && prevConnected) {
    disconnectedSinceMs = now;
    debugLoop("MQTT connection lost");
    debugSpecial("MQTT connection lost");
  }

  // ---- Stale-connection watchdog (broker restart recovery) ----
  // See MQTT_STALE_TIMEOUT_MS in HA_OTA.h. Covers the case where WiFi is
  // fine but the broker went away and the library's own reconnect never
  // genuinely re-establishes. Forces a clean teardown + rebuild, throttled
  // so it can't spin. Deliberately does NOT touch netHealthState - the WiFi
  // path really is healthy here; this is purely an MQTT-layer problem.
  if (!nowConnected) {
    if (disconnectedSinceMs == 0) disconnectedSinceMs = now;   // covers never-connected-since-boot
    if (now - disconnectedSinceMs >= MQTT_STALE_TIMEOUT_MS &&
      now - lastForcedRebuildMs >= MQTT_STALE_TIMEOUT_MS) {
      lastForcedRebuildMs = now;
      debugLoop("MQTT down %lus - forcing full reconnect", (now - disconnectedSinceMs) / 1000UL);
      debugSpecial("MQTT down %lus - forcing full reconnect", (now - disconnectedSinceMs) / 1000UL);
      mqtt.disconnect();
      mqtt.begin(BROKER_ADDR, 1883, MQTT_USERNAME, MQTT_PASSWORD);
    }
  }
  else {
    disconnectedSinceMs = 0;
  }

  prevConnected = nowConnected;
}


// ============================================================
//  MQTT callback — vent controller, time, and outdoor sensor
// ============================================================
static void ventMqttCallback(char* topic, byte* payload, unsigned int length) {
  const unsigned int maxLen = 31;
  char value[32];
  unsigned int copyLen = min(length, maxLen);
  memcpy(value, payload, copyLen);
  value[copyLen] = '\0';

  // ---- Vent controller topics ----
  if (strcmp(topic, "aha/bambu_vent_controller_s3/vent_inline_rpm/stat_t") == 0) {
    ventFanRPM = atoi(value);
  }
  else if (strcmp(topic, "aha/bambu_vent_controller_s3/vent_temp_chamber/stat_t") == 0) {
    ventChamberTemp = atof(value);
  }
  else if (strcmp(topic, "aha/bambu_vent_controller_s3/vent_temp_ambient/stat_t") == 0) {
    ventAmbientTemp = atof(value);
  }
  else if (strcmp(topic, "aha/bambu_vent_controller_s3/vent_temp_delta/stat_t") == 0) {
    ventDeltaTemp = atof(value);
  }
  else if (strcmp(topic, "aha/bambu_vent_controller_s3/vent_open_state/stat_t") == 0) {
    ventFlagOpen = (strcmp(value, "ON") == 0);
  }
  else if (strcmp(topic, "aha/bambu_vent_controller_s3/trigger_timer/stat_t") == 0) {
    ventTriggerTimer = atoi(value);
  }

  // ---- Time topic ----
  else if (strcmp(topic, "workshop/time") == 0) {
    unsigned int tLen = min(length, (unsigned int)(sizeof(displayTime) - 1));
    memcpy(displayTime, payload, tLen);
    displayTime[tLen] = '\0';
    setDirtyHeader();
  }

  // ---- Outdoor sensor: temperature ----
  // Wildcard subscription rtl_433/+/devices/Nexus-TH/+/+/temperature_C
  // matches regardless of House Code (survives battery swaps).
  // strstr check confirms topic contains "Nexus-TH" and "temperature_C".
  else if (strstr(topic, "Nexus-TH") != nullptr &&
    strstr(topic, "temperature_C") != nullptr) {
    float v = atof(value);
    if (v > -40.0f && v < 60.0f) {
      outdoorTempC = v;
      lastOutdoorUpdate = millis();
      setDirtyOutside();
      debugLoop("Outdoor temp: %.1f C", outdoorTempC);
    }
  }
  else if (strstr(topic, "Nexus-TH") != nullptr &&
    strstr(topic, "humidity") != nullptr) {
    float v = atof(value);
    if (v >= 0.0f && v <= 100.0f) {
      outdoorHumidity = v;
      lastOutdoorUpdate = millis();
      setDirtyOutside();
      debugLoop("Outdoor humidity: %.0f %%RH", outdoorHumidity);
    }
  }
}

// ============================================================
//  Single source of truth for the subscription list.
//  Called on first connect and on every reconnect.
// ============================================================
static void ventSubscribeAll() {
  ventClient.subscribe("aha/bambu_vent_controller_s3/vent_inline_rpm/stat_t");
  ventClient.subscribe("aha/bambu_vent_controller_s3/vent_temp_chamber/stat_t");
  ventClient.subscribe("aha/bambu_vent_controller_s3/vent_temp_ambient/stat_t");
  ventClient.subscribe("aha/bambu_vent_controller_s3/vent_temp_delta/stat_t");
  ventClient.subscribe("aha/bambu_vent_controller_s3/vent_open_state/stat_t");
  ventClient.subscribe("aha/bambu_vent_controller_s3/trigger_timer/stat_t");
  ventClient.subscribe("workshop/time");

  // Outdoor sensor — wildcard on channel + House Code (survives battery swaps).
  // Topic: rtl_433/<addon-id>/devices/Nexus-TH/<channel>/<housecode>/temperature_C
  ventClient.subscribe("rtl_433/+/devices/Nexus-TH/+/+/temperature_C");
  ventClient.subscribe("rtl_433/+/devices/Nexus-TH/+/+/humidity");
}


void setupVentSubscription() {
  ventClient.setServer(BROKER_ADDR, 1883);
  ventClient.setCallback(ventMqttCallback);
  // Bounds how long the underlying blocking connect() is allowed to hang -
  // default is much longer. After a real WiFi outage, WiFi.status() can
  // report connected again before the broker path is fully usable, and
  // individual connect() attempts during that window can each eat most of
  // VENT_RECONNECT_INTERVAL_MS blocking - stalling loop() (and therefore
  // touch) for many seconds across several attempts before finally
  // succeeding. A short timeout means more attempts may be needed, but each
  // one blocks for far less time.
  ventClient.setSocketTimeout(VENT_SOCKET_TIMEOUT_S);

  if (ventClient.connect("workshop_vent_listener", MQTT_USERNAME, MQTT_PASSWORD)) {
    ventSubscribeAll();
    Serial.println("Vent + outdoor subscriptions connected");
  }
  else {
    // NOT a dead end - loopVentSubscription() retries this automatically
    // (throttled to once per VENT_RECONNECT_INTERVAL_MS) on every subsequent
    // loop() until it succeeds, printing its own confirmation once it does.
    Serial.println("Vent subscriptions failed on initial connect - will retry automatically");
  }
}

void loopVentSubscription() {
  // Purely the vent controller / outdoor sensor feature. This client used
  // to double as the whole network's health canary - it no longer does;
  // NetworkCanary.h/.cpp fills that role now, entirely independently. This is still gated on netHealthState != NET_DOWN (not
  // NET_HEALTHY) purely for its own sake - no point spending a bounded-but-
  // nonzero connect attempt against WiFi that isn't even associated - and it
  // is deliberately allowed to probe during NET_PROBATION too, same as the
  // canary, since VENT_SOCKET_TIMEOUT_S/VENT_RECONNECT_INTERVAL_MS already
  // bound and throttle the cost of doing so.
  if (netHealthState == NET_DOWN) return;

  if (!ventClient.connected()) {
    static unsigned long lastReconnectAttemptMs = 0;
    static unsigned long currentBackoffMs = VENT_RECONNECT_INTERVAL_MS;
    unsigned long now = millis();
    // Exponential backoff (doubling, capped at VENT_RECONNECT_BACKOFF_MAX_MS)
    // instead of a flat interval - see HA_OTA.h for why. Resets to the base
    // interval the moment a connect succeeds, so a brief blip still recovers
    // fast; only a prolonged outage backs off hard.
    if (now - lastReconnectAttemptMs >= currentBackoffMs) {
      lastReconnectAttemptMs = now;
      if (ventClient.connect("workshop_vent_listener", MQTT_USERNAME, MQTT_PASSWORD)) {
        ventSubscribeAll();
        currentBackoffMs = VENT_RECONNECT_INTERVAL_MS;   // reset on success
        debugLoop("Vent + outdoor subscriptions reconnected");
        debugSpecial("Vent + outdoor subscriptions reconnected");
      }
      else {
        currentBackoffMs = min(currentBackoffMs * 2, VENT_RECONNECT_BACKOFF_MAX_MS);
        debugLoop("Vent reconnect failed - next attempt in %lus", currentBackoffMs / 1000UL);
        debugSpecial("Vent reconnect failed - next attempt in %lus", currentBackoffMs / 1000UL);
      }
    }
  }
  ventClient.loop();
}