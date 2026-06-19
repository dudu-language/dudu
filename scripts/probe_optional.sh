#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
source "$repo_root/scripts/dev_env.sh"
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
    "${CXX:-c++}" -std=c++20 "$cpp" $(pkg-config --cflags --libs sqlite3) -o "$bin"
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

probe_zlib() {
    if ! pkg-config --exists zlib; then
        echo "skip zlib: pkg-config package not found"
        return
    fi

    local cpp="$repo_root/build/probe_zlib_roundtrip.cpp"
    local bin="$repo_root/build/probe_zlib_roundtrip"
    "$repo_root/build/duc" emit "$repo_root/tests/fixtures/zlib_roundtrip.dd" -o "$cpp"
    "${CXX:-c++}" -std=c++20 "$cpp" $(pkg-config --cflags --libs zlib) -o "$bin"
    set +e
    "$bin"
    local status=$?
    set -e
    if [[ "$status" -ne 42 ]]; then
        echo "zlib probe returned $status, expected 42" >&2
        exit 1
    fi
    echo "ok zlib"
}

probe_curl() {
    if ! pkg-config --exists libcurl; then
        echo "skip libcurl: pkg-config package not found"
        return
    fi

    local cpp="$repo_root/build/probe_curl_version_info.cpp"
    local bin="$repo_root/build/probe_curl_version_info"
    "$repo_root/build/duc" emit "$repo_root/tests/fixtures/curl_version_info.dd" -o "$cpp"
    "${CXX:-c++}" -std=c++20 "$cpp" $(pkg-config --cflags --libs libcurl) -o "$bin"
    set +e
    "$bin"
    local status=$?
    set -e
    if [[ "$status" -ne 42 ]]; then
        echo "curl probe returned $status, expected 42" >&2
        exit 1
    fi
    echo "ok libcurl"
}

probe_libpng() {
    if ! pkg-config --exists libpng; then
        echo "skip libpng: pkg-config package not found"
        return
    fi

    local cpp="$repo_root/build/probe_libpng_signature.cpp"
    local bin="$repo_root/build/probe_libpng_signature"
    "$repo_root/build/duc" emit "$repo_root/tests/fixtures/libpng_signature.dd" -o "$cpp"
    "${CXX:-c++}" -std=c++20 "$cpp" $(pkg-config --cflags --libs libpng) -o "$bin"
    set +e
    "$bin"
    local status=$?
    set -e
    if [[ "$status" -ne 42 ]]; then
        echo "libpng probe returned $status, expected 42" >&2
        exit 1
    fi
    echo "ok libpng"
}

probe_openssl() {
    if ! pkg-config --exists openssl; then
        echo "skip openssl: pkg-config package not found"
        return
    fi

    local cpp="$repo_root/build/probe_openssl_sha256.cpp"
    local bin="$repo_root/build/probe_openssl_sha256"
    "$repo_root/build/duc" emit "$repo_root/tests/fixtures/openssl_sha256.dd" -o "$cpp"
    "${CXX:-c++}" -std=c++20 "$cpp" $(pkg-config --cflags --libs openssl) -o "$bin"
    set +e
    "$bin"
    local status=$?
    set -e
    if [[ "$status" -ne 42 ]]; then
        echo "openssl probe returned $status, expected 42" >&2
        exit 1
    fi
    echo "ok openssl"
}

probe_libevent() {
    if ! pkg-config --exists libevent; then
        echo "skip libevent: pkg-config package not found"
        return
    fi

    local cpp="$repo_root/build/probe_libevent_base.cpp"
    local bin="$repo_root/build/probe_libevent_base"
    "$repo_root/build/duc" emit "$repo_root/tests/fixtures/libevent_base.dd" -o "$cpp"
    "${CXX:-c++}" -std=c++20 "$cpp" $(pkg-config --cflags --libs libevent) -o "$bin"
    set +e
    "$bin"
    local status=$?
    set -e
    if [[ "$status" -ne 42 ]]; then
        echo "libevent probe returned $status, expected 42" >&2
        exit 1
    fi
    echo "ok libevent"
}

