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

// SGP30_VOC.cpp

#include "SGP30_VOC.h"

Adafruit_SGP30 sgp30;
Preferences preferences;

bool sgp30Detected = false;           // set true only if the sensor answers at boot
unsigned long lastSGP30OkMs = 0;
bool sgp30BaselineRestored = false;   // set true only by a valid restore

// ============================================================
//  SETUP
//
//  A 3-second retry loop gives the sensor time to come up on I2C, before we
//  write it off for the session - same pattern as LD2410B_Presence.cpp.
//  On success sgp30Detected is latched true,
//  HA entities are created, and we return immediately. If the loop falls
//  through the sensor is treated as ABSENT for the rest of the run:
//  sgp30Detected stays false, no HA entities are created for it, and
//  updateSGP30_VOC() / saveSGP30Baseline() / restoreSGP30Baseline() /
//  getTVOC() all gate on the flag - an absent sensor never generates I2C
//  traffic, never publishes stale/zero data to HA, and never gets an NVS
//  baseline write or read attempted.
// ============================================================
void setupSGP30_VOC() {
  uint32_t timeout = millis();
  while (millis() - timeout < SENSOR_BEGIN_RETRY_MS) {
    if (sgp30.begin(&Wire1)) {
      sgp30Detected = true;
      Serial.printf("SGP30: Found, Serial # %02X%02X%02X\n",
        sgp30.serialnumber[0],
        sgp30.serialnumber[1],
        sgp30.serialnumber[2]);

      ha_sgp30_eco2.setName("SGP30_eCO2");
      ha_sgp30_eco2.setUnitOfMeasurement("ppm");
      ha_sgp30_eco2.setIcon("mdi:molecule-co2");
      ha_sgp30_eco2.setStateClass("measurement");

      ha_sgp30_tvoc.setName("SGP30_TVOC");
      ha_sgp30_tvoc.setUnitOfMeasurement("ppb");
      ha_sgp30_tvoc.setIcon("mdi:air-filter");
      ha_sgp30_tvoc.setStateClass("measurement");

      return;
    }
    delay(SENSOR_BEGIN_RETRY_DELAY_MS);
  }
  sgp30Detected = false;
  Serial.println("SGP30: Sensor not found - check wiring on Wire1 (0x58)");
  Serial.println("SGP30: Running WITHOUT VOC sensor.");
}

// ============================================================
//  LOOP - call every loop()
// ============================================================
void updateSGP30_VOC() {
  // ---- Absent sensor: no I2C traffic, no HA publish, nothing else ----
  if (!sgp30Detected) {
    return;
  }

  // TIMING INSTRUMENTATION. IAQmeasure() is a blocking I2C call. This print
  // exists to see exactly when each attempt (success or failure) falls and
  // how long it took, so it can be lined up against SHT40's own timing
  // prints (see SHT40_TEMP_HUMIDITY.cpp) if a blocking-window collision
  // between the two is ever suspected again.
  // debugSpecial (not debugLoop) - useful for diagnosing blocking-related
  // faults, not needed in day-to-day operation. Flip DEBUG to 2 to isolate
  // just these prints.
  unsigned long sgp30StartUs = micros();
  bool ok = sgp30.IAQmeasure();
  unsigned long sgp30DurationUs = micros() - sgp30StartUs;

  if (!ok) {
    debugLoop("Measurement failed (blocked %lu us)", sgp30DurationUs);
    debugSpecial("Measurement failed (blocked %lu us)", sgp30DurationUs);
    return;
  }

  lastSGP30OkMs = millis();

  static unsigned long lastHAPublishMs = 0;
  if (millis() - lastHAPublishMs >= HA_PUBLISH_INTERVAL_MS) {
    lastHAPublishMs = millis();
    ha_sgp30_eco2.setValue(sgp30.eCO2);
    ha_sgp30_tvoc.setValue(sgp30.TVOC);
  }

  debugLoop("OK -> eCO2:%u ppm | TVOC:%u ppb (blocked %lu us)", sgp30.eCO2, sgp30.TVOC, sgp30DurationUs);

  setDisplayDirty();
}

// ============================================================
//  BASELINE SAVE  (two-tier warmup gate)
//
//  Two settle periods, per Sensirion's own guidance:
//   - No baseline was restored at boot (cold, uncalibrated chip): the
//     on-chip IAQ algorithm itself needs the full 12h run-in before its
//     internal state is meaningful enough to persist.
//   - A valid baseline WAS restored at boot: the algorithm starts from a
//     known-good point rather than zero, so Sensirion's guidance is to
//     resume periodic saves much sooner - around 1h - rather than repeating
//     the full cold-start wait unnecessarily.
//  sgp30BaselineRestored is only ever set true by a restore that also
//  passed the plausibility range check in restoreSGP30Baseline() below -
//  an out-of-range/corrupted restore is treated as no baseline at all, so
//  it falls back to the full 12h gate rather than the shortened one.
// ============================================================
#define SGP30_WARMUP_MS           (12UL * 60UL * 60UL * 1000UL)  // cold start, no baseline restored
#define SGP30_WARMUP_RESTORED_MS  (1UL * 60UL * 60UL * 1000UL)   // valid baseline restored at boot

