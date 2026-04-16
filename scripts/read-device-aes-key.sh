#!/usr/bin/env bash
set -euo pipefail

usage() {
    cat <<'EOF'
Usage:
  scripts/read-device-aes-key.sh --port <serial-port> [--baud <baud>]
  scripts/read-device-aes-key.sh --nvs-file <nvs-dump.bin>

Reads the BluButton AES key from NVS and prints it as lowercase hex.

Options:
  -p, --port <serial-port>   Serial port of the device to read from
  -b, --baud <baud>          Serial baud for flash reads (default: 115200)
      --nvs-file <file>      Parse an already dumped NVS partition instead
      --keep-dump            Keep the temporary NVS dump on disk
  -h, --help                 Show this help

Notes:
  - This uses the ESP-IDF Python environment and tools from $IDF_PATH or ~/esp/esp-idf-v6.0 by default.
  - It expects the AES key at namespace "identity", key "aes_key".
  - It assumes NVS is not encrypted, which matches the current project configs.
EOF
}

die() {
    echo "$*" >&2
    exit 1
}

port=""
baud="115200"
nvs_file=""
keep_dump=0

while [[ $# -gt 0 ]]; do
    case "$1" in
        -p|--port)
            [[ $# -ge 2 ]] || die "Missing value for $1"
            port="$2"
            shift 2
            ;;
        -b|--baud)
            [[ $# -ge 2 ]] || die "Missing value for $1"
            baud="$2"
            shift 2
            ;;
        --nvs-file)
            [[ $# -ge 2 ]] || die "Missing value for $1"
            nvs_file="$2"
            shift 2
            ;;
        --keep-dump)
            keep_dump=1
            shift
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        *)
            usage >&2
            die "Unknown argument: $1"
            ;;
    esac
done

if [[ -n "$port" && -n "$nvs_file" ]]; then
    die "Use either --port or --nvs-file, not both"
fi

if [[ -z "$port" && -z "$nvs_file" ]]; then
    usage >&2
    die "Either --port or --nvs-file is required"
fi

if [[ -n "$nvs_file" && ! -f "$nvs_file" ]]; then
    die "NVS file not found: $nvs_file"
fi

idf_default_path="$HOME/esp/esp-idf-v6.0"
if [[ -z "${IDF_PATH:-}" && ! -f "$idf_default_path/export.sh" && -f "$HOME/esp/esp-idf/export.sh" ]]; then
    idf_default_path="$HOME/esp/esp-idf"
fi

idf_path="${IDF_PATH:-$idf_default_path}"
idf_export="$idf_path/export.sh"
parttool_py="$idf_path/components/partition_table/parttool.py"
nvs_tool_py="$idf_path/components/nvs_flash/nvs_partition_tool/nvs_tool.py"

[[ -f "$idf_export" ]] || die "ESP-IDF export script not found: $idf_export"
[[ -f "$parttool_py" ]] || die "ESP-IDF parttool not found: $parttool_py"
[[ -f "$nvs_tool_py" ]] || die "ESP-IDF NVS tool not found: $nvs_tool_py"

tmp_dir="$(mktemp -d "${TMPDIR:-/tmp}/blu-button-key.XXXXXX")"
cleanup() {
    if [[ $keep_dump -eq 0 ]]; then
        rm -rf "$tmp_dir"
    fi
}
trap cleanup EXIT

if [[ -z "$nvs_file" ]]; then
    dump_path="$tmp_dir/nvs.bin"
    parttool_log="$tmp_dir/parttool.log"
    echo "Reading NVS partition from $port..." >&2
    # shellcheck source=/dev/null
    source "$idf_export" >/dev/null 2>&1
    if ! python "$parttool_py" \
        --port "$port" \
        --baud "$baud" \
        read_partition \
        --partition-name nvs \
        --output "$dump_path" >"$parttool_log" 2>&1; then
        echo "Failed to read NVS from device." >&2
        echo >&2
        cat "$parttool_log" >&2
        echo >&2
        echo "Hints:" >&2
        echo "  - close any serial monitor already attached to $port" >&2
        echo "  - verify the board is on the expected port" >&2
        echo "  - if needed, try another baud manually, for example: --baud 460800" >&2
        exit 1
    fi
    nvs_file="$dump_path"
    if [[ $keep_dump -eq 1 ]]; then
        echo "Kept NVS dump at: $nvs_file" >&2
    fi
fi

json_path="$tmp_dir/nvs.json"
# shellcheck source=/dev/null
source "$idf_export" >/dev/null 2>&1
python "$nvs_tool_py" -f json -d minimal "$nvs_file" > "$json_path"

python - "$json_path" <<'PY'
import base64
import json
import sys

json_path = sys.argv[1]

with open(json_path, "r", encoding="utf-8") as fh:
    entries = json.load(fh)

matches = [
    entry for entry in entries
    if entry.get("namespace") == "identity"
    and entry.get("key") == "aes_key"
    and entry.get("state") == "Written"
]

if not matches:
    print("AES key not found in NVS namespace 'identity' with key 'aes_key'", file=sys.stderr)
    sys.exit(1)

entry = matches[-1]
encoding = entry.get("encoding")
data = entry.get("data")

if encoding in ("blob", "blob_data"):
    value = base64.b64decode(data)
elif encoding == "string":
    value = data.encode("utf-8")
else:
    print(f"Unsupported AES key encoding: {encoding}", file=sys.stderr)
    sys.exit(1)

if len(value) != 16:
    print(f"Unexpected AES key length: {len(value)} bytes", file=sys.stderr)
    sys.exit(1)

print(value.hex())
PY
