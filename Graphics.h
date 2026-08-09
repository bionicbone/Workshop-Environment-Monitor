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

// Graphics.h
// 
// Workshop Environment Monitor - Display Layout & Constants
// 800x480 landscape, dark theme, ESP32-S3 (no PSRAM)
//
// LAYOUT:
//    Left panel:  CO2/HCHO/TVOC/PM2.5/PM10 status (top) + PM bars (bottom)
//    Right panel: Outside Ref temp/humidity (top) + Vent block (bottom)
//
// OUTSIDE REFERENCE BLOCK:
//    Right-panel top section showing outdoor temp and humidity from the
//    rtl_433 Nexus-TH sensor via MQTT wildcard subscription. Stale data
//    detection: values grey out after OUTSIDE_STALE_MS.
//
// PM BARS:
//    In the left panel. PBAR_X/PBAR_W reference STATUS panel geometry.
//    PBAR_CNT_Y starts below the 5 status indicator rows.

#ifndef _GRAPHICS_h
#define _GRAPHICS_h

#if defined(ARDUINO) && ARDUINO >= 100
#include "arduino.h"
#else
#include "WProgram.h"
#endif

#include "Global.h"
#include "LD2410B_Presence.h" 
#include "HA_OTA.h"
#include "SCD40_CO2.h"
#include "SFA40_HCHO_Sensor.h"
#include "SGP30_VOC.h"
#include "PMS5003_Particles.h"
#include "BME280_TEMP_HUMIDITY.h"
#include "Buzzer.h"

// ============================================================
//  GFX FONTS
//  NOTE: Font headers are NOT explicitly included here.
//  They are pulled in transitively via TFT_eSPI.h / Global.h.
//  Explicit includes cause duplicate-definition compile errors.
// ============================================================
#define FONT_SMALL    &FreeSans9pt7b
#define FONT_BOLD_SM  &FreeSansBold9pt7b
#define FONT_BOLD_MD  &FreeSansBold12pt7b
#define FONT_BOLD_LG  &FreeSansBold18pt7b
#define FONT_BOLD_XL  &FreeSansBold24pt7b

// ============================================================
//  COLOURS  (RGB565)
// ============================================================
#define COL_BG          0x0861    // very dark blue-grey
#define COL_PANEL       0x10A3
#define COL_BORDER      0x2945
#define COL_TEXT        0xFFFF
#define COL_LABEL       0x8C71    // mid grey
#define COL_STALE       0x4228    // dark grey - used for stale outdoor data
#define COL_GOOD        0x07E0    // green
#define COL_WARN        0xFD20    // orange
#define COL_DANGER      0xF800    // red
#define COL_ARC_BG      0x2124    // arc background track
#define COL_HEADER_BG   0x0C41    // dark teal header
#define COL_VENT_OPEN   0x051F    // blue
#define COL_VENT_CLOSED 0x4A69    // muted purple
#define COL_BAR_BG      0x28A5
#define COL_DIVIDER     0x2124

// ============================================================
//  LAYOUT  (800 x 480)
// ============================================================
#define DISP_W   800
#define DISP_H   480
#define HDR_H    28
#define HDR_Y    0
#define ZONE_Y   (HDR_H + 1)
#define ZONE_H   (DISP_H - ZONE_Y)

// Left: status panel + PM bars
#define STATUS_X      0
#define STATUS_W      165
#define STATUS_RIGHT  (STATUS_X + STATUS_W)

// Centre: arc gauges
#define GAUGE_X      (STATUS_RIGHT + 1)
#define GAUGE_W      445
#define GAUGE_RIGHT  (GAUGE_X + GAUGE_W)

// Right panel: outside ref + vent block + presence
// (named RIGHT_* - particles are drawn in the LEFT panel, see PBAR_* above)
#define RIGHT_X   (GAUGE_RIGHT + 1)
#define RIGHT_W   (DISP_W - RIGHT_X)

