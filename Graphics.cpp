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

// Graphics.cpp
// 
// Workshop Environment Monitor - Display Rendering
//
// LAYOUT:
//    Left panel:  status indicators (top) + PM bars (bottom)
//    Centre:      arc gauges
//    Right panel: Outside Ref block (top) + Vent block (bottom) + LD2410B
//                 presence block
//
// updateOutsideBlock():
//    Displays outdoor temperature and humidity from rtl_433 Nexus-TH via MQTT.
//    Stale detection: values rendered in COL_STALE if no update within
//    OUTSIDE_STALE_MS. Snapshot guard prevents unnecessary redraws (same
//    pattern as updateVentBlock()).
//
// PM BARS:
//    Geometry driven entirely from the PBAR_* defines in Graphics.h - no
//    hardcoded widths in this file.

#include "Graphics.h"
#include "LD2410B_Presence.h"   // ld2410Presence, ld2410MovingEnergy, ld2410StillEnergy, ld2410Distance
#include "TimeSync.h"           // timeSyncState - header sync warning
#include <WiFi.h>

// ============================================================
//  OPTIONAL DISPLAY GATE
//  Latched once by setupTFT(), read by updateDisplay() below, by
//  Backlight.cpp and by readTouchEvent() in Buzzer.cpp. Full rationale,
//  including why detection is by proxy via the touch controller, is in
//  Graphics.h - see "OPTIONAL DISPLAY GATE" there.
// ============================================================
bool displayDetected = false;

// ============================================================
//  PER-REGION DIRTY FLAGS
// ============================================================
volatile bool dirtyGauges = false;
volatile bool dirtyStatus = false;
volatile bool dirtyParticles = false;
volatile bool dirtyHeader = false;
volatile bool dirtyOutside = false;
volatile bool dirtyLD2410 = false;

void setDirtyGauges() {
  dirtyGauges = true;
  dirtyStatus = true;
}
void setDirtyParticles() {
  dirtyParticles = true;
  dirtyStatus = true;
}
void setDirtyHeader() {
  dirtyHeader = true;
}
void setDirtyOutside() {
  dirtyOutside = true;
}
void setDirtyLD2410() {
  dirtyLD2410 = true;
}
void setDisplayDirty() {
  dirtyGauges = true;
  dirtyStatus = true;
  dirtyParticles = true;
}

// ============================================================
//  TREND BUFFERS
// ============================================================
TrendBuffer trendCO2, trendHCHO, trendTVOC, trendPM25, trendTemp, trendHum;

void TrendBuffer::init() {
  memset(values, 0, sizeof(values));
  head = count = 0;
  initialised = true;
}
void TrendBuffer::push(float v) {
  if (!initialised) init();
  values[head] = v;
  head = (head + 1) % TREND_SAMPLES;
  if (count < TREND_SAMPLES) count++;
}
float TrendBuffer::latest() const {
  if (!count) return 0.0f;
  return values[head ? head - 1 : TREND_SAMPLES - 1];
}
int8_t TrendBuffer::trend() const {
  if (count < 4) return 0;
  uint8_t half = count / 2;
  float os = 0, ns = 0;
  for (uint8_t i = 0; i < half; i++)
    os += values[(head + TREND_SAMPLES - count + i) % TREND_SAMPLES];
  for (uint8_t i = half; i < count; i++)
    ns += values[(head + TREND_SAMPLES - count + i) % TREND_SAMPLES];
  float oa = os / half, na = ns / (count - half);
  float band = max(oa * 0.02f, 5.0f);
  if (na - oa > band) return  1;
  if (na - oa < -band) return -1;
  return 0;
}

// ============================================================
//  COLOUR HELPERS
// ============================================================
static uint16_t threshCol(float v, float good, float warn) {
  return (v <= good) ? COL_GOOD : (v <= warn) ? COL_WARN : COL_DANGER;
}
static uint16_t tempCol(float v) {
  if (v >= THRESH_TEMP_LOW_GOOD && v <= THRESH_TEMP_HIGH_GOOD) return COL_GOOD;
  if (v >= THRESH_TEMP_LOW_WARN && v <= THRESH_TEMP_HIGH_WARN) return COL_WARN;
  return COL_DANGER;
}
static uint16_t humCol(float v) {
  if (v >= THRESH_HUM_LOW_GOOD && v <= THRESH_HUM_HIGH_GOOD) return COL_GOOD;
  if (v >= THRESH_HUM_LOW_WARN && v <= THRESH_HUM_HIGH_WARN) return COL_WARN;
  return COL_DANGER;
}

// ============================================================
//  TREND ARROW
// ============================================================
static void drawTrendArrow(int16_t cx, int16_t cy, int8_t dir, uint16_t col) {
  tft.fillRect(cx - 6, cy - 7, 13, 15, COL_BG);
  if (dir > 0) tft.fillTriangle(cx, cy - 6, cx - 5, cy + 5, cx + 5, cy + 5, col);
  else if (dir < 0) tft.fillTriangle(cx, cy + 6, cx - 5, cy - 5, cx + 5, cy - 5, col);
  else              tft.fillTriangle(cx + 6, cy, cx - 3, cy - 5, cx - 3, cy + 5, col);
}

// ============================================================
//  DRAW ARC SWEEP  (helper)
//  Wrap-safe sweep draw shared by drawGauge()'s state ring and its
//  proportional live-data overlay below - both need the same 225 deg
//  start-angle wraparound-past-360 handling.
// ============================================================
static void drawArcSweep(TFT_eSprite& spr, int16_t cx, int16_t cy,
  int16_t r, int16_t arcW, uint16_t sweepDeg, uint16_t col) {
  if (sweepDeg <= 1) return;
  uint16_t endAngle = ARC_START_DEG + sweepDeg;
  if (endAngle <= 360) {
    spr.drawArc(cx, cy, r, r - arcW,
      ARC_START_DEG, endAngle,
      col, COL_BG, true);
  }
  else {
    spr.drawArc(cx, cy, r, r - arcW,
      ARC_START_DEG, 360,
      col, COL_BG, true);
    spr.drawArc(cx, cy, r, r - arcW,
      0, endAngle - 360,
      col, COL_BG, true);
  }
}

