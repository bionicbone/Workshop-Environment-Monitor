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
#include "SHT40_TEMP_HUMIDITY.h"   // sht40Detected/lastSHT40OkMs - humidity compensation source gate
#include <math.h>                  // expf() - humidity compensation conversion

Adafruit_SGP30 sgp30;
Preferences preferences;

bool sgp30Detected = false;           // set true only if the sensor answers at boot
unsigned long lastSGP30OkMs = 0;
bool sgp30BaselineRestored = false;   // set true only by a valid restore

// Current known baseline pair, for HA publishing only - kept separate from
// the NVS/live-chip values. 0 means "not yet known" (matches the existing
// 0 = no-baseline convention used elsewhere in this file). Set by a
// successful restore() or save() below; published from updateSGP30_VOC()'s
// existing periodic HA-publish window rather than at the point they're set -
// see the note above updateSGP30_VOC() for why.
static uint16_t currentEco2Baseline = 0;
static uint16_t currentTvocBaseline = 0;

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

      // Baseline entities - the saved/restored calibration state itself, not
      // a live reading. No unit set deliberately: these are raw on-chip
      // register values, not calibrated ppm/ppb (see the plausibility-range
      // check note under restoreSGP30Baseline() below for why they're only
      // conventionally treated as living in that numeric space). Set by
      // restoreSGP30Baseline()/saveSGP30Baseline() below, but actually
      // published from updateSGP30_VOC()'s periodic HA-publish window - see
      // the note in restoreSGP30Baseline() for why.
      ha_sgp30_eco2_baseline.setName("SGP30_eCO2_Baseline");
      ha_sgp30_eco2_baseline.setIcon("mdi:content-save-cog-outline");
      ha_sgp30_eco2_baseline.setStateClass("measurement");

      ha_sgp30_tvoc_baseline.setName("SGP30_TVOC_Baseline");
      ha_sgp30_tvoc_baseline.setIcon("mdi:content-save-cog-outline");
      ha_sgp30_tvoc_baseline.setStateClass("measurement");

      return;
    }
    delay(SENSOR_BEGIN_RETRY_DELAY_MS);
  }
  sgp30Detected = false;
  Serial.println("SGP30: Sensor not found - check wiring on Wire1 (0x58)");
  Serial.println("SGP30: Running WITHOUT VOC sensor.");
}

// ============================================================
//  HUMIDITY COMPENSATION
//
//  The SGP30's eCO2/TVOC algorithm has real cross-sensitivity to humidity.
//  Without ever calling setHumidity(), the chip runs on a fixed internal
//  assumption (Sensirion default, roughly equivalent to 25C/50%RH) with NO
//  correction for the real environment. An outdoor 12h soak on an otherwise
//  freshly NVS-reset chip logged TVOC tracking RH's phase changes (55% ->
//  70% -> 79% over one overnight session) rather than staying flat -
//  confirming this was a real, uncompensated source of error, not just a
//  theoretical one.
//
//  Sourced from SHT40 (see SHT40_TEMP_HUMIDITY.cpp) via the shared
//  temperature/humidity globals it already writes each ~5s cycle (declared
//  in SCD40_CO2.h - see that file's own note on why SHT40 is the sole,
//  no-fallback source for these globals project-wide). This function does
//  NOT read SHT40 itself, so it adds no new I2C traffic of its own beyond
//  the setHumidity() write below.
//
//  Throttled to once a minute: real humidity moves on a timescale of tens
//  of minutes to hours (weather, dew, HVAC cycling), not seconds, and every
//  extra I2C transaction on this bus has deserved caution ever since the
//  SHT40/SGP30 blocking-window investigation - no reason to add near-1Hz
//  traffic (updateSGP30_VOC() runs on a 1s timer) for a value that doesn't
//  change anywhere near that fast.
//
//  Gated on sht40Detected AND freshness (STALE_MED_MS - the same gate
//  Graphics.cpp/Buzzer.cpp already use for SHT40 elsewhere). A missing or
//  stalled SHT40 must NOT silently keep feeding an old, increasingly-wrong
//  humidity value forever - if gated out, the SGP30 just keeps using
//  whatever compensation it last had (its own uncompensated default on a
//  cold boot with no SHT40) - never worse than pre-existing behaviour.
//
//  Conversion is Sensirion's own documented approximation (SGP30 Driver
//  Integration app note, section 3.15) from degC + %RH to absolute
//  humidity, which setHumidity() expects in mg/m^3. A value of exactly 0
//  would DISABLE humidity compensation entirely (Adafruit_SGP30 library
//  behaviour) rather than error, so it's explicitly guarded against; the
//  library's own internal cap is 256000 mg/m^3, checked here too so an
//  implausible input is logged rather than silently failing the I2C call.
// ============================================================
#define SGP30_HUMIDITY_UPDATE_MS  (60UL * 1000UL)   // real humidity moves slowly - no need for 1Hz writes

