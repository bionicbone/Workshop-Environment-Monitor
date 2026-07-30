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

// Buzzer.h

// Workshop Environment Monitor - Passive Piezo Alarm Manager
//
// ALARM CONDITION:
//   Any fresh sensor reading over its warn threshold AND the presence gate is
//   satisfied. The presence gate is conditional on whether an LD2410B is fitted:
//       (!ld2410Detected || ld2410Presence)
//     - LD2410B fitted     -> require confirmed presence (no alarm in an empty
//                             workshop).
//     - LD2410B not fitted -> no presence signal exists, so the gate is bypassed
//                             and the alarm sounds on hazard alone.
//
// ALARM THRESHOLD HYSTERESIS:
//   A value hovering right at THRESH_x_WARN can toggle the alarm condition
//   faster than a single beep-gap-beep sequence can complete, causing rapid
//   ON/OFF flapping. Two mechanisms fix this together, each addressing a
//   different half of the problem:
//     - HYSTERESIS BAND (per hazard, in alarmConditionMet()): each of the
//       five hazards (CO2, HCHO, TVOC, PM2.5, PM10) latches independently.
//       ON at THRESH_x_WARN, OFF only once the value drops back below
//       THRESH_x_WARN * ALARM_HYSTERESIS_FACTOR (~90%). A stale sensor
//       drops its latch immediately regardless of the last value seen -
//       hysteresis must never hold the alarm open on data we no longer
//       trust. This alone suppresses almost all flapping.
//     - MINIMUM ON DWELL (in loopBuzzer()): once the (post-hysteresis)
//       alarm condition goes true, the effective alarming state is held
//       for at least one full BUZZER_CYCLE_MS from that trigger, even if
//       the underlying condition clears before the cycle completes. This
//       complements rather than duplicates the atomic-sequence design -
//       that guarantees a STARTED sequence completes; this guarantees a
//       sequence gets the chance to start and finish in the first place.
//
// BEEP SEQUENCE (atomic):
//   The alarm sounds a beep-gap-beep sequence. Once a sequence STARTS it always
//   runs to completion and ends silent, regardless of any change in the alarm
//   condition. The condition is only re-checked between sequences to decide
//   whether to fire the next one. The whole audible part is short (~390ms) so
//   there is no value in interrupting it, and it keeps the logic simple - a
//   frozen or flickering input can never leave the tone latched on.
//   Cadence: one beep-gap-beep every BUZZER_CYCLE_MS (start-to-start).
//
// OPTIONAL DISPLAY:
//   The TFT is optional (see "OPTIONAL DISPLAY GATE" in Graphics.h). The
//   FT5x06 touch controller lives on the display's own ribbon, so no display
//   means no touch: readTouchEvent() returns false immediately and generates
//   no I2C traffic. Consequences on a headless unit, both accepted for
//   v1.0.0 and documented in USER_GUIDE.md:
//     - The alarm CANNOT BE SNOOZED. It will sound its beep-gap-beep every
//       BUZZER_CYCLE_MS for as long as the hazard condition holds. Note this
//       usually compounds: a headless build often has no LD2410B either, in
//       which case the presence gate is bypassed too and the alarm sounds on
//       hazard alone.
//     - The SGP30 baseline CANNOT BE RESET. A user swapping the physical
//       SGP30 has no route to clear the stored NVS baseline short of a full
//       flash erase - the same blocker the long-press mechanism was built to
//       solve for a display-equipped unit.
//   Both are slated for resolution shortly after release by exposing snooze
//   and baseline-reset as Home Assistant buttons, which needs no touch.
//
// TOUCH (wake vs snooze):
//   Touch is read every loop so it can wake a timed-out display even when no
//   LD2410B is fitted (otherwise a slept display could never come back). What a
//   touch does depends on the display state at the moment of the touch:
//     - display OFF -> wake only (do NOT snooze). The user is likely walking up
//                      to see what triggered the alarm. Touch again to snooze.
//     - display ON  -> snooze the alarm for BUZZER_SNOOZE_MINUTES (if active).
//   Snoozing a live sequence cuts it short immediately (deliberate user action).
//   Touch detection uses direct Wire1 FT5x06 register reads - the Touch_FT5x06
//   library conflicts with Wire1 so is not used for reads.
//
// SGP30 BASELINE RESET (long-press):
//   Holding a touch continuously for SGP30_RESET_HOLD_MS calls
//   resetSGP30Baseline() (SGP30_VOC.cpp), which clears the stored NVS
//   baseline. This is independent of the normal tap wake/snooze action above
//   - both are driven off the same touch-down edge, so a long-press also
//   wakes the display (or snoozes an active alarm) exactly as a short tap
//   would, in addition to triggering the reset once the hold threshold is
//   reached. A confirmation tone plays once the long-press fires - 4 short
//   beeps then 1 long beep, deliberately distinct from the alarm's 2-beep
//   pattern so the two can never be confused by ear. Like the alarm
//   sequence, the confirmation tone is atomic once started - and if an
//   alarm sequence is already mid-beep when the long-press completes, the
//   confirmation is queued and plays as soon as that sequence finishes
//   rather than cutting in over it.
//
//   Clearing NVS alone does not touch the sensor's live on-chip IAQ state,
//   so the device reboots itself automatically once the confirmation tone
//   finishes - see SGP30_VOC.cpp for why the clear needs a reboot at all.
//   The reboot is deliberately gated on having SEEN THE TRIGGERING TOUCH
//   RELEASE, not just on the tone finishing.
//
// VISUAL INDICATOR:
//   A small "ZZZ" label appears in the header while snoozed,
//   drawn/erased via updateHeader() dirty flag mechanism.
//
// TONE:
//   PWM-driven passive piezo on GPIO47.
//   Frequency and duty cycle defined below - tune to taste.
//
// LEDC API - CORE 2.x vs 3.x (open-source compile compatibility):
//   ESP32 Arduino core 3.x removed the legacy channel-number LEDC API
//   (ledcSetup/ledcAttachPin, and ledcWrite-by-channel) in favour of a
//   pin-based one (ledcAttach(pin, freq, resolution), ledcWrite(pin, duty) -
//   no channel argument at all). setupBuzzer()/toneOn()/toneOff() in
//   Buzzer.cpp are version-gated on ESP_ARDUINO_VERSION_MAJOR so the same
//   source compiles unchanged on both:
//     - core < 3 (older toolchains pinned for compatibility with certain
//       IDE/build tooling) -> legacy channel API, BUZZER_LEDC_CHANNEL below.
//     - core >= 3 (current Arduino IDE default, what a fresh clone gets)
//       -> pin-based API, no channel needed.
//   This was chosen over a full migration to the new API so older
//   toolchains keep working unchanged.
//   Ordering constraint unchanged either way: setupBuzzer() must still be
//   called AFTER setupTFT(), since TFT_eSPI initialises LEDC internally -
//   this may depend on which core TFT_eSPI itself is compiled against, so
//   re-verify if switching cores.
//   WORTH KNOWING: with no display fitted, setupTFT() returns before
//   tft.begin() is ever called, so that LEDC side-effect does not happen at
//   all. If a headless build is ever found with a silent buzzer, this is the
//   first thing to check - the fix would be to hoist tft.begin() above the
//   detection gate in setupTFT(), not to reorder setup().