// ============================================================
//  DRAW ARC GAUGE
//
//  Tri-state pattern - mirrors drawOneBar() in updateParticleBars() below:
//    error     (not detected, or detected then timed out)  -> full grey
//              state ring, no proportional overlay. A broken/stale sensor
//              is grey regardless of what its frozen/zero value would
//              otherwise sweep to - this is what fixes "bad sensor reading
//              zero looks the same as grey at a glance".
//    neverRead (detected, no successful read yet)          -> full purple
//              state ring (COL_BAR_BG), "--" shown instead of a misleading
//              zero - visible immediately on first draw, no waiting for
//              the first read.
//    live      (detected, has real data)                   -> purple state
//              ring with a proportional threshold-coloured arc overlaid on
//              top, leaving the unswept remainder purple.
// ============================================================
static void drawGauge(uint16_t sprSide,
  int16_t pushX, int16_t pushY,
  int16_t cx, int16_t cy,
  int16_t r, int16_t arcW,
  float value, float minVal, float maxVal,
  uint16_t liveCol, bool error, bool neverRead,
  const char* valFmt, const GFXfont* valFont,
  const char* unit, const GFXfont* unitFont) {

  TFT_eSprite spr = TFT_eSprite(&tft);
  spr.createSprite(sprSide, sprSide);
  spr.setColorDepth(16);
  spr.fillSprite(COL_BG);

  // Background void ring - always drawn, faint track under everything else.
  spr.drawArc(cx, cy, r, r - arcW,
    ARC_START_DEG, 360,
    COL_ARC_BG, COL_BG, true);
  spr.drawArc(cx, cy, r, r - arcW,
    0, (ARC_START_DEG + ARC_SWEEP_DEG) - 360,
    COL_ARC_BG, COL_BG, true);

  // State ring - full sweep regardless of value (the arc equivalent of
  // drawOneBar()'s full-width track), so grey-vs-purple is never confused
  // with an actual reading's sweep length.
  drawArcSweep(spr, cx, cy, r, arcW, ARC_SWEEP_DEG, error ? COL_STALE : COL_BAR_BG);

  // Proportional live-data overlay, drawn on top of the purple state ring,
  // leaving the unswept remainder purple - same relationship as the bar's
  // coloured fill sitting on top of its purple track.
  if (!error && !neverRead) {
    float    pct = constrain((value - minVal) / (maxVal - minVal), 0.0f, 1.0f);
    uint16_t swDeg = (uint16_t)(ARC_SWEEP_DEG * pct);
    drawArcSweep(spr, cx, cy, r, arcW, swDeg, liveCol);
  }

  char buf[12];
  uint16_t valCol;
  if (neverRead) {
    snprintf(buf, sizeof(buf), "--");
    valCol = COL_LABEL;                       // neutral - detected and waiting, not an error
  }
  else {
    snprintf(buf, sizeof(buf), valFmt, value);
    valCol = error ? COL_STALE : COL_TEXT;    // last-known value greyed on timeout
  }
  spr.setFreeFont(valFont);
  spr.setTextColor(valCol, COL_BG);
  spr.setTextDatum(MC_DATUM);
  spr.drawString(buf, cx, cy - 6);

  if (unitFont) {
    spr.setFreeFont(unitFont);
    spr.setTextColor(COL_LABEL, COL_BG);
    spr.drawString(unit, cx, cy + 14);
  }
  else {
    spr.setFreeFont(nullptr);
    spr.setTextSize(1);
    spr.setTextColor(COL_LABEL, COL_BG);
    spr.setTextDatum(MC_DATUM);
    spr.drawString(unit, cx, cy + 18);
  }

  spr.pushSprite(pushX, pushY);
  spr.deleteSprite();
}

// ============================================================
//  BOOT SPLASH SCREEN
//  Full design rationale and lifecycle in Graphics.h - "BOOT SPLASH
//  SCREEN". splashNextY is append-only state: each splashLine() call
//  draws one row and advances it, nothing above is ever redrawn.
// ============================================================
static int16_t splashNextY = SPLASH_LIST_Y;

void drawSplashScreen() {
  tft.fillScreen(COL_BG);

  tft.setFreeFont(FONT_BOLD_XL);
  tft.setTextColor(COL_TEXT, COL_BG);
  tft.setTextDatum(TC_DATUM);
  tft.drawString("Workshop Environment Monitor", DISP_W / 2, SPLASH_TITLE_Y);

  tft.setFreeFont(FONT_SMALL);
  tft.setTextColor(COL_LABEL, COL_BG);
  tft.setTextDatum(TC_DATUM);
  tft.drawString(TOP_MENU_PROGRAM_VERSION, DISP_W / 2, SPLASH_VER_Y);
  tft.drawString("https://github.com/bionicbone/Workshop-Environment-Monitor",
    DISP_W / 2, SPLASH_SRC_Y);
  tft.drawString("AGPL-3.0-or-later - NO WARRANTY", DISP_W / 2, SPLASH_LIC_Y);
  tft.drawString("Not a certified gas detector or life-safety device",
    DISP_W / 2, SPLASH_DISCLAIMER_Y);

  tft.drawFastHLine(SPLASH_LIST_X, SPLASH_DIVIDER_Y,
    DISP_W - 2 * SPLASH_LIST_X, COL_DIVIDER);

  splashNextY = SPLASH_LIST_Y;
}

// Same left-label / right-value split as the status panel
// (STATUS_LABEL_X / STATUS_VAL_X) - kept consistent rather than trying to
// column-align a proportional font with padded strings.
void splashLine(const char* label, bool ok) {
  if (!displayDetected) return;   // no-op on headless - callers don't need their own gate

  tft.setFreeFont(FONT_SMALL);
  tft.setTextColor(COL_TEXT, COL_BG);
  tft.setTextDatum(TL_DATUM);
  tft.drawString(label, SPLASH_LIST_X, splashNextY);

  tft.setTextColor(ok ? COL_GOOD : COL_STALE, COL_BG);
  tft.setTextDatum(TR_DATUM);
  tft.drawString(ok ? "OK" : "not found", DISP_W - SPLASH_LIST_X, splashNextY);

  splashNextY += SPLASH_LINE_H;
}

