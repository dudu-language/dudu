#!/bin/sh
set -eu

repository="dudu-language/dudu"
operation="install"
requested_version=""
prefix="${DUDU_INSTALL_PREFIX:-$HOME/.local}"
modify_path=1
assume_yes=0
check_only=0
source_mode=1
rollback=0
dependency_mode="ask"
print_dependencies=0

say() {
    printf '%s\n' "$*"
}

die() {
    printf 'dudu install: %s\n' "$*" >&2
    exit 1
}

usage() {
    cat <<'USAGE'
usage: install.sh [options]

Install a tagged Dudu toolchain from source.

  --version VERSION   install an exact immutable release
  --prefix PATH       install under PATH (default: ~/.local)
  --source            build the tagged source archive locally
  --install-deps      install missing native dependencies without prompting
  --no-install-deps   report missing dependencies without installing them
  --print-deps        print the native dependency command and exit
  --no-modify-path    do not add PREFIX/bin to the shell PATH
  --help              show this help

The installed toolchain also supports these commands through dudu:

  dudu update --check
  dudu update [--version VERSION] [--source]
  dudu update --rollback
  dudu uninstall [--yes]
USAGE
}

while [ "$#" -gt 0 ]; do
    case "$1" in
        --update)
            operation="update"
            shift
            ;;
        --uninstall)
            operation="uninstall"
            shift
            ;;
        --check)
            check_only=1
            shift
            ;;
        --rollback)
            rollback=1
            shift
            ;;
        --version)
            [ "$#" -ge 2 ] || die "--version requires a value"
            requested_version=$2
            shift 2
            ;;
        --prefix)
            [ "$#" -ge 2 ] || die "--prefix requires a path"
            prefix=$2
            shift 2
            ;;
        --source)
            source_mode=1
            shift
            ;;
        --install-deps)
            dependency_mode="install"
            shift
            ;;
        --no-install-deps)
            dependency_mode="report"
            shift
            ;;
        --print-deps)
            print_dependencies=1
            shift
            ;;
        --no-modify-path)
            modify_path=0
            shift
            ;;
        --yes)
            assume_yes=1
            shift
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        *)
            die "unknown option: $1"
            ;;
    esac
done

[ "$source_mode" -eq 1 ] || die "only source toolchains are available"

require_command() {
    command -v "$1" >/dev/null 2>&1 || die "required command '$1' is not on PATH"
}

run_logged() {
    action=$1
    log=$2
    shift 2
    if "$@" >"$log" 2>&1; then
        return 0
    fi
    cat "$log" >&2
    die "$action failed"
}

host_os() {
    case "$(uname -s)" in
        Linux) printf '%s\n' "linux" ;;
        Darwin) printf '%s\n' "macos" ;;
        *) printf '%s\n' "unsupported" ;;
    esac
}

linux_package_manager() {
    for manager in apt-get dnf pacman; do
        if command -v "$manager" >/dev/null 2>&1; then
            printf '%s\n' "$manager"
            return
        fi
    done
}

dependency_command() {
    os=$1
    if [ "$os" = "macos" ]; then
        printf '%s\n' "xcode-select --install"
        printf '%s\n' "brew install cmake llvm pkg-config"
        return
    fi

    manager=$(linux_package_manager)
    case "$manager" in
        apt-get) printf '%s\n' "sudo apt-get update && sudo apt-get install -y ca-certificates git cmake clang libclang-dev g++ build-essential pkg-config curl tar" ;;
        dnf) printf '%s\n' "sudo dnf install -y ca-certificates git cmake make clang clang-devel gcc-c++ pkgconf-pkg-config curl tar" ;;
        pacman) printf '%s\n' "sudo pacman -S --needed ca-certificates git cmake make clang gcc pkgconf curl tar" ;;
        *) return 1 ;;
    esac
}

have_libclang() {
    for prefix in "${LLVM_ROOT:-}" /usr/lib/llvm-* /opt/homebrew/opt/llvm /usr/local/opt/llvm; do
        [ -n "$prefix" ] || continue
        [ -f "$prefix/include/clang-c/Index.h" ] || continue
        for library in "$prefix"/lib/libclang.so* "$prefix"/lib/libclang.dylib* "$prefix"/lib/libclang.a; do
            [ -e "$library" ] && return 0
        done
    done
    return 1
}

