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

// HA_OTA.h

#ifndef _HA_OTA_h
#define _HA_OTA_h

#if defined(ARDUINO) && ARDUINO >= 100
#include "Arduino.h"
#else
#include "WProgram.h"
#endif

// --- HA MQTT + OTA libraries ---
#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoHA.h>
#include <ESPmDNS.h>
#include <ArduinoOTA.h>
// NOTE: ArduinoOTA.h does #include "Update.h" with quotes. Visual Micro requires
// #include <Update.h> to be present in the main .ino sketch to resolve this.
// Do not add it here - Visual Micro's resolver does not see headers-of-headers
// the same way the Arduino IDE does.

#include "Backlight.h"

// --- Credentials ---
// Real values live in secrets.h (git-ignored). 
// Copy secrets.h.example to secrets.h and fill in your own.
// This keeps WiFi/MQTT passwords out of the repo.
#include "secrets.h"

// ============================================================
//  HA PUBLISH RATE
//  Sensors read at their required hardware cadence (SGP30: 1s, SFA40: every
//  loop, SCD40/BME280/SHT40: 5s) but only push setValue() to HA every
//  HA_PUBLISH_INTERVAL_MS. Reduces MQTT load on the Pi 3 broker by ~97%
//  with no meaningful loss of resolution for a workshop environment.
//  30 seconds chosen as a good balance: fine enough for HA history charts,
//  well within the stale-detection windows of all sensors.
// ============================================================
#define HA_PUBLISH_INTERVAL_MS  30000UL

// ============================================================
//  WIFI CONNECT RETRY
//  Cold-boot WiFi timeouts happen fairly often (router-side association
//  issue suspected). A single failed attempt otherwise just gives up
//  silently. Retry up to WIFI_CONNECT_RETRIES times, each with
//  its own WIFI_CONNECT_TIMEOUT_MS timeout, pausing WIFI_RETRY_DELAY_MS
//  between attempts.
// ============================================================
#define WIFI_CONNECT_TIMEOUT_MS  10000UL   // per-attempt timeout
#define WIFI_CONNECT_RETRIES     3         // total attempts before giving up
#define WIFI_RETRY_DELAY_MS      1000UL    // pause between attempts

// ============================================================
//  VENT MQTT RECONNECT THROTTLE
//  Without throttling, loopVentSubscription() would attempt
//  ventClient.connect() on every single loop() iteration while
//  disconnected - effectively hammering the broker continuously. Gate
//  reconnect attempts to once per this interval.
//  NOTE: this governs the vent controller / outdoor sensor feature only.
//  The network-health canary is a separate client - see NetworkCanary.h.
// ============================================================
#define VENT_RECONNECT_INTERVAL_MS  5000UL

// ============================================================
//  VENT MQTT RECONNECT BACKOFF
//  A flat 5s retry interval against a genuinely bad path means blocking
//  ~VENT_SOCKET_TIMEOUT_S out of every 5s while probing - in a prolonged
//  outage this adds up to touch being unresponsive for a large share of
//  that window (this was originally measured when ventClient itself was
//  the canary being probed during PROBATION; the vent feature keeps the
//  same backoff behaviour on its own merits now that it's a separate
//  client to NetworkCanary). Exponential backoff (doubling, capped) keeps
//  early retries fast for the common case (a brief blip) while backing off
//  hard during a genuinely prolonged outage, trading a few extra seconds of
//  recovery-detection latency for far less accumulated blocking.
// ============================================================
#define VENT_RECONNECT_BACKOFF_MAX_MS  15000UL   // cap - never wait longer between attempts

// ============================================================
//  NETWORK CONNECTION HEALTH STATE MACHINE  (see HA_OTA.cpp loopWifi())
//  Root problem this solves: WiFi.status() == WL_CONNECTED only means
//  "associated to an AP right now" - an instantaneous, local question.
//  During marginal-signal conditions the association can flap, or stay up
//  while the actual data path is unusable. mqtt.loop() called on a zombie
//  connection blocks loop() INDEFINITELY, entered but never returning until
//  connectivity is restored.
//
//  Time-based probation alone proved insufficient: even at 60s, WiFi held
//  "connected" the whole time while the data path was still unusable
//  (evidence: NTP reachable and current while MQTT time sat 3 minutes
//  stale, plus repeated blocking connects after probation passed).
//  Elapsed association time simply does not prove reachability.
//
//  CANARY DESIGN: promotion now requires a real end-to-end success, not a
//  timer. A dedicated, self-contained client (NetworkCanary.h/.cpp) is used
//  as the probe because it is bounded (CANARY_SOCKET_TIMEOUT_S) and
//  throttled (CANARY_RECONNECT_INTERVAL_MS). A successful canary connect()
//  proves routing, TCP handshake and broker auth all work - a genuine
//  reachability proof. Only then is the heavier, less controllable HAMqtt
//  allowed to run.
//
//  This probe used to be ventClient (the Bambu vent controller's MQTT
//  client) - functionally identical, but a bad architectural coupling for
//  open-source release: a user without vent hardware would reasonably
//  strip that feature out and unknowingly take the network health gate
//  with it. See NetworkCanary.h for the full rationale. ventClient is now
//  purely the vent controller / outdoor sensor client - it no longer
//  drives this state machine.
//
//  States:
//    NET_DOWN      - WiFi not associated. NOTHING network-touching runs.
//                    On entry, HAMqtt, ventClient AND the network canary
//                    are all force-disconnected so none of them can hold a
//                    zombie socket that "looks connected" when WiFi
//                    returns.
//    NET_PROBATION - WiFi associated and settled, but the path is UNPROVEN.
//                    The network canary is permitted to attempt its
//                    bounded, throttled connect. mqtt.loop() is NOT - it is
//                    the call that blocks indefinitely on a bad path.
//    NET_HEALTHY   - the network canary connected successfully: path
//                    proven end-to-end. mqtt.loop() permitted.
//
//  Demotion: WiFi drop -> NET_DOWN (+ force-disconnect all three clients).
//  Canary dropping while healthy -> back to NET_PROBATION, which stops
//  mqtt.loop() again before it can meet a degraded path.
//
//  Note the canary and HAMqtt target the same broker (BROKER_ADDR), so the
//  canary is a valid proxy for HAMqtt's reachability - not merely a
//  correlated guess.
// ============================================================
enum NetHealthState {
  NET_DOWN,
  NET_PROBATION,
  NET_HEALTHY
};
extern volatile NetHealthState netHealthState;

