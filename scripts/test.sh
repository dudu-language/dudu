#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
"$repo_root/scripts/build.sh"

"$repo_root/build/dudu" "$repo_root/examples/hello.dd" --emit-cpp "$repo_root/build/hello.cpp"
c++ -std=c++20 "$repo_root/build/hello.cpp" -o "$repo_root/build/hello"
"$repo_root/build/hello"

"$repo_root/build/dudu" "$repo_root/examples/use_math.dd" --emit-cpp "$repo_root/build/use_math.cpp"
c++ -std=c++20 "$repo_root/build/use_math.cpp" -o "$repo_root/build/use_math"
"$repo_root/build/use_math"

"$repo_root/build/dudu" "$repo_root/examples/raylib_window.dd" --emit-cpp "$repo_root/build/raylib_window.cpp"
echo "emitted raylib example; compile it on a machine with raylib headers/libs"
