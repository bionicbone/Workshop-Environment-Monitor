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

// Buzzer.cpp
// Workshop Environment Monitor
//
// Drives the piezo buzzer for hazard alarms and touch-driven UI feedback.
//
// KEY BEHAVIOUR:
//
// 1. ALARM SEQUENCE - atomic beep-gap-beep:
//    Once started, a sequence always runs to completion regardless of any
//    condition change mid-sequence. alarmConditionMet() is only consulted
//    between sequences to decide whether to fire the next one. Prevents a
//    flickering or frozen input from restarting the timeline or latching
//    the tone on.
//
// 1a. ALARM THRESHOLD HYSTERESIS:
//    A value hovering right at THRESH_x_WARN can toggle the alarm condition
//    faster than a single beep-gap-beep sequence can complete, causing rapid
//    ON/OFF flapping. Two mechanisms, used together:
//      - Per-hazard hysteresis latch inside alarmConditionMet() - ON at
//        THRESH_x_WARN, OFF only once the value drops back below
//        THRESH_x_WARN * ALARM_HYSTERESIS_FACTOR (~90%). A stale sensor
//        drops its latch immediately regardless of the last value seen.
//      - Minimum ON dwell in loopBuzzer() - once the (post-hysteresis)
//        condition goes true, "alarming" is held for at least one full
//        BUZZER_CYCLE_MS from that trigger, even if the condition clears
//        before the cycle completes. Complements rather than duplicates
//        the atomic-sequence design above: that guarantees a STARTED
//        sequence completes; this guarantees a sequence gets the chance to
//        start and finish in the first place.
//    See Buzzer.h for full rationale.
//
// 2. SGP30 BASELINE RESET - 5s touch long-press:
//    Long-press fires resetSGP30Baseline(), then queues a 4-short-beep +
//    1-long-beep confirmation tone (RESET_TONE_*), atomic once started and
//    never interrupts an in-flight alarm sequence. A restart follows once
//    BOTH the confirmation tone has fully finished AND the triggering touch
//    has been released (rebootArmed / rebootReleaseSeen).
//
// 3. TOUCH HANDLING (readTouchEvent()):
//    Gated on displayDetected - with no display fitted there is no touch
//    controller on the bus, so readTouchEvent() returns false immediately
//    and generates no I2C traffic at all. Everything in sections 2 and 4
//    below is therefore unreachable on a headless unit: the alarm cannot be
//    snoozed and the SGP30 baseline cannot be reset. Accepted for v1.0.0,
//    documented in USER_GUIDE.md, and slated to be resolved shortly after
//    release by exposing both actions as Home Assistant buttons.
//    Direct Wire1 register reads of the FT5x06 (0x38) - the touch library
//    is only used for begin(), never for reads. Diagnostic logging
//    (debugSpecial()) tracks every raw point-count change, which
//    distinguishes a progressive multi-finger sequence from a spontaneous
//    jump and catches any touch reported with nobody touching the screen -
//    useful for monitoring touch-controller health in the field.
//
// 4. TOUCH-TO-WAKE / SNOOZE:
//    display OFF                -> wake only, do not snooze
//    display ON  + alarming     -> snooze for BUZZER_SNOOZE_MINUTES
//    display ON  + not alarming -> wake (e.g. backlight inactivity reset)
//
// 5. LEDC API - CORE 2.x vs 3.x (open-source compile compatibility):
//    setupBuzzer()/toneOn()/toneOff() are version-gated on
//    ESP_ARDUINO_VERSION_MAJOR. Core 3.x removed the legacy channel-number
//    LEDC calls (ledcSetup/ledcAttachPin, ledcWrite-by-channel) in favour
//    of a pin-based one (ledcAttach(pin, freq, res), ledcWrite(pin, duty)).
//    See Buzzer.h for the full rationale. BUZZER_LEDC_CHANNEL is only
//    referenced on the core < 3 path - core 3.x has no channel concept.

#include "Buzzer.h"
#include "Global.h"
#include "Graphics.h"
#include "Backlight.h"
#include "LD2410B_Presence.h"
#include "SCD40_CO2.h"
#include "SFA40_HCHO_Sensor.h"
#include "SGP30_VOC.h"
#include "PMS5003_Particles.h"

volatile bool buzzerSnoozed = false;

static unsigned long snoozeUntilMs = 0;
static unsigned long seqStartMs = 0;
static bool          seqActive = false;
static bool          prevAlarming = false;

// SGP30 reset confirmation tone - separate atomic sequence from
// the alarm's beep-gap-beep above. "Pending" lets a long-press that completes
// mid-alarm-sequence queue quietly rather than cutting in over it - the alarm
// sequence's own atomicity is never interrupted.
static bool          resetConfirmPending = false;
static bool          resetConfirmActive = false;
static unsigned long resetConfirmStartMs = 0;

