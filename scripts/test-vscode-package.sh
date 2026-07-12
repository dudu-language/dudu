#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
version="$(tr -d '\r\n' <"$repo_root/VERSION")"
work="$repo_root/build/vscode-package-test"
vsix="$work/dudu-$version.vsix"

rm -rf "$work"
mkdir -p "$work"
"$repo_root/scripts/package-vscode.sh" --output "$vsix"

if command -v code >/dev/null 2>&1; then
    code --user-data-dir "$work/user" --extensions-dir "$work/extensions" \
        --install-extension "$vsix" --force >/dev/null
    code --user-data-dir "$work/user" --extensions-dir "$work/extensions" \
        --list-extensions --show-versions | grep -Fqx 'dudu.dudu@0.1.0'
fi

echo "VSIX package checks passed"
