#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build_dir="$repo_root/build/package-deb"
output_dir="$repo_root/dist"

usage() {
    cat <<'USAGE'
usage: scripts/build-deb.sh [--build-dir DIR] [--output DIR]

Builds a downloadable Debian package from the current tagged source tree using
the same CMake install rules as every other Dudu installation.
USAGE
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --build-dir)
            build_dir="${2:?missing value for --build-dir}"
            shift 2
            ;;
        --output)
            output_dir="${2:?missing value for --output}"
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

rm -rf "$build_dir"
mkdir -p "$output_dir"
cmake -S "$repo_root" -B "$build_dir" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX=/usr \
    -DDUDU_BUILD_TESTS=OFF \
    -DDUDU_INSTALL_OWNER=deb \
    -DDUDU_PACKAGE_DEB=ON
cmake --build "$build_dir" --parallel "${DUDU_BUILD_JOBS:-4}"
(
    cd "$build_dir"
    cpack -G DEB
)

package="$(find "$build_dir" -maxdepth 1 -type f -name 'dudu_*.deb' -print | sort | tail -1)"
[[ -n "$package" ]] || {
    echo "CPack did not produce a Dudu .deb" >&2
    exit 1
}
deb_name="$(basename "$package")"
deb_name="${deb_name//\~/-}"
deb="$output_dir/$deb_name"
cp "$package" "$deb"
printf 'Debian package: %s\n' "$deb"