static void updateSGP30HumidityCompensation() {
  if (!sht40Detected || sensorStale(lastSHT40OkMs, STALE_MED_MS)) {
    return;
  }

  static unsigned long lastHumidityUpdateMs = 0;
  if (millis() - lastHumidityUpdateMs < SGP30_HUMIDITY_UPDATE_MS) {
    return;
  }
  lastHumidityUpdateMs = millis();

  float t = temperature;   // shared global, SHT40-owned - see SCD40_CO2.h
  float rh = humidity;     // shared global, SHT40-owned - see SCD40_CO2.h

  float absHumidityGm3 = 216.7f *
    (((rh / 100.0f) * 6.112f * expf((17.62f * t) / (243.12f + t))) / (273.15f + t));
  uint32_t absHumidityMgm3 = (uint32_t)(absHumidityGm3 * 1000.0f);

  if (absHumidityMgm3 == 0 || absHumidityMgm3 > 256000UL) {
    debugLoop("Humidity compensation skipped - implausible result (%.2f g/m3 from %.1fC/%.1f%%RH)",
      absHumidityGm3, t, rh);
    debugSpecial("Humidity compensation skipped - implausible result (%.2f g/m3 from %.1fC/%.1f%%RH)",
      absHumidityGm3, t, rh);
    return;
  }

  bool ok = sgp30.setHumidity(absHumidityMgm3);
  debugLoop("Humidity compensation %s -> %.2f g/m3 (from %.1fC/%.1f%%RH)",
    ok ? "set" : "FAILED", absHumidityGm3, t, rh);
  debugSpecial("Humidity compensation %s -> %.2f g/m3 (from %.1fC/%.1f%%RH)",
    ok ? "set" : "FAILED", absHumidityGm3, t, rh);
}

// ============================================================
//  LOOP - call every loop()
// ============================================================
void updateSGP30_VOC() {
  // ---- Absent sensor: no I2C traffic, no HA publish, nothing else ----
  if (!sgp30Detected) {
    return;
  }

  updateSGP30HumidityCompensation();

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

    // Baseline pair - published unconditionally, including 0/0. 0 can never
    // be a legitimate restored/saved value (the plausibility-range floor
    // elsewhere in this file already enforces eCO2 >= 400), so it's an
    // unambiguous "no baseline currently known" signal - both for a chip
    // that's never had one, and right after an NVS reset. An earlier
    // version gated this on != 0 to avoid publishing a "misleading" zero,
    // but that meant a reset went silent in HA instead of showing the
    // clear: with nothing republished, HA just kept displaying whatever it
    // last received, making a working reset look like it had done nothing.
    // Piggybacks on this same throttle rather than its own timer: the pair
    // rarely changes (once at boot/reset, then hourly), so there's no
    // benefit to publishing more often, and reusing this window means it
    // self-heals on MQTT reconnect exactly like eCO2/TVOC above.
    ha_sgp30_eco2_baseline.setValue(currentEco2Baseline);
    ha_sgp30_tvoc_baseline.setValue(currentTvocBaseline);
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

  currentEco2Baseline = eco2Base;
  currentTvocBaseline = tvocBase;

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

  // NOT published to HA here - restoreSGP30Baseline() runs during setup(),
  // before Home Assistant MQTT is initialised (see the .ino's own "Set up
  // all sensors BEFORE setting up Home Assistant MQTT" ordering note), so a
  // setValue() call at this point would be published to nothing and
  // silently dropped, with no retry, every single boot. currentEco2Baseline/
  // currentTvocBaseline below are published from updateSGP30_VOC()'s
  // existing periodic HA-publish window instead, which only ever runs from
  // loop() - safely after MQTT setup - and self-heals every cycle exactly
  // like every other entity in this app already does.
  currentEco2Baseline = eco2Base;
  currentTvocBaseline = tvocBase;

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
  currentEco2Baseline = 0;   // defensive, same reasoning as sgp30BaselineRestored above
  currentTvocBaseline = 0;

  Serial.println("SGP30: Baseline cleared via long-press - restarting automatically to complete reset");
}

uint16_t getTVOC() {
  if (!sgp30Detected) {
    return 0;
  }
  return sgp30.TVOC;
}