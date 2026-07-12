#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
source "$repo_root/scripts/test_helpers.sh"

"$repo_root/scripts/build.sh" >/dev/null

check_example() {
    local example="$1"
    echo "example check $example"
    "$repo_root/build/dudu" "$repo_root/examples/$example" --check
}

object_example() {
    local example="$1"
    echo "example object $example"
    compile_example_object "$example"
}

toml_string_array() {
    local first=1
    printf '['
    for item in "$@"; do
        if [[ "$first" == 0 ]]; then
            printf ', '
        fi
        first=0
        printf '"%s"' "$item"
    done
    printf ']'
}

build_example_project_with_pkg() {
    local example="$1"
    local extra_cxx_flags="$2"
    shift 2
    local name
    name="$(basename "$example" .dd)"
    local packages=("$@")

    for pkg in "${packages[@]}"; do
        if ! pkg-config --exists "$pkg"; then
            echo "skip $example: pkg-config package not found: $pkg"
            return
        fi
    done

    local project="$repo_root/build/example_projects/$name"
    rm -rf "$project"
    mkdir -p "$project/src"
    cp "$repo_root/examples/$example" "$project/src/main.dd"

    {
        printf 'name = "example-%s"\n' "$name"
        printf 'entry = "src/main.dd"\n\n'
        printf '[cxx]\nstandard = "c++20"\n\n'
        if [[ -n "$extra_cxx_flags" ]]; then
            printf '[cc]\nflags = '
            # shellcheck disable=SC2086
            toml_string_array $extra_cxx_flags
            printf '\n\n'
        fi
        printf '[pkg]\nlibs = '
        toml_string_array "${packages[@]}"
        printf '\n\n'
        printf '[build]\ndir = "build"\n'
    } >"$project/dudu.toml"

    echo "example build $example (${packages[*]})"
    "$repo_root/build/dudu" build "$project" --quiet
}

check_example_project_with_pkg() {
    local example="$1"
    shift
    local name
    name="$(basename "$example" .dd)"
    local packages=("$@")

    for pkg in "${packages[@]}"; do
        if ! pkg-config --exists "$pkg"; then
            echo "skip $example: pkg-config package not found: $pkg"
            return
        fi
    done

    local project="$repo_root/build/example_projects/$name"
    rm -rf "$project"
    mkdir -p "$project/src"
    cp "$repo_root/examples/$example" "$project/src/main.dd"

    {
        printf 'name = "example-%s"\n' "$name"
        printf 'entry = "src/main.dd"\n\n'
        printf '[cxx]\nstandard = "c++20"\n\n'
        printf '[pkg]\nlibs = '
        toml_string_array "${packages[@]}"
        printf '\n\n'
        printf '[build]\ndir = "build"\n'
    } >"$project/dudu.toml"

    echo "example check $example (${packages[*]})"
    "$repo_root/build/dudu" check "$project" --quiet
}

required_check_examples=(
    allocators.dd
    compile_time.dd
    cpp_library.dd
    cuda_kernel.dd
    cuda_shared_memory_tile.dd
    fibonacci.dd
    function_pointers.dd
    interrupt_handler.dd
    layout_hardware.dd
    macro_bomb.dd
    modules_visibility.dd
    native_escape.dd
    numerics_kmeans.dd
    shader_compute.dd
    systems_mmap.dd
    threading_atomics.dd
    web_server.dd
)

required_object_examples=(
    allocators.dd
    compile_time.dd
    cpp_library.dd
    fibonacci.dd
    function_pointers.dd
    layout_hardware.dd
    macro_bomb.dd
    modules_visibility.dd
    numerics_kmeans.dd
    shader_compute.dd
    systems_mmap.dd
    threading_atomics.dd
)

for example in "${required_check_examples[@]}"; do
    check_example "$example"
done

for example in "${required_object_examples[@]}"; do
    object_example "$example"
done

compile_x86_example_object native_escape.dd

build_example_project_with_pkg audio_synth.dd "" raylib
build_example_project_with_pkg raylib_game.dd "" raylib
build_example_project_with_pkg sdl3_window.dd "" sdl3
build_example_project_with_pkg glfw_opengl_triangle.dd "" glfw3
build_example_project_with_pkg opencl_kernel_host.dd "-DCL_TARGET_OPENCL_VERSION=300" OpenCL
build_example_project_with_pkg vulkan_triangle.dd "" vulkan
build_example_project_with_pkg ffmpeg_probe_decode.dd "" libavcodec
build_example_project_with_pkg image_filter.dd "" opencv4

if pkg-config --exists sdl3 imgui; then
    check_example_project_with_pkg sdl3_imgui.dd sdl3 imgui
else
    echo "skip sdl3_imgui.dd: pkg-config packages not found: sdl3 and imgui"
fi

echo "examples ok"
