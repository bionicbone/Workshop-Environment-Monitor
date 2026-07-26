SPDX-License-Identifier: CC-BY-SA-4.0

# Workshop Environment Monitor — User Guide

This guide covers building confidence in a fresh WEM: first boot, day-to-day
touchscreen controls, what the hardware looks like under the hood, and what
to do when something looks wrong. For licensing, third-party attribution, and
repository structure, see the root [`README.md`](README.md).

This document describes WEM as built at **v1.0.0**. It does not cover the
project's development history — see the private Schematic Reference if
you're curious how a design decision was reached.

---

## 1. First Boot

### 1.1 Before you power on

- Confirm every sensor you intend to fit is connected — WEM auto-detects
  what's present at boot (see [Optional Sensors](#5-optional-sensors)), so a
  loose connector just means that sensor sits out, not a crash.
- Copy `secrets.h.example` to `secrets.h` and fill in your WiFi and MQTT
  details before flashing. `secrets.h` is git-ignored — never commit it.
- If you're powering from the 19V mains PSU, double-check polarity at the
  barrel connector before first power-up.

### 1.2 What happens on first boot

1. The display initialises and shows the WEM header/gauges.
2. WEM attempts to connect to WiFi (a few retries, then continues in a
   degraded/offline state rather than hanging — it'll pick the connection up
   automatically later if it appears).
3. If WiFi connects, MQTT and Home Assistant discovery follow, along with
   NTP time sync and OTA readiness.
4. Each fitted sensor is probed once. A sensor that doesn't answer is marked
   absent for the rest of the session — its gauge/row greys out permanently
   rather than freezing on stale data. No firmware changes are needed to run
   with a subset of sensors.
5. **The SGP30 (TVOC) sensor starts a warm-up period** before it publishes
   real numbers — 12 hours on a genuinely fresh start, or 1 hour if a
   previously-saved baseline was restored from flash. This is normal and
   expected of the sensor itself, not a fault.

### 1.3 First boot in a new or freshly-printed enclosure

If your WEM enclosure was just 3D printed, or you've just finished soldering
nearby, don't let the SGP30 form its first calibration baseline while
sitting in that off-gassing air — it will bake the fumes into its reference
point and under-report real contamination afterwards. This is the single
most important thing to get right on a first build, and it only takes a few
minutes of thought before you power on.

**Step 1 — ask yourself: was this build near fresh off-gassing sources?**
This includes a freshly-printed enclosure (ABS/PETG/PLA all off-gas for a
while after printing, ABS particularly), solder fumes from assembling the
PCB, or strong VOCs nearby (paint, solvent, adhesives, new foam packaging)
in the hours before first power-up.

