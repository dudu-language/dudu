#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

"$repo_root/build/duc" "$repo_root/tests/fixtures/c_api_export.dd" --emit-cpp \
    "$repo_root/build/c_api_export.cpp"
"$repo_root/build/duc" "$repo_root/tests/fixtures/c_api_export.dd" --emit-c-header \
    "$repo_root/build/c_api_export.h"
grep -q "int32_t exported_answer(void);" "$repo_root/build/c_api_export.h"
grep -q "int32_t exported_add(int32_t left, int32_t right);" "$repo_root/build/c_api_export.h"
grep -q "int32_t exported_load(int32_t\\* value);" "$repo_root/build/c_api_export.h"

"${CXX:-c++}" -std=c++20 -c "$repo_root/build/c_api_export.cpp" \
    -o "$repo_root/build/c_api_export.o"
printf '#include "c_api_export.h"\nint main(void) { int32_t value = 20; return exported_answer() + exported_add(5, 7) + exported_load(&value) - 32; }\n' \
    >"$repo_root/build/c_api_export_caller.c"
cc -std=c11 -I"$repo_root/build" -c "$repo_root/build/c_api_export_caller.c" \
    -o "$repo_root/build/c_api_export_caller.o"
"${CXX:-c++}" "$repo_root/build/c_api_export.o" "$repo_root/build/c_api_export_caller.o" \
    -o "$repo_root/build/c_api_export_caller"

set +e
"$repo_root/build/c_api_export_caller"
status=$?
set -e
if [[ "$status" -ne 42 ]]; then
    echo "c_api_export_caller returned $status, expected 42" >&2
    exit 1
fi