// ============================================================
//  STATIC CHROME  - drawn once at startup
// ============================================================
void drawStaticChrome() {
  tft.fillScreen(COL_BG);

  // Header
  tft.fillRect(0, 0, DISP_W, HDR_H, COL_HEADER_BG);
  tft.drawFastHLine(0, HDR_H, DISP_W, COL_BORDER);
  tft.setFreeFont(FONT_BOLD_SM);
  tft.setTextColor(COL_TEXT, COL_HEADER_BG);
  tft.setTextDatum(ML_DATUM);
  tft.drawString("Workshop Environment Monitor", 8, HDR_H / 2);
  tft.setTextColor(COL_LABEL, COL_HEADER_BG);
  tft.setTextDatum(MR_DATUM);
  tft.drawString(TOP_MENU_PROGRAM_VERSION, DISP_W - 6, HDR_H / 2);

  // Zone dividers
  tft.drawFastVLine(STATUS_RIGHT, ZONE_Y, ZONE_H, COL_DIVIDER);
  tft.drawFastVLine(STATUS_RIGHT + 1, ZONE_Y, ZONE_H, COL_BG);
  tft.drawFastVLine(GAUGE_RIGHT, ZONE_Y, ZONE_H, COL_DIVIDER);
  tft.drawFastVLine(GAUGE_RIGHT + 1, ZONE_Y, ZONE_H, COL_BG);

  // --- LEFT PANEL ---

  // Status panel sensor labels (CO2, HCHO, TVOC, PM2.5, PM10)
  const char* sLabels[STATUS_ITEMS] = { "CO2","HCHO","TVOC","PM2.5","PM10" };
  for (uint8_t i = 0; i < STATUS_ITEMS; i++) {
    int16_t cy = STATUS_FIRST_Y + i * STATUS_ITEM_H + STATUS_ITEM_H / 2;
    tft.setFreeFont(FONT_SMALL);
    tft.setTextColor(COL_LABEL, COL_BG);
    tft.setTextDatum(ML_DATUM);
    tft.drawString(sLabels[i], STATUS_LABEL_X, cy);
  }

  // Divider between status rows and PM bars
  tft.drawFastHLine(STATUS_X + 4, STATUS_PM_DIVIDER_Y, STATUS_W - 8, COL_BORDER);

  // PM bar headers and size labels - now in left panel
  tft.setFreeFont(FONT_BOLD_SM);
  tft.setTextColor(COL_LABEL, COL_BG);
  tft.setTextDatum(ML_DATUM);
  tft.drawString("Cnt /0.1L", PBAR_X, PBAR_CNT_Y - 1);
  tft.drawString("Mass ug/m3", PBAR_X, PBAR_MASS_Y - 1);

  const char* cL[] = { "0.3","0.5","1.0","2.5","5.0","10" };
  for (uint8_t i = 0; i < 6; i++) {
    tft.setFreeFont(FONT_SMALL);
    tft.setTextColor(COL_LABEL, COL_BG);
    tft.setTextDatum(MR_DATUM);
    tft.drawString(cL[i], PBAR_X + PBAR_LABEL_W,
      PBAR_CNT_Y + 14 + i * PBAR_ROW_H + PBAR_H / 2 - 2);
  }
  const char* mL[] = { "1.0","2.5","10" };
  for (uint8_t i = 0; i < 3; i++) {
    tft.setFreeFont(FONT_SMALL);
    tft.setTextColor(COL_LABEL, COL_BG);
    tft.setTextDatum(MR_DATUM);
    tft.drawString(mL[i], PBAR_X + PBAR_LABEL_W,
      PBAR_MASS_Y + 14 + i * PBAR_ROW_H + PBAR_H / 2 - 2);
  }

  // --- RIGHT PANEL ---

  // "Outside" static header label
  tft.setFreeFont(FONT_BOLD_SM);
  tft.setTextColor(COL_LABEL, COL_BG);
  tft.setTextDatum(ML_DATUM);
  tft.drawString("Outside", OUTSIDE_X, OUTSIDE_BLOCK_Y + 2);

  // Divider between outside block and vent block
  tft.drawFastHLine(RIGHT_X + 4,
    VENT_BLOCK_Y - 8,
    RIGHT_W - 8, COL_BORDER);

  // "VENT" static header label
  tft.setFreeFont(FONT_BOLD_SM);
  tft.setTextColor(COL_LABEL, COL_BG);
  tft.setTextDatum(ML_DATUM);
  tft.drawString("3D PRINTER VENT", OUTSIDE_X, VENT_BLOCK_Y + 2);

  // Vent block row labels - drawn once, never erased
  // Values are drawn in-place over COL_BG background, labels stay put
  tft.setFreeFont(FONT_SMALL);
  tft.setTextColor(COL_LABEL, COL_BG);
  tft.setTextDatum(ML_DATUM);
  {
    int16_t lx = OUTSIDE_X;
    int16_t ly = VENT_BLOCK_Y + 18 + 30;   // below badge
    tft.drawString("Amb:", lx, ly + 7);  ly += VENT_ROW_H;
    tft.drawString("Chm:", lx, ly + 7);  ly += VENT_ROW_H;
    tft.drawString("DeltaT:", lx, ly + 7);  ly += VENT_ROW_H;
    tft.drawString("Fan:", lx, ly + 7);  ly += VENT_ROW_H;
    tft.drawString("Trig:", lx, ly + 7);
  }

  // Divider above LD2410B presence block
  tft.drawFastHLine(RIGHT_X + 4,
    LD2410_BLOCK_Y - 8,
    RIGHT_W - 8, COL_BORDER);

  // "Presence" static header label
  tft.setFreeFont(FONT_BOLD_SM);
  tft.setTextColor(COL_LABEL, COL_BG);
  tft.setTextDatum(ML_DATUM);
  tft.drawString("Presence", OUTSIDE_X, LD2410_BLOCK_Y + 2);

  // LD2410B row labels - drawn once, never erased
  tft.setFreeFont(FONT_SMALL);
  tft.setTextColor(COL_LABEL, COL_BG);
  tft.setTextDatum(ML_DATUM);
  {
    int16_t lx = OUTSIDE_X;
    int16_t ly = LD2410_BLOCK_Y + 18;
    tft.drawString("Pres:", lx, ly + 7);  ly += LD2410_ROW_H;
    tft.drawString("Dist:", lx, ly + 7);
  }
  struct { int16_t cx; int16_t cy; int16_t r; const char* label; } gLabels[] = {
    { GAUGE_TVOC_CX, GAUGE_TVOC_CY, GAUGE_LG_R, "TVOC"     },
    { GAUGE_HCHO_CX, GAUGE_HCHO_CY, GAUGE_LG_R, "HCHO"     },
    { GAUGE_CO2_CX,  GAUGE_CO2_CY,  GAUGE_LG_R, "CO2"      },
    { GAUGE_TEMP_CX, GAUGE_TEMP_CY, GAUGE_XS_R, "Temp"     },
    { GAUGE_HUM_CX,  GAUGE_HUM_CY,  GAUGE_XS_R, "Humidity" },
  };
  for (auto& g : gLabels) {
    tft.setFreeFont(FONT_BOLD_SM);
    tft.setTextColor(COL_LABEL, COL_BG);
    tft.setTextDatum(BC_DATUM);
    int16_t labelY = g.cy - g.r - GAUGE_ARC_W - GAUGE_SPR_PAD - 7;
    tft.drawString(g.label, g.cx, labelY);
  }
}

