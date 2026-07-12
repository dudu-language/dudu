#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
extension_dir="$repo_root/editors/vscode"
version="$(tr -d '\r\n' <"$repo_root/VERSION")"
output="$repo_root/dist/dudu-$version.vsix"
install_dependencies=1

usage() {
    cat <<'USAGE'
usage: scripts/package-vscode.sh [--output FILE] [--skip-npm-ci]

Builds the production-bundled prerelease Dudu VSIX. The package contains no
node_modules tree and records the matching minimum Dudu toolchain version.
USAGE
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --output)
            output="${2:?missing value for --output}"
            shift 2
            ;;
        --skip-npm-ci)
            install_dependencies=0
            shift
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        *)
            echo "unknown option: $1" >&2
            usage >&2
            exit 2
            ;;
    esac
done

"$repo_root/scripts/sync_version.py"
if [[ "$install_dependencies" -eq 1 ]]; then
    npm ci --prefix "$extension_dir"
fi
npm run build --prefix "$extension_dir"
mkdir -p "$(dirname "$output")"
(
    cd "$extension_dir"
    npx vsce package --pre-release --out "$output"
)

if unzip -l "$output" | grep -q 'node_modules/'; then
    echo "VSIX unexpectedly contains node_modules" >&2
    exit 1
fi
unzip -p "$output" extension/package.json | python3 -m json.tool >/dev/null
unzip -p "$output" extension/dist/extension.js | grep -Fq 'Dudu Language Server'
printf 'VSIX: %s\n' "$output"
