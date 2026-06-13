#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
"$repo_root/scripts/build.sh" >/dev/null

probe_glm() {
    if [[ ! -f /usr/include/glm/glm.hpp ]]; then
        echo "skip glm: /usr/include/glm/glm.hpp not found"
        return
    fi

    local cpp="$repo_root/build/probe_glm_math.cpp"
    local bin="$repo_root/build/probe_glm_math"
    "$repo_root/build/duc" emit "$repo_root/tests/fixtures/glm_math.dd" -o "$cpp"
    "${CXX:-c++}" -std=c++20 "$cpp" -o "$bin"
    set +e
    "$bin"
    local status=$?
    set -e
    if [[ "$status" -ne 32 ]]; then
        echo "glm probe returned $status, expected 32" >&2
        exit 1
    fi
    echo "ok glm"
}

probe_opencv() {
    if ! pkg-config --exists opencv4; then
        echo "skip opencv4: pkg-config package not found"
        return
    fi

    local cpp="$repo_root/build/probe_image_filter.cpp"
    local bin="$repo_root/build/probe_image_filter"
    local run_dir="$repo_root/build/probe_image_filter_run"
    "$repo_root/build/duc" emit "$repo_root/examples/image_filter.dd" -o "$cpp"
    "${CXX:-c++}" -std=c++20 -w "$cpp" $(pkg-config --cflags --libs opencv4) -o "$bin"
    rm -rf "$run_dir"
    mkdir -p "$run_dir"
    printf 'P3\n2 2\n255\n255 0 0 0 255 0 0 0 255 255 255 255\n' >"$run_dir/input.png"
    (
        cd "$run_dir"
        "$bin"
    )
    if [[ ! -s "$run_dir/output.png" ]]; then
        echo "opencv4 probe did not write output.png" >&2
        exit 1
    fi
    echo "ok opencv4"
}

probe_sqlite() {
    if ! pkg-config --exists sqlite3; then
        echo "skip sqlite3: pkg-config package not found"
        return
    fi

    local cpp="$repo_root/build/probe_sqlite_crud.cpp"
    local bin="$repo_root/build/probe_sqlite_crud"
    "$repo_root/build/duc" emit "$repo_root/tests/fixtures/sqlite_crud.dd" -o "$cpp"
    "${CXX:-c++}" -std=c++20 -I"$repo_root/tests/fixtures" "$cpp" \
        $(pkg-config --cflags --libs sqlite3) -o "$bin"
    set +e
    "$bin"
    local status=$?
    set -e
    if [[ "$status" -ne 42 ]]; then
        echo "sqlite probe returned $status, expected 42" >&2
        exit 1
    fi
    echo "ok sqlite3"
}

probe_threading() {
    local cpp="$repo_root/build/probe_threading_atomics.cpp"
    local bin="$repo_root/build/probe_threading_atomics"
    "$repo_root/build/duc" emit "$repo_root/examples/threading_atomics.dd" -o "$cpp"
    "${CXX:-c++}" -std=c++20 "$cpp" -pthread -o "$bin"
    set +e
    "$bin"
    local status=$?
    set -e
    if [[ "$status" -ne 100 ]]; then
        echo "threading probe returned $status, expected 100" >&2
        exit 1
    fi
    echo "ok threading"
}

probe_raylib() {
    if ! pkg-config --exists raylib; then
        echo "skip raylib: pkg-config package not found"
        return
    fi

    local cpp="$repo_root/build/probe_raylib_game.cpp"
    local bin="$repo_root/build/probe_raylib_game"
    "$repo_root/build/duc" emit "$repo_root/examples/raylib_game.dd" -o "$cpp"
    "${CXX:-c++}" -std=c++20 "$cpp" $(pkg-config --cflags --libs raylib) -o "$bin"
    echo "ok raylib"
}

probe_glm
probe_opencv
probe_sqlite
probe_threading
probe_raylib