probe_fmt() {
    if ! pkg-config --exists fmt; then
        echo "skip fmt: pkg-config package not found"
        return
    fi

    local cpp="$repo_root/build/probe_fmt_format.cpp"
    local bin="$repo_root/build/probe_fmt_format"
    "$repo_root/build/duc" emit "$repo_root/tests/fixtures/fmt_format.dd" -o "$cpp"
    "${CXX:-c++}" -std=c++20 "$cpp" $(pkg-config --cflags --libs fmt) -o "$bin"
    set +e
    "$bin"
    local status=$?
    set -e
    if [[ "$status" -ne 42 ]]; then
        echo "fmt probe returned $status, expected 42" >&2
        exit 1
    fi
    echo "ok fmt"
}

probe_eigen() {
    if ! pkg-config --exists eigen3; then
        echo "skip eigen3: pkg-config package not found"
        return
    fi

    local cpp="$repo_root/build/probe_eigen_vector.cpp"
    local bin="$repo_root/build/probe_eigen_vector"
    "$repo_root/build/duc" emit "$repo_root/tests/fixtures/eigen_vector.dd" -o "$cpp"
    "${CXX:-c++}" -std=c++20 "$cpp" $(pkg-config --cflags eigen3) -o "$bin"
    set +e
    "$bin"
    local status=$?
    set -e
    if [[ "$status" -ne 42 ]]; then
        echo "eigen3 probe returned $status, expected 42" >&2
        exit 1
    fi
    echo "ok eigen3"
}

probe_openblas() {
    if ! pkg-config --exists openblas; then
        echo "skip openblas: pkg-config package not found"
        return
    fi

    local cpp="$repo_root/build/probe_openblas_ddot.cpp"
    local bin="$repo_root/build/probe_openblas_ddot"
    "$repo_root/build/duc" emit "$repo_root/tests/fixtures/openblas_ddot.dd" -o "$cpp"
    "${CXX:-c++}" -std=c++20 "$cpp" $(pkg-config --cflags --libs openblas) -o "$bin"
    set +e
    "$bin"
    local status=$?
    set -e
    if [[ "$status" -ne 42 ]]; then
        echo "openblas probe returned $status, expected 42" >&2
        exit 1
    fi
    echo "ok openblas"
}