// ============================================================
//  ARC GAUGES - two-row layout
// ============================================================
#define GAUGE_LG_R    78
#define GAUGE_XS_R    42
#define GAUGE_ARC_W   11

#define GAUGE_SPR_PAD   4
#define GAUGE_LG_SIDE   ((GAUGE_LG_R + GAUGE_ARC_W + GAUGE_SPR_PAD) * 2)  // 186
#define GAUGE_XS_SIDE   ((GAUGE_XS_R + GAUGE_ARC_W + GAUGE_SPR_PAD) * 2)  // 114

#define GAUGE_LG_OUT    (GAUGE_LG_R + GAUGE_ARC_W)   // 89
#define GAUGE_XS_OUT    (GAUGE_XS_R + GAUGE_ARC_W)   // 53

#define GAUGE_R1_CY   (ZONE_Y + 20 + 18 + GAUGE_LG_OUT)
#define GAUGE_R2_CY   (ZONE_Y + ZONE_H - 8 - GAUGE_LG_OUT)

#define GAUGE_TVOC_CX  (GAUGE_X + GAUGE_W / 4)
#define GAUGE_HCHO_CX  (GAUGE_X + 3 * GAUGE_W / 4)
#define GAUGE_TVOC_CY  GAUGE_R1_CY
#define GAUGE_HCHO_CY  GAUGE_R1_CY

#define GAUGE_CO2_CX   (GAUGE_X + GAUGE_W / 2)
#define GAUGE_TEMP_CX  (GAUGE_X + 64)
#define GAUGE_HUM_CX   (GAUGE_RIGHT - 64)
#define GAUGE_CO2_CY   GAUGE_R2_CY
#define GAUGE_TEMP_CY  GAUGE_R2_CY
#define GAUGE_HUM_CY   GAUGE_R2_CY

#define ARC_START_DEG  225
#define ARC_SWEEP_DEG  270

// ============================================================
//  STATUS PANEL  (left panel top - 5 sensor rows)
// ============================================================
#define STATUS_ITEM_H    28
#define STATUS_FIRST_Y   (ZONE_Y + 8)
#define STATUS_IND_R     6
#define STATUS_IND_X     (STATUS_X + 12)
#define STATUS_LABEL_X   (STATUS_X + 24)
#define STATUS_VAL_X     (STATUS_RIGHT - 18)
#define STATUS_ARROW_X   (STATUS_RIGHT - 7)
#define STATUS_ITEMS     5

// Divider between status rows and PM bars (left panel)
#define STATUS_PM_DIVIDER_Y  (STATUS_FIRST_Y + STATUS_ITEMS * STATUS_ITEM_H + 5)

// ============================================================
//  PARTICLE BARS  (now in left panel, below status rows)
// ============================================================
#define PBAR_X          (STATUS_X + 5)
#define PBAR_W          (STATUS_W - 10)
#define PBAR_LABEL_W    22
#define PBAR_VAL_W      36
#define PBAR_BAR_X      (PBAR_X + PBAR_LABEL_W + 2)
#define PBAR_BAR_W      (PBAR_W - PBAR_LABEL_W - PBAR_VAL_W - 4)
#define PBAR_H          14
#define PBAR_ROW_H      20
#define PBAR_CNT_Y      (STATUS_PM_DIVIDER_Y + 14)   // header sits here
#define PBAR_MASS_Y     (PBAR_CNT_Y + 6 * PBAR_ROW_H + 18)

#define PBAR_CNT_MAX_03    8000.0f
#define PBAR_CNT_MAX_05    4000.0f
#define PBAR_CNT_MAX_10    2000.0f
#define PBAR_CNT_MAX_25     500.0f
#define PBAR_CNT_MAX_50     200.0f
#define PBAR_CNT_MAX_100  50000.0f

// ============================================================
//  OUTSIDE REFERENCE BLOCK  (right panel top)
// ============================================================
#define OUTSIDE_X          (RIGHT_X + 5)
#define OUTSIDE_W          (RIGHT_W - 10)
#define OUTSIDE_BLOCK_Y    (ZONE_Y + 8)
#define OUTSIDE_ROW_H      28
#define OUTSIDE_STALE_MS   600000UL   // 10 minutes - grey out if no update

