# BluButton

ESP-IDF firmware for an open-source BLE button transmitter designed to be indistinguishable from a Shelly BLU Button from the point of view of BluButtonBridge.

## Read This First

- `AGENTS.md`
- `docs/WORKFLOW.md`
- `docs/COMPATIBILITY_NOTES.md`
- `docs/PAGES_INSTALLER_CONTRACT.md`
- `docs/BOOTSTRAP_PLAN.md`

## Current scope

This repository starts intentionally small:

- no web UI on the device
- no Wi-Fi or AP mode
- no OTA runtime flow on the device
- no MQTT or GPIO action engine
- USB-powered experimental phase first
- board-aware multi-target build setup from day zero
- browser-based installer hosted on GitHub Pages

The first goal is to prove the compatibility path:

- stable device identity
- persisted AES key
- persisted monotonic counter
- 4 button events
- BLE encrypted advertising compatible with the existing bridge receiver

## Wi-Fi Note

BluButton does not initialize or use Wi-Fi in the v0 runtime.
On ESP32-class targets, ESP-IDF 6.1 still forces `CONFIG_ESP_WIFI_ENABLED=y`
for `SOC_WIFI_SUPPORTED` chips in Kconfig, so some Wi-Fi and LWIP components
remain in the build graph even though the firmware does not provide any Wi-Fi
feature or control surface.

## Supported board profiles

- ESP32 DevKit V1
- ESP32-C3 SuperMini

Terms used in this repo:

- `board id`: canonical profile id from `config/boards.json`, for example `esp32c3-supermini`
- `board alias`: short token accepted by helper scripts, for example `esp32c3`
- `target`: the ESP-IDF chip target such as `esp32` or `esp32c3`

## Prerequisites

- ESP-IDF 6.1 or compatible environment export at `~/esp/esp-idf/export.sh`
- `jq`
- `esptool`
- a USB data cable
- `ESPPORT=/dev/...` when flashing or opening the monitor

Use the helper script so each target keeps its own `sdkconfig` and build directory:

```bash
source ~/esp/esp-idf/export.sh
scripts/idf-target.sh esp32 build
scripts/idf-target.sh esp32c3 build
```

You can also use the board aliases:

```bash
scripts/idf-target.sh esp32-devkit-v1 build
scripts/idf-target.sh esp32c3-supermini build
```

For flashing and serial monitoring, provide the port explicitly:

```bash
source ~/esp/esp-idf/export.sh
ESPPORT=/dev/cu.usbmodem3101 scripts/idf-target.sh esp32c3-supermini flash
ESPPORT=/dev/cu.usbmodem3101 scripts/idf-target.sh esp32c3-supermini monitor
```

## Browser installer

The repository includes a GitHub Pages installer based on `ESP Web Tools`.

- Expected installer site: `https://robertoamd90.github.io/blu-button/`
- Installer source: `site/index.html`
- Shared board catalog: `config/boards.json`
- Pages workflow: `.github/workflows/pages.yml`

The browser installer flashes the mirrored board-specific split install set:

- `bootloader.bin`
- `partition-table.bin`
- `ota-data.bin`
- app image

using the board-specific offsets from `config/boards.json`.
That keeps ordinary browser reinstalls aligned with the NVS persistence contract.
When the installer offers a clean-erase choice, that path is for explicit reprovisioning, not the default reflash flow.

Each release should include the board-specific full images for manual recovery and release completeness:

- `BluButton-esp32-devkit-v1-full.bin`
- `BluButton-esp32c3-supermini-full.bin`

For local release packaging from existing build output:

```bash
source ~/esp/esp-idf/export.sh
scripts/package-release.sh all
```

That script also produces the board-specific app images:

- `BluButton-esp32-devkit-v1.bin`
- `BluButton-esp32c3-supermini.bin`

The packaging step also emits the split artifacts used by the browser installer:

- `BluButton-<board>-bootloader.bin`
- `BluButton-<board>-partition-table.bin`
- `BluButton-<board>-ota-data.bin`

The merged `full.bin` remains useful for manual recovery and low-level reprovisioning, but it is not the default browser-install path.

## Prototype maintenance model

In the current phase, the maintenance surface is intentionally simple:

- on first boot, the device generates and stores an AES key if one is not already present
- the serial log is expected to show the device identity and registration credentials
- a 10-second hold is reserved for local maintenance behavior such as reprinting credentials

After flashing, open a serial monitor at `115200` baud. From this repo the simplest path is:

```bash
source ~/esp/esp-idf/export.sh
ESPPORT=/dev/cu.usbmodem3101 scripts/idf-target.sh esp32c3-supermini monitor
```

## What Was Ported From BluButtonBridge

- board catalog in `config/boards.json`
- target helper scripts in `scripts/`
- per-target `sdkconfig` profiles in `configs/`
- board profile abstraction in `components/board_config/`
- version derivation from `git describe`
- GitHub Pages browser installer in `site/`
- Pages deployment workflow in `.github/workflows/pages.yml`

## What Was Intentionally Not Ported

- web UI hosted by the device
- Wi-Fi / AP runtime
- MQTT manager
- OTA manager and staged OTA flow
- console SSE plumbing
- bridge-side BLE receiver implementation

## Documentation

- `docs/WORKFLOW.md`: development cycle and multi-agent review expectations
- `docs/BOOTSTRAP_PLAN.md`: current v0 status, validation gaps, and likely next slices
- `docs/COMPATIBILITY_NOTES.md`: BLE compatibility contract toward BluButtonBridge and generic BTHome listeners
- `docs/PAGES_INSTALLER_CONTRACT.md`: browser installer and Pages deploy contract
