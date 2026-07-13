#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
staging="$(mktemp -d)"
trap 'rm -rf "$staging"' EXIT

if ! command -v npx >/dev/null 2>&1; then
    echo "site deploy requires npx" >&2
    exit 1
fi

"$repo_root/scripts/check_site.sh"
cp -R "$repo_root/site/." "$staging/"
cp "$repo_root/install.sh" "$staging/install.sh"

if [[ "${1:-}" == "--assemble-only" ]]; then
    echo "site assembly passed: $staging"
    exit 0
fi
if [[ $# -gt 0 ]]; then
    echo "usage: scripts/deploy-site.sh [--assemble-only]" >&2
    exit 2
fi

cd "$repo_root"
npx --yes wrangler@4.110.0 pages deploy "$staging" \
    --project-name dudu \
    --branch master
