#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
"$repo_root/scripts/build.sh"
ctest --test-dir "$repo_root/build" --output-on-failure \
    --parallel "${DUDU_TEST_JOBS:-4}"

"$repo_root/scripts/test_examples_and_install.sh"
"$repo_root/scripts/test_cli_and_project_smoke.sh"
"$repo_root/scripts/test_dudu_test_runner.sh"
"$repo_root/scripts/test_project_backends.sh"
"$repo_root/scripts/test_fixture_execution.sh"
