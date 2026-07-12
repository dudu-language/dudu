#!/usr/bin/env bash
set -euo pipefail

manifest=""
key=""
asset=""

usage() {
    cat <<'USAGE'
usage: scripts/add-release-asset.sh --manifest FILE --key NAME --asset FILE

Adds a packaged release asset and its SHA-256 checksum to a release manifest.
The key must be unique and use lowercase letters, digits, and underscores.
USAGE
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --manifest) manifest="${2:?missing manifest}"; shift 2 ;;
        --key) key="${2:?missing key}"; shift 2 ;;
        --asset) asset="${2:?missing asset}"; shift 2 ;;
        -h|--help) usage; exit 0 ;;
        *) echo "unknown option: $1" >&2; usage >&2; exit 2 ;;
    esac
done

[[ -f "$manifest" ]] || { echo "manifest does not exist: $manifest" >&2; exit 2; }
[[ -f "$asset" ]] || { echo "asset does not exist: $asset" >&2; exit 2; }
[[ "$key" =~ ^[a-z][a-z0-9_]*$ ]] || { echo "invalid asset key: $key" >&2; exit 2; }
if grep -Eq "^${key}(_sha256)?=" "$manifest"; then
    echo "manifest already contains asset key: $key" >&2
    exit 1
fi

if command -v sha256sum >/dev/null 2>&1; then
    sha256="$(sha256sum "$asset" | awk '{print $1}')"
else
    sha256="$(shasum -a 256 "$asset" | awk '{print $1}')"
fi
printf '%s=%s\n' "$key" "$(basename "$asset")" >>"$manifest"
printf '%s_sha256=%s\n' "$key" "$sha256" >>"$manifest"
printf '%s  %s\n' "$sha256" "$(basename "$asset")" >"$asset.sha256"
