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

// PMS5003_Particles.cpp

#include "PMS5003_Particles.h"

Adafruit_PM25AQI pms5003 = Adafruit_PM25AQI();
PM25_AQI_Data pmsData;

bool pms5003Detected = false;   // set true only if the sensor answers at boot
unsigned long lastPMS5003OkMs = 0;

// Forward declaration - defined below alongside updatePMS5003(), but
// setupPMS5003() needs to call it before that point in the file.
static void publishPMS5003Frame();

// ============================================================
//  SETUP
//
//  A 3-second retry loop gives the sensor time to come up on UART, before we
//  write it off for the session - same pattern as LD2410B_Presence.cpp,
//  SGP30_VOC.cpp, BME280_TEMP_HUMIDITY.cpp, SHT40_TEMP_HUMIDITY.cpp and
//  SCD40_CO2.cpp. On success pms5003Detected is latched true, HA entities
//  are created, and we return immediately. If the loop falls through the
//  sensor is treated as ABSENT for the rest of the run: pms5003Detected
//  stays false, no HA entities are created for it, and updatePMS5003() /
//  sleepPMS5003() / wakePMS5003() all gate on the flag - an absent sensor
//  never gets read attempts or sleep/wake bytes written to it, and never
//  publishes stale/zero data to HA.
//
//  DETECTION DESIGN: Adafruit_PM25AQI::begin_UART() guards on an internal
//  pointer and returns false unconditionally on every call after the
//  first, regardless of sensor state - it cannot be retried. Its return
//  value is therefore never used as the detection signal here. begin_UART()
//  is called exactly once to attach the stream; detection is instead based
//  on waiting for a genuine checksum-passed read() within the retry
//  window, which has no such one-shot restriction. This scan is done
//  manually against the raw Serial2 bytes for the 0x42 0x4D sync pair
//  before handing off to the library's read(), rather than trusting the
//  library to resync on its own, so it doesn't depend on any assumption
//  about read()'s internal resync behaviour.
//
//  ROOT CAUSE OF INTERMITTENT POST-OTA DETECTION FAILURE: sleepPMS5003()
//  puts the sensor to sleep as part of the ~120s duty cycle. An OTA/soft
//  reset (RTC_SW_CPU_RST) resets the ESP32 but does NOT power-cycle the
//  PMS5003 - so the sensor can come back to a freshly-booted ESP32 still
//  asleep, fan and laser off, transmitting NOTHING. No byte stream exists
//  to sync to in that state. A cold power-on always succeeds because
//  power-up puts the sensor in active mode. Structurally the same class of
//  fault as the FT5x06 touch controller finding - a soft reset leaves
//  peripheral state latched where a power cycle would clear it.
//  FIX: unconditionally send the wake command at the start of setup,
//  before looking for any data. Harmless if the sensor was already awake.
// ============================================================
void setupPMS5003() {
  Serial2.begin(9600, SERIAL_8N1, PMS5003_RX_PIN, PMS5003_TX_PIN);
  delay(100);

  // ---- Wake the sensor before looking for data - see root cause note above ----
  // An OTA/soft reset resets the ESP32 but does NOT power-cycle the PMS5003,
  // so it can still be asleep from the previous session's duty cycle and
  // transmitting nothing at all. Unconditionally waking it here costs one
  // harmless command if it was already awake.
  uint8_t wakeCmd[] = { 0x42, 0x4D, 0xE4, 0x00, 0x01, 0x01, 0x74 };
  Serial2.write(wakeCmd, sizeof(wakeCmd));
  Serial2.flush();
  delay(1000);            // let the fan/laser spin up and streaming resume
  resetPMS5003Stable();   // data won't be trustworthy until the 30s window elapses

  // Attach the stream once - see note above. Return value deliberately not
  // checked (cannot be retried - see note #1).
  pms5003.begin_UART(&Serial2);

  uint32_t timeout = millis();

  // ---- Stage 1: manual raw-byte resync to the frame boundary ----
  bool synced = false;
  uint8_t prevByte = 0;
  while (millis() - timeout < SENSOR_BEGIN_RETRY_MS && !synced) {
    while (Serial2.available()) {
      uint8_t b = Serial2.read();
      if (prevByte == 0x42 && b == 0x4D) {
        synced = true;
        break;
      }
      prevByte = b;
    }
  }

  if (synced) {
    Serial.printf("PMS5003: sync bytes found at t+%lums, waiting for a full frame...\n",
      millis() - timeout);

    // ---- Stage 2: let the library parse the next complete, checksum-passed frame ----
    while (millis() - timeout < SENSOR_BEGIN_RETRY_MS) {
      if (pms5003.read(&pmsData)) {
        pms5003Detected = true;
        Serial.printf("PMS5003: Found and initialised (first frame at t+%lums)\n",
          millis() - timeout);

        ha_pms_pm10_std.setName("PMS5003_PM1.0_Standard");
        ha_pms_pm10_std.setUnitOfMeasurement("\u00B5g/m\u00B3");
        ha_pms_pm10_std.setIcon("mdi:blur");

        ha_pms_pm25_std.setName("PMS5003_PM2.5_Standard");
        ha_pms_pm25_std.setUnitOfMeasurement("\u00B5g/m\u00B3");
        ha_pms_pm25_std.setIcon("mdi:blur");

        ha_pms_pm100_std.setName("PMS5003_PM10_Standard");
        ha_pms_pm100_std.setUnitOfMeasurement("\u00B5g/m\u00B3");
        ha_pms_pm100_std.setIcon("mdi:blur");

        ha_pms_pm10_atm.setName("PMS5003_PM1.0_Atmospheric");
        ha_pms_pm10_atm.setUnitOfMeasurement("\u00B5g/m\u00B3");
        ha_pms_pm10_atm.setIcon("mdi:blur");

        ha_pms_pm25_atm.setName("PMS5003_PM2.5_Atmospheric");
        ha_pms_pm25_atm.setUnitOfMeasurement("\u00B5g/m\u00B3");
        ha_pms_pm25_atm.setIcon("mdi:blur");

        ha_pms_pm100_atm.setName("PMS5003_PM10_Atmospheric");
        ha_pms_pm100_atm.setUnitOfMeasurement("\u00B5g/m\u00B3");
        ha_pms_pm100_atm.setIcon("mdi:blur");

        ha_pms_cnt_03.setName("PMS5003_Count_0.3um");
        ha_pms_cnt_03.setUnitOfMeasurement("cnt/0.1L");
        ha_pms_cnt_03.setIcon("mdi:dots-circle");

        ha_pms_cnt_05.setName("PMS5003_Count_0.5um");
        ha_pms_cnt_05.setUnitOfMeasurement("cnt/0.1L");
        ha_pms_cnt_05.setIcon("mdi:dots-circle");

        ha_pms_cnt_10.setName("PMS5003_Count_1.0um");
        ha_pms_cnt_10.setUnitOfMeasurement("cnt/0.1L");
        ha_pms_cnt_10.setIcon("mdi:dots-circle");

        ha_pms_cnt_25.setName("PMS5003_Count_2.5um");
        ha_pms_cnt_25.setUnitOfMeasurement("cnt/0.1L");
        ha_pms_cnt_25.setIcon("mdi:dots-circle");

        ha_pms_cnt_50.setName("PMS5003_Count_5.0um");
        ha_pms_cnt_50.setUnitOfMeasurement("cnt/0.1L");
        ha_pms_cnt_50.setIcon("mdi:dots-circle");

        ha_pms_cnt_100.setName("PMS5003_Count_10um");
        ha_pms_cnt_100.setUnitOfMeasurement("cnt/0.1L");
        ha_pms_cnt_100.setIcon("mdi:dots-circle");

        // ---- Publish the detection frame immediately ----
        // The detection scan above already parsed a genuine checksum-passed
        // frame to confirm the sensor is present - publishing it here means
        // the display/HA show real PM data from boot instead of waiting up
        // to 2 minutes for loopPMS5003()'s first normal cycle. Deliberately
        // skips the usual 30s-stabilisation convention: this frame arrives
        // well after the sensor's power-up (setupPMS5003() already sent an
        // unconditional wake and waited 1000ms before the detection scan
        // even started), so it is not a cold/unstable reading. Low-risk
        // either way - if this publish is ever wrong for any reason, the
        // normal 120s cycle overwrites it within 2 minutes regardless.
        lastPMS5003OkMs = millis();
        publishPMS5003Frame();
        Serial.println("PMS5003: Detection frame published immediately");
        return;
      }
      // No delay here - resync is byte-driven, not time-paced.
    }
  }
  else {
    Serial.println("PMS5003: No sync bytes seen within the retry window");
  }

  pms5003Detected = false;
  Serial.println("PMS5003: Sensor not found - check wiring on Serial2/GPIO21/35");
  Serial.println("PMS5003: Running WITHOUT PMS5003 sensor.");
}