// ============================================================
//  VENT BLOCK  (right panel bottom - moved from left panel)
// ============================================================
// Sits below the outside ref block + a divider line
// Outside block: header (18px) + 2 rows (2 * OUTSIDE_ROW_H) + gap (10px)
#define VENT_BLOCK_Y    (OUTSIDE_BLOCK_Y + 18 + 2 * OUTSIDE_ROW_H + 18)
#define VENT_ROW_H      24

// ============================================================
//  SAFETY THRESHOLDS
//
//  STANCE (v1.0.0 release default): conservative, alarm early.
//  Alarm points are anchored to RESIDENTIAL / general-population
//  air-quality guidance (WHO, US EPA), NOT to occupational limits.
//  Occupational limits (OSHA/NIOSH) assume a healthy adult on an 8h
//  shift; residential guidance assumes everyone - children, the
//  elderly, the unwell - breathing the air continuously, and is far
//  stricter. WEM is a home device, so it uses the home numbers. A
//  user whose workshop runs hotter can raise these to suit their own
//  use case - that is a conscious, owned decision, and it can push a
//  reading into the 'poor' band before the buzzer sounds.
//
//  Two thresholds per reading:
//    GOOD - green ceiling (display colour only)
//    WARN - orange/red boundary AND the buzzer trigger
//
//  Only five drive the alarm: CO2, HCHO, TVOC, PM2.5, PM10. Temp and
//  humidity are comfort bands - display colour only, never the buzzer.
//
//  Basis for each alarm point (WARN):
//    CO2  2000 ppm   - ventilation/comfort proxy, not a toxin at these
//                      levels. 1000 = adequate-ventilation marker;
//                      effects clear by 2000; occupational limit is
//                      5000 (8h TWA). Deliberately not pulled to 1500 -
//                      a closed one-person workshop would nuisance-alarm.
//    HCHO   80 ppb   - WHO residential 30-min guideline (0.1 mg/m3 ~=
//                      80 ppb). Occupational PEL is 750 ppb, ~9x higher.
//                      Alarming AT the home guideline is the honest
//                      'early' point.
//    TVOC  660 ppb   - no health-based limit exists for TVOC; it is a
//                      relative indicator only. 660 = common IAQ
//                      moderate->poor boundary; alarm on entry to 'poor'.
//    PM2.5  35 ug/m3 - onset of 'unhealthy for sensitive groups' (EPA).
//                      GOOD 9 = EPA 2024 good/moderate line (was 12);
//                      WHO 24h guideline is 15.
//    PM10  154 ug/m3 - top of EPA 'moderate' band. GOOD 45 = WHO 2021
//                      24h PM10 guideline (was EPA's 54).
// ============================================================
#define THRESH_CO2_GOOD       1000.0f         // Original 1000
#define THRESH_CO2_WARN       2000.0f         // Original 2000
#define THRESH_HCHO_GOOD        25.0f         // Original 25
#define THRESH_HCHO_WARN        80.0f         // Original 80
#define THRESH_TVOC_GOOD       220.0f         // Original 220
#define THRESH_TVOC_WARN       660.0f         // Original 660
#define THRESH_PM25_GOOD         9.0f         // Original 9
#define THRESH_PM25_WARN        35.0f         // Original 35
#define THRESH_PM10_GOOD        45.0f         // Original 45
#define THRESH_PM10_WARN       154.0f         // Original 154
#define THRESH_TEMP_LOW_GOOD    15.0f         // Original 15
#define THRESH_TEMP_HIGH_GOOD   25.0f         // Original 25
#define THRESH_TEMP_LOW_WARN    10.0f         // Original 10
#define THRESH_TEMP_HIGH_WARN   30.0f         // Original 30
#define THRESH_HUM_LOW_GOOD     30.0f         // Original 30
#define THRESH_HUM_HIGH_GOOD    60.0f         // Original 60
#define THRESH_HUM_LOW_WARN     20.0f         // Original 20
#define THRESH_HUM_HIGH_WARN    70.0f         // Original 70

