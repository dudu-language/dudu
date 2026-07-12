#!/usr/bin/env bash
set -euo pipefail

trap 'status=$?; printf "%s:%s: command failed (%s): %s\n" "${BASH_SOURCE[0]}" "$LINENO" "$status" "$BASH_COMMAND" >&2; exit "$status"' ERR

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
source "$repo_root/scripts/test_helpers.sh"

echo "test runner smoke: basic and zero-test suites"
"$repo_root/build/dudu" test "$repo_root/tests/fixtures/dudu_tests.dd" \
    >"$repo_root/build/dudu_tests.out" 2>"$repo_root/build/dudu_test_steps.err"
grep -q "3/3 tests passed" "$repo_root/build/dudu_tests.out"
grep -Eq "cmake build/dudu-tests/dudu_tests-[0-9a-f]+-cmake/source/CMakeLists\\.txt" \
    "$repo_root/build/dudu_test_steps.err"
grep -Eq "test .*/?build/dudu-tests/dudu_tests-[0-9a-f]+-cmake/build/dudu_tests-[0-9a-f]+" \
    "$repo_root/build/dudu_test_steps.err"
"$repo_root/build/dudu" test "$repo_root/tests/fixtures/simple_program.dd" \
    >"$repo_root/build/dudu_tests_zero.out"
grep -q "running 0 tests" "$repo_root/build/dudu_tests_zero.out"
grep -q "test result: ok. 0 passed; 0 failed; 0 filtered out" \
    "$repo_root/build/dudu_tests_zero.out"
echo "test runner smoke: discovery and filtering"
test_discovery_dir="$repo_root/build/dudu_test_discovery"
rm -rf "$test_discovery_dir"
mkdir -p "$test_discovery_dir"
cat >"$test_discovery_dir/not_a_test.dd" <<'DD'
def main() -> i32:
    text = "@test should not count inside a string"
    # @test should not count inside a comment
    return 0
DD
cat >"$test_discovery_dir/real_test.dd" <<'DD'
@test
def discovered():
    assert 40 + 2 == 42
DD
"$repo_root/build/dudu" test "$test_discovery_dir" \
    >"$repo_root/build/dudu_test_discovery.out"
grep -q "1/1 tests passed" "$repo_root/build/dudu_test_discovery.out"
grep -q "ok discovered" "$repo_root/build/dudu_test_discovery.out"
"$repo_root/build/dudu" test "$repo_root/tests/fixtures/dudu_tests.dd" --filter bool \
    >"$repo_root/build/dudu_tests_filter.out" 2>"$repo_root/build/dudu_test_filter_steps.err"
grep -q "1/1 tests passed" "$repo_root/build/dudu_tests_filter.out"
grep -q "ok bool_result" "$repo_root/build/dudu_tests_filter.out"
if grep -q "ok add_works" "$repo_root/build/dudu_tests_filter.out"; then
    echo "dudu test filter ran an unfiltered test" >&2
    exit 1
fi
unfiltered_test_binary=$(sed -n 's/^test //p' "$repo_root/build/dudu_test_steps.err")
filtered_test_binary=$(sed -n 's/^test //p' "$repo_root/build/dudu_test_filter_steps.err")
if [ "$unfiltered_test_binary" = "$filtered_test_binary" ]; then
    echo "dudu test reused the unfiltered binary path for a filtered test" >&2
    exit 1
fi
echo "test runner smoke: failure and decorator behavior"
cat >"$repo_root/build/dudu_failing_test.dd" <<'DD'
@test
def fails():
    assert 1 == 2
DD
if "$repo_root/build/dudu" test "$repo_root/build/dudu_failing_test.dd" \
    >"$repo_root/build/dudu_failing_test.out"; then
    echo "failing dudu test unexpectedly passed" >&2
    exit 1
fi
grep -q "FAILED fails: assert failed: 1 == 2" "$repo_root/build/dudu_failing_test.out"
cat >"$repo_root/build/dudu_assert_message_test.dd" <<'DD'
@test
def fails_with_message():
    assert 1 == 2, "numbers are still wrong"
DD
if "$repo_root/build/dudu" test "$repo_root/build/dudu_assert_message_test.dd" \
    >"$repo_root/build/dudu_assert_message_test.out"; then
    echo "dudu custom assert message test unexpectedly passed" >&2
    exit 1
fi
grep -q "FAILED fails_with_message: numbers are still wrong" \
    "$repo_root/build/dudu_assert_message_test.out"
cat >"$repo_root/build/dudu_test_decorators.dd" <<'DD'
@test.ignore
def slow_case():
    assert 1 == 2

@test.should_panic
def panics():
    assert 1 == 2

@test.should_panic("bad input")
def panics_with_message():
    assert 1 == 2, "bad input reached"
DD
"$repo_root/build/dudu" test "$repo_root/build/dudu_test_decorators.dd" \
    >"$repo_root/build/dudu_test_decorators.out"
grep -q "ignored slow_case" "$repo_root/build/dudu_test_decorators.out"
grep -q "ok panics" "$repo_root/build/dudu_test_decorators.out"
grep -q "ok panics_with_message" "$repo_root/build/dudu_test_decorators.out"
cat >"$repo_root/build/dudu_should_panic_fails.dd" <<'DD'
@test.should_panic
def does_not_panic():
    pass
DD
if "$repo_root/build/dudu" test "$repo_root/build/dudu_should_panic_fails.dd" \
    >"$repo_root/build/dudu_should_panic_fails.out"; then
    echo "dudu should_panic test unexpectedly passed without panic" >&2
    exit 1
fi
grep -q "FAILED does_not_panic: expected panic" \
    "$repo_root/build/dudu_should_panic_fails.out"
echo "test runner smoke: output capture"
cat >"$repo_root/build/dudu_capture_test.dd" <<'DD'
@test
def passing_output():
    print("hidden line")

@test
def failing_output():
    print("visible failure line")
    assert 1 == 2
DD
"$repo_root/build/dudu" test "$repo_root/build/dudu_capture_test.dd" --filter passing \
    >"$repo_root/build/dudu_capture_pass.out"
grep -q "ok passing_output" "$repo_root/build/dudu_capture_pass.out"
if grep -q "hidden line" "$repo_root/build/dudu_capture_pass.out"; then
    echo "dudu test leaked captured output from passing test" >&2
    exit 1
fi
"$repo_root/build/dudu" test "$repo_root/build/dudu_capture_test.dd" --filter passing \
    --no-capture >"$repo_root/build/dudu_capture_nocapture.out"
grep -q "hidden line" "$repo_root/build/dudu_capture_nocapture.out"
if "$repo_root/build/dudu" test "$repo_root/build/dudu_capture_test.dd" --filter failing \
    >"$repo_root/build/dudu_capture_fail.out"; then
    echo "dudu capture failure test unexpectedly passed" >&2
    exit 1
fi
grep -q "visible failure line" "$repo_root/build/dudu_capture_fail.out"
grep -q "FAILED failing_output: assert failed: 1 == 2" \
    "$repo_root/build/dudu_capture_fail.out"
echo "test runner smoke: codegen shapes"
"$repo_root/scripts/test_codegen_shapes.sh"