// PMS_STABILISE_MS - 30s from wake to window open. PMS_READ_WINDOW_MS and
// the three PMS_RECOVERY_TIERn_MS thresholds define a window that keeps
// trying for a fresh frame, escalates through progressively more drastic
// recovery actions if none arrives, and always still sleeps and restarts
// the cycle at the end - never leaves the sensor wedged awake indefinitely.
#define PMS_STABILISE_MS        30000UL   // t=90s wake -> t=120s window open
#define PMS_READ_WINDOW_MS      12000UL   // max time to get a frame once open
#define PMS_RECOVERY_TIER1_MS    3000UL   // no frame yet -> flush RX
#define PMS_RECOVERY_TIER2_MS    6000UL   // no frame yet -> re-send wake
#define PMS_RECOVERY_TIER3_MS    9000UL   // no frame yet -> reinit Serial2

// Discards whatever is currently sitting in the Serial2 RX buffer. Used
// (a) once when the read window opens, to clear the ~30s backlog that
// accumulates while the sensor streams unread during stabilisation -
// without this, a read at window-open could pick up data captured a full
// cycle stale, and (b) as recovery tier 1, on the theory that a misaligned
// byte sitting at the head of the buffer can otherwise wedge the library's
// resync logic indefinitely.
static void pmsFlushRx() {
  int flushed = 0;
  while (Serial2.available()) {
    Serial2.read();
    flushed++;
  }
  debugLoop("RX flushed (%d bytes discarded)", flushed);
  debugSpecial("RX flushed (%d bytes discarded)", flushed);
}