missing_dependencies() {
    missing=""
    for tool in git cmake make clang++ pkg-config curl tar; do
        if ! command -v "$tool" >/dev/null 2>&1; then
            missing="$missing $tool"
        fi
    done
    if ! command -v "${CXX:-c++}" >/dev/null 2>&1; then
        missing="$missing ${CXX:-c++}"
    fi
    if ! have_libclang; then
        missing="$missing libclang-development-files"
    fi
    printf '%s\n' "${missing# }"
}

confirm_dependency_install() {
    if [ "$dependency_mode" = "install" ]; then
        return 0
    fi
    if [ "$dependency_mode" = "report" ] || [ ! -r /dev/tty ]; then
        return 1
    fi
    printf 'Install missing native dependencies now? [Y/n] ' >/dev/tty
    if ! IFS= read -r answer </dev/tty; then
        return 1
    fi
    case "$answer" in
        ''|y|Y|yes|YES) return 0 ;;
        *) return 1 ;;
    esac
}

run_as_root() {
    if [ "$(id -u)" -eq 0 ]; then
        "$@"
        return
    fi
    require_command sudo
    sudo "$@"
}

install_linux_dependencies() {
    manager=$(linux_package_manager)
    case "$manager" in
        apt-get)
            run_as_root apt-get update
            run_as_root apt-get install -y ca-certificates git cmake clang libclang-dev g++ build-essential pkg-config curl tar
            ;;
        dnf) run_as_root dnf install -y ca-certificates git cmake make clang clang-devel gcc-c++ pkgconf-pkg-config curl tar ;;
        pacman) run_as_root pacman -S --needed ca-certificates git cmake make clang gcc pkgconf curl tar ;;
        *) die "no supported Linux package manager found; install git, CMake, Clang, libclang development files, a C++20 compiler, pkg-config, curl, and tar" ;;
    esac
}

install_macos_dependencies() {
    if ! xcode-select -p >/dev/null 2>&1; then
        xcode-select --install || true
        die "finish installing the Xcode command-line tools, then rerun this command"
    fi
    command -v brew >/dev/null 2>&1 ||
        die "Homebrew is required to install CMake, LLVM, and pkg-config: https://brew.sh"
    brew install cmake llvm pkg-config
}

ensure_build_dependencies() {
    os=$(host_os)
    [ "$os" != "unsupported" ] || die "automatic dependency setup supports Linux and macOS"
    missing=$(missing_dependencies)
    [ -n "$missing" ] || return 0

    say "missing native dependencies: $missing"
    command=$(dependency_command "$os") || command=""
    if [ -n "$command" ]; then
        say "dependency command:"
        say "$command"
    fi
    confirm_dependency_install ||
        die "native dependencies are required; rerun with --install-deps or run the command above"

    say "install native dependencies"
    case "$os" in
        linux) install_linux_dependencies ;;
        macos) install_macos_dependencies ;;
    esac

    missing=$(missing_dependencies)
    [ -z "$missing" ] || die "native dependencies are still missing:$missing"
}

fetch() {
    source_url=$1
    destination=$2
    if [ "${DUDU_INSECURE_TEST_URL:-0}" = "1" ]; then
        curl -fsSL "$source_url" -o "$destination"
        return
    fi
    curl --proto '=https' --tlsv1.2 -fsSL "$source_url" -o "$destination"
}

sha256_file() {
    if command -v sha256sum >/dev/null 2>&1; then
        sha256sum "$1" | awk '{print $1}'
        return
    fi
    if command -v shasum >/dev/null 2>&1; then
        shasum -a 256 "$1" | awk '{print $1}'
        return
    fi
    die "SHA-256 verification requires sha256sum or shasum"
}

manifest_value() {
    key=$1
    file=$2
    awk -F= -v key="$key" '$1 == key { value = substr($0, length(key) + 2); count++ }
        END { if (count == 1) print value; else exit 1 }' "$file"
}

validate_version() {
    case "$1" in
        ''|*[!0-9A-Za-z.-]*) die "invalid release version: $1" ;;
    esac
}

