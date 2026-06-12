#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
"$repo_root/scripts/build.sh"
ctest --test-dir "$repo_root/build" --output-on-failure

required_examples=(
    allocators.dd
    audio_synth.dd
    compile_time.dd
    cpp_library.dd
    cuda_kernel.dd
    function_pointers.dd
    image_filter.dd
    layout_hardware.dd
    modules_visibility.dd
    native_escape.dd
    numerics_kmeans.dd
    raylib_game.dd
    shader_compute.dd
    systems_mmap.dd
    threading_atomics.dd
    web_server.dd
)

for example in "${required_examples[@]}"; do
    test -f "$repo_root/examples/$example"
    "$repo_root/build/dudu" "$repo_root/examples/$example" --check
done

generated_header="$repo_root/build/cpp_library.hpp"
"$repo_root/build/dudu" "$repo_root/examples/cpp_library.dd" --emit-header "$generated_header"
printf '#include "cpp_library.hpp"\nint main() { return 0; }\n' >"$repo_root/build/header_smoke.cpp"
"${CXX:-c++}" -std=c++20 -I"$repo_root/build" -c "$repo_root/build/header_smoke.cpp" \
    -o "$repo_root/build/header_smoke.o"

compile_and_expect() {
    local name="$1"
    local expected="$2"
    local cpp="$repo_root/build/$name.cpp"
    local bin="$repo_root/build/$name"

    "$repo_root/build/dudu" "$repo_root/tests/fixtures/$name.dd" --emit-cpp "$cpp"
    "${CXX:-c++}" -std=c++20 "$cpp" -o "$bin"
    set +e
    "$bin"
    local status=$?
    set -e
    if [[ "$status" -ne "$expected" ]]; then
        echo "$name returned $status, expected $expected" >&2
        exit 1
    fi
}

compile_and_expect simple_program 42
compile_and_expect control_flow 55

echo "compiler builds and canonical examples are present"
