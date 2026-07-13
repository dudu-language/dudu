#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
duc="${1:-$repo_root/build/duc}"
cxx="${CXX:-c++}"
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT

"$duc" emit-modules "$repo_root/tests/fixtures/macro_sdk_import.dd" -o "$work/generated"
cat >"$work/bridge.cpp" <<'CPP'
#include "dudu/macro/macro_sdk_bridge_generated.hpp"

int main() {
    dudu::macro::protocol::Expansion encoded;
    auto sdk = dudu::macro::sdk_bridge::from_protocol(encoded);
    auto roundtrip = dudu::macro::sdk_bridge::to_protocol(sdk);
    return static_cast<int>(roundtrip.members.size());
}
CPP

"$cxx" -std=c++20 -I"$repo_root/src" -I"$work/generated" \
    "$work/bridge.cpp" "$work/generated/dudu/ast.cpp" -o "$work/bridge"
"$work/bridge"
