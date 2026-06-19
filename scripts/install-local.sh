#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
prefix="${DUDU_INSTALL_PREFIX:-$HOME/.local}"
build_dir="$repo_root/build/install-local"
build_type="Release"
generator=()

usage() {
    cat <<'USAGE'
usage: scripts/install-local.sh [--prefix path] [--build-dir path] [--debug] [--generator name]

Builds this checkout and installs dudu/duc, docs, and editor support through
CMake's install rules. Defaults to prefix ~/.local.
USAGE
}

while [[ $# -gt 0 ]]; do
    case "$1" in
    --prefix)
        prefix="${2:?missing value for --prefix}"
        shift 2
        ;;
    --build-dir)
        build_dir="${2:?missing value for --build-dir}"
        shift 2
        ;;
    --debug)
        build_type="Debug"
        shift
        ;;
    --generator|-G)
        generator=(-G "${2:?missing value for --generator}")
        shift 2
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

jobs="$(getconf _NPROCESSORS_ONLN 2>/dev/null || printf '1')"
case "$jobs" in
''|*[!0-9]*) jobs=1 ;;
esac

cmake -S "$repo_root" -B "$build_dir" \
    "${generator[@]}" \
    -DCMAKE_BUILD_TYPE="$build_type" \
    -DCMAKE_INSTALL_PREFIX="$prefix" \
    -DDUDU_BUILD_TESTS=OFF \
    -DDUDU_STRICT=ON

cmake --build "$build_dir" --target dudu duc --parallel "$jobs"
cmake --install "$build_dir"

cat <<EOF
installed dudu to $prefix/bin/dudu
installed duc to $prefix/bin/duc

Add this to PATH if needed:
  export PATH="$prefix/bin:\$PATH"
EOF
