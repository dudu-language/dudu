#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build_dir="${DUDU_LIBCXX_BUILD_DIR:-$repo_root/build/libcxx}"
jobs="${DUDU_BUILD_JOBS:-4}"

command -v clang++ >/dev/null 2>&1 || {
    echo "libc++ validation requires clang++" >&2
    exit 1
}

probe="$(mktemp --suffix=.cpp)"
trap 'rm -f "$probe"' EXIT
printf '#include <filesystem>\nint main() { return 0; }\n' >"$probe"
if ! clang++ -std=c++20 -stdlib=libc++ -fsyntax-only "$probe" >/dev/null 2>&1; then
    echo "libc++ validation requires libc++ development headers" >&2
    echo "Ubuntu: sudo apt install libc++-18-dev libc++abi-18-dev" >&2
    exit 1
fi

cmake -S "$repo_root" -B "$build_dir" \
    -DCMAKE_BUILD_TYPE=Debug \
    -DCMAKE_CXX_COMPILER=clang++ \
    -DCMAKE_CXX_FLAGS=-stdlib=libc++ \
    -DCMAKE_EXE_LINKER_FLAGS=-stdlib=libc++ \
    -DDUDU_BUILD_TESTS=ON \
    -DDUDU_STRICT=ON \
    -DDUDU_WARN_AS_ERROR=ON
cmake --build "$build_dir" --parallel "$jobs"
ctest --test-dir "$build_dir" --output-on-failure

echo "clang + libc++ portability checks passed"