- **No, none of that applies** (an already-aired-out enclosure, board
  assembled a while ago, normal workshop air) → power on and let the
  standard 12h warm-up gate run as-is. Nothing further needed — skip to
  [section 2](#2-touchscreen-controls).
- **Yes, or you're not sure** → follow the procedure below.

**Step 2 — the recommended procedure:**

1. **Power on and let the enclosure/board air out somewhere well
   ventilated first** — outdoors or by an open window, for a few hours if
   possible. The display and other sensors work normally during this time;
   only the SGP30's calibration is at risk.
2. **Then start the 12h warm-up "for real"** once the air around the unit
   is back to normal. You don't need to do anything to "start" it — just
   make sure the SGP30 forms its baseline breathing normal air, not fresh
   off-gassing.
3. **If you skipped step 1** — you powered on directly in a contaminated
   environment, or you're unsure whether the warm-up window already
   overlapped with off-gassing — **clear the SGP30 baseline** (see
   [section 3](#3-clearing-the-sgp30-baseline-nvs-reset)) once the air has
   settled. This discards whatever baseline was forming and restarts a
   clean 12h window. It's quick (a 5-second touch long-press) and doesn't
   touch your WiFi/MQTT configuration.

**This only affects the SGP30 (TVOC/eCO2).** The other sensors don't hold a
calibration baseline and need no special first-boot handling — CO2, HCHO,
temperature, humidity, and particulates all read normally from the first
boot regardless of the environment.

When genuinely unsure which path applies, **step 3 (clear the baseline) is
always the safe default** — there's no harm in clearing a baseline that
didn't need clearing, but there's no way to un-bake a contaminated one
short of clearing it.

---

## 2. Touchscreen Controls

| Action | Effect |
|---|---|
| Short tap | Wakes the display from backlight sleep, or snoozes an active alarm |
| Hold for 5 seconds | Clears the stored SGP30 baseline and reboots (see below) |

The long-press is confirmed by a distinct tone (four short beeps + one long
beep) — different from the two-beep alarm pattern, so you can tell them
apart without looking at the screen. The unit then reboots automatically
once the tone finishes **and** your finger has lifted off the screen.

---

## 3. Clearing the SGP30 Baseline (NVS Reset)

### Why you'd want to do this

The SGP30 saves its calibration baseline to flash (NVS) roughly once an
hour, and restores it on every boot so it doesn't have to fully recalibrate
after a normal power cycle. That saved baseline is tied to the **physical
chip** it came from. You should clear it whenever:

- **You've replaced the physical SGP30** — restoring an old chip's baseline
  onto a new one produces plausible-looking but wrong readings (commonly
  seen as TVOC/eCO2 reading flat or stuck). Clear it *before* trusting the
  new sensor, not after you've noticed bad data.
- **First boot happened in a contaminated environment** (see 1.3 above).
- Readings look implausibly flat or pinned and you suspect a bad baseline.

### How to clear it

**Recommended — long-press the touchscreen for 5 seconds.** Hold until you
hear the confirmation tone (four short beeps, one long beep), then release.
The unit reboots automatically and starts a fresh calibration window. This
is quick, doesn't touch your WiFi/MQTT settings, and is the only method most
users will ever need.

**Fallback — full flash erase.** Only necessary if the long-press mechanism
itself is unavailable (e.g. touchscreen not fitted, or a display fault).
This wipes *everything* in flash, including your WiFi and MQTT credentials
— you'll need to reconfigure `secrets.h` and reflash afterwards.

---

## 4. Hardware Reference

### 4.1 Sensor summary

| Sensor | Measures | Interface | Voltage | Read cadence | HA publish rate |
|---|---|---|---|---|---|
| SGP30 | TVOC / eCO2 | I2C (0x58) | 3.3V | Every 1s (sensor requirement) | ~every 30s |
| SFA40 | HCHO (formaldehyde) | UART | 5V | Every loop (UART-driven) | ~every 30s |
| SCD40 | CO2 | I2C (0x62) | 5V | Every 5s | ~every 30s |
| SHT40 | Temperature / humidity (primary, drives the display) | I2C (0x44) | 5V | Every 5s | ~every 30s |
| BME280 | Temperature / humidity (secondary, HA-only) | I2C (0x76) | 3.3V | Every 5s | ~every 30s |
| PMS5003 | Particulates (PM1.0/2.5/10) | UART | 5V | 120s duty cycle (25% on-time) | ~every 120s |
| LD2410B | mmWave presence (optional) | UART | 5V (3.3V logic) | Continuous | — (local use only) |
| FT5x06 | Capacitive touch | I2C (0x38) | 3.3V | Continuous | — |

Sensor reads happen at the cadence each sensor's own hardware needs; Home
Assistant publishing is decoupled from that and rate-limited separately, so
HA doesn't get flooded with near-duplicate values.

A sensor that isn't fitted is simply skipped — no reads attempted, no
gauge/entity populated, no crash. See [Optional Sensors](#5-optional-sensors).

### 4.2 I2C bus (Wire1 — SDA on GPIO1, SCL on GPIO4)

| Address | Device | Sensor |
|---|---|---|
| 0x38 | FT5x06 | Capacitive touch |
| 0x44 | SHT40 | Temperature / humidity |
| 0x58 | SGP30 | TVOC / eCO2 |
| 0x62 | SCD40 | CO2 / temp / humidity |
| 0x76 | BME280 | Temp / humidity / pressure |

### 4.3 UART connections

| Interface | Device | Baud |
|---|---|---|
| Serial0 | LD2410B (mmWave presence) | 256000 |
| Serial1 | SFA40 (HCHO) | 9600 |
| Serial2 | PMS5003 (particulates) | 9600 |

### 4.4 GPIO pin map (ESP32-S3)

| GPIO | Function | Notes |
|---|---|---|
| 1 | I2C1 SDA | Shared sensor bus (Wire1) |
| 2 | Touch interrupt | FT5x06 |
| 3 | TFT write strobe | Parallel display interface |
| 4 | I2C1 SCL | Shared sensor bus (Wire1) |
| 5–8, 15–18 | TFT data bus (D0–D7) | 8-bit parallel display interface |
| 10 | Backlight control | HIGH = on, LOW = off |
| 21 | PMS5003 RX (Serial2) | Particulate sensor |
| 35 | PMS5003 TX (Serial2) | Particulate sensor |
| 36 | Oscilloscope test point | Debug only — not required for normal use |
| 38 | SFA40 TX (Serial1) | HCHO sensor |
| 39 | LD2410B RX (Serial0) | Presence sensor |
| 40 | LD2410B TX (Serial0) | Presence sensor |
| 41 | LD2410B presence output | Direct digital read |
| 42 | SFA40 RX (Serial1) | HCHO sensor |
| 47 | Buzzer PWM | Drives alert piezo via transistor |
| 48 | TFT D/C | Data/command select |

GPIO 19, 20 (USB), 33–37 (PSRAM traces), 43, 44 (USB serial) are reserved by
the board itself — don't repurpose them if you're modifying the PCB.

### 4.5 Power

| Rail | Source |
|---|---|
| 19V | External mains PSU (barrel connector) |
| 5V | On-board switching regulator, from 19V |
| 3.3V | ESP32-S3 dev board's own regulator, from 5V |

WEM can also run from USB power alone (5V) for bench testing, but the 19V
input is recommended for permanent installs.

---

## 5. Optional Sensors

Every sensor in the table above is optional. WEM probes each one once at
boot; anything not fitted (or not responding) is marked absent for that
session and cleanly excluded from:

- the display (its gauge/row greys out rather than showing stale data),
- Home Assistant (no entity ever gets pushed for a sensor that never
  answered),
- alarm logic (an absent sensor can never trigger a false alarm).

This means you can build a cut-down WEM with only the sensors that matter to
you — no firmware changes required either way.

---

## 6. Alarms and Thresholds

WEM raises an audible alarm (piezo buzzer) and a visual warning on the
display when a reading crosses its threshold. Thresholds are chosen to flag
"you should probably act on this now", not raw sensor limits:

| Reading | Warning threshold |
|---|---|
| TVOC | 1000 ppb |
| HCHO | 150 ppb |

A sensor that's gone stale (hasn't reported recently) is excluded from alarm
logic entirely — WEM won't sound an alarm based on old data, and won't stay
silent forever on a genuinely dead sensor either, since the display shows
that reading as greyed-out/stale so you can see something's wrong at a
glance.

---

## 7. Troubleshooting

**A sensor's gauge is permanently grey.** That sensor wasn't detected at
boot — check its wiring/power, then power-cycle the unit (a soft
reset/reboot alone won't re-probe it).

**TVOC/eCO2 reads flat or stuck at an implausible value.** Almost always a
mismatched SGP30 baseline — see [Clearing the SGP30 Baseline](#3-clearing-the-sgp30-baseline-nvs-reset).

**Touchscreen stops responding entirely.** Rare, and under active
monitoring. If it happens, a full power cycle (disconnect and reconnect
power) clears it — a reset button/pulse alone will not, since the touch
controller has its own separate reset circuit. If you see this, it'd help
the project to report it (see the repository's issue tracker).

**Display readings look plausible but Home Assistant shows nothing for a
sensor.** Check that WiFi/MQTT actually connected (the header status icons
reflect this). A sensor that's detected locally but never reaches HA is
almost always a network/broker issue, not a sensor fault.

---

## 8. Licensing

WEM firmware is licensed under AGPL-3.0-or-later; hardware (PCB and
enclosure) under CERN-OHL-W-2.0. This document is licensed under
[CC BY-SA 4.0](https://creativecommons.org/licenses/by-sa/4.0/). See the
root [`README.md`](README.md) and [`THIRD_PARTY_LICENSES.md`](THIRD_PARTY_LICENSES.md)
for full detail.
