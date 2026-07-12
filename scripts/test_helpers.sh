#!/usr/bin/env bash

compile_example_object() {
    local example="$1"
    local name
    name="$(basename "$example" .dd)"
    local cpp="$repo_root/build/example_$name.cpp"
    local object="$repo_root/build/example_$name.o"

    "$repo_root/build/dudu" "$repo_root/examples/$example" --emit-cpp "$cpp"
    "${CXX:-c++}" -std=c++20 -c "$cpp" -o "$object"
}

compile_x86_example_object() {
    local example="$1"
    local host_arch="${DUDU_TEST_ARCH:-$(uname -m)}"
    case "$host_arch" in
        x86_64|amd64)
            compile_example_object "$example"
            ;;
        *)
            echo "skip $example object: x86-specific example on $host_arch"
            ;;
    esac
}

compile_and_expect() {
    local name="$1"
    local expected="$2"
    local cpp="$repo_root/build/$name.cpp"
    local bin="$repo_root/build/$name"

    "$repo_root/build/dudu" "$repo_root/tests/fixtures/$name.dd" --emit-cpp "$cpp"
    "${CXX:-c++}" -std=c++20 -I"$repo_root/tests/fixtures" "$cpp" -o "$bin"
    set +e
    "$bin"
    local status=$?
    set -e
    if [[ "$status" -ne "$expected" ]]; then
        echo "$name returned $status, expected $expected" >&2
        exit 1
    fi
}

compile_path_and_expect() {
    local name="$1"
    local path="$2"
    local expected="$3"
    local cpp="$repo_root/build/$name.cpp"
    local bin="$repo_root/build/$name"

    "$repo_root/build/dudu" "$repo_root/$path" --emit-cpp "$cpp"
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

expect_fail() {
    local name="$1"
    local mode="$2"
    local expected="$3"
    local cmd=("$repo_root/build/dudu" "$repo_root/tests/fixtures/$name.dd" "$mode")
    if [[ "$mode" != "--check" ]]; then
        cmd+=("$repo_root/build/$name.out")
    fi
    if "${cmd[@]}" 2>"$repo_root/build/$name.err"; then
        echo "$name unexpectedly passed" >&2
        exit 1
    fi
    if ! grep -q "$expected" "$repo_root/build/$name.err"; then
        echo "$name failed with an unexpected diagnostic; expected: $expected" >&2
        cat "$repo_root/build/$name.err" >&2
        exit 1
    fi
}
