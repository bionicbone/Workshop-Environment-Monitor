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

// LD2410B_Presence.cpp

// Workshop Environment Monitor - HLK-LD2410B mmWave Presence Sensor

#include "LD2410B_Presence.h"
#include "Graphics.h"   // setDirtyLD2410() triggers presence block redraw

// ============================================================
//  GLOBALS (exported for Graphics.cpp)
// ============================================================
bool     ld2410Detected = false;   // set true only if the sensor answers at boot
bool     ld2410Presence = false;
bool     ld2410StillPresence = false;
uint16_t ld2410MovingEnergy = 0;
uint16_t ld2410StillEnergy = 0;
uint16_t ld2410Distance = 0;

// ============================================================
//  LIBRARY INSTANCE
//  Uses Serial0 on custom pins via HardwareSerial
// ============================================================
static ld2410         radar;
static HardwareSerial radarSerial(0);   // UART0, pins reassigned below

// ============================================================
//  SETUP
//
//  A 3-second retry loop gives the sensor time to come up on UART (it needs
//  ~1-2s after power-on). On success ld2410Detected is latched true and we
//  return immediately. If the loop falls through the sensor is treated as
//  ABSENT for the rest of the run: ld2410Detected stays false.
//
//  When absent, two things change elsewhere (see loop below and Buzzer.cpp):
//    - loopLD2410B() skips all reads and forces ld2410Presence = false, so a
//      floating UART/GPIO cannot inject phantom presence.
//    - The buzzer's presence gate is bypassed (no presence sensor = alarm on
//      hazard alone), and the display is woken/kept awake by touch instead.
//
//  THE TOUCH FALLBACK IS ITSELF CONDITIONAL. It only exists when a display is
//  fitted - the FT5x06 lives on the display's own ribbon (see "OPTIONAL
//  DISPLAY GATE" in Graphics.h). The not-found message below is therefore
//  gated on displayDetected: a headless unit has no touch to fall back to,
//  and telling its user otherwise was exactly the kind of misleading boot
//  line the optional-display work set out to remove.
//
//  No-LD2410B AND no-display is the one combination worth calling out
//  explicitly at boot, because the two limitations compound: the presence
//  gate is bypassed so the alarm sounds on hazard alone, AND there is no
//  touch to snooze it with. Both are accepted v1.0.0 behaviour (see
//  USER_GUIDE.md), but the user should not have to infer the combination by
//  reading two separate blocks of the boot log.
//
//  ORDERING: this relies on setupLD2410B() running AFTER setupTFT(), which is
//  what latches displayDetected. That order is already fixed in setup() and
//  documented there.
// ============================================================
void setupLD2410B() {
  // INPUT_PULLDOWN, not INPUT: if the sensor dies or is unplugged this line
  // floats. A pulldown forces a dead sensor to read LOW (= no presence) so it
  // fails safe - the display sleeps and the buzzer's presence gate stays shut,
  // rather than flickering false presence.
  pinMode(LD2410_GPIO_PIN, INPUT_PULLDOWN);
  radarSerial.begin(LD2410_BAUD, SERIAL_8N1, LD2410_RX_PIN, LD2410_TX_PIN);

  uint32_t timeout = millis();
  while (millis() - timeout < SENSOR_BEGIN_RETRY_MS) {
    if (radar.begin(radarSerial)) {
      ld2410Detected = true;
      Serial.println("LD2410B: Sensor found and initialised");
      Serial.printf("LD2410B: Firmware %u.%u.%u\n",
        radar.firmware_major_version,
        radar.firmware_minor_version,
        radar.firmware_bugfix_version);
      // Force the presence block's first draw now, even though the presence
      // globals are still at their zero/false defaults - otherwise the block
      // stays blank until loopLD2410B() sees an actual change, which for a
      // quiet room can be a long wait. Same fix class as setDisplayDirty()
      // picking up dirtyParticles for an absent PMS5003.
      setDirtyLD2410();
      return;
    }
    delay(SENSOR_BEGIN_RETRY_DELAY_MS);
  }
  ld2410Detected = false;
  Serial.println("LD2410B: Sensor not found - check wiring on GPIO39/40");
  Serial.println("LD2410B: Running WITHOUT presence sensor.");
  if (displayDetected) {
    Serial.println("LD2410B:   - display wake/keep-awake falls back to touch");
  }
  Serial.println("LD2410B:   - alarm no longer gated on presence");
  if (!displayDetected) {
    Serial.println("LD2410B:   - no display fitted either, so the alarm sounds on");
    Serial.println("LD2410B:     hazard alone AND cannot be snoozed - see USER_GUIDE.md");
  }
  // Force the presence block's first (grey, error-state) draw. Without this,
  // an absent sensor's globals never change from their zero defaults, so
  // loopLD2410B() never calls setDirtyLD2410() and the block stays blank
  // forever instead of showing the grey "not detected" state.
  setDirtyLD2410();
}

