# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

## [1.1.0] - 2026-09-20

### Added

- SGP30 humidity compensation — the sensor's eCO2/TVOC readings are now
  corrected against live temperature/humidity data, rather than running on
  the chip's fixed internal default.
- Two new Home Assistant sensors, `SGP30_eCO2_Baseline` and
  `SGP30_TVOC_Baseline`, exposing the sensor's saved calibration baseline
  so it can be inspected and tracked over time.

### Fixed

- SGP30 baseline sensors in Home Assistant could continue showing stale,
  pre-reset values after clearing the sensor's calibration — a cleared
  baseline is now correctly reflected in Home Assistant.

## [1.0.0] - 2026-07-30

Initial public release.

### Added

- ESP32-S3 firmware for the Workshop Environment Monitor: a 7" TFT
  touchscreen dashboard with Home Assistant MQTT integration.
- Multi-sensor environmental monitoring — VOC/eCO2 (SGP30), CO2 (SCD40),
  HCHO (SFA40), particulates (PMS5003), temperature/humidity (SHT40
  primary, BME280 secondary), and mmWave presence (LD2410B).
- Every sensor is optional and independently detected at boot — an
  unfitted or unresponsive sensor is cleanly excluded from the display,
  Home Assistant, and alarm logic rather than showing stale or invalid
  data.
- Configurable alarm thresholds (residential defaults) with hysteresis,
  and touchscreen snooze.
- SGP30 baseline management — automatic save/restore with a two-tier
  warm-up, and a touchscreen long-press procedure for resetting the
  baseline (e.g. after replacing the sensor).
- Home Assistant long-term statistics support across all numeric
  entities.
- Backlight sleep with presence-based wake, to reduce display burn-in
  risk.
- Headless (no display fitted) operation supported.
- Full hardware design released alongside the firmware — PCB Gerbers,
  enclosure/sensor-mount STL files, and a bill of materials.

[Unreleased]: https://github.com/bionicbone/Workshop-Environment-Monitor/compare/v1.1.0...HEAD
[1.1.0]: https://github.com/bionicbone/Workshop-Environment-Monitor/releases/tag/v1.1.0
[1.0.0]: https://github.com/bionicbone/Workshop-Environment-Monitor/releases/tag/v1.0.0
