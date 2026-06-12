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
    "$repo_root/build/duc" emit "$repo_root/examples/image_filter.dd" -o "$cpp"
    "${CXX:-c++}" -std=c++20 -w "$cpp" $(pkg-config --cflags --libs opencv4) -o "$bin"
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

probe_glm
probe_opencv
probe_sqlite
