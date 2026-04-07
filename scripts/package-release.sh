#!/usr/bin/env bash
set -euo pipefail

root_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
ROOT_DIR="$root_dir"
source "$root_dir/scripts/boards-lib.sh"

usage() {
    local board_tokens=""
    if command -v jq >/dev/null 2>&1; then
        board_tokens="$(boards_list_tokens | sed 's/^/  /')"
    else
        board_tokens="  jq is required to list supported board tokens"
    fi

    cat <<'USAGE'
Usage: scripts/package-release.sh <board|all>

Boards:
USAGE
    printf '%s\n' "$board_tokens"
    cat <<'USAGE'
  all

Outputs:
  dist/BluButton-<board>.bin
  dist/BluButton-<board>-bootloader.bin
  dist/BluButton-<board>-partition-table.bin
  dist/BluButton-<board>-ota-data.bin
  dist/BluButton-<board>-full.bin
USAGE
}

if [[ $# -ne 1 ]]; then
    usage
    exit 1
fi

dist_dir="$root_dir/dist"

package_board() {
    local board="$1"
    local board_json=""
    local board_id=""
    local build_dir=""
    local chip=""
    local full_name=""
    local app_name=""
    local bootloader_name=""
    local partition_name=""
    local otadata_name=""

    if ! board_json="$(boards_resolve_json "$board")"; then
        echo "Unknown board: $board" >&2
        usage
        exit 1
    fi

    board_id="$(jq -r '.id' <<<"$board_json")"
    build_dir="$root_dir/$(jq -r '.build_dir' <<<"$board_json")"
    chip="$(jq -r '.chip' <<<"$board_json")"
    app_name="$(jq -r '.install_parts[] | select(.kind == "app") | .asset_name' <<<"$board_json")"
    bootloader_name="$(jq -r '.install_parts[] | select(.kind == "bootloader") | .asset_name' <<<"$board_json")"
    partition_name="$(jq -r '.install_parts[] | select(.kind == "partition-table") | .asset_name' <<<"$board_json")"
    otadata_name="$(jq -r '.install_parts[] | select(.kind == "ota-data") | .asset_name' <<<"$board_json")"
    full_name="$(jq -r '.full_asset_name' <<<"$board_json")"

    if [[ ! -f "$build_dir/BluButton.bin" ]]; then
        echo "Missing firmware binary in $build_dir. Run the build first." >&2
        exit 1
    fi
    if [[ ! -f "$build_dir/flash_args" ]]; then
        echo "Missing flash_args in $build_dir. Run the build first." >&2
        exit 1
    fi

    mkdir -p "$dist_dir"

    local app_out="$dist_dir/$app_name"
    local bootloader_out="$dist_dir/$bootloader_name"
    local partition_out="$dist_dir/$partition_name"
    local otadata_out="$dist_dir/$otadata_name"
    local full_out="$dist_dir/$full_name"

    cp "$build_dir/BluButton.bin" "$app_out"
    cp "$build_dir/bootloader/bootloader.bin" "$bootloader_out"
    cp "$build_dir/partition_table/partition-table.bin" "$partition_out"
    cp "$build_dir/ota_data_initial.bin" "$otadata_out"

    local merge_args=()
    read -r -a merge_args <<< "$(head -n 1 "$build_dir/flash_args")"
    while read -r addr file; do
        [[ -n "${addr:-}" ]] || continue
        merge_args+=("$addr" "$file")
    done < <(tail -n +2 "$build_dir/flash_args")

    (
        cd "$build_dir"
        esptool --chip "$chip" merge-bin -o "$full_out" "${merge_args[@]}"
    )

    echo "Packaged $board_id"
    shasum -a 256 "$app_out" "$bootloader_out" "$partition_out" "$otadata_out" "$full_out"
}

case "$1" in
    all)
        while IFS= read -r board_id; do
            package_board "$board_id"
        done < <(boards_list_ids)
        ;;
    *)
        package_board "$1"
        ;;
esac