// ============================================================
//  LOOP  - call every loop()
//
//  GPIO pin checked first for fast response (no UART parsing delay).
//  UART data read for signal strength and distance display.
//  Backlight timer reset on any presence event.
//
//  NOTE: signal strength methods in ncmreynolds/ld2410 v0.2.x are:
//    stationaryTargetDistance()      - distance in cm
//    movingTargetDistance()          - distance in cm
//    stationaryTargetSignalStrength()- NOT available in this version
//    movingTargetSignalStrength()    - NOT available in this version
//  Using distance() as a proxy for energy display - scales 0-600cm
//  mapped to 0-100 for the bar display.
// ============================================================
void loopLD2410B() {
  // ---- Absent sensor: force "no presence" and do nothing else ----
  // Skipping the read means a floating bus can never fake presence, and the
  // globals stay in a known state for Graphics.cpp (block renders empty/absent).
  if (!ld2410Detected) {
    if (ld2410Presence || ld2410StillPresence ||
      ld2410MovingEnergy || ld2410StillEnergy || ld2410Distance) {
      ld2410Presence = false;
      ld2410StillPresence = false;
      ld2410MovingEnergy = 0;
      ld2410StillEnergy = 0;
      ld2410Distance = 0;
      setDirtyLD2410();
    }
    return;
  }

  radar.read();   // process any incoming UART bytes

  // ---- GPIO presence (fast path) ----
  bool gpioPresence = (digitalRead(LD2410_GPIO_PIN) == HIGH);

  // ---- UART data (rich path) ----
  bool     newPresence = false;
  bool     newStill = false;
  uint16_t newMovingEnergy = 0;
  uint16_t newStillEnergy = 0;
  uint16_t newDistance = 0;

  if (radar.isConnected() && radar.presenceDetected()) {
    newPresence = true;
    if (radar.stationaryTargetDetected()) {
      newStill = true;
      // Map distance (cm) to 0-100 energy proxy: closer = higher bar
      // Max expected range ~600cm, clamp and invert so near = full bar
      uint16_t d = radar.stationaryTargetDistance();
      newStillEnergy = (uint16_t)constrain(100 - (d * 100 / 600), 0, 100);
      newDistance = d;
    }
    if (radar.movingTargetDetected()) {
      uint16_t d = radar.movingTargetDistance();
      newMovingEnergy = (uint16_t)constrain(100 - (d * 100 / 600), 0, 100);
      if (!newStill) newDistance = d;
    }
  }

  // GPIO as fallback if UART lags
  bool combinedPresence = newPresence || gpioPresence;

  // ---- Update globals and flag redraw if anything changed ----
  if (combinedPresence != ld2410Presence ||
    newStill != ld2410StillPresence ||
    newMovingEnergy != ld2410MovingEnergy ||
    newStillEnergy != ld2410StillEnergy ||
    newDistance != ld2410Distance) {

    ld2410Presence = combinedPresence;
    ld2410StillPresence = newStill;
    ld2410MovingEnergy = newMovingEnergy;
    ld2410StillEnergy = newStillEnergy;
    ld2410Distance = newDistance;

    // Own dirty flag - keeps presence updates isolated from vent block
    setDirtyLD2410();
  }

  // ---- Reset backlight timer on presence ----
  if (combinedPresence) {
    setPresenceDetected();
  }
}