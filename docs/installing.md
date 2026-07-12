# Installing Dudu

Dudu is pre-alpha. The repository identifies as `0.1.0-alpha.5`, but public
package channels remain unavailable until that immutable tag passes the release
gate and is published.

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

Then install from the current checkout:

```sh
git clone https://github.com/dudu-language/dudu.git
cd dudu
./scripts/install-local.sh
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
release manifest. They are implemented and locally validated, but become
public only after the first tag is published.

| Channel | User command or artifact | Ownership |
| --- | --- | --- |
| Bootstrap installer | `curl -sSf https://dudulang.org/install.sh | sh -s -- --version VERSION` | Dudu installer |
| GitHub source archive | `dudu-VERSION-source.tar.gz` plus SHA-256 manifest | User/source build |
| Debian/Ubuntu download | `dudu_VERSION_amd64.deb` attached to GitHub Releases | `dpkg`/APT |
| Homebrew tap | `brew install dudu-language/dudu/dudu` | Homebrew |
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
- Linux and Apple Silicon source-toolchain workflows

Registry publication itself cannot be validated before a release exists.
Visual Studio Marketplace publication is manual for the first alpha. Open VSX
publication is wired to a scoped GitHub Actions secret. AUR publication waits
for upstream account registration to reopen.

See [Distribution Plan](distribution-plan.md),
[Release Procedure](releasing.md), and
[Distribution Operator Checklist](distribution-operator-checklist.md) for the
authoritative implementation and operator details.
