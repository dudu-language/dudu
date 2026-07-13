# Installing Dudu

Dudu is pre-alpha. The current release is `0.1.0-alpha.13`; language and
generated ABI compatibility may change between alpha versions.

## Current Installation

Install native prerequisites first.

Ubuntu and Debian:

```sh
sudo apt install git cmake clang libclang-dev g++ build-essential pkg-config
```

macOS Apple Silicon:

```sh
xcode-select --install
brew install cmake llvm pkg-config
```

Then install the immutable tagged source release:

```sh
curl --proto '=https' --tlsv1.2 -sSf https://dudulang.org/install.sh \
    | sh -s -- --version 0.1.0-alpha.13
export PATH="$HOME/.local/bin:$PATH"
dudu --version
```

Create and run a project:

```sh
dudu init hello
cd hello
dudu run
```

## Tagged Release Channels

The following channels are generated from the same immutable source tag and
release manifest. AUR publication remains pending upstream account access.

| Channel | User command or artifact | Ownership |
| --- | --- | --- |
| Bootstrap installer | `curl -sSf https://dudulang.org/install.sh | sh -s -- --version VERSION` | Dudu installer |
| GitHub source archive | `dudu-VERSION-source.tar.gz` plus SHA-256 manifest | User/source build |
| Debian/Ubuntu download | `dudu_VERSION_amd64.deb` attached to GitHub Releases | `dpkg`/APT |
| Homebrew tap | `brew install dudu-language/dudu/dudu` (best-effort on macOS pending user confirmation) | Homebrew |
| Arch AUR | Package `dudu` | pacman/AUR helper |
| VS Code Marketplace | Extension publisher `dudu` | VS Code |
| Open VSX | Extension namespace `dudu` | Open VSX client |
| GitHub VSIX | `dudu-VERSION.vsix` attached to GitHub Releases | VS Code/manual |

The bootstrap installer builds tagged source locally, verifies its checksum,
installs atomically under `~/.local`, and records ownership. Installer-owned
toolchains support:

```sh
dudu update --check
dudu update
dudu update --rollback
dudu uninstall
```

Package-manager-owned installations intentionally reject `dudu update` and
direct the user back to Homebrew, AUR, or `dpkg`/APT.

## Validation Status

The release gate checks:

- clean Release configuration, build, and isolated installation
- installed `dudu`, `duc`, and `dudu-lsp`
- a fresh generated project and C/C++ import smoke programs
- source archive and SHA-256 manifest generation
- bootstrap install, update, rollback, and uninstall across two versions
- `.deb`, Homebrew, and AUR recipe contents
- VSIX packaging and isolated clean installation
- Linux x86_64 source-toolchain release validation

The macOS bootstrap path uses the same tagged source and builds against the
user's Xcode/Homebrew toolchain, but complete Apple Silicon release validation
is not an alpha publication gate. Visual Studio Marketplace publication is
manual for the first alpha. Open VSX publication uses a scoped registry secret.
AUR publication waits for upstream account registration to reopen.

See [Distribution Plan](distribution-plan.md),
[Release Procedure](releasing.md), and
[Distribution Operator Checklist](distribution-operator-checklist.md) for the
authoritative implementation and operator details.
