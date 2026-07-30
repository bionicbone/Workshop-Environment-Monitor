*****COMING VERY SOON, Expected released date is in the 1st week of August 2026*****
*****I just need to check the final PCB v1.2 in a live situation***** 

# Workshop Environment Monitor (WEM)

An open-source ESP32-S3 based air quality and environmental monitor, built for workshops where 3D printing, soldering, and other fume-producing work happens in an enclosed space.

WEM continuously measures CO2, TVOC, HCHO (formaldehyde), particulates, temperature and humidity, displays live readings on a 7" touchscreen (if fitted), and publishes everything to [Home Assistant](https://www.home-assistant.io/) over MQTT for logging (if available), automation and alerting.

This project exists because 3D printing ASA, ABS or PETG and soldering in a workshop produces measurable off-gassing and fumes — WEM was built to quantify that, and to benchmark a custom venting system against real data rather than guesswork.

![WEM 7" touchscreen display showing live sensor gauges](images/display-teaser.png)

---

## Quality, and modular

These two ideas pull against each other, and how WEM resolves that is the main thing worth understanding about the design.

**Quality first.** WEM uses good sensors rather than the cheapest ones that will fit the footprint. A monitor that reports comfortable numbers while the air is actually bad is worse than no monitor at all — you'd act on it. That rules out the bargain modules, and it means a full build isn't cheap.

**So the build is modular.** Every sensor is optional, and so is the touchscreen. WEM probes what's connected at boot and adapts: absent hardware is excluded from the display, from the alarms, and from Home Assistant, with no code changes, no build flags, and nothing to configure.

The practical upshot: **start with the sensors that matter most to you and add the rest as you can afford them.** A WEM with two sensors and no screen is a legitimate build, not a broken one — and adding a sensor later is a matter of plugging it in and restarting.

---

## Getting started

Everything you need is here: the firmware, the vendored libraries it builds against, the hardware design files, and a full [`USER_GUIDE.md`](USER_GUIDE.md) covering building, flashing, first boot and configuration.

**v1.0.0 is a compile-it-yourself release.** A compiled image has your WiFi and MQTT credentials baked into it, so there's no shared binary to download — you build the firmware from source against your own `secrets.h` and flash it to the ESP32-S3. [`USER_GUIDE.md`](USER_GUIDE.md) walks through the whole process, including the exact board settings and the vendored libraries you must build against.

A way to enter WiFi/MQTT details on the device at first boot is planned as a fast-follow — after which pre-built binaries can be published too.

---

## What it monitors

| Sensor | Measures | Interface |
|---|---|---|
| SGP30 | TVOC (total volatile organic compounds) | I2C |
| SFA40 | HCHO (formaldehyde) | UART |
| SCD40 | CO2 | I2C |
| SHT40 | Temperature / humidity (primary) | I2C |
| BME280 | Temperature / humidity (secondary, HA-only) | I2C |
| PMS5003 | Particulates — see below | UART |
| LD2410B | mmWave presence detection | UART |

### Particulates, in detail

The PMS5003 reports particulates two ways, and WEM publishes all nine channels to Home Assistant and to the display:

- **Particle counts** (cnt/0.1L) at **0.3, 0.5, 1.0, 2.5, 5.0 and 10 µm**
- **Mass concentrations** (µg/m³) at **PM1.0, PM2.5 and PM10**

**PM0.3 and PM0.5 are the ones to watch for 3D printing.** Ultrafine particles from a hot nozzle are numerous but individually almost weightless, so they can be present in large numbers while the mass figures still read close to zero. A monitor reporting only µg/m³ would tell you the air was fine. That's exactly the false-comfort problem the count channels exist to prevent — and a good illustration of why sensor choice matters more than sensor price.

Readings are shown on a 7" 800×480 SSD1963 TFT touchscreen (if fitted) with live gauges, and published to Home Assistant via [ArduinoHA](https://github.com/dawidchyrzynski/arduino-home-assistant) over MQTT.

---

## Running without a display

The touchscreen is optional on the same terms as the sensors — leave it off and WEM runs headless, reading every sensor and publishing to Home Assistant exactly as it otherwise would. It's the single biggest cost saving available, and a sensible build if you already live in the HA dashboard or you're mounting the unit somewhere nobody will look at it.

Two things to know before choosing that route: touch is currently WEM's only input, so a headless build **can't snooze an active alarm** and **can't clear the SGP30 calibration baseline**. Both will be exposed as Home Assistant buttons shortly after release. `USER_GUIDE.md` covers this fully.

---

## Repository structure

```
├── Workshop Environment Monitor/   Arduino sketch — the firmware source
├── hardware/                  PCB design files (Gerbers), STEP model, hardware README
│                              (attribution / Source Location notice), Bill of Materials,
│                              display manufacturer datasheet, breakout-board references
├── Libraries/                 Full vendored copies of every Arduino library WEM
│                              depends on, exactly as used to build and test it
├── stl for printing/          3D-printable enclosure and sensor mount STL files
├── images/                    Images used by the README, user guide and Bill of Materials
├── USER_GUIDE.md              Build, flash, first-boot and configuration guide
├── LICENSE                    Firmware licence (AGPL-3.0-or-later)
├── LICENSE-hardware.txt       Hardware licence (CERN-OHL-W-2.0)
├── THIRD_PARTY_LICENSES.md    Full attribution and licence text for every dependency
└── secrets.h.example          Template for your own WiFi/MQTT credentials
```

Copy `secrets.h.example` to `secrets.h` in the sketch folder and add your own WiFi/MQTT credentials before building — `secrets.h` is git-ignored and must never be committed. The user guide covers this.

---

## Building the PCB

Gerber files for the WEM main PCB are in [`hardware/`](hardware). To get boards made:

1. Download the Gerber `.zip` from `hardware/`.
2. Upload it directly to a fab house's quoting page — [JLCPCB](https://jlcpcb.com/).
3. Default settings (1.6mm thickness, HASL finish, 1oz copper) match what this design was built and tested with; nothing exotic is required.

Breakout-board module links and assembly photos are being put together for a launch video, for now see the BOM in the hardware folder or the pictures in the images folder.

---

## Licensing

- **Firmware**: [AGPL-3.0-or-later](LICENSE) — required because WEM links against ArduinoHA, which is itself AGPL-3.0. See [`THIRD_PARTY_LICENSES.md`](THIRD_PARTY_LICENSES.md) for the full rationale and every dependency's licence.
- **Hardware (PCB + enclosure)**: [CERN-OHL-W-2.0](LICENSE-hardware.txt).

Full attribution for every third-party library, with verbatim licence text, is in [`THIRD_PARTY_LICENSES.md`](THIRD_PARTY_LICENSES.md).

---

## Why "Workshop" Environment Monitor?

Built to answer a simple question: is my workshop air actually safe to breathe while I'm printing ABS or soldering, and does my venting setup actually help? WEM logs real numbers to Home Assistant so that question has a real answer instead of a guess.

---

## Acknowledgements

Built with [ArduinoHA](https://github.com/dawidchyrzynski/arduino-home-assistant), [TFT_eSPI](https://github.com/Bodmer/TFT_eSPI), and sensor libraries from Adafruit and Sensirion. Full credits in [`THIRD_PARTY_LICENSES.md`](THIRD_PARTY_LICENSES.md).
