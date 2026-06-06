# CLAUDE.md — ESP32 Marauder

## Project Overview

ESP32 Marauder is an Arduino/C++ firmware providing a suite of WiFi and Bluetooth offensive and defensive security tools for ESP32 microcontrollers. Version: **v1.12.1**. The firmware compiles for 19+ distinct hardware targets from a single codebase using preprocessor defines.

This is an embedded systems project — there is no host-side test suite. CI validates the codebase by compiling for all targets.

---

## Repository Layout

```
/
├── esp32_marauder/          # All firmware source (C++/Arduino)
│   ├── esp32_marauder.ino   # Arduino entry point (setup/loop)
│   ├── configs.h            # Master config: board targets, version, constants
│   ├── WiFiScan.cpp/h       # Core WiFi scanning and attack logic (~10k lines)
│   ├── MenuFunctions.cpp/h  # TFT UI menu system
│   ├── CommandLine.cpp/h    # Serial/BLE CLI
│   ├── Display.cpp/h        # TFT display abstraction
│   ├── EvilPortal.cpp/h     # Rogue AP / captive portal
│   ├── GpsInterface.cpp/h   # GPS module support
│   ├── SDInterface.cpp/h    # SD card logging
│   ├── settings.cpp/h       # Persistent settings (JSON)
│   ├── BatteryInterface.cpp/h
│   ├── LedInterface.cpp/h   # NeoPixel LED feedback
│   ├── User_Setup_*.h       # TFT_eSPI display configs (one per board)
│   ├── User_Setup_Select.h  # Selects which User_Setup_*.h to use
│   └── libraries/           # Git submodules (TFT_eSPI, NimBLE, LVGL, etc.)
├── libraries/               # Local library overrides (ESPAsyncWebServer, Adafruit_TCA8418)
├── FlashFiles/              # Pre-built binaries and flashing tools
├── PCBs/                    # Hardware schematics and PCB designs
├── bootloaders/             # Board-specific bootloader binaries
├── .github/workflows/       # CI/CD (build_parallel.yml, nightly_build.yml)
└── TestFile/TestFile.ino    # Minimal smoke-test sketch used by CI
```

---

## Hardware Targets

Each target is a preprocessor `#define` set at compile time. **Never hardcode hardware-specific logic without a corresponding `#ifdef` guard.**

| Define | Hardware |
|---|---|
| `MARAUDER_FLIPPER` | Flipper Zero WiFi Dev Board (ESP32-S2) |
| `MARAUDER_MULTIBOARD_S3` | Flipper Zero Multi Board S3 (ESP32-S3) |
| `MARAUDER_V4` | OG Marauder |
| `MARAUDER_V6` / `MARAUDER_V6_1` | Marauder v6 / v6.1 |
| `MARAUDER_V7` / `MARAUDER_V7_1` | Marauder v7 / v7.1 |
| `MARAUDER_KIT` | Marauder Kit |
| `MARAUDER_MINI` / `MARAUDER_MINI_V3` | Marauder Mini / Mini v3 |
| `MARAUDER_M5STICKC` | M5Stick-C Plus |
| `MARAUDER_M5STICKCP2` | M5Stick-C Plus2 |
| `MARAUDER_CARDPUTER` / `MARAUDER_CARDPUTER_ADV` | M5 Cardputer |
| `MARAUDER_CYD_MICRO` | CYD 2432S028 |
| `MARAUDER_CYD_2USB` | CYD 2432S028 (2 USB) |
| `MARAUDER_CYD_GUITION` | CYD 2432S024 GUITION |
| `MARAUDER_CYD_3_5_INCH` | CYD 3.5-inch |
| `MARAUDER_C5` | ESP32-C5 DevKitC-1 (5 GHz) |
| `MARAUDER_REV_FEATHER` | Adafruit Feather ESP32-S2 Reverse TFT |
| `MARAUDER_DEV_BOARD_PRO` | Dev Board Pro |
| `ESP32_LDDB` | ESP32 LDDB |
| `GENERIC_ESP32` | Generic ESP32 |

The active target is selected by uncommenting exactly one line in `configs.h`. In CI, it is injected via `--build-property build.extra_flags=-D<FLAG>`.

---

## Key Configuration: `configs.h`

This is the single source of truth for compile-time settings:

- `MARAUDER_VERSION` — version string (update here only)
- Board target `#define` blocks — also sets `HARDWARE_NAME`, pin assignments, display dimensions, feature flags
- `JSON_SETTING_SIZE` — ArduinoJson document size for settings
- `DISPLAY_BUFFER_LIMIT` — max lines in TFT scroll buffer
- `TRACK_EVICT_SEC` — seconds before a tracked MAC is tombstoned
- `DUAL_BAND_CHANNELS` — channel count for dual-band (C5) builds

