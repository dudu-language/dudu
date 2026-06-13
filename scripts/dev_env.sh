#!/usr/bin/env bash

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
dev_prefix="$repo_root/third_party/install"

if [[ -d "$dev_prefix/lib/pkgconfig" ]]; then
    export PKG_CONFIG_PATH="$dev_prefix/lib/pkgconfig${PKG_CONFIG_PATH:+:$PKG_CONFIG_PATH}"
fi

if [[ -d "$dev_prefix/lib" ]]; then
    export LD_LIBRARY_PATH="$dev_prefix/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
fi