// Reboot-after-reset state. rebootArmed latches when a long-press
// fires; rebootReleaseSeen only latches once the SAME triggering touch has
// been released. The actual restart (in loopBuzzer) requires both this AND
// the confirmation tone having fully finished
static bool          rebootArmed = false;
static bool          rebootReleaseSeen = false;

#if ESP_ARDUINO_VERSION_MAJOR >= 3
static void toneOn() { ledcWrite(BUZZER_PIN, BUZZER_DUTY); }
static void toneOff() { ledcWrite(BUZZER_PIN, 0); }
#else
static void toneOn() { ledcWrite(BUZZER_LEDC_CHANNEL, BUZZER_DUTY); }
static void toneOff() { ledcWrite(BUZZER_LEDC_CHANNEL, 0); }
#endif

static bool readTouchEvent() {
  // ---- Optional display gate ----
  // No display fitted means no FT5x06 on the bus at all (see "OPTIONAL
  // DISPLAY GATE" in Graphics.h). Bail out before generating any I2C
  // traffic: without this the reads below fail on every single loop() for
  // the life of the run, and the rate-limited failure log fires every 5
  // seconds forever. Both were observed on the 27/07/2026 headless boot.
  if (!displayDetected) {
    return false;
  }

  Wire1.beginTransmission(0x38);
  Wire1.write(0x02);
  if (Wire1.endTransmission(false) != 0) {
    // Distinguishes "touch controller genuinely stuck reporting a fixed
    // value" from "we stopped being able to talk to it at all" - these look
    // identical in the points-based log without this, since readTouchEvent()
    // bails out before points is ever read on this path.
    static unsigned long lastI2CFailLogMs = 0;
    unsigned long nowMs = millis();
    if (nowMs - lastI2CFailLogMs >= 5000) {
      debugLoop("I2C endTransmission failed");
      debugSpecial("I2C endTransmission failed");
      lastI2CFailLogMs = nowMs;
    }
    return false;
  }
  if (Wire1.requestFrom((uint8_t)0x38, (uint8_t)1) != 1) {
    static unsigned long lastReqFailLogMs = 0;
    unsigned long nowMs = millis();
    if (nowMs - lastReqFailLogMs >= 5000) {
      debugLoop("I2C requestFrom failed");
      debugSpecial("I2C requestFrom failed");
      lastReqFailLogMs = nowMs;
    }
    return false;
  }
  uint8_t points = Wire1.read() & 0x0F;   // low nibble = touch-point count

  // Full touch-event logging. Tracks every CHANGE in the raw reading, not
  // just the 0<->nonzero boundary. This distinguishes a progressive
  // multi-finger sequence (0->1->2->3, each value logged separately) from a
  // spontaneous direct jump (0->2 with no 1 ever seen) - two very different
  // underlying events that would otherwise look identical if only the zero
  // boundary were tracked. A "detected" line with no matching "latched for"
  // line ever following it means that value never changed again, i.e. the
  // touch controller is stuck. Also directly answers "is a touch ever
  // reported when nobody is touching the screen" - any unexpected "detected"
  // line while the screen isn't being touched is a false trigger.
  static uint8_t      lastRawPoints = 0;
  static unsigned long rawTouchStartMs = 0;
  if (points != lastRawPoints) {
    if (lastRawPoints != 0) {
      unsigned long heldMs = millis() - rawTouchStartMs;
      debugLoop("Raw points=%u latched for %lu ms", lastRawPoints, heldMs);
      debugSpecial("Raw points=%u latched for %lu ms", lastRawPoints, heldMs);
    }
    if (points != 0) {
      rawTouchStartMs = millis();
      debugLoop("Raw points=%u detected", points);
      debugSpecial("Raw points=%u detected", points);
    }
  }
  lastRawPoints = points;

  bool touched = (points >= 1 && points <= 5);

  // ---- SGP30 baseline reset - long-press detection ----
  // Independent of the tap debounce/event logic below - both key off the
  // same touch-down edge (touched && !lastTouched), so a long-press also
  // fires the normal tap action (wake/snooze) at the moment the touch
  // begins, in addition to the reset once the hold threshold is reached.
  static unsigned long touchHoldStartMs = 0;
  static bool          longPressFired = false;
  if (touched) {
    if (touchHoldStartMs == 0) {
      touchHoldStartMs = millis();
    }
    if (!longPressFired && (millis() - touchHoldStartMs >= SGP30_RESET_HOLD_MS)) {
      longPressFired = true;
      debugLoop("Long-press detected - triggering SGP30 baseline reset");
      debugSpecial("Long-press detected - triggering SGP30 baseline reset");
      resetSGP30Baseline();
      resetConfirmPending = true;
      rebootArmed = true;
      rebootReleaseSeen = false;   // this specific hold must end before reboot can fire
    }
  }
  else {
    // Release edge. Only meaningful for arming the reboot if THIS hold was
    // the one that fired the long-press - a plain short tap releasing
    // shouldn't touch reboot state at all.
    if (longPressFired) {
      rebootReleaseSeen = true;
      debugLoop("Long-press touch released - reboot will proceed once tone finishes");
      debugSpecial("Long-press touch released - reboot will proceed once tone finishes");
    }
    touchHoldStartMs = 0;
    longPressFired = false;
  }

  static bool          lastTouched = false;
  static unsigned long lastEventMs = 0;
  const  unsigned long DEBOUNCE_MS = 800;
  bool event = false;
  if (touched && !lastTouched) {
    unsigned long now = millis();
    if (now - lastEventMs >= DEBOUNCE_MS) {
      lastEventMs = now;
      event = true;
    }
  }
  lastTouched = touched;
  return event;
}