When adding a new board: add a `#define` constant, a `HARDWARE_NAME` entry, and all pin/feature guards in `configs.h`. Then add a `User_Setup_*.h` if the board has a display, and add a matrix entry in `build_parallel.yml`.

---

## Build System

The project uses **Arduino CLI** — not PlatformIO or CMake.

### Local build (Arduino IDE / Arduino CLI)

1. Install Arduino CLI and the `esp32:esp32` core matching the target board (2.0.11 or 3.3.4).
2. Install all library dependencies (see `build_parallel.yml` for exact versions and install commands).
3. Uncomment exactly one board target in `esp32_marauder/configs.h`.
4. Copy the appropriate `User_Setup_<board>.h` content into `TFT_eSPI`'s `User_Setup.h` if the board has a display (`tft: true` in CI matrix).
5. Compile with Arduino CLI or open `esp32_marauder/esp32_marauder.ino` in Arduino IDE.

### CI (GitHub Actions)

- **`build_parallel.yml`** — triggers on push to `master`, tags, and PRs. Compiles all 19 targets in parallel using `ArminJo/arduino-test-compile@v3.3.0`. On manual trigger or tag, packages release artifacts.
- **`nightly_build.yml`** — runs daily at 03:00 UTC; only publishes if new commits exist since the last nightly release.
- **`close_stale.yml`** — manages stale issues.

CI is the only way to verify a change compiles across all targets. A change that breaks one target will fail the corresponding matrix job.

---

## Library Dependencies

### Git submodules (`esp32_marauder/libraries/`)
- `TFT_eSPI` v2.5.34 — TFT display driver (config via `User_Setup_*.h`)
- `NimBLE-Arduino` 1.3.8 / 2.3.8 — BLE stack (version depends on IDF)
- `lv_arduino` 3.0.0 — LVGL graphics
- `LinkedList` v1.3.3
- `JPEGDecoder` 1.8.0
- `Adafruit_NeoPixel` 1.12.0
- `ArduinoJson` v6.18.2

### Installed via Arduino CLI in CI
- AsyncTCP v3.4.8
- ESPAsyncWebServer v3.8.1 (from `bigbrodude6119/ESPAsyncWebServer`)
- MicroNMEA v2.0.6
- XPT2046_Touchscreen v1.4
- EspSoftwareSerial 8.1.0
- Adafruit_BusIO 1.15.0
- Adafruit_MAX1704X 1.0.2
- ESP32Ping

### Local overrides (`/libraries/`)
- `ESPAsyncWebServer` — local copy that takes precedence over installed version for some targets
- `Adafruit_TCA8418` — keyboard controller driver

---

## Code Conventions

### Preprocessor guards
Every piece of hardware-specific code must be wrapped:
```cpp
#ifdef MARAUDER_FLIPPER
  // flipper-only code
#endif
```
Feature guards (e.g., `HAS_SCREEN`, `HAS_GPS`, `HAS_SD`) are derived from board defines inside `configs.h` — use these rather than repeating board lists.

### File/class pattern
Each major subsystem is a `.cpp`/`.h` pair exposing a single global instance declared in the header and defined in the `.cpp`. The main sketch (`esp32_marauder.ino`) ties everything together via `setup()` and `loop()`.

### CLI commands (`CommandLine.cpp`)
New commands are registered by:
1. Adding a string match branch in `CommandLine::runCommand()`.
2. Implementing the handler — typically delegating to `WiFiScan`, `EvilPortal`, or another subsystem.
3. Adding the command to the help text.

### Settings (`settings.cpp`)
Persistent settings are serialized as JSON to SPIFFS/LittleFS. Use `settings.getSetting()` / `settings.setSetting()` — do not read/write flash directly.

### Display output
Use `Display::print()` and related helpers (not `Serial.print()`) for user-visible output so it appears on both the TFT and serial simultaneously where supported.

### No comments by default
Only add comments when behavior would surprise a reader — hidden constraints, workarounds for silicon errata, subtle invariants. Do not document what the code obviously does.

---

## Version Bumping

Update only `MARAUDER_VERSION` in `configs.h`. The CI workflow reads this value to name release artifacts.

---

## Flashing

- Pre-built binaries are in `FlashFiles/` (latest) and `Release Bins/` (historical).
- Flash address varies by chip: `0x1000` for ESP32/S2/S3, `0x2000` for ESP32-C5.
- Python flashing scripts for C5: `C5_Py_Flasher_for_v8/c5_flasher.py`.

---

## Branches and PRs

- `master` is the release branch — CI runs and artifacts are produced on pushes here.
- Feature work goes through PRs from forks or `develop` branches.
- Nightly builds publish pre-releases from `master` when new commits are present.
