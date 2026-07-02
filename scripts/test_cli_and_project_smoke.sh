#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
source "$repo_root/scripts/test_helpers.sh"

"$repo_root/build/dudu" "$repo_root/tests/fixtures/simple_program.dd" --format - >/dev/null
"$repo_root/build/duc" --version | grep -q '^duc 0\.1\.0$'
"$repo_root/build/dudu" --version | grep -q '^dudu 0\.1\.0$'
"$repo_root/build/dudu" bench --version | grep -q '^dudu 0\.1\.0$'
"$repo_root/build/duc" "$repo_root/tests/fixtures/simple_program.dd" --check
"$repo_root/build/duc" check "$repo_root/tests/fixtures/simple_program.dd"
"$repo_root/build/duc" emit "$repo_root/tests/fixtures/simple_program.dd" \
    -o "$repo_root/build/duc_emit_simple.cpp"
grep -q "dudu: .*simple_program.dd:7:" "$repo_root/build/duc_emit_simple.cpp"
"$repo_root/scripts/test_c_api.sh"
"$repo_root/scripts/test_dynamic_library.sh"
"$repo_root/build/duc" fmt "$repo_root/tests/fixtures/simple_program.dd" \
    -o "$repo_root/build/duc_fmt_simple.dd"
"$repo_root/build/duc" fmt "$repo_root/tests/fixtures/simple_program.dd" --check
if "$repo_root/build/duc" fmt "$repo_root/tests/fixtures/unformatted.dd" --check \
    2>"$repo_root/build/duc_fmt_check.err"; then
    echo "unformatted fixture unexpectedly passed format check" >&2
    exit 1
fi
grep -q "would reformat" "$repo_root/build/duc_fmt_check.err"
fmt_dir="$repo_root/build/fmt_dir"
rm -rf "$fmt_dir"
mkdir -p "$fmt_dir"
cp "$repo_root/tests/fixtures/unformatted.dd" "$fmt_dir/sample.dd"
"$repo_root/build/duc" fmt "$fmt_dir"
"$repo_root/build/duc" fmt "$fmt_dir" --check
"$repo_root/build/duc" fmt "$repo_root/examples" --check
fmt_project="$repo_root/build/fmt_project"
rm -rf "$fmt_project"
mkdir -p "$fmt_project/src"
cat >"$fmt_project/dudu.toml" <<'TOML'
name = "fmt_project"
entry = "src/main.dd"
[build]
dir = "out"
TOML
printf 'def main() -> i32:   \n    return 0\n' >"$fmt_project/src/main.dd"
printf 'def helper() -> i32:   \n    return 42\n' >"$fmt_project/src/helper.dd"
mkdir -p "$fmt_project/build" "$fmt_project/out"
printf 'def generated() -> i32:   \n    return 1\n' >"$fmt_project/build/generated.dd"
printf 'def generated() -> i32:   \n    return 2\n' >"$fmt_project/out/generated.dd"
(
    cd "$fmt_project"
    if "$repo_root/build/dudu" fmt --check 2>"$repo_root/build/dudu_fmt_project_check.err"; then
        echo "unformatted dudu project unexpectedly passed format check" >&2
        exit 1
    fi
    if grep -q "generated.dd" "$repo_root/build/dudu_fmt_project_check.err"; then
        echo "dudu fmt --check should ignore build directories" >&2
        exit 1
    fi
    "$repo_root/build/dudu" fmt
    "$repo_root/build/dudu" fmt --check
    grep -q 'def generated() -> i32:   ' build/generated.dd
    grep -q 'def generated() -> i32:   ' out/generated.dd
)
grep -q "./src/main.dd: would reformat" "$repo_root/build/dudu_fmt_project_check.err"
grep -q "./src/helper.dd: would reformat" "$repo_root/build/dudu_fmt_project_check.err"
"$repo_root/build/duc" run "$repo_root/tests/fixtures/run_zero.dd" \
    -o "$repo_root/build/duc_run_zero"
clean_smoke="$repo_root/build/clean_smoke"
rm -rf "$clean_smoke"
"$repo_root/build/dudu" new "$clean_smoke" >/dev/null
(
    cd "$clean_smoke"
    "$repo_root/build/dudu" run -- --user-flag "two words" >/dev/null \
        2>"$repo_root/build/dudu_run_steps.err"
    test -d build
    "$repo_root/build/dudu" build >/dev/null 2>"$repo_root/build/dudu_build_cached.err"
    "$repo_root/build/dudu" build --quiet >/dev/null 2>"$repo_root/build/dudu_build_quiet.err"
    "$repo_root/build/dudu" clean 2>"$repo_root/build/dudu_clean.err"
    test ! -e build
)
grep -Eq "backend cmake" "$repo_root/build/dudu_run_steps.err"
grep -Eq "cmake .*clean_smoke/build/cmake-backend/source/CMakeLists.txt" \
    "$repo_root/build/dudu_run_steps.err"
grep -Eq "emit .*clean_smoke/build/cmake-backend/build/generated" \
    "$repo_root/build/dudu_run_steps.err"
grep -Eq "run .*clean_smoke/build/cmake-backend/build/clean_smoke" \
    "$repo_root/build/dudu_run_steps.err"
grep -Eq "run .*--user-flag.*two words" "$repo_root/build/dudu_run_steps.err"
grep -Eq "output .*clean_smoke/build/cmake-backend/build/clean_smoke" \
    "$repo_root/build/dudu_build_cached.err"
test ! -s "$repo_root/build/dudu_build_quiet.err"
grep -q "clean ./build" "$repo_root/build/dudu_clean.err"
cache_smoke="$repo_root/build/clean_cache_smoke"
rm -rf "$cache_smoke"
mkdir -p "$cache_smoke/include" "$cache_smoke/src"
cp "$repo_root/tests/fixtures/native_headers/simple_cpp.hpp" "$cache_smoke/include/simple_cpp.hpp"
cat >"$cache_smoke/dudu.toml" <<'TOML'
name = "clean_cache_smoke"
entry = "src/main.dd"

[include]
paths = ["include"]

[build]
dir = "build"
TOML
cat >"$cache_smoke/src/main.dd" <<'DD'
from cpp.path import ../include/simple_cpp.hpp


def main() -> i32:
    return dudu_native.add(20, 22)
DD
(
    cd "$cache_smoke"
    "$repo_root/build/dudu" build >/dev/null 2>/dev/null
    test -d build/dudu-header-cache
    "$repo_root/build/dudu" clean-cache 2>"$repo_root/build/dudu_clean_cache.err"
    test ! -e build/dudu-header-cache
)
grep -Eq "clean-cache .*clean_cache_smoke/build/dudu-header-cache" \
    "$repo_root/build/dudu_clean_cache.err"
