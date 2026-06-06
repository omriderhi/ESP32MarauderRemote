# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

ESP32 Marauder is an Arduino-based firmware (C++) for the ESP32 microcontroller providing WiFi/Bluetooth scanning, packet capture, and attack tools. It compiles to a `.bin` flash image targeting 20+ different ESP32 hardware variants via compile-time preprocessor flags.

## Build System

This project uses **Arduino CLI** — there is no `platformio.ini` or `CMakeLists.txt`. The sketch entry point is `esp32_marauder/esp32_marauder.ino`.

### Local compilation (Arduino CLI)

```bash
# Install Arduino CLI first, then install the ESP32 core
arduino-cli core install esp32:esp32@3.3.4

# Install required libraries (mirrors what CI does)
# ESP32Ping, AsyncTCP, MicroNMEA, ESPAsyncWebServer, TFT_eSPI, XPT2046_Touchscreen

# Compile for a specific target (example: OG Marauder v4)
arduino-cli compile \
  -b esp32:esp32:d32:PartitionScheme=min_spiffs \
  --build-property "compiler.cpp.extra_flags=-DMARAUDER_V4" \
  esp32_marauder/esp32_marauder.ino
```

### Selecting a hardware target

Before compiling, uncomment exactly one board define in `esp32_marauder/configs.h` (lines 12–38). All other targets must remain commented out:

```cpp
// In configs.h, uncomment ONE of:
#define MARAUDER_V4
// #define MARAUDER_MINI
// #define MARAUDER_CARDPUTER
// ... etc.
```

For boards with a TFT display (`tft: true` in the CI matrix), copy the matching `User_Setup_*.h` file from the repo root into the `TFT_eSPI` library directory as `User_Setup.h` before compiling. The mapping is in `.github/workflows/build_parallel.yml`.

### Flashing

```bash
esptool.py write_flash -z -fm dio -fs detect 0x1000 firmware.bin
# ESP32-C5 uses address 0x2000 instead
```

### CI

GitHub Actions (`.github/workflows/build_parallel.yml`) compiles all 20+ targets in parallel. Each matrix entry specifies its board FQBN, compiler flag, TFT setup file, ESP-IDF version (`2.0.11` or `3.3.4`), and NimBLE version (`1.3.8` or `2.3.8`). There are no automated tests beyond successful compilation.

## Architecture

### Main loop structure (`esp32_marauder.ino`)

`setup()` initializes all subsystems in order: serial → display → GPS → settings (SPIFFS) → WiFiScan → EvilPortal → **RemoteServer** → Battery → LED → GPS → MenuFunctions → CommandLine.

`loop()` calls `.main(currentTime)` on each subsystem every iteration (~1000 Hz with display, ~20 Hz headless):

```
cli_obj.main()              → parse & execute serial commands
remote_server_obj.main()    → restore AP after scan stops
wifi_scan_obj.main()        → core WiFi/BT engine (scanning, attacks, captures)
gps_obj.main()              → read NMEA from UART
buffer_obj.save()           → flush PCAP frames to SD card
battery_obj.main()          → ADC battery level
menu_function_obj.main()    → TFT display rendering & button input
led_obj.main()              → NeoPixel/LED state
```

### Module responsibilities

| Module | Files | Role |
|--------|-------|------|
| **WiFiScan** | `WiFiScan.cpp/h` (~10K lines) | Core engine: promiscuous capture, frame injection, BLE scanning/spoofing, all scan/attack state machines |
| **RemoteServer** | `RemoteServer.cpp/h` | WiFi AP + REST API on port 8080; bridges CLI commands to HTTP clients |
| **MenuFunctions** | `MenuFunctions.cpp/h` | TFT menu tree rendering, button/touch input, calls into WiFiScan on user action |
| **CommandLine** | `CommandLine.cpp/h` | Serial CLI: parses text commands, maps to WiFiScan/Settings methods |
| **Display** | `Display.cpp/h` | Low-level TFT draw primitives used by MenuFunctions |
| **EvilPortal** | `EvilPortal.cpp/h` | Async web server that serves a captive portal; started/stopped by WiFiScan |
| **Buffer** | `Buffer.cpp/h` | Double-buffered async SD writes for PCAP/GPX/LOG output |
| **Settings** | `settings.cpp/h` | SPIFFS JSON persistence; values cached in memory after first load |
| **GpsInterface** | `GpsInterface.cpp/h` | UART GPS, MicroNMEA parsing, wardriving GPX export |