// ---- Alarm threshold hysteresis ----
// Shared per-hazard latch helper. ON at `threshold` (value strictly over);
// OFF only once value drops back below threshold * ALARM_HYSTERESIS_FACTOR.
// A stale sensor clears the latch immediately - hysteresis must never hold
// the alarm open on a value we no longer trust. See Buzzer.h for rationale.
static bool hysteresisLatch(bool& latched, bool fresh, float value, float threshold) {
  if (!fresh) {
    latched = false;
    return false;
  }
  if (!latched) {
    if (value > threshold) latched = true;
  }
  else {
    if (value < (threshold * ALARM_HYSTERESIS_FACTOR)) latched = false;
  }
  return latched;
}

static bool alarmConditionMet() {
  if (ld2410Detected && !ld2410Presence) return false;

  // Independent latch per hazard - each hazard flaps on its own reading,
  // so each needs its own hysteresis state rather than one shared latch.
  static bool co2Latched = false;
  static bool hchoLatched = false;
  static bool tvocLatched = false;
  static bool pm25Latched = false;
  static bool pm10Latched = false;

  bool co2Danger = hysteresisLatch(co2Latched,
    !sensorStale(lastSCD40OkMs, STALE_MED_MS), (float)co2, THRESH_CO2_WARN);
  bool hchoDanger = hysteresisLatch(hchoLatched,
    !sensorStale(lastSFA40OkMs, STALE_FAST_MS), (float)hcho_ppb, THRESH_HCHO_WARN);
  bool tvocDanger = hysteresisLatch(tvocLatched,
    !sensorStale(lastSGP30OkMs, STALE_FAST_MS), (float)getTVOC(), THRESH_TVOC_WARN);
  bool pm25Danger = hysteresisLatch(pm25Latched,
    !sensorStale(lastPMS5003OkMs, STALE_PM_MS), (float)pmsData.pm25_env, THRESH_PM25_WARN);
  bool pm10Danger = hysteresisLatch(pm10Latched,
    !sensorStale(lastPMS5003OkMs, STALE_PM_MS), (float)pmsData.pm100_env, THRESH_PM10_WARN);

  return co2Danger || hchoDanger || tvocDanger || pm25Danger || pm10Danger;
}

void setupBuzzer() {
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  // Core 3.x pin-based LEDC API - no explicit channel argument.
  bool attached = ledcAttach(BUZZER_PIN, BUZZER_FREQ_HZ, 8);
  if (!attached) {
    Serial.println("[Buzzer] ledcAttach FAILED on GPIO47 - buzzer will be silent");
  }
  ledcWrite(BUZZER_PIN, 0);
  Serial.printf("[Buzzer] Initialised on GPIO47 (core %d.x pin-based LEDC)\r\n", ESP_ARDUINO_VERSION_MAJOR);
#else
  // Core < 3.x legacy channel-number LEDC API.
  ledcSetup(BUZZER_LEDC_CHANNEL, BUZZER_FREQ_HZ, 8);
  ledcAttachPin(BUZZER_PIN, BUZZER_LEDC_CHANNEL);
  ledcWrite(BUZZER_LEDC_CHANNEL, 0);
  Serial.println("[Buzzer] Initialised on GPIO47 (LEDC ch7)");
#endif
}