// ============================================================
//  LOOP TIMING - call every loop()
//
//  Uses an open read window and a recovery ladder rather than a
//  single-shot "read once at t=120s, sleep regardless of success"
//  approach - a single-shot read that happens to land on a bad frame can
//  otherwise leave the sensor's last known value flat-lined in HA for
//  hours until something forces a reboot.
//
//  State machine:
//    PMS_ASLEEP  - t=0 cycle start. At t=90s: wake, -> PMS_WARMING.
//    PMS_WARMING - 30s stabilisation. At the end: mark stable, flush the
//                  RX backlog, -> PMS_READING.
//    PMS_READING - read window open, sensor stays awake. Every loop:
//                  attempt a read via updatePMS5003().
//                    - success -> sleep, -> PMS_ASLEEP, cycle restarts.
//                    - no success, window not yet expired -> escalate
//                      through the recovery ladder at the tier thresholds
//                      above (each tier fires once per window, logged via
//                      both debugLoop and debugSpecial).
//                    - window expires with no success -> give up cleanly:
//                      sleep, -> PMS_ASLEEP, cycle restarts. Sensor is
//                      never left awake/wedged indefinitely regardless of
//                      what went wrong.
//
//  wakePMS5003() must remain non-blocking. The tier-3 Serial2 reinit is
//  likewise kept delay-free to respect that and the SFA40 "every loop(),
//  must never be delayed" constraint - Serial2.end()/begin() are called
//  back-to-back with no settle delay. Flag for hardware testing: if this
//  proves insufficient on real hardware, a non-blocking settle (state-
//  machine substep rather than delay()) would need to be designed in,
//  never a blocking delay() in loop().
// ============================================================
void loopPMS5003() {
  enum PmsState { PMS_ASLEEP, PMS_WARMING, PMS_READING };
  static PmsState      pmsState = PMS_ASLEEP;
  static unsigned long pmsStateStart = 0;
  static int           pmsRecoveryTier = 0;   // 0 = none fired yet this window

  unsigned long now = millis();
  unsigned long elapsed = now - pmsStateStart;

  switch (pmsState) {

  case PMS_ASLEEP:
    if (elapsed >= 90000UL) {
      wakePMS5003();
      resetPMS5003Stable();   // mark unstable - sensor warming up
      pmsState = PMS_WARMING;
      pmsStateStart = now;
    }
    break;

  case PMS_WARMING:
    if (elapsed >= PMS_STABILISE_MS) {
      markPMS5003Stable();    // 30s elapsed since wake - safe to read
      pmsFlushRx();           // clear the 30s backlog before reading it
      pmsState = PMS_READING;
      pmsStateStart = now;
      pmsRecoveryTier = 0;
    }
    break;

  case PMS_READING:
    if (updatePMS5003()) {
      sleepPMS5003();
      pmsState = PMS_ASLEEP;
      pmsStateStart = now;
      break;
    }

    if (elapsed >= PMS_READ_WINDOW_MS) {
      debugLoop("Read window expired with no frame - giving up this cycle");
      debugSpecial("Read window expired with no frame - giving up this cycle");
      sleepPMS5003();
      pmsState = PMS_ASLEEP;
      pmsStateStart = now;
      break;
    }

    if (pmsRecoveryTier < 1 && elapsed >= PMS_RECOVERY_TIER1_MS) {
      debugLoop("Recovery tier 1: flushing RX buffer");
      debugSpecial("Recovery tier 1: flushing RX buffer");
      pmsFlushRx();
      pmsRecoveryTier = 1;
    }
    else if (pmsRecoveryTier < 2 && elapsed >= PMS_RECOVERY_TIER2_MS) {
      debugLoop("Recovery tier 2: re-sending wake command");
      debugSpecial("Recovery tier 2: re-sending wake command");
      wakePMS5003();
      pmsRecoveryTier = 2;
    }
    else if (pmsRecoveryTier < 3 && elapsed >= PMS_RECOVERY_TIER3_MS) {
      debugLoop("Recovery tier 3: reinitialising Serial2");
      debugSpecial("Recovery tier 3: reinitialising Serial2");
      Serial2.end();
      Serial2.begin(9600, SERIAL_8N1, PMS5003_RX_PIN, PMS5003_TX_PIN);
      pmsRecoveryTier = 3;
    }
    break;
  }
}