### Key data flow

WiFiScan populates shared `LinkedList<T>*` structures (`access_points`, `stations`, `airtags`, `ssids`, `probe_req_ssids`) that MenuFunctions and CommandLine read for display and output. Raw captured frames are pushed into Buffer's queue and flushed to SD asynchronously to avoid blocking the main loop.

### Hardware abstraction

Everything is gated by `#ifdef` on capability flags that `configs.h` derives from the selected board target:

```
HAS_SCREEN, HAS_BT, HAS_GPS, HAS_SD, HAS_BATTERY, HAS_PSRAM,
HAS_NEOPIXEL_LED, HAS_FLIPPER_LED, USE_SD, MARAUDER_MINI_SCREEN, ...
```

Never add runtime feature detection — all hardware differences are resolved at compile time through these flags.

### Scan/attack mode constants

`WiFiScan.h` defines 80+ integer constants (`WIFI_SCAN_OFF = 0`, `WIFI_SCAN_PROBE = 1`, `WIFI_ATTACK_DEAUTH = 20`, etc.) that drive the state machine inside `WiFiScan::main()`. When adding a new scan or attack mode, assign the next available integer and add corresponding cases to the state machine.

### Settings

Read/write persistent settings via the typed template API:

```cpp
Settings::loadSetting<bool>("SavePCAP");
Settings::saveSetting<String>("ClientSSID", value);
```

Settings are lazy-loaded from SPIFFS JSON on first access and cached. `JSON_SETTING_SIZE` (2048 bytes) in `configs.h` controls the JSON document size.

### RemoteServer — WiFi AP + REST API

`RemoteServer` starts a WPA2 soft-AP on boot and runs an `AsyncWebServer` on port **8080**. Credentials and the API token are stored in NVS via `Preferences` (namespace `remote_ap`; keys `ssid`, `pw`, `token`). Defaults: SSID=`MarauderAP`, PW=`marauder1`, token=`marauder`.

All REST endpoints require the header `X-API-Key: <token>`.

| Method | Path | Description |
|--------|------|-------------|
| GET | `/api/status` | Current scan mode, scanning flag, AP clients, free heap |
| GET | `/api/aps` | Discovered access points (up to 15) |
| GET | `/api/stations` | Associated stations (up to 15) |
| GET | `/api/ssids` | Custom SSID list used for beacon/probe attacks |
| GET | `/api/probes` | Captured probe-request SSIDs |
| POST | `/api/cmd` | Execute any CLI command — body: `{"cmd": "sniffbeacon"}` |
| POST | `/api/stopscan` | Stop the current scan/attack |
| GET | `/api/apconfig` | Read stored AP SSID and password |
| POST | `/api/apconfig` | Update AP SSID/password/token — body: `{"ssid":"…","pw":"…","token":"…"}` |

**Single-radio constraint**: The ESP32 has one WiFi radio. When WiFiScan starts any scan or attack it takes full control of the radio, disrupting the AP. The AP is automatically restored in `RemoteServer::main()` once the scan mode returns to `WIFI_SCAN_OFF` and the radio is released (`wifi_scan_obj.wifi_initialized == false`). There is a 5-second cooldown between restart attempts.

**EvilPortal conflict**: EvilPortal (`WIFI_SCAN_EVIL_PORTAL`) creates its own AP and overwrites RemoteServer's. This is expected — EvilPortal and RemoteServer cannot run simultaneously because the ESP32 supports only one soft-AP.

**Credential persistence**: RemoteServer stores its own credentials in NVS (Preferences), separate from the existing SPIFFS-based Settings JSON. Do not add RemoteServer keys to the Settings class.

## Conventions

- **Naming**: Classes `PascalCase`, methods `camelCase`, constants and preprocessor flags `SCREAMING_SNAKE_CASE`
- **No subdirectories** under `esp32_marauder/` — all `.cpp`/`.h` source files are flat in that directory
- **Libraries** live in `esp32_marauder/libraries/` (bundled submodules: LinkedList, TFT_eSPI fork, NimBLE-Arduino, ArduinoJson, etc.)
- **SPIFFS data** (startup JPEG images) lives in `esp32_marauder/data/`
- **Hardware design files** (Eagle PCB, 3D models) live in `PCBs/` and `mechanical/` — not built by Arduino CLI
- **Version string** is `MARAUDER_VERSION` in `configs.h`; update it there only