void loopBuzzer() {
  unsigned long now = millis();

  if (buzzerSnoozed && now >= snoozeUntilMs) {
    buzzerSnoozed = false;
    setDirtyHeader();
    debugLoop("Snooze expired");
    debugSpecial("Snooze expired");
  }

  // Promote a queued confirmation tone to active as soon as no alarm
  // sequence is mid-flight - never interrupts an already-started alarm beep.
  if (resetConfirmPending && !seqActive && !resetConfirmActive) {
    resetConfirmPending = false;
    resetConfirmActive = true;
    resetConfirmStartMs = now;
  }

  if (resetConfirmActive) {
    // Atomic confirmation pattern - 4 short beeps then 1 long beep - once
    // started, always runs to completion regardless of anything else.
    unsigned long phase = now - resetConfirmStartMs;
    const unsigned long shortCycleLen = RESET_TONE_BEEP_MS + RESET_TONE_GAP_MS;
    const unsigned long shortPhaseLen = (unsigned long)RESET_TONE_SHORT_COUNT * shortCycleLen;
    const unsigned long totalLen = shortPhaseLen + RESET_TONE_LONG_MS;

    if (phase >= totalLen) {
      toneOff();
      resetConfirmActive = false;
    }
    else if (phase < shortPhaseLen) {
      unsigned long posInCycle = phase % shortCycleLen;
      if (posInCycle < RESET_TONE_BEEP_MS) toneOn();
      else toneOff();
    }
    else {
      toneOn();   // final long beep
    }
  }
  else if (seqActive) {
    unsigned long phase = now - seqStartMs;
    const unsigned long beep2Start = BUZZER_BEEP_MS + BUZZER_GAP_MS;
    const unsigned long beep2End = beep2Start + BUZZER_BEEP_MS;

    if (phase < BUZZER_BEEP_MS)       toneOn();
    else if (phase < beep2Start)      toneOff();
    else if (phase < beep2End)        toneOn();
    else { toneOff(); seqActive = false; }
  }
  else {
    toneOff();
  }

  // ---- Alarm threshold hysteresis, part 2: minimum ON dwell ----
  // alarmConditionMet() already applies per-hazard hysteresis (see above),
  // which alone suppresses almost all flapping. This adds a second, cheap
  // guarantee: once the (post-hysteresis) condition goes true, "alarming"
  // is held for at least one full BUZZER_CYCLE_MS from that trigger, even
  // if the condition clears again before the cycle completes. Complements
  // the atomic-sequence design rather than duplicating it - that guarantees
  // a STARTED sequence completes; this guarantees a sequence gets the
  // chance to start and finish in the first place.
  bool rawAlarming = alarmConditionMet();

  static bool          dwellActive = false;
  static unsigned long dwellStartMs = 0;
  if (rawAlarming) {
    if (!dwellActive) dwellStartMs = now;
    dwellActive = true;
  }
  else if (dwellActive && (now - dwellStartMs < BUZZER_CYCLE_MS)) {
    // Underlying condition has cleared but the minimum dwell hasn't
    // elapsed yet - keep alarming true until it has.
  }
  else {
    dwellActive = false;
  }

  bool alarming = dwellActive && !buzzerSnoozed;

  if (alarming && !prevAlarming) {
    debugLoop("Alarm condition ON");
    debugSpecial("Alarm condition ON");
  }
  if (!alarming && prevAlarming) {
    debugLoop("Alarm condition OFF");
    debugSpecial("Alarm condition OFF");
  }
  prevAlarming = alarming;

  if (!seqActive && !resetConfirmActive && alarming && (now - seqStartMs) >= BUZZER_CYCLE_MS) {
    seqActive = true;
    seqStartMs = now;
  }

  // ---- SGP30 reset reboot ----
  // Fires once: the confirmation tone has fully finished (not merely queued
  // - resetConfirmPending must also be clear, in case an alarm sequence is
  // still delaying it from starting) AND the triggering touch has been
  // released. See Buzzer.h for why the release-gate matters - it's what
  // stops a stuck touch controller from reboot-looping the device forever.
  if (rebootArmed && rebootReleaseSeen && !resetConfirmActive && !resetConfirmPending) {
    Serial.println("SGP30: Confirmation tone complete, touch released - restarting now to complete baseline reset");
    delay(50);   // let the serial print flush before restart
    ESP.restart();
  }

  if (readTouchEvent()) {
    if (!isBacklightOn()) {
      wakeBacklight();
      debugLoop("Touch wake (display was off) - not snoozing");
      debugSpecial("Touch wake (display was off) - not snoozing");
    }
    else if (seqActive || alarming) {
      toneOff();
      seqActive = false;
      buzzerSnoozed = true;
      snoozeUntilMs = now + BUZZER_SNOOZE_MS;
      setDirtyHeader();
      debugLoop("Snoozed for %lu min", BUZZER_SNOOZE_MINUTES);
      debugSpecial("Snoozed for %lu min", BUZZER_SNOOZE_MINUTES);
    }
    else {
      wakeBacklight();
    }
  }
}