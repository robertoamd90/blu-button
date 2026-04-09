# Current State And Next Steps

This document is no longer just a bootstrap wishlist. It tracks what already shipped in the current v0 branch, what was intentionally left out, and what still deserves follow-up before a broader release.

## Foundation Carried From BluButtonBridge

These pieces are worth carrying over from BluButtonBridge immediately:

- `config/boards.json`
  - single source of truth for board ids, aliases, targets, and build directories
- `scripts/boards-lib.sh`
  - lightweight board resolution helper around the catalog
- `scripts/idf-target.sh`
  - stable per-board build/flash workflow
- `configs/sdkconfig.*`
  - practical starting point for dual-target ESP-IDF builds
- `components/board_config/`
  - keeps BOOT button and LED mappings out of shared logic
- `site/`
  - reusable browser installer surface that does not affect battery-oriented runtime design
- `.github/workflows/pages.yml`
  - publishes the installer and mirrored split-image bundle to GitHub Pages
- `scripts/package-release.sh`
  - emits board-specific split-image assets and manual recovery full images for releases
- `docs/PAGES_INSTALLER_CONTRACT.md`
  - keeps the Pages flow and installer assumptions explicit
- root `CMakeLists.txt`
  - preserves the same versioning behavior via `git describe`

## Still Intentionally Excluded

These belong to the bridge product, not the button transmitter MVP:

- web manager
- Wi-Fi manager
- MQTT manager
- OTA manager
- console manager
- bridge-specific BLE receiver/registration flow
- backup/restore schema and HTTP API contracts

## Current v0 Runtime

The bootstrap phase has now been folded into a small working runtime:

1. `components/device_identity/`
   - generates the AES key on first boot
   - persists the AES key in NVS
   - formats the key for serial output

2. `components/button_event/`
   - defines the shared button-event taxonomy used across input, feedback, and BLE transmission
   - keeps the neutral event contract separate from the GPIO implementation

3. `components/gpio_manager/`
   - reuses the board wiring abstraction from `board_config`
   - provides BOOT-button state, wake detection, gesture capture, and wake-source configuration helpers
   - captures a short post-wake gesture session for single, double, triple, long, and 10-second maintenance hold
   - keeps the gesture classifier separate from downstream BLE and LED behavior

4. `components/led_feedback/`
   - drives deterministic LED feedback patterns

5. `components/ble_button_tx/`
   - initializes NimBLE in broadcaster-only mode
   - allows BLE startup to overlap with gesture capture so wake-to-advertise latency stays bounded
   - persists and advances the anti-replay counter
   - emits encrypted BTHome service data with Shelly-compatible button events

6. `components/system_runtime/`
   - composes identity, GPIO, LED feedback, BLE transmission, and the deep-sleep lifecycle
   - prints `MAC + AES key` on first boot
   - reprints registration credentials on a 10-second hold
   - degrades to warning-and-sleep if BLE startup or event delivery fails during a wake session
   - returns to deep sleep after each wake-driven interaction

## Remaining v0 Validation

- flash and test both boards on hardware
- verify that `bbb` accepts the advertisements without bridge-side changes
- compare event timing against a real Shelly BLU Button and tune thresholds if needed
- measure wake-to-advertise timing and deep-sleep current on real hardware
- validate that borderline long-press and multi-click gestures still feel repeatable after the wake transition
- decide whether to add battery telemetry placeholders in the next iteration

## Likely next slices after review

- trim or document any remaining ESP-IDF config noise that does not affect runtime behavior
- decide whether the replay counter persistence needs additional wear-leveling strategy
- evaluate whether a multi-button sibling profile belongs in this repo or in a separate product line

## Hidden Compatibility Risks To Design For Early

- the bridge accepts only encrypted BTHome advertisements
- the bridge expects a strictly increasing counter per device
- MAC address stability matters because bridge registration keys off device identity
- event timing should be chosen with enough care that user gestures are repeatable across boards