// How long WiFi must hold association before probing begins. Only needs to
// absorb association flapping now - the canary does the real proving, so
// this no longer has to be long (was 60s when time alone was the gate).
#define NET_WIFI_SETTLE_MS        5000UL

// ============================================================
//  WIFI RECONNECT MONITOR
//  The ESP32 Arduino core's built-in WiFi auto-reconnect is not reliable
//  after a genuine AP/router power-cycle (as opposed to a brief signal
//  drop) - WEM has been observed not to automatically rejoin the mesh after
//  the router was power-cycled. loopWifi() actively monitors connection
//  state and forces a fresh WiFi.begin() if down for more than
//  WIFI_RECONNECT_GRACE_MS, throttled to at most one attempt per
//  WIFI_RECONNECT_RETRY_MS so it doesn't hammer association attempts
//  continuously while genuinely out of range.
// ============================================================
#define WIFI_RECONNECT_GRACE_MS   10000UL   // tolerate this long before acting
#define WIFI_RECONNECT_RETRY_MS   15000UL   // minimum gap between reconnect attempts

// ============================================================
//  VENT MQTT SOCKET TIMEOUT
//  PubSubClient's underlying connect() can block for a long time if WiFi
//  reports connected but the broker path isn't fully usable yet (observed:
//  90+ seconds across several attempts after a real WiFi outage). Caps each
//  individual attempt so a slow/unresponsive path can't stall loop() for
//  extended periods - more attempts may be needed, but each blocks far less.
//  Note: even with this bound, a failed connect still blocks ~2-3s, which is
//  why loopVentSubscription() still gates its own attempts on netHealthState
//  != NET_DOWN - not on NET_HEALTHY. The vent feature is deliberately
//  allowed to probe during NET_PROBATION too (its cost is bounded and
//  throttled the same as the canary's), it just no longer has any say in
//  the PROBATION -> HEALTHY promotion itself - see NetworkCanary.h.
// ============================================================
#define VENT_SOCKET_TIMEOUT_S     1

// ============================================================
//  PRIMARY MQTT (HAMqtt) STALE WATCHDOG
//  Distinct failure mode from a WiFi outage: the broker itself goes away
//  (e.g. HA's MQTT add-on restarted) while WiFi stays perfectly associated
//  throughout. netHealthState therefore stays NET_HEALTHY - correctly, the
//  WiFi path IS fine - so the network state machine never intervenes.
//  This has been observed to leave every WEM entity in HA unavailable for
//  hours even after the underlying network recovered, meaning the primary
//  HAMqtt connection never genuinely re-established. If mqtt.isConnected()
//  stays false for this long, force a full teardown and rebuild rather
//  than trusting the library's own internal reconnect.
// ============================================================
#define MQTT_STALE_TIMEOUT_MS     30000UL   // force a rebuild after this long disconnected

extern PubSubClient ventClient;
extern volatile int   ventFanRPM;
extern volatile float ventChamberTemp;
extern volatile float ventAmbientTemp;
extern volatile float ventDeltaTemp;
extern volatile bool  ventFlagOpen;
extern volatile int   ventTriggerTimer;
extern char displayTime[20];   // "dd/mm/yyyy HH:MM" from HA MQTT

// ============================================================
//  OUTDOOR SENSOR  (rtl_433 Nexus-TH via MQTT wildcard)
//  Updated by ventClient callback on matching topics.
//  lastOutdoorUpdate = 0 means no data received yet (stale).
//  Used by Graphics.cpp updateOutsideBlock() for stale detection.
// ============================================================
extern volatile float outdoorTempC;
extern volatile float outdoorHumidity;
extern volatile unsigned long lastOutdoorUpdate;  // millis() of last valid receive

// SHT40 - primary temperature/humidity sensor
extern HASensorNumber ha_sht40_temp;
extern HASensorNumber ha_sht40_humidity;

// Display backlight
extern HASensor ha_backlight;

void setupWifi();
void loopWifi();      // call every loop() - actively monitors + recovers WiFi connection
void setupOTA();
void setupHA();
void loopHA();
void setupVentSubscription();
void loopVentSubscription();

#endif