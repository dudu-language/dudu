#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
dudu_bin="$repo_root/build/dudu"
work_root="$repo_root/build/dependency_smoke"

if [[ ! -x "$dudu_bin" ]]; then
    echo "missing built dudu binary: $dudu_bin" >&2
    exit 1
fi

rm -rf "$work_root"
mkdir -p "$work_root"

expect_failure_contains() {
    local expected="$1"
    shift
    local output
    set +e
    output="$("$@" 2>&1)"
    local status=$?
    set -e
    if [[ "$status" -eq 0 ]]; then
        printf 'expected failure but command passed: %s\n' "$*" >&2
        exit 1
    fi
    if [[ "$output" != *"$expected"* ]]; then
        printf 'expected diagnostic containing: %s\nactual output:\n%s\n' "$expected" "$output" >&2
        exit 1
    fi
}

path_dep="$work_root/local_math"
path_app="$work_root/path_app"
mkdir -p "$path_dep/src" "$path_app/src"

cat >"$path_dep/dudu.toml" <<'TOML'
name = "local_math"
entry = "src/local_math.dd"
TOML

cat >"$path_dep/src/local_math.dd" <<'DD'
def value() -> i32:
    return 42
DD

cat >"$path_app/dudu.toml" <<'TOML'
name = "path_app"
entry = "src/main.dd"

[deps]
local_math = { path = "../local_math" }
TOML

cat >"$path_app/src/main.dd" <<'DD'
from local_math import value


def main() -> i32:
    print(value())
    return 0
DD

(
    cd "$path_app"
    "$dudu_bin" deps fetch --quiet
    before="$(sha256sum dudu.lock | awk '{print $1}')"
    output="$("$dudu_bin" run --quiet)"
    after="$(sha256sum dudu.lock | awk '{print $1}')"
    [[ "$before" == "$after" ]]
    [[ "$output" == "42" ]]
    grep -q 'name = "local_math"' dudu.lock
    grep -q 'kind = "path"' dudu.lock
)

git_dep="$work_root/gitmath"
git_app="$work_root/git_app"
mkdir -p "$git_dep/src" "$git_app/src"

cat >"$git_dep/dudu.toml" <<'TOML'
name = "gitmath"
entry = "src/gitmath.dd"
TOML

cat >"$git_dep/src/gitmath.dd" <<'DD'
def value() -> i32:
    return 7
DD

git -C "$git_dep" init -q
git -C "$git_dep" add .
git -C "$git_dep" \
    -c user.name="Dudu Tests" \
    -c user.email="dudu-tests@example.invalid" \
    commit -q -m "initial git dep"
git_rev="$(git -C "$git_dep" rev-parse HEAD)"

cat >"$git_app/dudu.toml" <<TOML
name = "git_app"
entry = "src/main.dd"

[deps]
gitmath = { git = "file://$git_dep", rev = "$git_rev" }
TOML

cat >"$git_app/src/main.dd" <<'DD'
from gitmath import value


def main() -> i32:
    print(value())
    return 0
DD

(
    cd "$git_app"
    "$dudu_bin" deps fetch --quiet
    output="$("$dudu_bin" run --quiet)"
    [[ "$output" == "7" ]]
    grep -q 'name = "gitmath"' dudu.lock
    grep -q 'kind = "git"' dudu.lock
    grep -q "$git_rev" dudu.lock
    test -d ".dudu/deps/gitmath/.git"
)

bad_path_app="$work_root/bad_path_app"
mkdir -p "$bad_path_app/src"
cat >"$bad_path_app/dudu.toml" <<'TOML'
name = "bad_path_app"
entry = "src/main.dd"

[deps]
missing_math = { path = "../missing_math" }
TOML
cat >"$bad_path_app/src/main.dd" <<'DD'
def main() -> i32:
    return 0
DD
(
    cd "$bad_path_app"
    expect_failure_contains "Dudu dependency 'missing_math' path not found" \
        "$dudu_bin" deps fetch --quiet
)

bad_root_dep="$work_root/not_a_package"
bad_root_app="$work_root/bad_root_app"
mkdir -p "$bad_root_dep" "$bad_root_app/src"
cat >"$bad_root_dep/helper.dd" <<'DD'
def value() -> i32:
    return 9
DD
cat >"$bad_root_app/dudu.toml" <<'TOML'
name = "bad_root_app"
entry = "src/main.dd"

[deps]
not_a_package = { path = "../not_a_package" }
TOML
cat >"$bad_root_app/src/main.dd" <<'DD'
def main() -> i32:
    return 0
DD
(
    cd "$bad_root_app"
    expect_failure_contains "Dudu dependency 'not_a_package' package root is missing dudu.toml" \
        "$dudu_bin" deps fetch --quiet
)

bad_ref_app="$work_root/bad_ref_app"
mkdir -p "$bad_ref_app/src"
cat >"$bad_ref_app/dudu.toml" <<TOML
name = "bad_ref_app"
entry = "src/main.dd"

[deps]
gitmath = { git = "file://$git_dep", tag = "definitely_missing_tag" }
TOML
cat >"$bad_ref_app/src/main.dd" <<'DD'
def main() -> i32:
    return 0
DD
(
    cd "$bad_ref_app"
    expect_failure_contains "checkout --quiet 'tags/definitely_missing_tag'" \
        "$dudu_bin" deps fetch --quiet
)

bad_git_app="$work_root/bad_git_app"
mkdir -p "$bad_git_app/src"
cat >"$bad_git_app/dudu.toml" <<TOML
name = "bad_git_app"
entry = "src/main.dd"

[deps]
ghost = { git = "file://$work_root/definitely_missing_git_repo", rev = "abc123" }
TOML
cat >"$bad_git_app/src/main.dd" <<'DD'
def main() -> i32:
    return 0
DD
(
    cd "$bad_git_app"
    expect_failure_contains "git clone --quiet 'file://$work_root/definitely_missing_git_repo'" \
        "$dudu_bin" deps fetch --quiet
)

native_dep_app="$work_root/native_dep_app"
mkdir -p "$native_dep_app/src"
cat >"$native_dep_app/dudu.toml" <<'TOML'
name = "native_dep_app"
entry = "src/main.dd"

[pkg]
libs = ["definitely_missing_dudu_native_pkg"]
TOML
cat >"$native_dep_app/src/main.dd" <<'DD'
def main() -> i32:
    return 0
DD
(
    cd "$native_dep_app"
    expect_failure_contains "definitely_missing_dudu_native_pkg" \
        "$dudu_bin" build --quiet
)

echo "dependency smoke ok"
