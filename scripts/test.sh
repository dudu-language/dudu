#!/usr/bin/env bash
set -euo pipefail

trap 'status=$?; printf "%s:%s: command failed (%s): %s\n" "${BASH_SOURCE[0]}" "$LINENO" "$status" "$BASH_COMMAND" >&2; exit "$status"' ERR

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
"$repo_root/scripts/build.sh"
ctest --test-dir "$repo_root/build" --output-on-failure

echo "test stage: examples and install"
"$repo_root/scripts/test_examples_and_install.sh"
echo "test stage: CLI and project smoke"
"$repo_root/scripts/test_cli_and_project_smoke.sh"
echo "test stage: Dudu test runner"
"$repo_root/scripts/test_dudu_test_runner.sh"
echo "test stage: project backends"
"$repo_root/scripts/test_project_backends.sh"
echo "test stage: fixture execution"
"$repo_root/scripts/test_fixture_execution.sh"
