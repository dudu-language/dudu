#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
source "$repo_root/scripts/dev_env.sh"
"$repo_root/scripts/build.sh" >/dev/null

build_pkg_probe() {
    local name="$1"
    local source="$2"
    local package="$3"
    local bin="$repo_root/build/$name"
    local project="$repo_root/build/${name}_project"
    rm -rf "$project"
    mkdir -p "$project"
    cp "$source" "$project/main.dd"
    cat >"$project/dudu.toml" <<TOML
name = "$name"
entry = "main.dd"
build_dir = "../${name}_build"

[pkg_config]
packages = ["$package"]
TOML
    "$repo_root/build/dudu" build "$project" -o "$bin" --quiet
}

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

    local bin="$repo_root/build/probe_image_filter"
    local project="$repo_root/build/probe_image_filter_project"
    local run_dir="$repo_root/build/probe_image_filter_run"
    rm -rf "$project"
    mkdir -p "$project"
    cp "$repo_root/examples/image_filter.dd" "$project/main.dd"
    cat >"$project/dudu.toml" <<'TOML'
name = "probe_image_filter"
entry = "main.dd"
build_dir = "../probe_image_filter_build"

[pkg_config]
packages = ["opencv4"]
TOML
    "$repo_root/build/dudu" build "$project" -o "$bin" --quiet
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

