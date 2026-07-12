#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
work="$repo_root/build/package-test"
version="$(tr -d '\r\n' <"$repo_root/VERSION")"

rm -rf "$work"
mkdir -p "$work"
"$repo_root/scripts/build-release-artifacts.sh" \
    --output "$work/artifacts" --ref HEAD --allow-untagged >/dev/null
manifest="$work/artifacts/dudu-$version-manifest.txt"
"$repo_root/scripts/generate-package-recipes.sh" \
    --manifest "$manifest" --output "$work/recipes" >/dev/null

grep -Fq -- '-DDUDU_INSTALL_OWNER=homebrew' "$work/recipes/homebrew/dudu.rb"
grep -Fq -- '-DDUDU_INSTALL_OWNER=aur' "$work/recipes/aur/PKGBUILD"
grep -Fq "releases/download/v$version/dudu-$version.tar.gz" \
    "$work/recipes/homebrew/dudu.rb"
grep -Fq "$(awk -F= '$1 == "source_sha256" { print $2 }' "$manifest")" \
    "$work/recipes/aur/PKGBUILD"
bash -n "$work/recipes/aur/PKGBUILD"

"$repo_root/scripts/build-deb.sh" \
    --build-dir "$work/deb-build" --output "$work/deb" >/dev/null
deb="$(find "$work/deb" -name 'dudu_*.deb' -print -quit)"
[[ -f "$deb" ]]
dpkg-deb --info "$deb" >/dev/null
dpkg-deb --extract "$deb" "$work/deb-root"
grep -Fqx deb "$work/deb-root/usr/share/dudu/install-owner"
"$work/deb-root/usr/bin/dudu" --version | grep -Fqx "dudu $version"
if "$work/deb-root/usr/bin/dudu" update --check >"$work/deb-update.out" \
    2>"$work/deb-update.err"; then
    echo "deb-owned Dudu unexpectedly allowed self-update" >&2
    exit 1
fi
grep -Fq "owned by dpkg" "$work/deb-update.err"

echo "package recipe checks passed"
