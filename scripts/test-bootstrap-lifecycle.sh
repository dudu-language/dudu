#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
work="$repo_root/build/bootstrap-lifecycle"
version_one="$(tr -d '\r\n' <"$repo_root/VERSION")"
version_two="${DUDU_TEST_UPDATE_VERSION:-0.1.0-alpha.2}"
commit="$(git -C "$repo_root" rev-parse HEAD)"
server_pid=""

cleanup() {
    if [[ -n "$server_pid" ]]; then
        kill "$server_pid" 2>/dev/null || true
    fi
}
trap cleanup EXIT

sha256_file() {
    if command -v sha256sum >/dev/null 2>&1; then
        sha256sum "$1" | awk '{print $1}'
    else
        shasum -a 256 "$1" | awk '{print $1}'
    fi
}

write_manifest() {
    local version="$1"
    local release_dir="$2"
    local archive="dudu-$version.tar.gz"
    local sha256
    sha256="$(sha256_file "$release_dir/$archive")"
    cat >"$release_dir/dudu-$version-manifest.txt" <<EOF
schema=1
version=$version
tag=v$version
commit=$commit
source_archive=$archive
source_sha256=$sha256
EOF
}

rm -rf "$work"
mkdir -p "$work/releases/v$version_one" "$work/releases/v$version_two" "$work/tmp"
"$repo_root/scripts/build-release-artifacts.sh" \
    --output "$work/release-one" --ref HEAD --allow-untagged >/dev/null
cp "$work/release-one"/* "$work/releases/v$version_one/"

mkdir -p "$work/source-two"
git -C "$repo_root" archive --format=tar --prefix="dudu-$version_two/" HEAD |
    tar -xf - -C "$work/source-two"
printf '%s\n' "$version_two" >"$work/source-two/dudu-$version_two/VERSION"
(
    cd "$work/source-two/dudu-$version_two"
    ./scripts/sync_version.py --write >/dev/null
)
tar -czf "$work/releases/v$version_two/dudu-$version_two.tar.gz" \
    -C "$work/source-two" "dudu-$version_two"
write_manifest "$version_two" "$work/releases/v$version_two"

python3 - "$work/releases" "$work/port" <<'PY' &
import http.server
import os
import pathlib
import socketserver
import sys

class QuietHandler(http.server.SimpleHTTPRequestHandler):
    def log_message(self, format, *args):
        pass

os.chdir(sys.argv[1])
with socketserver.TCPServer(("127.0.0.1", 0), QuietHandler) as server:
    pathlib.Path(sys.argv[2]).write_text(str(server.server_address[1]))
    server.serve_forever()
PY
server_pid=$!
for _ in $(seq 1 100); do
    [[ -s "$work/port" ]] && break
    sleep 0.05
done
[[ -s "$work/port" ]]
port="$(cat "$work/port")"
release_url="http://127.0.0.1:$port"

env DUDU_RELEASE_BASE_URL="$release_url" DUDU_INSECURE_TEST_URL=1 \
    TMPDIR="$work/tmp" "$repo_root/install.sh" \
    --version "$version_one" --prefix "$work/prefix" --source --no-modify-path >/dev/null
"$work/prefix/bin/dudu" --version | grep -Fqx "dudu $version_one"
grep -Fq '"owner": "installer"' "$work/prefix/share/dudu/installs.json"

env DUDU_RELEASE_BASE_URL="$release_url" DUDU_INSECURE_TEST_URL=1 \
    DUDU_LATEST_VERSION="$version_two" \
    "$work/prefix/bin/dudu" update --check | grep -Fq "available $version_two"
env DUDU_RELEASE_BASE_URL="$release_url" DUDU_INSECURE_TEST_URL=1 \
    TMPDIR="$work/tmp" "$work/prefix/bin/dudu" update \
    --version "$version_two" --source >/dev/null
"$work/prefix/bin/dudu" --version | grep -Fqx "dudu $version_two"
[[ "$(readlink "$work/prefix/share/dudu/previous")" == "toolchains/$version_one" ]]

"$work/prefix/bin/dudu" update --rollback >/dev/null
"$work/prefix/bin/dudu" --version | grep -Fqx "dudu $version_one"
[[ "$(readlink "$work/prefix/share/dudu/previous")" == "toolchains/$version_two" ]]

"$work/prefix/bin/dudu" uninstall --yes >/dev/null
[[ ! -e "$work/prefix/share/dudu" ]]
for tool in dudu duc dudu-lsp; do
    [[ ! -e "$work/prefix/bin/$tool" ]]
done
if find "$work/tmp" -mindepth 1 -maxdepth 1 -name 'dudu-install.*' | grep -q .; then
    echo "bootstrap installer leaked a temporary directory" >&2
    exit 1
fi

echo "bootstrap install/update/rollback/uninstall checks passed"