void saveSGP30Baseline() {
  if (!sgp30Detected) {
    return;
  }

  unsigned long warmup = sgp30BaselineRestored ? SGP30_WARMUP_RESTORED_MS : SGP30_WARMUP_MS;

  if (millis() < warmup) {
    debugLoop("Baseline save skipped - still in warmup (%s)",
      sgp30BaselineRestored ? "restored baseline, 1h gate" : "cold start, 12h gate");
    debugSpecial("Baseline save skipped - still in warmup (%s)",
      sgp30BaselineRestored ? "restored baseline, 1h gate" : "cold start, 12h gate");
    return;
  }

  uint16_t eco2Base, tvocBase;
  sgp30.getIAQBaseline(&eco2Base, &tvocBase);

  preferences.begin("sgp30", false);
  preferences.putUShort("eco2base", eco2Base);
  preferences.putUShort("tvocbase", tvocBase);
  preferences.end();
  debugLoop("Baseline saved");
  debugSpecial("Baseline saved");
}

// ============================================================
//  BASELINE RESTORE
//
//  Plausibility range check: the SGP30 datasheet specifies measurement
//  output ranges of eCO2 400-60000ppm and TVOC 0-60000ppb.
//  getIAQBaseline()/setIAQBaseline() values are not separately specified by
//  the datasheet, but are conventionally treated (Adafruit driver, wider
//  community practice) as living in that same numeric space, and this
//  matches what was actually observed on hardware: a corrupted baseline
//  restored from a mismatched chip read eCO2 57330 - just under the 60000
//  ceiling. A pair outside these bounds is discarded rather than trusted -
//  this does NOT catch every bad baseline (a plausible-looking but
//  wrong-chip baseline, like an eCO2 61052/TVOC 54 pair observed on
//  hardware, passes both this check and the pre-existing >0 check) - it
//  only catches saturated/corrupted values. The Sensor Replacement
//  Procedure (Schematic Reference) - clear NVS via long-press BEFORE
//  running a newly-fitted chip - remains the real defence against the
//  wrong-chip case.
//
//  sgp30BaselineRestored is only latched true on a restore that passes
//  BOTH checks - it gates the shortened warmup in saveSGP30Baseline() above,
//  so a discarded/corrupt restore correctly falls back to the full 12h gate
//  rather than persisting a bad baseline sooner.
// ============================================================
#define SGP30_ECO2_MIN  400
#define SGP30_ECO2_MAX  60000
#define SGP30_TVOC_MIN  0
#define SGP30_TVOC_MAX  60000

void restoreSGP30Baseline() {
  if (!sgp30Detected) {
    return;
  }

  preferences.begin("sgp30", true);
  uint16_t eco2Base = preferences.getUShort("eco2base", 0);
  uint16_t tvocBase = preferences.getUShort("tvocbase", 0);
  preferences.end();

  if (eco2Base == 0 || tvocBase == 0) {
    Serial.println("SGP30: No saved baseline found, starting fresh");
    return;
  }

  if (eco2Base < SGP30_ECO2_MIN || eco2Base > SGP30_ECO2_MAX ||
    tvocBase < SGP30_TVOC_MIN || tvocBase > SGP30_TVOC_MAX) {
    Serial.printf("SGP30: Stored baseline out of plausible range (eCO2:%u TVOC:%u) - discarding, starting fresh\n",
      eco2Base, tvocBase);
    return;
  }

  sgp30.setIAQBaseline(eco2Base, tvocBase);
  sgp30BaselineRestored = true;
  Serial.printf("SGP30: Baseline restored -> eCO2: %u | TVOC: %u\n",
    eco2Base, tvocBase);
}

// ============================================================
//  RESET  - long-press triggered, see Buzzer.cpp readTouchEvent()
//
//  Clears the STORED NVS baseline only. Opens its own independent read-write
//  Preferences session on the "sgp30" namespace, distinct from and never
//  overlapping with restoreSGP30Baseline()'s read-only session - each
//  function holds write access only for the instant it needs it, matching
//  the pattern saveSGP30Baseline() already uses for its hourly write.
//
//  This does NOT touch the sensor's live on-chip IAQ algorithm state - that
//  was already set by setIAQBaseline() at boot if a (possibly mismatched)
//  baseline was restored, and clearing NVS now cannot undo that for the
//  current session. A reboot is still required afterward for the clear to
//  take full effect - Buzzer.cpp handles this automatically once the
//  confirmation tone finishes AND the triggering touch has been released
//  (see Buzzer.h for why the release-gate matters). This function only ever
//  clears NVS - it never reboots itself, so it stays safely callable on its
//  own (e.g. for a future non-touch trigger) without pulling in reboot
//  behaviour implicitly.
//
//  sgp30BaselineRestored is cleared here too - defensive only, since this
//  function is only ever called on a path that reboots immediately after,
//  but keeps in-memory state consistent with NVS in case that ever changes.
// ============================================================
void resetSGP30Baseline() {
  if (!sgp30Detected) {
    debugLoop("Reset skipped - sensor not detected");
    debugSpecial("Reset skipped - sensor not detected");
    return;
  }

  preferences.begin("sgp30", false);
  preferences.clear();
  preferences.end();

  sgp30BaselineRestored = false;

  Serial.println("SGP30: Baseline cleared via long-press - restarting automatically to complete reset");
}

uint16_t getTVOC() {
  if (!sgp30Detected) {
    return 0;
  }
  return sgp30.TVOC;
}