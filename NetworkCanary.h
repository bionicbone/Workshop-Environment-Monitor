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

// NetworkCanary.h

#ifndef _NETWORK_CANARY_h
#define _NETWORK_CANARY_h

#if defined(ARDUINO) && ARDUINO >= 100
#include "Arduino.h"
#else
#include "WProgram.h"
#endif

#include <WiFi.h>
#include <PubSubClient.h>

// ============================================================
//  NETWORK CANARY  (open-source decoupling of the network health probe)
//  Self-contained MQTT heartbeat client whose sole job is to prove real
//  end-to-end broker reachability (DNS/routing/TCP/broker-auth) before the
//  network health state machine (see HA_OTA.h/.cpp, NetHealthState) permits
//  the heavier, less-controllable HAMqtt connection to run mqtt.loop().
//
//  This role used to be filled by ventClient (the Bambu vent controller's
//  MQTT client) - functionally fine, since a successful connect() was all
//  the canary ever needed and it didn't require anything to actually
//  publish on the vent topics. But it was a bad architectural coupling for
//  an open-source release: a user without a Bambu vent controller would
//  reasonably strip the vent feature out entirely when adapting this
//  firmware, and would unknowingly take the network health gate with it -
//  silently reverting to raw WiFi.status() blocking with no warning.
//
//  This module has NO dependency on vent hardware, or on any other
//  optional feature/sensor. It targets a topic the WEM owns itself
//  (CANARY_TOPIC) rather than piggybacking on vent-controller topics, so it
//  works identically whether or not vent hardware - or any other optional
//  feature - is fitted. Clone-and-run for any user.
//
//  It DOES depend on NetHealthState (declared in HA_OTA.h) so it knows not
//  to burn reconnect attempts during NET_DOWN. That's a dependency on the
//  general network-health concept HA_OTA.h owns, not on vent-specific code,
//  so it doesn't reintroduce the coupling this module exists to remove.
// ============================================================

#define CANARY_CLIENT_ID  "workshop_environment_monitor_canary"
#define CANARY_TOPIC      "workshop_environment_monitor/canary"

// Same bound/throttle rationale that applied when ventClient filled this
// role (see HA_OTA.h history): this client must stay cheap to fail, since
// it is the one deliberately probed against an unproven path so the
// heavier HAMqtt client never has to meet a bad path itself.
#define CANARY_SOCKET_TIMEOUT_S           1
#define CANARY_RECONNECT_INTERVAL_MS      5000UL
#define CANARY_RECONNECT_BACKOFF_MAX_MS   15000UL

extern PubSubClient canaryClient;

void setupNetworkCanary();        // call once from setup()
void loopNetworkCanary();         // call every loop() - internally throttled/backed-off, gated on netHealthState
void disconnectNetworkCanary();   // force-disconnect - call on WiFi loss (see loopWifi() NET_DOWN entry in HA_OTA.cpp)
bool isNetworkCanaryConnected();  // true = path proven end-to-end; this is what promotes NET_PROBATION -> NET_HEALTHY

#endif