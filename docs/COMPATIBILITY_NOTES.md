# Compatibility Notes

BluButton should be indistinguishable from a Shelly BLU Button to BluButtonBridge.

That implies at least:

- compatible encrypted BTHome service-data layout
- stable MAC address across boots
- per-device AES key persistence
- strictly increasing counter persistence
- support for these button events:
  - `press`
  - `double_press`
  - `triple_press`
  - `long_press`

## Hard compatibility requirements

- The runtime path must emit encrypted BTHome advertisements only.
- The anti-replay counter must survive reboots and must never be reset as part of normal upgrades.
- The browser installer must not erase or overwrite the NVS partition during ordinary reinstalls.
- A full erase may exist as an explicit user-chosen recovery action, but it must never be the silent default browser path.
- A first boot with empty NVS may generate and persist a new AES key exactly once.
- The maintenance path may reprint credentials, but it must not silently rotate keys or counters.
- Maintenance-time AES-key extraction may read the persisted `identity:aes_key` value from NVS, but it must not modify the stored key or counter state.

## Identity model

- Bluetooth identity is anchored to the board's stable BT MAC address.
- Registration credentials are `MAC + AES key`.
- If NVS is wiped on purpose, the device becomes a new logical transmitter and must be re-registered.

## Consumer expectations

BluButton is optimized first for BluButtonBridge, but the emitted payload should also remain sensible for generic BTHome consumers such as Home Assistant:

- single-button payload shape
- standard BTHome button event object
- no dependency on Shelly branding strings for the event semantics to remain useful

## Local-only behavior

The following behavior is intentionally local to BluButton and must not leak into the BLE compatibility contract:

- LED blink feedback
- 10-second credential reprint maintenance hold
- serial-only maintenance output
- script-based AES-key extraction from NVS over the bootloader serial path

Maintenance behavior such as a 10-second hold to reprint credentials is local-only and must not interfere with the normal user-event contract.
