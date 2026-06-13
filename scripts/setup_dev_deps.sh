#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
third_party="$repo_root/third_party"
src_dir="$third_party/src"
build_dir="$third_party/build"
prefix="$third_party/install"

raylib_version="${DUDU_RAYLIB_VERSION:-5.5}"
sdl3_version="${DUDU_SDL3_VERSION:-3.2.22}"

usage() {
    cat <<EOF
usage: ./scripts/setup_dev_deps.sh [all|raylib|sdl3]

Installs optional developer probe dependencies into:
  $prefix

The main Dudu build does not require these. They are only for running the full
native interop probes locally. Use scripts/dev_env.sh or probe_optional.sh to
put this install on PKG_CONFIG_PATH.
EOF
}

need_tool() {
    if ! command -v "$1" >/dev/null 2>&1; then
        echo "missing required tool: $1" >&2
        exit 1
    fi
}

clone_or_update() {
    local url="$1"
    local tag="$2"
    local dest="$3"

    if [[ ! -d "$dest/.git" ]]; then
        git clone --depth 1 --branch "$tag" "$url" "$dest"
        return
    fi

    git -C "$dest" fetch --depth 1 origin "refs/tags/$tag:refs/tags/$tag"
    git -C "$dest" checkout --detach "$tag"
}

install_raylib() {
    local src="$src_dir/raylib"
    local build="$build_dir/raylib"

    clone_or_update "https://github.com/raysan5/raylib.git" "$raylib_version" "$src"
    cmake -S "$src" -B "$build" \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_INSTALL_PREFIX="$prefix" \
        -DBUILD_EXAMPLES=OFF \
        -DBUILD_SHARED_LIBS=OFF
    cmake --build "$build" -j"$(nproc)"
    cmake --install "$build"
}

install_sdl3() {
    local src="$src_dir/SDL"
    local build="$build_dir/sdl3"

    clone_or_update "https://github.com/libsdl-org/SDL.git" "release-$sdl3_version" "$src"
    cmake -S "$src" -B "$build" \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_INSTALL_PREFIX="$prefix" \
        -DSDL_SHARED=OFF \
        -DSDL_STATIC=ON \
        -DSDL_TEST_LIBRARY=OFF
    cmake --build "$build" -j"$(nproc)"
    cmake --install "$build"
}

main() {
    local target="${1:-all}"
    if [[ "$target" == "-h" || "$target" == "--help" ]]; then
        usage
        return
    fi

    need_tool git
    need_tool cmake
    need_tool pkg-config
    mkdir -p "$src_dir" "$build_dir" "$prefix"

    case "$target" in
        all)
            install_raylib
            install_sdl3
            ;;
        raylib)
            install_raylib
            ;;
        sdl3)
            install_sdl3
            ;;
        *)
            usage >&2
            exit 2
            ;;
    esac

    echo "dev deps installed in $prefix"
    echo "run: source scripts/dev_env.sh"
}

main "$@"