// ============================================================
//  GAUGE FULL-SCALE
// ============================================================
#define GAUGE_CO2_MAX     THRESH_CO2_WARN
#define GAUGE_TVOC_MAX    THRESH_TVOC_WARN
#define GAUGE_HCHO_MAX    THRESH_HCHO_WARN
#define GAUGE_TEMP_MIN      0.0f
#define GAUGE_TEMP_MAX     30.0f
#define GAUGE_HUM_MAX      70.0f

// ============================================================
//  BOOT SPLASH SCREEN
//
//  Shown by setupTFT() in place of drawStaticChrome() for the remainder of
//  setup() - covers the WiFi/sensor connect window, which otherwise reads
//  as a frozen dashboard (empty/grey gauges sitting static for anywhere
//  from ~9s to ~32s). Also the natural home for the licence/source notice
//  (AGPL keep-notices-intact intent) - a user who only ever sees the
//  device, never the source, still sees where it came from.
//
//  LIFECYCLE (see setup() in the .ino for the actual call sites):
//    1. setupTFT() calls drawSplashScreen() instead of drawStaticChrome().
//    2. Each subsequent setup() step - WiFi, then each sensor - calls
//       splashLine() once, appending one more row. Append-only: nothing
//       above the new line is touched or redrawn.
//    3. At the very end of setup(), a short SPLASH_HOLD_MS pause lets the
//       last line be read, then drawStaticChrome() is called once - it
//       already does its own fillScreen(), so the splash is cleared and
//       the normal dashboard takes over in a single step. No separate
//       teardown function is needed.
//
//  Gated on displayDetected same as everything else here - splashLine()
//  itself is a no-op on a headless build, so call sites in the .ino don't
//  need their own displayDetected checks.
//
//  NOTE: cannot cover the whole dark period - Wire1.begin(), the I2C bus
//  scan and the touch probe all run before setupTFT(), so ~1s remains
//  unavoidably blank before the splash itself appears. Not worth
//  restructuring setup() to close that gap.
// ============================================================
#define SPLASH_TITLE_Y       70
#define SPLASH_VER_Y        116
#define SPLASH_SRC_Y        142
#define SPLASH_LIC_Y        164
#define SPLASH_DISCLAIMER_Y 186
#define SPLASH_DIVIDER_Y    210
#define SPLASH_LIST_X         60
#define SPLASH_LIST_Y        234
#define SPLASH_LINE_H          22
#define SPLASH_HOLD_MS       1500UL   // held on screen after the last line before the dashboard takes over

void drawSplashScreen();
void splashLine(const char* label, bool ok);

// ============================================================
//  PER-REGION DIRTY FLAGS
// ============================================================
#define MIN_DISPLAY_INTERVAL_MS  500UL

extern volatile bool dirtyGauges;
extern volatile bool dirtyStatus;
extern volatile bool dirtyParticles;
extern volatile bool dirtyHeader;
extern volatile bool dirtyOutside;   // outdoor temp/humidity changed
extern volatile bool dirtyLD2410;    // LD2410B presence/energy/distance changed

// ============================================================
//  SENSOR STALENESS
//  Each sensor .cpp updates its lastXxxOkMs on a successful read.
//  Consumers (display + buzzer) treat a value as stale once its window
//  elapses, and grey it out / ignore it. Windows scale to update cadence:
//    FAST (SFA40 every loop, SGP30 1s) -> 10s
//    MED  (SCD40/BME280/SHT40 5s)      -> 30s
//    PM   (PMS5003 120s cycle)         -> 5min
// ============================================================
#define STALE_FAST_MS   10000UL
#define STALE_MED_MS    30000UL
#define STALE_PM_MS     300000UL

