#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
"$repo_root/scripts/build.sh"

required_examples=(
    allocators.dd
    audio_synth.dd
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
done

echo "compiler builds and canonical examples are present"