// ============================================================
//  UPDATE HEADER
// ============================================================
void updateHeader() {
  tft.fillRect(380, 1, 280, HDR_H - 2, COL_HEADER_BG);

  bool ok = (WiFi.status() == WL_CONNECTED);
  tft.setFreeFont(FONT_BOLD_SM);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(ok ? COL_GOOD : COL_DANGER, COL_HEADER_BG);
  tft.drawString(ok ? "WiFi OK" : "WiFi --", 450, HDR_H / 2);

  // Time / time-sync warning - reuses the same slot as the plain date/time
  // string so no layout changes are needed either way.
  tft.setTextDatum(MC_DATUM);
  if (timeSyncState == TIMESYNC_NTP_FAILED) {
    tft.setTextColor(COL_DANGER, COL_HEADER_BG);
    tft.drawString("NTP SYNC FAILED", 570, HDR_H / 2);
  }
  else if (timeSyncState == TIMESYNC_MISMATCH) {
    tft.setTextColor(COL_WARN, COL_HEADER_BG);
    tft.drawString("TIME SYNC ISSUE", 570, HDR_H / 2);
  }
  else {
    tft.setTextColor(COL_TEXT, COL_HEADER_BG);
    tft.drawString(displayTime, 570, HDR_H / 2);
  }

  // Snooze indicator - shown while buzzer is silenced
  if (buzzerSnoozed) {
    tft.setTextColor(COL_WARN, COL_HEADER_BG);
    tft.setTextDatum(MR_DATUM);
    tft.drawString("ZZZ", 730, HDR_H / 2);
  }
  else {
    tft.fillRect(680, 1, 52, HDR_H - 2, COL_HEADER_BG);  // erase ZZZ area
  }
}

// ============================================================
//  UPDATE STATUS PANEL
// ============================================================
void updateStatusPanel() {
  struct Item {
    float val; float good; float warn;
    const char* fmt; TrendBuffer* trend; bool stale;
  } items[STATUS_ITEMS] = {
    { (float)co2,            THRESH_CO2_GOOD,  THRESH_CO2_WARN,  "%.0f", &trendCO2,  sensorStale(lastSCD40OkMs,   STALE_MED_MS)  },
    { (float)hcho_ppb,       THRESH_HCHO_GOOD, THRESH_HCHO_WARN, "%.0f", &trendHCHO, sensorStale(lastSFA40OkMs,   STALE_FAST_MS) },
    { (float)getTVOC(),      THRESH_TVOC_GOOD, THRESH_TVOC_WARN, "%.0f", &trendTVOC, sensorStale(lastSGP30OkMs,   STALE_FAST_MS) },
    { (float)pmsData.pm25_env,  THRESH_PM25_GOOD, THRESH_PM25_WARN, "%.0f", &trendPM25, sensorStale(lastPMS5003OkMs, STALE_PM_MS)   },
    { (float)pmsData.pm100_env, THRESH_PM10_GOOD, THRESH_PM10_WARN, "%.0f", nullptr,    sensorStale(lastPMS5003OkMs, STALE_PM_MS)   },
  };
  for (uint8_t i = 0; i < STATUS_ITEMS; i++) {
    Item& it = items[i];
    int16_t cy = STATUS_FIRST_Y + i * STATUS_ITEM_H + STATUS_ITEM_H / 2;
    // Stale -> grey dot/value, so a frozen reading can't masquerade as "good".
    uint16_t c = it.stale ? COL_STALE : threshCol(it.val, it.good, it.warn);
    tft.fillCircle(STATUS_IND_X, cy, STATUS_IND_R, c);
    tft.drawCircle(STATUS_IND_X, cy, STATUS_IND_R + 1, COL_BORDER);
    tft.fillRect(STATUS_X + 85, cy - STATUS_ITEM_H / 2 + 3,
      STATUS_W - 85 - 18, STATUS_ITEM_H - 6, COL_BG);
    char buf[10];
    snprintf(buf, sizeof(buf), it.fmt, it.val);
    tft.setFreeFont(FONT_BOLD_SM);
    tft.setTextColor(c, COL_BG);
    tft.setTextDatum(MR_DATUM);
    tft.drawString(buf, STATUS_VAL_X, cy);
    drawTrendArrow(STATUS_ARROW_X, cy, it.trend ? it.trend->trend() : 0, c);
  }
}