extern unsigned long lastSCD40OkMs;
extern unsigned long lastSGP30OkMs;
extern unsigned long lastSFA40OkMs;
extern unsigned long lastPMS5003OkMs;
extern unsigned long lastBME280OkMs;
extern unsigned long lastSHT40OkMs;
extern bool sht40Detected;   // optional-sensor gate - declared here (not via SHT40_TEMP_HUMIDITY.h)
                             // because that header includes Graphics.h, same reason the
                             // lastXxxOkMs externs above are declared directly instead of
                             // pulled in via an include

// lastOkMs == 0 means "never read" -> stale until the first good read.
inline bool sensorStale(unsigned long lastOkMs, unsigned long windowMs) {
  return lastOkMs == 0 || (millis() - lastOkMs) > windowMs;
}

void setDirtyGauges();
void setDirtyParticles();
void setDirtyHeader();
void setDisplayDirty();
void setDirtyOutside();   // call from MQTT callback when outdoor data arrives
void setDirtyLD2410();    // call from LD2410B_Presence.cpp on data change

// ============================================================
//  TREND BUFFER
// ============================================================
#define TREND_SAMPLES  8
struct TrendBuffer {
  float   values[TREND_SAMPLES];
  uint8_t head;
  uint8_t count;
  bool    initialised;
  void   init();
  void   push(float v);
  int8_t trend() const;
  float  latest() const;
};
extern TrendBuffer trendCO2, trendHCHO, trendTVOC, trendPM25, trendTemp, trendHum;

// ============================================================
//  LD2410B PRESENCE BLOCK  (right panel bottom - anchored to bottom)
//  Sits below the vent block with its own dirty flag so presence
//  updates don't cause the vent block to flash.
// ============================================================
#define LD2410_BLOCK_H    66
#define LD2410_BLOCK_Y    (ZONE_Y + ZONE_H - LD2410_BLOCK_H - 4)
#define LD2410_ROW_H      24

// ============================================================
//  OPTIONAL DISPLAY GATE
//
//  The 7" TFT is optional. With no display fitted the WEM runs headless,
//  publishing to Home Assistant over MQTT exactly as normal. displayDetected
//  is latched once in setupTFT() and never re-evaluated afterwards - the same
//  contract as every xxxDetected sensor flag (see "OPTIONAL-SENSOR DETECTION
//  GATE" in Global.h). A display connected after boot therefore needs a
//  restart to be picked up.
//
//  Everything display-related gates on this one flag:
//    - setupTFT()        skips tft.begin(), trend init and static chrome
//    - updateDisplay()   skips all rendering (the single rendering gate -
//                        every updateXxx() below is reached only from here)
//    - Backlight.cpp     skips BACKLIGHT_PIN and the HA backlight entity
//    - readTouchEvent()  (Buzzer.cpp) skips all FT5x06 I2C traffic
//
//  DETECTION IS BY PROXY - this matters, so it is stated plainly. The SSD1963
//  itself cannot be probed: the read strobe (E_/RD, connector pin 5) is not
//  connected, so there is no readback path of any kind on the parallel bus.
//  The FT5x06 capacitive touch controller at 0x38 shares the panel's ribbon
//  and power rail, so its presence on Wire1 stands in for "a display is
//  fitted". Consequence: a panel whose touch ribbon is unseated, or a
//  resistive-touch variant of the panel, reads as ABSENT and the display
//  stays dark. Documented for users in USER_GUIDE.md.
//
//  ORDERING: setupTFT() must now be called AFTER Wire1.begin(), since the
//  probe is an I2C transaction. See setup() in the .ino.
// ============================================================
extern bool displayDetected;

// ============================================================
//  PUBLIC API
// ============================================================
void setupTFT();
void updateDisplay();
void drawStaticChrome();
void updateHeader();
void updateStatusPanel();
void updateGauges();
void updateParticleBars();
void updateVentBlock();
void updateOutsideBlock();
void updateLD2410Block();

#endif