resolve_latest_version() {
    if [ -n "${DUDU_LATEST_VERSION:-}" ]; then
        printf '%s\n' "$DUDU_LATEST_VERSION"
        return
    fi
    require_command curl
    releases_url="https://api.github.com/repos/$repository/releases?per_page=20"
    latest=$(curl --proto '=https' --tlsv1.2 -fsSL "$releases_url" |
        sed -n 's/^[[:space:]]*"tag_name":[[:space:]]*"v\([^"]*\)".*/\1/p' |
        sed -n '1p')
    [ -n "$latest" ] || die "could not resolve the latest tagged release"
    printf '%s\n' "$latest"
}

manager_prefix() {
    script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
    CDPATH= cd -- "$script_dir/../.." && pwd
}

read_owner() {
    install_prefix=$1
    owner_file="$install_prefix/share/dudu/install-owner"
    [ -f "$owner_file" ] || die "installation ownership metadata is missing: $owner_file"
    owner=$(sed -n '1p' "$owner_file")
    [ -n "$owner" ] || die "installation ownership metadata is empty: $owner_file"
    printf '%s\n' "$owner"
}

package_update_message() {
    case "$1" in
        homebrew) printf '%s\n' "this Dudu installation is owned by Homebrew; run: brew upgrade dudu" ;;
        aur) printf '%s\n' "this Dudu installation is owned by AUR; update it with your AUR helper or pacman" ;;
        deb) printf '%s\n' "this Dudu installation is owned by dpkg; update it with apt or dpkg" ;;
        local|source) printf '%s\n' "this Dudu installation is owned by a local source install; rerun scripts/install-local.sh" ;;
        *) printf '%s\n' "this Dudu installation is owned by '$1' and cannot self-update" ;;
    esac
}

atomic_link() {
    target=$1
    link=$2
    link_temp="$link.tmp.$$"
    rm -f "$link_temp"
    ln -s "$target" "$link_temp"
    if mv -Tf "$link_temp" "$link" 2>/dev/null; then
        return
    fi
    if mv -fh "$link_temp" "$link" 2>/dev/null; then
        return
    fi
    rm -f "$link_temp"
    die "this platform cannot atomically replace the toolchain link: $link"
}

write_metadata() {
    state_root=$1
    active_version=$2
    previous_version=$3
    cat >"$state_root/installs.json.tmp.$$" <<EOF
{
  "schema": 1,
  "owner": "installer",
  "active": "$active_version",
  "previous": "$previous_version",
  "prefix": "$prefix"
}
EOF
    mv -f "$state_root/installs.json.tmp.$$" "$state_root/installs.json"
}

link_version() {
    state_root=$1
    version=$2
    current_target=""
    if [ -L "$state_root/current" ]; then
        current_target=$(readlink "$state_root/current")
    fi
    if [ "$current_target" = "toolchains/$version" ]; then
        return
    fi
    if [ -n "$current_target" ]; then
        atomic_link "$current_target" "$state_root/previous"
    fi
    atomic_link "toolchains/$version" "$state_root/current"
}

link_commands() {
    state_root=$1
    bin_dir=$2
    mkdir -p "$bin_dir"
    for tool in dudu duc dudu-lsp; do
        link="$bin_dir/$tool"
        if [ -e "$link" ] && [ ! -L "$link" ]; then
            die "refusing to replace non-symlink command: $link"
        fi
        relative="../share/dudu/current/bin/$tool"
        atomic_link "$relative" "$link"
    done
}

shell_profile() {
    case "${SHELL:-}" in
        */zsh) printf '%s\n' "$HOME/.zprofile" ;;
        *) printf '%s\n' "$HOME/.profile" ;;
    esac
}

ensure_path() {
    bin_dir=$1
    case ":$PATH:" in
        *":$bin_dir:"*) return ;;
    esac
    if [ "$modify_path" -eq 0 ]; then
        say "add Dudu to PATH: export PATH=\"$bin_dir:\$PATH\""
        return
    fi
    profile=$(shell_profile)
    marker="# Dudu toolchain"
    if [ ! -f "$profile" ] || ! grep -Fq "$marker" "$profile"; then
        {
            printf '\n%s\n' "$marker"
            printf 'export PATH="%s:$PATH"\n' "$bin_dir"
        } >>"$profile"
        say "updated PATH in $profile"
    fi
    say "restart the shell or run: export PATH=\"$bin_dir:\$PATH\""
}