probe_lua() {
    if ! pkg-config --exists lua; then
        echo "skip lua: pkg-config package not found"
        return
    fi

    local bin="$repo_root/build/probe_lua_stack"
    build_pkg_probe "probe_lua_stack" "$repo_root/tests/fixtures/lua_stack.dd" "lua"
    set +e
    "$bin"
    local status=$?
    set -e
    if [[ "$status" -ne 42 ]]; then
        echo "lua probe returned $status, expected 42" >&2
        exit 1
    fi
    echo "ok lua"
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

probe_lzma() {
    if ! pkg-config --exists liblzma; then
        echo "skip liblzma: pkg-config package not found"
        return
    fi

    local cpp="$repo_root/build/probe_lzma_version.cpp"
    local bin="$repo_root/build/probe_lzma_version"
    "$repo_root/build/duc" emit "$repo_root/tests/fixtures/lzma_version.dd" -o "$cpp"
    "${CXX:-c++}" -std=c++20 "$cpp" $(pkg-config --cflags --libs liblzma) -o "$bin"
    set +e
    "$bin"
    local status=$?
    set -e
    if [[ "$status" -ne 42 ]]; then
        echo "lzma probe returned $status, expected 42" >&2
        exit 1
    fi
    echo "ok lzma"
}

probe_uuid() {
    if ! pkg-config --exists uuid; then
        echo "skip uuid: pkg-config package not found"
        return
    fi

    local cpp="$repo_root/build/probe_uuid_parse.cpp"
    local bin="$repo_root/build/probe_uuid_parse"
    "$repo_root/build/duc" emit "$repo_root/tests/fixtures/uuid_parse.dd" -o "$cpp"
    "${CXX:-c++}" -std=c++20 "$cpp" $(pkg-config --cflags --libs uuid) -o "$bin"
    set +e
    "$bin"
    local status=$?
    set -e
    if [[ "$status" -ne 42 ]]; then
        echo "uuid probe returned $status, expected 42" >&2
        exit 1
    fi
    echo "ok uuid"
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

probe_libjpeg() {
    if ! pkg-config --exists libjpeg; then
        echo "skip libjpeg: pkg-config package not found"
        return
    fi

    local cpp="$repo_root/build/probe_libjpeg_compress_setup.cpp"
    local bin="$repo_root/build/probe_libjpeg_compress_setup"
    "$repo_root/build/duc" emit "$repo_root/tests/fixtures/libjpeg_compress_setup.dd" -o "$cpp"
    "${CXX:-c++}" -std=c++20 "$cpp" $(pkg-config --cflags --libs libjpeg) -o "$bin"
    set +e
    "$bin"
    local status=$?
    set -e
    if [[ "$status" -ne 42 ]]; then
        echo "libjpeg probe returned $status, expected 42" >&2
        exit 1
    fi
    echo "ok libjpeg"
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

probe_libxml2() {
    if ! pkg-config --exists libxml-2.0; then
        echo "skip libxml2: pkg-config package not found"
        return
    fi

    local bin="$repo_root/build/probe_libxml_parse_memory"
    build_pkg_probe "probe_libxml_parse_memory" \
        "$repo_root/tests/fixtures/libxml_parse_memory.dd" "libxml-2.0"
    set +e
    "$bin"
    local status=$?
    set -e
    if [[ "$status" -ne 42 ]]; then
        echo "libxml2 probe returned $status, expected 42" >&2
        exit 1
    fi
    echo "ok libxml2"
}

probe_expat() {
    if ! pkg-config --exists expat; then
        echo "skip expat: pkg-config package not found"
        return
    fi

    local cpp="$repo_root/build/probe_expat_parse.cpp"
    local bin="$repo_root/build/probe_expat_parse"
    "$repo_root/build/duc" emit "$repo_root/tests/fixtures/expat_parse.dd" -o "$cpp"
    "${CXX:-c++}" -std=c++20 "$cpp" $(pkg-config --cflags --libs expat) -o "$bin"
    set +e
    "$bin"
    local status=$?
    set -e
    if [[ "$status" -ne 42 ]]; then
        echo "expat probe returned $status, expected 42" >&2
        exit 1
    fi
    echo "ok expat"
}

probe_cairo() {
    if ! pkg-config --exists cairo; then
        echo "skip cairo: pkg-config package not found"
        return
    fi

    local bin="$repo_root/build/probe_cairo_image_surface"
    build_pkg_probe "probe_cairo_image_surface" \
        "$repo_root/tests/fixtures/cairo_image_surface.dd" "cairo"
    set +e
    "$bin"
    local status=$?
    set -e
    if [[ "$status" -ne 42 ]]; then
        echo "cairo probe returned $status, expected 42" >&2
        exit 1
    fi
    echo "ok cairo"
}

probe_freetype() {
    if ! pkg-config --exists freetype2; then
        echo "skip freetype2: pkg-config package not found"
        return
    fi

    local bin="$repo_root/build/probe_freetype_version"
    build_pkg_probe "probe_freetype_version" \
        "$repo_root/tests/fixtures/freetype_version.dd" "freetype2"
    set +e
    "$bin"
    local status=$?
    set -e
    if [[ "$status" -ne 42 ]]; then
        echo "freetype2 probe returned $status, expected 42" >&2
        exit 1
    fi
    echo "ok freetype2"
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

    local bin="$repo_root/build/probe_eigen_vector"
    build_pkg_probe "probe_eigen_vector" "$repo_root/tests/fixtures/eigen_vector.dd" "eigen3"
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
        echo "openblas ddot probe returned $status, expected 42" >&2
        exit 1
    fi

    cpp="$repo_root/build/probe_openblas_sgemm.cpp"
    bin="$repo_root/build/probe_openblas_sgemm"
    "$repo_root/build/duc" emit "$repo_root/tests/fixtures/openblas_sgemm.dd" -o "$cpp"
    "${CXX:-c++}" -std=c++20 "$cpp" $(pkg-config --cflags --libs openblas) -o "$bin"
    set +e
    "$bin"
    status=$?
    set -e
    if [[ "$status" -ne 42 ]]; then
        echo "openblas sgemm probe returned $status, expected 42" >&2
        exit 1
    fi

    cpp="$repo_root/build/probe_openblas_tensor_compare.cpp"
    bin="$repo_root/build/probe_openblas_tensor_compare"
    "$repo_root/build/duc" emit "$repo_root/tests/fixtures/tensor_dogfood/openblas_compare.dd" -o "$cpp"
    "${CXX:-c++}" -std=c++20 "$cpp" $(pkg-config --cflags --libs openblas) -o "$bin"
    set +e
    "$bin"
    status=$?
    set -e
    if [[ "$status" -ne 42 ]]; then
        echo "openblas tensor compare probe returned $status, expected 42" >&2
        exit 1
    fi
    echo "ok openblas"
}

probe_spdlog() {
    if [[ "${DUDU_PROBE_HEAVY:-0}" != "1" ]]; then
        echo "skip spdlog: set DUDU_PROBE_HEAVY=1 for heavy template-header probe"
        return
    fi
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

probe_stb() {
    if ! pkg-config --exists stb; then
        echo "skip stb: pkg-config package not found"
        return
    fi

    local bin="$repo_root/build/probe_stb_image_info"
    build_pkg_probe "probe_stb_image_info" "$repo_root/tests/fixtures/stb_image_info.dd" "stb"
    set +e
    "$bin"
    local status=$?
    set -e
    if [[ "$status" -ne 42 ]]; then
        echo "stb probe returned $status, expected 42" >&2
        exit 1
    fi
    echo "ok stb"
}

probe_x11() {
    if ! pkg-config --exists x11; then
        echo "skip x11: pkg-config package not found"
        return
    fi

    local cpp="$repo_root/build/probe_x11_display.cpp"
    local bin="$repo_root/build/probe_x11_display"
    "$repo_root/build/duc" emit "$repo_root/tests/fixtures/x11_display_probe.dd" -o "$cpp"
    "${CXX:-c++}" -std=c++20 "$cpp" $(pkg-config --cflags --libs x11) -o "$bin"
    set +e
    "$bin"
    local status=$?
    set -e
    if [[ "$status" -ne 42 ]]; then
        echo "x11 probe returned $status, expected 42" >&2
        exit 1
    fi
    echo "ok x11"
}

probe_boost_filesystem() {
    if [[ ! -f /usr/include/boost/filesystem.hpp ||
          ! -f /usr/lib/x86_64-linux-gnu/libboost_filesystem.so ||
          ! -f /usr/lib/x86_64-linux-gnu/libboost_system.so ]]; then
        echo "skip boost filesystem: headers or libraries not found"
        return
    fi

    local cpp="$repo_root/build/probe_boost_filesystem.cpp"
    local bin="$repo_root/build/probe_boost_filesystem"
    "$repo_root/build/duc" emit "$repo_root/tests/fixtures/boost_filesystem.dd" -o "$cpp"
    "${CXX:-c++}" -std=c++20 "$cpp" -lboost_filesystem -lboost_system -o "$bin"
    set +e
    "$bin"
    local status=$?
    set -e
    if [[ "$status" -ne 42 ]]; then
        echo "boost filesystem probe returned $status, expected 42" >&2
        exit 1
    fi
    echo "ok boost filesystem"
}

probe_imgui() {
    if ! pkg-config --exists imgui; then
        echo "skip imgui: pkg-config package not found"
        return
    fi

    local project="$repo_root/build/probe_imgui_context_project"
    local bin="$project/build/cmake-backend/build/probe_imgui_context"
    rm -rf "$project"
    mkdir -p "$project"
    cp "$repo_root/tests/fixtures/imgui_context.dd" "$project/main.dd"
    cat >"$project/dudu.toml" <<'EOF'
name = "probe_imgui_context"
main = "main.dd"
build_dir = "build"

[pkg]
libs = ["imgui"]
EOF
    "$repo_root/build/dudu" build "$project" --quiet
    set +e
    "$bin"
    local status=$?
    set -e
    if [[ "$status" -ne 42 ]]; then
        echo "imgui probe returned $status, expected 42" >&2
        exit 1
    fi
    echo "ok imgui"
}

probe_wayland() {
    if ! pkg-config --exists wayland-client; then
        echo "skip wayland-client: pkg-config package not found"
        return
    fi

    local cpp="$repo_root/build/probe_wayland_display.cpp"
    local bin="$repo_root/build/probe_wayland_display"
    "$repo_root/build/duc" emit "$repo_root/tests/fixtures/wayland_display_probe.dd" -o "$cpp"
    "${CXX:-c++}" -std=c++20 "$cpp" $(pkg-config --cflags --libs wayland-client) -o "$bin"
    set +e
    "$bin"
    local status=$?
    set -e
    if [[ "$status" -ne 42 ]]; then
        echo "wayland probe returned $status, expected 42" >&2
        exit 1
    fi
    echo "ok wayland-client"
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
    "${CXX:-c++}" -std=c++20 "$cpp" -pthread -o "$bin"
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

    cpp="$repo_root/build/probe_opencl_tensor_add.cpp"
    bin="$repo_root/build/probe_opencl_tensor_add"
    "$repo_root/build/duc" emit "$repo_root/tests/fixtures/tensor_dogfood/opencl_tensor_add.dd" -o "$cpp"
    "${CXX:-c++}" -std=c++20 -DCL_TARGET_OPENCL_VERSION=300 "$cpp" \
        $(pkg-config --cflags --libs OpenCL) -o "$bin"
    set +e
    "$bin"
    status=$?
    set -e
    if [[ "$status" -ne 42 ]]; then
        echo "OpenCL tensor probe returned $status, expected 42" >&2
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
    "${CXX:-c++}" -std=c++20 "$cpp" \
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

run_probe() {
    case "$1" in
        glm) probe_glm ;;
        opencv | opencv4) probe_opencv ;;
        sqlite | sqlite3) probe_sqlite ;;
        lua) probe_lua ;;
        zlib) probe_zlib ;;
        lzma | liblzma) probe_lzma ;;
        uuid) probe_uuid ;;
        curl | libcurl) probe_curl ;;
        libpng | png) probe_libpng ;;
        libjpeg | jpeg) probe_libjpeg ;;
        openssl) probe_openssl ;;
        libevent) probe_libevent ;;
        libxml2) probe_libxml2 ;;
        expat) probe_expat ;;
        cairo) probe_cairo ;;
        freetype) probe_freetype ;;
        fmt) probe_fmt ;;
        eigen | eigen3) probe_eigen ;;
        openblas | openblas_tensor_matmul) probe_openblas ;;
        spdlog) probe_spdlog ;;
        stb) probe_stb ;;
        x11) probe_x11 ;;
        boost_filesystem | boost-filesystem) probe_boost_filesystem ;;
        imgui) probe_imgui ;;
        wayland) probe_wayland ;;
        threading) probe_threading ;;
        posix_mmap | posix-mmap) probe_posix_mmap ;;
        posix_threads | posix-threads) probe_posix_threads ;;
        raylib) probe_raylib ;;
        sdl3) probe_sdl3 ;;
        glfw | glfw3) probe_glfw ;;
        opencl) probe_opencl ;;
        vulkan) probe_vulkan ;;
        ffmpeg) probe_ffmpeg ;;
        *)
            echo "unknown optional probe: $1" >&2
            exit 2
            ;;
    esac
}

if [[ "$#" -gt 0 ]]; then
    for probe in "$@"; do
        run_probe "$probe"
    done
    exit 0
fi

for probe in \
    glm opencv sqlite lua zlib lzma uuid curl libpng libjpeg openssl \
    libevent libxml2 expat cairo freetype fmt eigen openblas spdlog stb \
    x11 boost_filesystem imgui wayland threading posix_mmap posix_threads \
    raylib sdl3 glfw opencl vulkan ffmpeg; do
    run_probe "$probe"
done