// ============================================================
//  UPDATE VENT BLOCK  (right panel)
//
//  Smooth update strategy - no full-area erase:
//  - Badge:      redrawn only when ventFlagOpen changes (bg colour change).
//                All other updates skip the badge entirely.
//  - Text rows:  setTextColor(fg, COL_BG) draws text with bg fill in-place.
//                Labels are static chrome - drawn once, never erased.
//  - Trig bar:   background track + fill drawn each time (bar is its own erase).
//  - No fillRect clearing the whole area - eliminates the flicker that was
//                wiping the LD2410B presence block below.
// ============================================================
void updateVentBlock() {
  static int   lastFanRPM = -1;
  static float lastChamberTemp = -999.f;
  static float lastAmbientTemp = -999.f;
  static float lastDeltaTemp = -999.f;
  static bool  lastFlagOpen = false;
  static int   lastTriggerTimer = -1;

  bool flagChanged = (ventFlagOpen != lastFlagOpen);

  if (ventFanRPM == lastFanRPM &&
    ventChamberTemp == lastChamberTemp &&
    ventAmbientTemp == lastAmbientTemp &&
    ventDeltaTemp == lastDeltaTemp &&
    ventFlagOpen == lastFlagOpen &&
    ventTriggerTimer == lastTriggerTimer) {
    return;
  }
  lastFanRPM = ventFanRPM;
  lastChamberTemp = ventChamberTemp;
  lastAmbientTemp = ventAmbientTemp;
  lastDeltaTemp = ventDeltaTemp;
  lastFlagOpen = ventFlagOpen;
  lastTriggerTimer = ventTriggerTimer;

  int16_t x = OUTSIDE_X;
  int16_t y = VENT_BLOCK_Y + 18;
  int16_t w = OUTSIDE_W;

  // ---- Badge: only redraw when open/closed state changes ----
  // Background colour changes require a full badge redraw.
  // Skip on value-only updates to avoid visible flash.
  if (flagChanged) {
    uint16_t vc = ventFlagOpen ? COL_VENT_OPEN : COL_VENT_CLOSED;
    tft.fillRoundRect(x, y, w, 26, 5, vc);
    tft.fillRect(x + 5, y + 25, w - 10, 1, vc);
    tft.setFreeFont(FONT_BOLD_SM);
    tft.setTextColor(COL_TEXT, vc);
    tft.setTextDatum(MC_DATUM);
    tft.drawString(ventFlagOpen ? "OPEN" : "CLOSED", x + w / 2, y + 13);
  }
  y += 30;

  // ---- Text rows: in-place update with bg fill ----
  // setTextColor(fg, COL_BG) fills behind the text automatically -
  // no separate erase needed. Labels are static chrome, skip redraw.
  char buf[16];
  tft.setFreeFont(FONT_SMALL);

  snprintf(buf, sizeof(buf), "%.1f C", ventAmbientTemp);
  tft.setTextColor(COL_TEXT, COL_BG); tft.setTextDatum(MR_DATUM);
  // Before each drawString, erase the text field
  tft.fillRect(x + w - 80, y, 80, VENT_ROW_H - 4, COL_BG);
  tft.drawString(buf, x + w, y + 7);  y += VENT_ROW_H;

  snprintf(buf, sizeof(buf), "%.1f C", ventChamberTemp);
  tft.setTextColor(COL_TEXT, COL_BG); tft.setTextDatum(MR_DATUM);
  tft.fillRect(x + w - 80, y, 80, VENT_ROW_H - 4, COL_BG);
  tft.drawString(buf, x + w, y + 7);  y += VENT_ROW_H;

  snprintf(buf, sizeof(buf), "%+.1f C", ventDeltaTemp);
  tft.setTextColor(fabsf(ventDeltaTemp) > 10.0f ? COL_WARN : COL_TEXT, COL_BG);
  tft.setTextDatum(MR_DATUM);
  tft.fillRect(x + w - 80, y, 80, VENT_ROW_H - 4, COL_BG);
  tft.drawString(buf, x + w, y + 7);  y += VENT_ROW_H;

  snprintf(buf, sizeof(buf), "%d rpm", ventFanRPM);
  tft.setTextColor((ventFlagOpen && ventFanRPM < 100) ? COL_DANGER : COL_TEXT, COL_BG);
  tft.setTextDatum(MR_DATUM);
  tft.fillRect(x + w - 80, y, 80, VENT_ROW_H - 4, COL_BG);
  tft.drawString(buf, x + w, y + 7);  y += VENT_ROW_H;

  // ---- Trig bar: background track is its own erase ----
  const int16_t labelW = 34;
  const int16_t barH = 14;
  const int16_t barX = x + labelW + 3;
  const int16_t barW = w - labelW - 3;
  const int16_t trigMax = 120;

  tft.fillRoundRect(barX, y, barW, barH, 3, COL_BAR_BG);
  if (ventTriggerTimer > 0) {
    float pct = constrain((float)ventTriggerTimer / (float)trigMax, 0.0f, 1.0f);
    int16_t fw = (int16_t)(barW * pct);
    if (fw > 2) {
      uint16_t barCol = (ventTriggerTimer > 30) ? COL_GOOD : COL_WARN;
      tft.fillRoundRect(barX, y, fw, barH, 3, barCol);
    }
  }
}

// ============================================================
//  UPDATE OUTSIDE BLOCK  (right panel top)
//
//  Displays outdoor temperature and humidity from rtl_433
//  Nexus-TH sensor via MQTT (outdoorTempC, outdoorHumidity).
//  Stale detection: if lastOutdoorUpdate == 0 or more than
//  OUTSIDE_STALE_MS ms ago, values are rendered in COL_STALE
//  and a "No signal" indicator replaces the values.
//  Snapshot guard prevents unnecessary redraws.
// ============================================================
void updateOutsideBlock() {
  static float lastTemp = -999.f;
  static float lastHumidity = -999.f;
  static bool  lastStale = false;

  unsigned long now = millis();
  bool stale = (lastOutdoorUpdate == 0 ||
    (now - lastOutdoorUpdate) > OUTSIDE_STALE_MS);

  // Only redraw if data or stale state has changed
  if (outdoorTempC == lastTemp &&
    outdoorHumidity == lastHumidity &&
    stale == lastStale) {
    return;
  }
  lastTemp = outdoorTempC;
  lastHumidity = outdoorHumidity;
  lastStale = stale;

  int16_t x = OUTSIDE_X;
  int16_t w = OUTSIDE_W;
  int16_t y = OUTSIDE_BLOCK_Y + 18;   // below static "Outside" label

  // Erase outside block area (header row to divider)
  tft.fillRect(x, y, w, 2 * OUTSIDE_ROW_H + 4, COL_BG);

  char buf[16];

  tft.setFreeFont(FONT_SMALL);

  // Temperature row
  tft.setTextColor(COL_LABEL, COL_BG); tft.setTextDatum(ML_DATUM);
  tft.drawString("Temp:", x, y + 7);
  if (stale) {
    tft.setTextColor(COL_STALE, COL_BG); tft.setTextDatum(MR_DATUM);
    tft.drawString("--.- C", x + w, y + 7);
  }
  else {
    snprintf(buf, sizeof(buf), "%.1f C", outdoorTempC);
    tft.setTextColor(COL_TEXT, COL_BG); tft.setTextDatum(MR_DATUM);
    tft.drawString(buf, x + w, y + 7);
  }
  y += OUTSIDE_ROW_H;

  // Humidity row
  tft.setTextColor(COL_LABEL, COL_BG); tft.setTextDatum(ML_DATUM);
  tft.drawString("Hum:", x, y + 7);
  if (stale) {
    tft.setTextColor(COL_STALE, COL_BG); tft.setTextDatum(MR_DATUM);
    tft.drawString("-- %RH", x + w, y + 7);
  }
  else {
    snprintf(buf, sizeof(buf), "%.0f %%RH", outdoorHumidity);
    tft.setTextColor(COL_TEXT, COL_BG); tft.setTextDatum(MR_DATUM);
    tft.drawString(buf, x + w, y + 7);
  }
}

