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

    cat <<'EOF'
Usage: scripts/idf-target.sh <target> <action> [extra idf.py args...]

Targets:
EOF
    printf '%s\n' "$board_tokens"
    cat <<'EOF'

Actions:
  build
  flash
  monitor
  size
  menuconfig
  reconfigure
  clean
  fullclean

Examples:
  scripts/idf-target.sh esp32 build
  scripts/idf-target.sh esp32 flash
  scripts/idf-target.sh esp32c3-supermini build
  scripts/idf-target.sh xiao-esp32-c3 build
  ESPPORT=/dev/cu.usbmodem123 scripts/idf-target.sh xiao-esp32-c3 flash
EOF
}

if [[ $# -lt 2 ]]; then
    usage
    exit 1
fi

board="$1"
action="$2"
shift 2

if ! board_json="$(boards_resolve_json "$board")"; then
    echo "Unknown target: $board" >&2
    usage
    exit 1
fi

idf_target="$(jq -r '.target' <<<"$board_json")"
build_dir="$root_dir/$(jq -r '.build_dir' <<<"$board_json")"
sdkconfig_path="$build_dir/sdkconfig"
sdkconfig_defaults_arg=""

while IFS= read -r sdkconfig_default; do
    [[ -n "$sdkconfig_default" ]] || continue

    if [[ "$sdkconfig_default" = /* ]]; then
        sdkconfig_default_path="$sdkconfig_default"
    else
        sdkconfig_default_path="$root_dir/$sdkconfig_default"
    fi

    if [[ ! -f "$sdkconfig_default_path" ]]; then
        echo "Missing sdkconfig defaults file: $sdkconfig_default_path" >&2
        exit 1
    fi

    if [[ -n "$sdkconfig_defaults_arg" ]]; then
        sdkconfig_defaults_arg+=";"
    fi
    sdkconfig_defaults_arg+="$sdkconfig_default_path"
done < <(jq -r '.sdkconfig_defaults[]' <<<"$board_json")

if [[ -z "$sdkconfig_defaults_arg" ]]; then
    echo "Board catalog entry must define sdkconfig_defaults[]: $board" >&2
    exit 1
fi

idf_args=(
    -B "$build_dir"
    "-DSDKCONFIG=$sdkconfig_path"
    "-DSDKCONFIG_DEFAULTS=$sdkconfig_defaults_arg"
    "-DIDF_TARGET=$idf_target"
)

case "$action" in
    build)
        exec idf.py "${idf_args[@]}" build "$@"
        ;;
    flash)
        if [[ -z "${ESPPORT:-}" ]]; then
            echo "ESPPORT is required for flash. Example: ESPPORT=/dev/cu.usbmodem3101 scripts/idf-target.sh $board flash" >&2
            exit 1
        fi
        exec idf.py "${idf_args[@]}" -p "$ESPPORT" build flash "$@"
        ;;
    monitor)
        if [[ -z "${ESPPORT:-}" ]]; then
            echo "ESPPORT is required for monitor. Example: ESPPORT=/dev/cu.usbmodem3101 scripts/idf-target.sh $board monitor" >&2
            exit 1
        fi
        exec idf.py "${idf_args[@]}" -p "$ESPPORT" monitor "$@"
        ;;
    size)
        exec idf.py "${idf_args[@]}" size "$@"
        ;;
    menuconfig)
        exec idf.py "${idf_args[@]}" menuconfig "$@"
        ;;
    reconfigure)
        exec idf.py "${idf_args[@]}" reconfigure "$@"
        ;;
    clean)
        exec idf.py "${idf_args[@]}" clean "$@"
        ;;
    fullclean)
        exec idf.py "${idf_args[@]}" fullclean "$@"
        ;;
    *)
        echo "Unknown action: $action" >&2
        usage
        exit 1
        ;;
esac
