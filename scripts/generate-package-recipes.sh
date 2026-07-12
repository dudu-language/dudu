#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
manifest=""
output_dir="$repo_root/dist/recipes"

usage() {
    cat <<'USAGE'
usage: scripts/generate-package-recipes.sh --manifest FILE [--output DIR]

Generates immutable-tag Homebrew and AUR recipes from a Dudu release manifest.
USAGE
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --manifest)
            manifest="${2:?missing value for --manifest}"
            shift 2
            ;;
        --output)
            output_dir="${2:?missing value for --output}"
            shift 2
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

[[ -f "$manifest" ]] || {
    echo "--manifest must name an existing release manifest" >&2
    exit 2
}

manifest_value() {
    local key="$1"
    awk -F= -v key="$key" '$1 == key { print substr($0, length(key) + 2) }' "$manifest"
}

version="$(manifest_value version)"
tag="$(manifest_value tag)"
archive="$(manifest_value source_archive)"
sha256="$(manifest_value source_sha256)"
[[ -n "$version" && "$tag" == "v$version" && "$archive" == "dudu-$version.tar.gz" ]]
[[ "$sha256" =~ ^[0-9a-f]{64}$ ]]
url="https://github.com/wegfawefgawefg/dudu/releases/download/$tag/$archive"
aur_version="${version//-/_}"

mkdir -p "$output_dir/homebrew" "$output_dir/aur"
cat >"$output_dir/homebrew/dudu.rb" <<EOF
class Dudu < Formula
  desc "Python-shaped systems language with direct C and C++ interop"
  homepage "https://github.com/wegfawefgawefg/dudu"
  url "$url"
  version "$version"
  sha256 "$sha256"
  license any_of: ["MIT", "Apache-2.0"]

  depends_on "cmake" => :build
  depends_on "llvm"

  def install
    system "cmake", "-S", ".", "-B", "build",
           *std_cmake_args,
           "-DCMAKE_BUILD_TYPE=Release",
           "-DDUDU_BUILD_TESTS=OFF",
           "-DDUDU_INSTALL_OWNER=homebrew",
           "-DLLVM_ROOT=#{Formula["llvm"].opt_prefix}"
    system "cmake", "--build", "build", "--parallel"
    system "cmake", "--install", "build"
  end

  test do
    assert_match "$version", shell_output("#{bin}/dudu --version")
    system bin/"dudu", "init", testpath/"hello"
    system bin/"dudu", "check", testpath/"hello"
  end
end
EOF

cat >"$output_dir/aur/PKGBUILD" <<EOF
# Maintainer: Dudu Language <https://github.com/wegfawefgawefg/dudu/issues>
pkgname=dudu
pkgver=$aur_version
pkgrel=1
pkgdesc='Python-shaped systems language with direct C and C++ interop'
arch=('x86_64' 'aarch64')
url='https://github.com/wegfawefgawefg/dudu'
license=('MIT' 'Apache-2.0')
depends=('clang' 'gcc-libs' 'glibc')
makedepends=('cmake')
source=("$archive::$url")
sha256sums=('$sha256')

build() {
    cmake -S "\$srcdir/dudu-$version" -B build \\
        -DCMAKE_BUILD_TYPE=Release \\
        -DCMAKE_INSTALL_PREFIX=/usr \\
        -DDUDU_BUILD_TESTS=OFF \\
        -DDUDU_INSTALL_OWNER=aur
    cmake --build build --parallel
}

check() {
    build/dudu --version | grep -Fqx 'dudu $version'
}

package() {
    DESTDIR="\$pkgdir" cmake --install build
}
EOF

cat >"$output_dir/aur/.SRCINFO" <<EOF
pkgbase = dudu
	pkgdesc = Python-shaped systems language with direct C and C++ interop
	pkgver = $aur_version
	pkgrel = 1
	url = https://github.com/wegfawefgawefg/dudu
	arch = x86_64
	arch = aarch64
	license = MIT
	license = Apache-2.0
	makedepends = cmake
	depends = clang
	depends = gcc-libs
	depends = glibc
	source = $archive::$url
	sha256sums = $sha256

pkgname = dudu
EOF

printf 'Homebrew formula: %s\n' "$output_dir/homebrew/dudu.rb"
printf 'AUR package:      %s\n' "$output_dir/aur/PKGBUILD"