// ============================================================
//  UPDATE LD2410B PRESENCE BLOCK  (right panel, anchored to bottom)
//
//  Smooth update - no full-area erase:
//  - Labels are static chrome (drawn once in drawStaticChrome)
//  - Pres bar: background track is its own erase (drawn first, fill on top)
//  - Dist: in-place text with COL_BG background fill
// ============================================================
void updateLD2410Block() {
  // lastDetected seeded true: ld2410Detected defaults false at declaration,
  // so an absent sensor mismatches on the very first call and gets its
  // initial grey draw without relying on the other fields' 9999 sentinels.
  static bool     lastDetected = true;
  static bool     lastPresence = false;
  static uint16_t lastMovingEnergy = 9999;
  static uint16_t lastStillEnergy = 9999;
  static uint16_t lastDistance = 9999;

  bool error = !ld2410Detected;

  if (ld2410Detected == lastDetected &&
    ld2410Presence == lastPresence &&
    ld2410MovingEnergy == lastMovingEnergy &&
    ld2410StillEnergy == lastStillEnergy &&
    ld2410Distance == lastDistance) {
    return;
  }
  lastDetected = ld2410Detected;
  lastPresence = ld2410Presence;
  lastMovingEnergy = ld2410MovingEnergy;
  lastStillEnergy = ld2410StillEnergy;
  lastDistance = ld2410Distance;

  int16_t x = OUTSIDE_X;
  int16_t w = OUTSIDE_W;
  int16_t y = LD2410_BLOCK_Y + 18;   // below static "Presence" label

  tft.setFreeFont(FONT_SMALL);

  // ---- Pres bar: background track is its own erase ----
  const int16_t pLabelW = 37;   // label width - bar starts clear of ":"
  const int16_t pBarH = 14;
  const int16_t pBarX = x + pLabelW + 3;
  const int16_t pBarW = w - pLabelW - 3;

  // Tri-state track, same pattern as the gauges/particle bars: grey = sensor
  // absent (error), purple = detected. "No presence" on a detected sensor is
  // a genuine, meaningful zero (empty purple track) - not an error state, so
  // it needs no separate never-read handling the way a numeric sensor does.
  tft.fillRoundRect(pBarX, y, pBarW, pBarH, 3, error ? COL_STALE : COL_BAR_BG);
  if (!error && ld2410Presence) {
    uint16_t energy = max(ld2410MovingEnergy, ld2410StillEnergy);
    float pct = constrain(energy / 100.0f, 0.0f, 1.0f);
    int16_t fw = (int16_t)(pBarW * pct);
    if (fw > 2) {
      uint16_t presCol = (ld2410MovingEnergy >= ld2410StillEnergy)
        ? COL_GOOD : 0x07FF;   // green=moving, cyan=still
      tft.fillRoundRect(pBarX, y, fw, pBarH, 3, presCol);
    }
  }
  y += LD2410_ROW_H;

  // ---- Dist: in-place text with bg fill ----
  char distBuf[12];
  tft.fillRect(x + 30, y + 1, w - 30, LD2410_ROW_H - 2, COL_BG);
  // erase value area
  if (!error && ld2410Presence && ld2410Distance > 0) {
    snprintf(distBuf, sizeof(distBuf), "%u cm", ld2410Distance);
    tft.setTextColor(COL_TEXT, COL_BG);
  }
  else {
    snprintf(distBuf, sizeof(distBuf), "--");
    tft.setTextColor(error ? COL_STALE : COL_LABEL, COL_BG);
  }
  tft.setTextDatum(MR_DATUM);  tft.drawString(distBuf, x + w, y + 7);
}

