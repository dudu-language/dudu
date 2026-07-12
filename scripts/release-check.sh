#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build_root="$repo_root/build/release-check"
allow_dirty=0

usage() {
    cat <<'USAGE'
usage: scripts/release-check.sh [--allow-dirty]

Runs the complete local pre-release validation, builds and installs a clean
Release toolchain, and exercises the installed tools. A release candidate must
run from a clean Git tree; --allow-dirty exists only for developing this gate.
USAGE
}

while [[ $# -gt 0 ]]; do
    case "$1" in
    --allow-dirty)
        allow_dirty=1
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

require_command() {
    if ! command -v "$1" >/dev/null 2>&1; then
        echo "release check requires '$1' on PATH" >&2
        exit 1
    fi
}

for command in cmake git pkg-config "${CXX:-c++}"; do
    require_command "$command"
done

if [[ "$allow_dirty" -eq 0 ]] && [[ -n "$(git -C "$repo_root" status --porcelain)" ]]; then
    echo "release check requires a clean Git tree" >&2
    echo "commit or stash changes, or use --allow-dirty while developing the gate" >&2
    exit 1
fi

echo "==> source and version checks"
git -C "$repo_root" diff --check
"$repo_root/scripts/sync_version.py"
"$repo_root/scripts/check_ast_migration_guards.sh"

echo "==> complete local validation"
"$repo_root/scripts/test_full.sh"
"$repo_root/scripts/test_dogfood.sh"

echo "==> clean Release build"
rm -rf "$build_root"
cmake -S "$repo_root" -B "$build_root/build" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX="$build_root/prefix" \
    -DDUDU_BUILD_TESTS=OFF \
    -DDUDU_STRICT=ON \
    -DDUDU_WARN_AS_ERROR=ON
cmake --build "$build_root/build" --parallel
cmake --install "$build_root/build"

installed_bin="$build_root/prefix/bin"
installed_docs="$build_root/prefix/share/doc/dudu"
version="$(tr -d '\r\n' <"$repo_root/VERSION")"

for tool in dudu duc dudu-lsp; do
    test -x "$installed_bin/$tool"
done
for file in COPYRIGHT LICENSE-APACHE LICENSE-MIT README.md VERSION; do
    test -f "$installed_docs/$file"
done
"$installed_bin/dudu" --version | grep -Fqx "dudu $version"
"$installed_bin/duc" --version | grep -Fqx "duc $version"

echo "==> installed hello project"
PATH="$installed_bin:$PATH" "$installed_bin/dudu" init "$build_root/hello" >/dev/null
(
    cd "$build_root/hello"
    PATH="$installed_bin:$PATH" dudu run --quiet
)

echo "==> installed native C/C++ import project"
PATH="$installed_bin:$PATH" "$installed_bin/dudu" init "$build_root/native-smoke" >/dev/null
cat >"$build_root/native-smoke/src/main.dd" <<'DD'
from c import stdlib.h as c
from cpp import vector


def main() -> i32:
    values: std.vector[i32]
    values.push_back(c.abs(-20))
    values.push_back(22)
    return values[0] + values[1] - 42
DD
(
    cd "$build_root/native-smoke"
    PATH="$installed_bin:$PATH" dudu run --quiet
)

echo "==> installed dependency diagnostics"
DUDU_BIN="$installed_bin/dudu" "$repo_root/scripts/test_dependencies.sh"

echo "==> distribution artifacts"
"$repo_root/scripts/test-vscode-package.sh"
"$repo_root/scripts/test-packages.sh"
"$repo_root/scripts/test-bootstrap-lifecycle.sh"

echo "release check passed: Dudu $version"
