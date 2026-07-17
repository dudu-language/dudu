#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
fixture_root="$repo_root/tests/fixtures/native_graduation"
workspace_root="$repo_root/build/native-graduation"
duc="$repo_root/build/duc"

all_libraries=(nlohmann_json boost_asio range_v3 protobuf entt_registry abseil)
header_only_libraries=(nlohmann_json boost_asio range_v3 entt_registry)

usage() {
    cat <<'EOF'
usage: scripts/probe_native_graduation.sh [gcc|clang|libcxx] [library...]

Runs the six-library native compatibility graduation with GCC/libstdc++ by
default. The libcxx mode covers the four header-only consumers because the
installed protobuf and Abseil binaries use the libstdc++ ABI.
EOF
}

mode="${1:-gcc}"
case "$mode" in
    gcc|clang|libcxx)
        shift || true
        ;;
    -h|--help)
        usage
        exit 0
        ;;
    *)
        mode=gcc
        ;;
esac

if [[ "$#" -gt 0 ]]; then
    libraries=("$@")
elif [[ "$mode" == libcxx ]]; then
    libraries=("${header_only_libraries[@]}")
else
    libraries=("${all_libraries[@]}")
fi

case "$mode" in
    gcc)
        native_compiler="${DUDU_GRADUATION_GXX:-g++}"
        native_flags=()
        native_link_flags=()
        ;;
    clang)
        native_compiler="${DUDU_GRADUATION_CLANGXX:-clang++}"
        native_flags=()
        native_link_flags=()
        ;;
    libcxx)
        native_compiler="${DUDU_GRADUATION_CLANGXX:-clang++}"
        native_flags=(-stdlib=libc++)
        native_link_flags=(-stdlib=libc++)
        ;;
esac

require_command() {
    if ! command -v "$1" >/dev/null 2>&1; then
        echo "native graduation requires: $1" >&2
        exit 1
    fi
}

require_file() {
    if [[ ! -f "$1" ]]; then
        echo "native graduation requires: $1" >&2
        exit 1
    fi
}

toml_array() {
    local first=1
    printf '['
    for value in "$@"; do
        if [[ "$first" -eq 0 ]]; then
            printf ', '
        fi
        printf '"%s"' "$value"
        first=0
    done
    printf ']'
}

ensure_entt() {
    local checkout="$repo_root/build/native_compat_deps/entt"
    if [[ -f "$checkout/src/entt/entt.hpp" ]]; then
        return
    fi
    require_command git
    mkdir -p "$(dirname "$checkout")"
    git clone --depth 1 --branch v3.15.0 https://github.com/skypjack/entt.git "$checkout"
}

write_manifest() {
    local library="$1"
    local project="$2"
    local packages=()
    local include_dirs=()
    local cpp_sources=()
    local libs=()

    case "$library" in
        boost_asio)
            libs=(pthread)
            ;;
        protobuf)
            packages=(protobuf)
            cpp_sources=(generated/player.pb.cc)
            include_dirs=(.)
            ;;
        entt_registry)
            include_dirs=("$repo_root/build/native_compat_deps/entt/src")
            ;;
        abseil)
            packages=(absl_flat_hash_map absl_strings)
            ;;
    esac

    {
        printf 'name = "native-graduation-%s-%s"\n' "$library" "$mode"
        printf 'entry = "main.dd"\n'
        printf 'build_dir = "build"\n'
        printf 'cpp_std = "c++20"\n\n'
        printf '[cc]\n'
        printf 'compiler = "%s"\n' "$native_compiler"
        printf 'flags = '
        toml_array "${native_flags[@]}"
        printf '\ninclude_dirs = '
        toml_array "${include_dirs[@]}"
        printf '\nlibs = '
        toml_array "${libs[@]}"
        printf '\n\n[link]\nflags = '
        toml_array "${native_link_flags[@]}"
        printf '\n\n[sources]\ncpp = '
        toml_array "${cpp_sources[@]}"
        printf '\n\n[pkg]\nlibs = '
        toml_array "${packages[@]}"
        printf '\n'
    } >"$project/dudu.toml"
}

prepare_project() {
    local library="$1"
    local project="$2"
    rm -rf "$project"
    mkdir -p "$project"
    cp "$fixture_root/$library.dd" "$project/main.dd"

    case "$library" in
        protobuf)
            require_command protoc
            mkdir -p "$project/generated"
            cp "$fixture_root/protobuf/player.proto" "$project/player.proto"
            protoc --cpp_out="$project/generated" --proto_path="$project" "$project/player.proto"
            ;;
        entt_registry)
            ensure_entt
            ;;
    esac
    write_manifest "$library" "$project"
}

validate_library() {
    local library="$1"
    case "$library" in
        nlohmann_json)
            require_file /usr/include/nlohmann/json.hpp
            ;;
        boost_asio)
            require_file /usr/include/boost/asio.hpp
            ;;
        range_v3)
            require_file /usr/include/range/v3/view/iota.hpp
            ;;
        protobuf)
            require_command pkg-config
            pkg-config --exists protobuf || {
                echo "native graduation requires pkg-config package: protobuf" >&2
                exit 1
            }
            ;;
        entt_registry)
            ;;
        abseil)
            require_command pkg-config
            pkg-config --exists absl_flat_hash_map absl_strings || {
                echo "native graduation requires Abseil pkg-config metadata" >&2
                exit 1
            }
            ;;
        *)
            echo "unknown native graduation library: $library" >&2
            usage >&2
            exit 2
            ;;
    esac
    if [[ "$mode" == libcxx && ("$library" == protobuf || "$library" == abseil) ]]; then
        echo "libcxx mode does not link system $library binaries built for libstdc++" >&2
        exit 2
    fi
}

run_probe() {
    local library="$1"
    local project="$workspace_root/$mode/$library"
    validate_library "$library"
    prepare_project "$library" "$project"

    echo "[$mode] $library: check"
    "$duc" check "$project/main.dd" --quiet
    echo "[$mode] $library: emit"
    "$duc" emit "$project/main.dd" -o "$project/emitted.cpp"
    test -s "$project/emitted.cpp"
    echo "[$mode] $library: build and run"
    "$duc" run "$project" --quiet
    echo "[$mode] $library: pass"
}

require_command "$native_compiler"
if [[ ! -x "$duc" ]]; then
    "$repo_root/scripts/build.sh" >/dev/null
fi

for library in "${libraries[@]}"; do
    run_probe "$library"
done

echo "native graduation passed: $mode (${libraries[*]})"