// ============================================================
//  UPDATE GAUGES
// ============================================================
void updateGauges() {
  const int16_t lgH = GAUGE_LG_SIDE / 2;
  const int16_t xsH = GAUGE_XS_SIDE / 2;

  // Push every trend on one fixed cadence (not on every redraw). Pushing on
  // each redraw let fast sensors (SFA40) repeatedly stuff stale CO2 values into
  // the buffer, polluting the trend window. One 5s tick keeps all six aligned.
  static unsigned long lastTrendPush = 0;
  unsigned long nowMs = millis();
  bool pushTrend = (nowMs - lastTrendPush >= 5000);
  if (pushTrend) lastTrendPush = nowMs;

  // Temp/humidity come from the SHT40 only (SCD40 is not a display fallback).
  // Tri-state derivation matches drawOneBar()'s pattern in updateParticleBars()
  // below: neverRead (no successful read yet, not an error) is now distinct
  // from error (not detected, or detected then timed out).
  bool sht40NeverRead = (lastSHT40OkMs == 0);
  bool sht40Error = !sht40Detected || (!sht40NeverRead && sensorStale(lastSHT40OkMs, STALE_MED_MS));

  // TVOC (SGP30)
  {
    float v = (float)getTVOC();
    bool neverRead = (lastSGP30OkMs == 0);
    bool error = !sgp30Detected || (!neverRead && sensorStale(lastSGP30OkMs, STALE_FAST_MS));
    if (pushTrend && !error && !neverRead) trendTVOC.push(v);
    drawGauge(GAUGE_LG_SIDE,
      GAUGE_TVOC_CX - lgH, GAUGE_TVOC_CY - lgH, lgH, lgH,
      GAUGE_LG_R, GAUGE_ARC_W,
      v, 0, GAUGE_TVOC_MAX,
      threshCol(v, THRESH_TVOC_GOOD, THRESH_TVOC_WARN), error, neverRead,
      "%.0f", FONT_BOLD_LG, "ppb", FONT_SMALL);
  }
  // HCHO (SFA40 - no detected flag; passive UART with no probe-able
  // handshake, so it cannot follow the same optional-sensor gate pattern
  // as the I2C sensors. neverRead/timeout is the only distinction
  // available, so a genuinely-absent SFA40 and one that's simply never
  // been read yet cannot be told apart here - a known limitation.)
  {
    float v = (float)hcho_ppb;
    bool neverRead = (lastSFA40OkMs == 0);
    bool error = !neverRead && sensorStale(lastSFA40OkMs, STALE_FAST_MS);
    if (pushTrend && !error && !neverRead) trendHCHO.push(v);
    drawGauge(GAUGE_LG_SIDE,
      GAUGE_HCHO_CX - lgH, GAUGE_HCHO_CY - lgH, lgH, lgH,
      GAUGE_LG_R, GAUGE_ARC_W,
      v, 0, GAUGE_HCHO_MAX,
      threshCol(v, THRESH_HCHO_GOOD, THRESH_HCHO_WARN), error, neverRead,
      "%.0f", FONT_BOLD_LG, "ppb", FONT_SMALL);
  }
  // CO2 (SCD40)
  {
    float v = (float)co2;
    bool neverRead = (lastSCD40OkMs == 0);
    bool error = !scd40Detected || (!neverRead && sensorStale(lastSCD40OkMs, STALE_MED_MS));
    if (pushTrend && !error && !neverRead) trendCO2.push(v);
    drawGauge(GAUGE_LG_SIDE,
      GAUGE_CO2_CX - lgH, GAUGE_CO2_CY - lgH, lgH, lgH,
      GAUGE_LG_R, GAUGE_ARC_W,
      v, 0, GAUGE_CO2_MAX,
      threshCol(v, THRESH_CO2_GOOD, THRESH_CO2_WARN), error, neverRead,
      "%.0f", FONT_BOLD_LG, "ppm", FONT_SMALL);
  }
  // Temp (SHT40)
  {
    float v = temperature;
    if (pushTrend && !sht40Error && !sht40NeverRead) trendTemp.push(v);
    drawGauge(GAUGE_XS_SIDE,
      GAUGE_TEMP_CX - xsH, GAUGE_TEMP_CY - xsH, xsH, xsH,
      GAUGE_XS_R, GAUGE_ARC_W,
      v, GAUGE_TEMP_MIN, GAUGE_TEMP_MAX,
      tempCol(v), sht40Error, sht40NeverRead,
      "%.1f", FONT_BOLD_SM, "C", nullptr);
  }
  // Humidity (SHT40)
  {
    float v = humidity;
    if (pushTrend && !sht40Error && !sht40NeverRead) trendHum.push(v);
    drawGauge(GAUGE_XS_SIDE,
      GAUGE_HUM_CX - xsH, GAUGE_HUM_CY - xsH, xsH, xsH,
      GAUGE_XS_R, GAUGE_ARC_W,
      v, 0, GAUGE_HUM_MAX,
      humCol(v), sht40Error, sht40NeverRead,
      "%.0f", FONT_BOLD_SM, "%RH", nullptr);
  }
}

// ============================================================
//  UPDATE PARTICLE BARS  (now in left panel)
// ============================================================
static void drawOneBar(int16_t y, float value, float maxVal,
  float goodT, float warnT, const char* fmt, bool error, bool neverRead) {
  // Track: solid grey in an error state (sensor absent, or timed out after
  // previously reporting) so the fault is visible even though the fill
  // width would otherwise be zero. Normal purple track otherwise.
  tft.fillRoundRect(PBAR_BAR_X, y, PBAR_BAR_W, PBAR_H, 3, error ? COL_STALE : COL_BAR_BG);

  // A magnitude fill is only meaningful once we have live, trustworthy data.
  // A detected sensor still in its stabilisation window (neverRead, !error)
  // shows the plain track with no fill - not an error, just no data yet.
  if (!error && !neverRead) {
    uint16_t fw = (uint16_t)(PBAR_BAR_W * constrain(value / maxVal, 0.0f, 1.0f));
    if (fw > 2)
      tft.fillRoundRect(PBAR_BAR_X, y, fw, PBAR_H, 3, threshCol(value, goodT, warnT));
  }

  tft.fillRect(PBAR_BAR_X + PBAR_BAR_W, y + 1, PBAR_VAL_W + 2, PBAR_H - 1, COL_BG);
  char buf[10];
  uint16_t txtCol;
  if (neverRead) {
    snprintf(buf, sizeof(buf), "--");
    // Grey if that's because the sensor is absent; neutral label-grey if
    // it's simply warming up and hasn't been read yet.
    txtCol = error ? COL_STALE : COL_LABEL;
  }
  else {
    snprintf(buf, sizeof(buf), fmt, value);
    // Preserve the last known value on timeout, just grey it - same
    // convention as the gauges and status panel.
    txtCol = error ? COL_STALE : COL_TEXT;
  }
  tft.setFreeFont(FONT_SMALL);
  tft.setTextColor(txtCol, COL_BG);
  tft.setTextDatum(MR_DATUM);
  tft.drawString(buf, PBAR_BAR_X + PBAR_BAR_W + PBAR_VAL_W + 2, y + PBAR_H / 2 - 1);
}

void updateParticleBars() {
  bool neverRead = (lastPMS5003OkMs == 0);
  bool timedOut = !neverRead && sensorStale(lastPMS5003OkMs, STALE_PM_MS);
  bool error = !pms5003Detected || timedOut;

  // PM2.5 trend is sampled here - this runs on the PMS 2-min cadence, which
  // is the right rate for the trend window (sampling it at the much faster
  // gauge-redraw rate would over-weight recent noise).
  if (!error && !neverRead)
    trendPM25.push((float)pmsData.pm25_env);

  struct { uint16_t val; float mx; } cR[] = {
    {pmsData.particles_03um,  PBAR_CNT_MAX_03},
    {pmsData.particles_05um,  PBAR_CNT_MAX_05},
    {pmsData.particles_10um,  PBAR_CNT_MAX_10},
    {pmsData.particles_25um,  PBAR_CNT_MAX_25},
    {pmsData.particles_50um,  PBAR_CNT_MAX_50},
    {pmsData.particles_100um, PBAR_CNT_MAX_100},
  };
  for (uint8_t i = 0; i < 6; i++)
    drawOneBar(PBAR_CNT_Y + 14 + i * PBAR_ROW_H,
      (float)cR[i].val, cR[i].mx, 9999999.f, 9999999.f, "%.0f", error, neverRead);

  struct { float val; float mx; float g; float w; } mR[] = {
    {(float)pmsData.pm10_env,  100.f, 9999.f,           9999.f          },
    {(float)pmsData.pm25_env,   75.f, THRESH_PM25_GOOD,  THRESH_PM25_WARN},
    {(float)pmsData.pm100_env, 200.f, THRESH_PM10_GOOD,  THRESH_PM10_WARN},
  };
  for (uint8_t i = 0; i < 3; i++)
    drawOneBar(PBAR_MASS_Y + 14 + i * PBAR_ROW_H,
      mR[i].val, mR[i].mx, mR[i].g, mR[i].w, "%.0f", error, neverRead);
}

