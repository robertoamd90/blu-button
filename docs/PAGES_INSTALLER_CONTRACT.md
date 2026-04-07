# Pages Installer Contract

This document captures the contract between GitHub release assets, the GitHub Pages workflow, and the browser installer site for BluButton.

## Scope

Relevant files:

- `site/index.html`
- `site/app.js`
- `site/styles.css`
- `config/boards.json`
- `.github/workflows/pages.yml`
- `scripts/package-release.sh`
- `README.md`

This contract is about the public installer site served from GitHub Pages.

## Board catalog source of truth

Board identity and release asset naming are centralized in:

- `config/boards.json`

For each board, the catalog defines:

- `id`
- `aliases`
- `target`
- `chip_family`
- `build_dir`
- `full_asset_name`
- `install_parts[]`
  - auxiliary release artifacts used outside the default browser-install path

That catalog drives:

- installer board choices and chip-family metadata in `site/app.js`
- installer manifest generation in `.github/workflows/pages.yml`
- release asset download and mirrored metadata generation in `.github/workflows/pages.yml`
- build/profile aliases in local helper scripts
- packaging output names in `scripts/package-release.sh`

The Pages workflow publishes that shared catalog into the site root as `./boards.json` so the browser installer can load it without depending on repository layout.

## Published URL

Expected installer URL:

- `https://robertoamd90.github.io/blu-button/`

The README should link to that URL directly.

## Release asset contract

Expected release assets per board:

- bootloader image
- partition table image
- OTA data image
- app image
- merged full image

For the currently supported boards that means:

- `BluButton-esp32-devkit-v1-bootloader.bin`
- `BluButton-esp32-devkit-v1-partition-table.bin`
- `BluButton-esp32-devkit-v1-ota-data.bin`
- `BluButton-esp32-devkit-v1.bin`
- `BluButton-esp32-devkit-v1-full.bin`
- `BluButton-esp32c3-supermini-bootloader.bin`
- `BluButton-esp32c3-supermini-partition-table.bin`
- `BluButton-esp32c3-supermini-ota-data.bin`
- `BluButton-esp32c3-supermini.bin`
- `BluButton-esp32c3-supermini-full.bin`

Meaning of the assets:

- the merged `full.bin` is the normal browser-install path
- the split-image assets remain available as auxiliary release artifacts

## Safety boundary

The browser installer should flash the board-specific merged `full.bin` from offset `0`
without requesting a whole-device erase for ordinary reinstalls.

That keeps the browser path aligned with the compatibility contract:

- the merged image remains the normal install artifact
- persisted NVS state such as AES key and anti-replay counter should survive ordinary reinstalls
- a deliberate wipe is still a separate recovery or reprovisioning action, not the default browser flow

## Pages workflow contract

Current workflow behavior in `.github/workflows/pages.yml`:

- triggers on GitHub `release.published`
- can also be run manually with a release tag via `workflow_dispatch`
- checks out the repository
- copies `site/` into `_site/`
- reads `config/boards.json`
- verifies that every declared board `full_asset_name` exists in the selected release and exposes a SHA-256 digest plus download URL
- downloads every declared board `full_asset_name` into `_site/firmware/`
- writes one per-board `esp-web-tools` manifest into `_site/firmware/`
- copies the shared board catalog to `_site/boards.json`
- writes mirror metadata to `_site/firmware/metadata.json`
- uploads `_site/` as the Pages artifact
- deploys that artifact to GitHub Pages

Expected published payload includes:

- site HTML/CSS/JS from `site/`
- mirrored full images at `./firmware/<full_asset_name>`
- mirrored per-board manifests at `./firmware/<board-id>-manifest.json`
- mirror metadata at `./firmware/metadata.json`

## Mirror metadata contract

Expected shape of `./firmware/metadata.json`:

```json
{
  "tag": "v0.1.0",
  "published_at": "2026-04-07T12:00:00Z",
  "html_url": "https://github.com/owner/repo/releases/tag/v0.1.0",
  "assets": {
    "esp32c3-supermini": {
      "manifest_path": "./firmware/esp32c3-supermini-manifest.json",
      "full_image": {
        "asset_name": "BluButton-esp32c3-supermini-full.bin",
        "asset_size": 4194304,
        "asset_sha256": "sha256:...",
        "browser_download_url": "https://github.com/..."
      }
    }
  }
}
```

Requirements:

- `manifest_path` must point at the prebuilt same-origin manifest for that board
- `full_image` must be present for every supported board

## Browser-side manifest contract

Current `site/app.js` behavior:

- loads board definitions from `./boards.json`
- loads mirrored metadata from `./firmware/metadata.json`
- falls back to `../config/boards.json` for local source-tree previews
- reads the prevalidated manifest path from mirror metadata
- points the install button at the same-origin per-board manifest published by the Pages workflow
- keeps the install button hidden until both board catalog and mirror metadata are loaded

Manifest requirements:

- `new_install_prompt_erase` must remain `false`
- `parts[]` must contain exactly one entry
- that entry must point at the mirrored board-specific `full.bin`
- the offset must remain `0`

## Browser behavior

Success path:

- board catalog loads
- mirror metadata loads
- the selected board entry includes `full_image` metadata and a manifest path
- the install button becomes visible

Failure path:

- the install button stays hidden
- the UI explains what metadata is missing
- the manual release link remains available

## Design boundary

This installer is intentionally external to the device runtime.

It must not imply that BluButton itself provides:

- a hosted web UI
- Wi-Fi onboarding
- AP mode
- OTA update logic running on the device

The browser installer is only a release-distribution surface for flashing firmware images.

## Change guardrails

Before changing the installer flow, verify:

- whether `config/boards.json` still matches the supported boards and release asset names
- whether `scripts/package-release.sh` still emits each board `full.bin`
- whether the workflow still mirrors the full images into `./firmware/`
- whether the workflow still writes matching metadata into `./firmware/metadata.json`
- whether the browser manifest still points at the same-origin mirrored full image
- whether README links and user-facing instructions still match the deployed site

If you change:

- the release asset name
- the mirrored firmware path
- the published Pages URL
- the mirrored manifest layout
- the supported board list

update all relevant copies in:

- this document
- `README.md`
- `site/index.html`
- `site/app.js`
- `config/boards.json`
- `scripts/package-release.sh`
- `.github/workflows/pages.yml`

## Validation hints

When touching the Pages installer flow, validate at least:

- `site/` still renders with no broken local asset references
- `node --check site/app.js`
- `bash -n scripts/package-release.sh`
- the install button manifest points at the selected board's mirrored full image
- the happy path works when the selected board metadata includes a manifest path and `full_image`
- the missing-metadata path hides the install button and points manual download to the release page or recovery asset
