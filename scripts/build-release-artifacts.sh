#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
output_dir="$repo_root/dist"
ref=""
allow_untagged=0

usage() {
    cat <<'USAGE'
usage: scripts/build-release-artifacts.sh [--output DIR] [--ref REF] [--allow-untagged]

Builds the deterministic tagged source archive, SHA-256 file, and immutable
release manifest. Public release builds require REF to be the exact vVERSION
tag. --allow-untagged exists only for local packaging development.
USAGE
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --output)
            output_dir="${2:?missing value for --output}"
            shift 2
            ;;
        --ref)
            ref="${2:?missing value for --ref}"
            shift 2
            ;;
        --allow-untagged)
            allow_untagged=1
            shift
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        *)
            echo "unknown option: $1" >&2
            usage >&2
            exit 2
            ;;
    esac
done

version="$(tr -d '\r\n' <"$repo_root/VERSION")"
tag="v$version"
ref="${ref:-$tag}"

if [[ "$allow_untagged" -eq 0 ]]; then
    [[ "$ref" == "$tag" ]] || {
        echo "release ref must be $tag, got $ref" >&2
        exit 1
    }
    git -C "$repo_root" rev-parse --verify --quiet "refs/tags/$tag^{commit}" >/dev/null || {
        echo "missing release tag: $tag" >&2
        exit 1
    }
    [[ -z "$(git -C "$repo_root" status --porcelain)" ]] || {
        echo "release artifacts require a clean Git tree" >&2
        exit 1
    }
fi

git -C "$repo_root" rev-parse --verify "$ref^{commit}" >/dev/null
commit="$(git -C "$repo_root" rev-parse "$ref^{commit}")"
archive="dudu-$version.tar.gz"
manifest="dudu-$version-manifest.txt"

rm -rf "$output_dir"
mkdir -p "$output_dir"
git -C "$repo_root" archive --format=tar --prefix="dudu-$version/" "$ref" |
    gzip -n >"$output_dir/$archive"

if command -v sha256sum >/dev/null 2>&1; then
    sha256="$(sha256sum "$output_dir/$archive" | awk '{print $1}')"
else
    sha256="$(shasum -a 256 "$output_dir/$archive" | awk '{print $1}')"
fi

printf '%s  %s\n' "$sha256" "$archive" >"$output_dir/$archive.sha256"
cat >"$output_dir/$manifest" <<EOF
schema=1
version=$version
tag=$tag
commit=$commit
source_archive=$archive
source_sha256=$sha256
EOF

printf 'source archive: %s\n' "$output_dir/$archive"
printf 'checksum:       %s\n' "$output_dir/$archive.sha256"
printf 'manifest:       %s\n' "$output_dir/$manifest"