// ============================================================
//  TOUCH CONTROLLER PROBE  (display detection)
//
//  Deliberately performs the EXACT transaction readTouchEvent() (Buzzer.cpp)
//  uses at runtime - a register-pointer write to TD_STATUS (0x02) followed by
//  a one-byte read - rather than a bare address-ACK test. An address that
//  ACKs but will not serve a register read is precisely the documented
//  touch-lockout signature, so the probe exercises the whole path it is
//  standing in for, not just the first half of it.
//
//  The returned byte is discarded: only the transaction completing matters
//  here. Whatever the controller reports at boot (including the 0xD0 "never
//  touched since reset" value) is a valid answer for detection purposes.
// ============================================================
static bool probeTouchController() {
  Wire1.beginTransmission(I2C_FT5206);
  Wire1.write(0x02);
  if (Wire1.endTransmission(false) != 0) {
    return false;
  }
  if (Wire1.requestFrom((uint8_t)I2C_FT5206, (uint8_t)1) != 1) {
    return false;
  }
  Wire1.read();
  return true;
}

// ============================================================
//  SETUP
//
//  Opens with the optional-display detection gate. If no display is fitted,
//  NOTHING below the gate runs - no tft.begin(), no trend buffer init, no
//  static chrome - and displayDetected stays false for the whole session,
//  which is what every other display consumer keys off.
//
//  Uses the shared SENSOR_BEGIN_RETRY_MS window (Global.h) for consistency
//  with the six gated sensors, and it earns its keep here rather than being
//  cosmetic: this probe runs EARLIER in boot than the FT5x06 has ever been
//  addressed before. setupTFT() used to be the very first call in setup(),
//  ahead of Wire1.begin() entirely, so a controller still settling behind the
//  panel's own power-on-reset RC gets a fair chance rather than being written
//  off on a single failed transaction.
//
//  ORDERING NOTE (unchanged, but now load-bearing in a new way):
//  setupBuzzer() must still be called AFTER setupTFT() - TFT_eSPI initialises
//  LEDC internally. With no display fitted tft.begin() never runs, so that
//  side-effect never happens either. If the buzzer is ever found silent in a
//  headless build, that is the first place to look, and the fix is to hoist
//  tft.begin() above the gate rather than to reorder setup().
// ============================================================
void setupTFT() {
  uint32_t timeout = millis();
  while (millis() - timeout < SENSOR_BEGIN_RETRY_MS) {
    if (probeTouchController()) {
      displayDetected = true;
      break;
    }
    delay(SENSOR_BEGIN_RETRY_DELAY_MS);
  }

  if (!displayDetected) {
    Serial.println("TFT: Touch controller not found at 0x38 - display assumed NOT fitted");
    Serial.println("TFT: Running WITHOUT display (Home Assistant only).");
    Serial.println("TFT:   - graphics rendering disabled");
    Serial.println("TFT:   - touch input disabled");
    Serial.println("TFT:   - backlight control disabled");
    Serial.println("TFT:   - alarm snooze UNAVAILABLE - the buzzer cannot be silenced");
    Serial.println("TFT:   - SGP30 baseline reset UNAVAILABLE - see USER_GUIDE.md");
    return;
  }

  Serial.println("TFT: Touch controller found at 0x38 - display fitted");

  tft.begin();
  tft.setRotation(1);
  tft.fillScreen(COL_BG);
  pinMode(BACKLIGHT_PIN, OUTPUT);
  digitalWrite(BACKLIGHT_PIN, HIGH);

  trendCO2.init();  trendHCHO.init(); trendTVOC.init();
  trendPM25.init(); trendTemp.init(); trendHum.init();

  // Splash screen takes the dashboard's place until setup() finishes -
  // see "BOOT SPLASH SCREEN" in Graphics.h. drawStaticChrome() is now
  // called once, at the end of setup() in the .ino, to switch over.
  drawSplashScreen();
  debugLoop("TFT initialised");
}

// ============================================================
//  UPDATE DISPLAY
//
//  Single rendering gate for the whole module - every updateXxx() region
//  function is reached only from here, so one check covers all of them.
//
//  The per-region dirty flags are deliberately NOT cleared on the headless
//  path. The sensor modules that call setDirtyGauges()/setDisplayDirty()/etc
//  know nothing about whether a display is fitted, and should not have to -
//  leaving their flags latched costs nothing and keeps the gate in exactly
//  one place instead of scattered across every caller.
//
//  Side benefit: this also removes the regular ~100ms I2C block that
//  updateDisplay() is responsible for, for the whole headless session.
// ============================================================
void updateDisplay() {
  if (!displayDetected) {
    return;
  }

  unsigned long now = millis();
  static unsigned long lastGauges = 0;
  static unsigned long lastStatus = 0;
  static unsigned long lastParticles = 0;
  static unsigned long lastHeader = 0;
  static unsigned long lastOutside = 0;

  if (dirtyHeader && now - lastHeader >= MIN_DISPLAY_INTERVAL_MS) {
    updateHeader();
    dirtyHeader = false;
    lastHeader = now;
  }
  if (dirtyStatus && now - lastStatus >= MIN_DISPLAY_INTERVAL_MS) {
    updateStatusPanel();
    updateVentBlock();
    dirtyStatus = false;
    lastStatus = now;
  }
  if (dirtyGauges && now - lastGauges >= MIN_DISPLAY_INTERVAL_MS) {
    updateGauges();
    dirtyGauges = false;
    lastGauges = now;
  }
  if (dirtyParticles && now - lastParticles >= MIN_DISPLAY_INTERVAL_MS) {
    updateParticleBars();
    dirtyParticles = false;
    lastParticles = now;
  }
  if (dirtyOutside && now - lastOutside >= MIN_DISPLAY_INTERVAL_MS) {
    updateOutsideBlock();
    dirtyOutside = false;
    lastOutside = now;
  }

  static unsigned long lastLD2410 = 0;
  if (dirtyLD2410 && now - lastLD2410 >= MIN_DISPLAY_INTERVAL_MS) {
    updateLD2410Block();
    dirtyLD2410 = false;
    lastLD2410 = now;
  }
}