probe_spdlog() {
    if ! pkg-config --exists spdlog; then
        echo "skip spdlog: pkg-config package not found"
        return
    fi

    local cpp="$repo_root/build/probe_spdlog_basic.cpp"
    local bin="$repo_root/build/probe_spdlog_basic"
    "$repo_root/build/duc" emit "$repo_root/tests/fixtures/spdlog_basic.dd" -o "$cpp"
    "${CXX:-c++}" -std=c++20 "$cpp" $(pkg-config --cflags --libs spdlog) -o "$bin"
    set +e
    "$bin" >/dev/null
    local status=$?
    set -e
    if [[ "$status" -ne 42 ]]; then
        echo "spdlog probe returned $status, expected 42" >&2
        exit 1
    fi
    echo "ok spdlog"
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

probe_posix_mmap() {
    local cpp="$repo_root/build/probe_posix_mmap_hash.cpp"
    local bin="$repo_root/build/probe_posix_mmap_hash"
    "$repo_root/build/duc" emit "$repo_root/tests/fixtures/posix_mmap_hash.dd" -o "$cpp"
    "${CXX:-c++}" -std=c++20 "$cpp" -o "$bin"
    set +e
    "$bin"
    local status=$?
    set -e
    if [[ "$status" -ne 42 ]]; then
        echo "POSIX mmap probe returned $status, expected 42" >&2
        exit 1
    fi
    echo "ok POSIX mmap"
}

probe_posix_threads() {
    local cpp="$repo_root/build/probe_posix_threads_mutex.cpp"
    local bin="$repo_root/build/probe_posix_threads_mutex"
    "$repo_root/build/duc" emit "$repo_root/tests/fixtures/posix_threads_mutex.dd" -o "$cpp"
    "${CXX:-c++}" -std=c++20 -I"$repo_root/tests/fixtures" "$cpp" -pthread -o "$bin"
    set +e
    "$bin"
    local status=$?
    set -e
    if [[ "$status" -ne 42 ]]; then
        echo "POSIX pthread probe returned $status, expected 42" >&2
        exit 1
    fi
    echo "ok POSIX pthread"
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
    grep -q "player.vel.x" "$cpp"

    local audio_cpp="$repo_root/build/probe_raylib_audio_synth.cpp"
    local audio_bin="$repo_root/build/probe_raylib_audio_synth"
    "$repo_root/build/duc" emit "$repo_root/examples/audio_synth.dd" -o "$audio_cpp"
    "${CXX:-c++}" -std=c++20 "$audio_cpp" $(pkg-config --cflags --libs raylib) -o "$audio_bin"
    echo "ok raylib"
}

probe_sdl3() {
    if ! pkg-config --exists sdl3; then
        echo "skip sdl3: pkg-config package not found"
        return
    fi

    local cpp="$repo_root/build/probe_sdl3_window.cpp"
    local bin="$repo_root/build/probe_sdl3_window"
    "$repo_root/build/duc" emit "$repo_root/examples/sdl3_window.dd" -o "$cpp"
    "${CXX:-c++}" -std=c++20 "$cpp" $(pkg-config --cflags --libs sdl3) -o "$bin"
    echo "ok sdl3"
}

probe_glfw() {
    if ! pkg-config --exists glfw3; then
        echo "skip glfw3: pkg-config package not found"
        return
    fi

    local cpp="$repo_root/build/probe_glfw_opengl_triangle.cpp"
    local bin="$repo_root/build/probe_glfw_opengl_triangle"
    "$repo_root/build/duc" emit "$repo_root/examples/glfw_opengl_triangle.dd" -o "$cpp"
    "${CXX:-c++}" -std=c++20 "$cpp" $(pkg-config --cflags --libs glfw3) -o "$bin"
    echo "ok glfw3"
}

probe_opencl() {
    if ! pkg-config --exists OpenCL; then
        echo "skip OpenCL: pkg-config package not found"
        return
    fi

    local cpp="$repo_root/build/probe_opencl_kernel_host.cpp"
    local bin="$repo_root/build/probe_opencl_kernel_host"
    "$repo_root/build/duc" emit "$repo_root/examples/opencl_kernel_host.dd" -o "$cpp"
    "${CXX:-c++}" -std=c++20 -DCL_TARGET_OPENCL_VERSION=300 "$cpp" \
        $(pkg-config --cflags --libs OpenCL) -o "$bin"
    set +e
    "$bin"
    local status=$?
    set -e
    if [[ "$status" -ne 42 ]]; then
        echo "OpenCL probe returned $status, expected 42" >&2
        exit 1
    fi
    echo "ok OpenCL"
}

probe_vulkan() {
    if ! pkg-config --exists vulkan; then
        echo "skip vulkan: pkg-config package not found"
        return
    fi

    local cpp="$repo_root/build/probe_vulkan_triangle.cpp"
    local bin="$repo_root/build/probe_vulkan_triangle"
    "$repo_root/build/duc" emit "$repo_root/examples/vulkan_triangle.dd" -o "$cpp"
    "${CXX:-c++}" -std=c++20 "$cpp" $(pkg-config --cflags --libs vulkan) -o "$bin"
    set +e
    "$bin"
    local status=$?
    set -e
    if [[ "$status" -ne 42 ]]; then
        echo "vulkan probe returned $status, expected 42" >&2
        exit 1
    fi
    echo "ok vulkan"
}

probe_ffmpeg() {
    if ! pkg-config --exists libavcodec; then
        echo "skip libavcodec: pkg-config package not found"
        return
    fi

    local cpp="$repo_root/build/probe_ffmpeg_probe_decode.cpp"
    local bin="$repo_root/build/probe_ffmpeg_probe_decode"
    "$repo_root/build/duc" emit "$repo_root/examples/ffmpeg_probe_decode.dd" -o "$cpp"
    "${CXX:-c++}" -std=c++20 -I"$repo_root" "$cpp" \
        $(pkg-config --cflags --libs libavcodec) -o "$bin"
    set +e
    "$bin"
    local status=$?
    set -e
    if [[ "$status" -ne 42 ]]; then
        echo "ffmpeg probe returned $status, expected 42" >&2
        exit 1
    fi
    echo "ok ffmpeg"
}

probe_glm
probe_opencv
probe_sqlite
probe_zlib
probe_curl
probe_libpng
probe_openssl
probe_libevent
probe_fmt
probe_eigen
probe_openblas
probe_spdlog
probe_threading
probe_posix_mmap
probe_posix_threads
probe_raylib
probe_sdl3
probe_glfw
probe_opencl
probe_vulkan
probe_ffmpeg