#ifndef _BUZZER_h
#define _BUZZER_h

#if defined(ARDUINO) && ARDUINO >= 100
#include "arduino.h"
#else
#include "WProgram.h"
#endif

// ============================================================
//  PIN
// ============================================================
#define BUZZER_PIN          47

// ============================================================
//  LEDC CHANNEL  (core < 3.x only - see "LEDC API" note above)
// ============================================================
#define BUZZER_LEDC_CHANNEL 7

// ============================================================
//  TONE PARAMETERS  (tune to taste)
// ============================================================
#define BUZZER_FREQ_HZ      1200                                      // Pitch of the beep
#define BUZZER_DUTY         128                                       // PWM duty cycle 0-255 (128 = 50%)
#define BUZZER_BEEP_MS      120                                       // Duration of each beep
#define BUZZER_GAP_MS       150                                       // Gap between the two beeps
#define BUZZER_CYCLE_MS     3000                                      // Sequence repeat period (start-to-start).
                                                                      // Also the minimum ON dwell - see
                                                                      // "ALARM THRESHOLD HYSTERESIS" above.

// ============================================================
//  ALARM THRESHOLD HYSTERESIS
//  See "ALARM THRESHOLD HYSTERESIS" note above for full rationale.
// ============================================================
#define ALARM_HYSTERESIS_FACTOR   0.90f                               // OFF threshold = ON threshold * this

// ============================================================
//  SNOOZE
// ============================================================
#define BUZZER_SNOOZE_MINUTES   15UL
#define BUZZER_SNOOZE_MS        (BUZZER_SNOOZE_MINUTES * 60UL * 1000UL)

// ============================================================
//  SGP30 BASELINE RESET - long-press + confirmation tone
//  Pattern: 4 short beeps, then 1 long beep. Deliberately distinct from the
//  alarm's 2-beep pattern, and the long final beep makes it unmistakable
//  even if it happens to play while an alarm condition is also present.
// ============================================================
#define SGP30_RESET_HOLD_MS      5000UL                               // ms of continuous touch to trigger reset
#define RESET_TONE_BEEP_MS       80                                   // duration of each SHORT confirmation beep
#define RESET_TONE_GAP_MS        80                                   // gap between beeps in the pattern
#define RESET_TONE_SHORT_COUNT   4                                    // number of short beeps before the long beep
#define RESET_TONE_LONG_MS       500                                  // duration of the final long beep

// ============================================================
//  PUBLIC STATE  (read by Graphics.cpp for snooze indicator)
// ============================================================
extern volatile bool buzzerSnoozed;                                   // true while snooze is active

// ============================================================
//  API
// ============================================================
void setupBuzzer();
void loopBuzzer();                                                    // call every loop()

#endif