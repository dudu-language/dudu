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

echo "dependency smoke ok"
