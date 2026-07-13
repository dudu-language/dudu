#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
installer="$repo_root/install.sh"

help="$($installer --help)"
grep -Fq -- "--install-deps" <<<"$help"
grep -Fq -- "--no-install-deps" <<<"$help"
grep -Fq -- "--print-deps" <<<"$help"

dependency_command="$($installer --print-deps)"
[[ -n "$dependency_command" ]]
case "$(uname -s)" in
    Linux) grep -Eq "apt-get|dnf|pacman" <<<"$dependency_command" ;;
    Darwin) grep -Fq "brew install cmake llvm pkg-config" <<<"$dependency_command" ;;
esac

set +e
missing_output="$(CXX=dudu-test-missing-cxx "$installer" \
    --check --no-install-deps 2>&1)"
missing_status=$?
set -e
[[ "$missing_status" -ne 0 ]]
grep -Fq "missing native dependencies:" <<<"$missing_output"
grep -Fq "dudu-test-missing-cxx" <<<"$missing_output"
grep -Fq "rerun with --install-deps" <<<"$missing_output"

echo "installer dependency checks passed"