static bool pms5003Stable = false;

void markPMS5003Stable() {
  pms5003Stable = true;
}

void resetPMS5003Stable() {
  pms5003Stable = false;
}

// ============================================================
//  PUBLISH - shared by updatePMS5003() (normal cycle) and setupPMS5003()
//  (detection-frame fast path). One place for the setValue/debug/
//  setDirtyParticles block so the two call sites cannot drift apart.
//  Caller is responsible for having already confirmed a checksum-passed
//  frame sits in `pmsData` and for setting lastPMS5003OkMs.
// ============================================================
static void publishPMS5003Frame() {
  ha_pms_pm10_std.setValue(pmsData.pm10_standard);
  ha_pms_pm25_std.setValue(pmsData.pm25_standard);
  ha_pms_pm100_std.setValue(pmsData.pm100_standard);

  ha_pms_pm10_atm.setValue(pmsData.pm10_env);
  ha_pms_pm25_atm.setValue(pmsData.pm25_env);
  ha_pms_pm100_atm.setValue(pmsData.pm100_env);

  ha_pms_cnt_03.setValue(pmsData.particles_03um);
  ha_pms_cnt_05.setValue(pmsData.particles_05um);
  ha_pms_cnt_10.setValue(pmsData.particles_10um);
  ha_pms_cnt_25.setValue(pmsData.particles_25um);
  ha_pms_cnt_50.setValue(pmsData.particles_50um);
  ha_pms_cnt_100.setValue(pmsData.particles_100um);

  debugLoop("PM1.0: %u | PM2.5: %u | PM10: %u ug/m3 (atm)", pmsData.pm10_env, pmsData.pm25_env, pmsData.pm100_env);
  debugLoop("0.3um: %u | 0.5um: %u | 1.0um: %u cnt/0.1L", pmsData.particles_03um, pmsData.particles_05um, pmsData.particles_10um);

  setDirtyParticles();
}

// ============================================================
//  READ - called by loopPMS5003() while the read window is open
//  Returns true on a successful, published read; false otherwise.
// ============================================================
bool updatePMS5003() {
  // ---- Absent sensor: no UART traffic, no HA publish, nothing else ----
  if (!pms5003Detected) {
    return false;
  }

  // Only reachable via loopPMS5003() after markPMS5003Stable() has already
  // been called - kept as a defensive guard for any future direct call,
  // not as a real gate in the current call path.
  if (!pms5003Stable) {
    debugLoop("Skipping read - stabilisation window not elapsed");
    debugSpecial("Skipping read - stabilisation window not elapsed");
    return false;
  }

  int rxBefore = Serial2.available();
  if (!pms5003.read(&pmsData)) {
    // debugLoop only, not debugSpecial - this fires on every failed loop()
    // attempt while the read window is open (dozens per cycle in normal
    // operation), so it dominates log volume and isn't useful for ongoing
    // ladder-validation monitoring, where the signal is
    // pmsFlushRx/wake/sleep/Recovery-tier lines, not this. Bump to DEBUG 1
    // if per-attempt RX byte counts are ever needed for characterisation.
    debugLoop("Read failed - RX %d bytes before, %d after", rxBefore, Serial2.available());
    return false;
  }

  lastPMS5003OkMs = millis();
  publishPMS5003Frame();
  return true;
}

void sleepPMS5003() {
  if (!pms5003Detected) {
    return;
  }
  uint8_t sleepCmd[] = { 0x42, 0x4D, 0xE4, 0x00, 0x00, 0x01, 0x73 };
  Serial2.write(sleepCmd, sizeof(sleepCmd));
  debugLoop("Sleeping");
  debugSpecial("Sleeping");
}

void wakePMS5003() {
  if (!pms5003Detected) {
    return;
  }
  uint8_t wakeCmd[] = { 0x42, 0x4D, 0xE4, 0x00, 0x01, 0x01, 0x74 };
  Serial2.write(wakeCmd, sizeof(wakeCmd));
  debugLoop("Awake and stabilised");
  debugSpecial("Awake and stabilised");
}