remove_path_entry() {
    profile=$(shell_profile)
    [ -f "$profile" ] || return 0
    profile_temp="$profile.dudu-remove.$$"
    awk '
        $0 == "# Dudu toolchain" { skip = 1; next }
        skip == 1 && $0 ~ /^export PATH=/ { skip = 0; next }
        { skip = 0; print }
    ' "$profile" >"$profile_temp"
    mv -f "$profile_temp" "$profile"
}

installed_version() {
    state_root=$1
    if [ ! -x "$state_root/current/bin/dudu" ]; then
        return
    fi
    "$state_root/current/bin/dudu" --version | awk '{print $2}'
}

perform_rollback() {
    state_root=$1
    [ -L "$state_root/current" ] || die "no active Dudu toolchain"
    [ -L "$state_root/previous" ] || die "no previous Dudu toolchain to restore"
    old_current=$(readlink "$state_root/current")
    old_previous=$(readlink "$state_root/previous")
    [ -x "$state_root/$old_previous/bin/dudu" ] || die "previous toolchain is incomplete"
    atomic_link "$old_previous" "$state_root/current"
    atomic_link "$old_current" "$state_root/previous"
    active=${old_previous##*/}
    previous=${old_current##*/}
    write_metadata "$state_root" "$active" "$previous"
    say "rolled back Dudu to $active"
}

perform_uninstall() {
    toolchain_prefix=$(manager_prefix)
    owner=$(read_owner "$toolchain_prefix")
    if [ "$owner" != "installer" ]; then
        package_update_message "$owner" >&2
        exit 1
    fi
    state_root=$(dirname "$(dirname "$toolchain_prefix")")
    prefix=$(dirname "$(dirname "$state_root")")
    bin_dir="$prefix/bin"
    if [ "$assume_yes" -ne 1 ]; then
        printf 'remove installer-owned Dudu from %s? [y/N] ' "$state_root"
        read answer
        case "$answer" in y|Y|yes|YES) ;; *) say "uninstall cancelled"; exit 0 ;; esac
    fi
    for tool in dudu duc dudu-lsp; do
        link="$bin_dir/$tool"
        if [ -L "$link" ]; then
            target=$(readlink "$link")
            case "$target" in *share/dudu/current/bin/*) rm -f "$link" ;; esac
        fi
    done
    remove_path_entry
    rm -rf "$state_root"
    say "uninstalled installer-owned Dudu"
}

if [ "$print_dependencies" -eq 1 ]; then
    os=$(host_os)
    [ "$os" != "unsupported" ] || die "automatic dependency setup supports Linux and macOS"
    dependency_command "$os" ||
        die "no supported package manager found on this host"
    exit 0
fi

if [ "$operation" = "uninstall" ]; then
    perform_uninstall
    exit 0
fi

if [ "$operation" = "update" ]; then
    toolchain_prefix=$(manager_prefix)
    owner=$(read_owner "$toolchain_prefix")
    if [ "$owner" != "installer" ]; then
        package_update_message "$owner" >&2
        exit 1
    fi
    state_root=$(dirname "$(dirname "$toolchain_prefix")")
    prefix=$(dirname "$(dirname "$state_root")")
    if [ "$rollback" -eq 1 ]; then
        perform_rollback "$state_root"
        exit 0
    fi
else
    case "$prefix" in
        /*) ;;
        *) prefix="$(pwd)/$prefix" ;;
    esac
    state_root="$prefix/share/dudu"
fi

ensure_build_dependencies
require_command curl
require_command tar
require_command cmake
require_command "${CXX:-c++}"

version=${requested_version:-$(resolve_latest_version)}
validate_version "$version"

if [ "$check_only" -eq 1 ]; then
    current=$(installed_version "$state_root")
    [ -n "$current" ] || current="not installed"
    say "installed $current"
    say "available $version"
    [ "$current" = "$version" ] && say "Dudu is up to date" || say "Dudu update available"
    exit 0
fi

release_base=${DUDU_RELEASE_BASE_URL:-"https://github.com/$repository/releases/download"}
release_dir="$release_base/v$version"
manifest_name="dudu-$version-manifest.txt"
work_dir=$(mktemp -d "${TMPDIR:-/tmp}/dudu-install.XXXXXX")
trap 'rm -rf "$work_dir"' EXIT HUP INT TERM

say "fetch Dudu $version manifest"
fetch "$release_dir/$manifest_name" "$work_dir/$manifest_name"
manifest_version=$(manifest_value version "$work_dir/$manifest_name") || die "invalid release manifest version"
manifest_schema=$(manifest_value schema "$work_dir/$manifest_name") || die "invalid release manifest schema"
manifest_tag=$(manifest_value tag "$work_dir/$manifest_name") || die "invalid release manifest tag"
manifest_commit=$(manifest_value commit "$work_dir/$manifest_name") || die "invalid release manifest commit"
archive=$(manifest_value source_archive "$work_dir/$manifest_name") || die "invalid release manifest archive"
expected_sha=$(manifest_value source_sha256 "$work_dir/$manifest_name") || die "invalid release manifest checksum"
[ "$manifest_schema" = "1" ] || die "unsupported release manifest schema: $manifest_schema"
[ "$manifest_version" = "$version" ] || die "manifest version '$manifest_version' does not match '$version'"
[ "$manifest_tag" = "v$version" ] || die "manifest tag '$manifest_tag' does not match 'v$version'"
case "$manifest_commit" in
    *[!0-9a-fA-F]*|'') die "manifest contains an invalid commit" ;;
esac
[ "${#manifest_commit}" -eq 40 ] || die "manifest commit has the wrong length"
case "$archive" in dudu-"$version".tar.gz) ;; *) die "unexpected source archive name: $archive" ;; esac
case "$expected_sha" in
    *[!0-9a-fA-F]*|'') die "manifest contains an invalid SHA-256 checksum" ;;
esac
[ "${#expected_sha}" -eq 64 ] || die "manifest SHA-256 checksum has the wrong length"

say "fetch $archive"
fetch "$release_dir/$archive" "$work_dir/$archive"
actual_sha=$(sha256_file "$work_dir/$archive")
[ "$actual_sha" = "$expected_sha" ] || die "source archive checksum mismatch"

mkdir -p "$work_dir/source"
tar -xzf "$work_dir/$archive" -C "$work_dir/source"
source_dir="$work_dir/source/dudu-$version"
[ -f "$source_dir/CMakeLists.txt" ] || die "source archive layout is invalid"
[ "$(tr -d '\r\n' <"$source_dir/VERSION")" = "$version" ] || die "source archive VERSION mismatch"

toolchains="$state_root/toolchains"
final_prefix="$toolchains/$version"
stage_prefix="$toolchains/.install-$version-$$"
mkdir -p "$toolchains"
if [ ! -x "$final_prefix/bin/dudu" ]; then
    rm -rf "$stage_prefix"
    say "configure Dudu $version"
    run_logged "configure Dudu" "$work_dir/configure.log" \
        cmake -S "$source_dir" -B "$work_dir/build" \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_INSTALL_PREFIX="$stage_prefix" \
        -DDUDU_BUILD_TESTS=OFF \
        -DDUDU_STRICT=ON \
        -DDUDU_INSTALL_OWNER=installer
    say "build Dudu $version"
    run_logged "build Dudu" "$work_dir/build.log" \
        cmake --build "$work_dir/build" --parallel
    say "install Dudu $version"
    run_logged "install Dudu" "$work_dir/install.log" \
        cmake --install "$work_dir/build"
    [ -x "$stage_prefix/bin/dudu" ] || die "installed dudu executable is missing"
    "$stage_prefix/bin/dudu" --version | grep -Fqx "dudu $version" ||
        die "installed dudu version smoke check failed"
    mv "$stage_prefix" "$final_prefix"
fi

link_version "$state_root" "$version"
bin_dir="$prefix/bin"
link_commands "$state_root" "$bin_dir"
previous=""
if [ -L "$state_root/previous" ]; then
    previous=$(readlink "$state_root/previous")
    previous=${previous##*/}
fi
write_metadata "$state_root" "$version" "$previous"
ensure_path "$bin_dir"
say "installed Dudu $version"
say "toolchain $final_prefix"
say "commands $bin_dir"
