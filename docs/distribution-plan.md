# Dudu Distribution Plan

This document is the authoritative plan for licensing, versioning, installing,
updating, and distributing Dudu. It separates the minimum product work required
before distribution from the machinery required to publish the first alpha.
Human account, identity, payment, and secret setup is tracked in
[Distribution Operator Checklist](distribution-operator-checklist.md).
The broader language roadmap lives in
[Le Plan](le_plan.md), and release validation lives in
[Validation Matrix](validation-matrix.md).

The intended model is:

- Rust-shaped user ergonomics
- C/C++-shaped native ecosystem ownership
- tagged releases as the only public toolchain inputs
- local validation as the normal development loop
- atomic, user-local installation as the primary path
- package-manager ownership when Dudu is installed through a package manager

Dudu should be easy to install and update without pretending to replace the
host C++ compiler, linker, platform SDK, CMake, pkg-config, or native package
manager.

## License

The target project license is dual MIT OR Apache-2.0, at the user's option.
Before the first alpha, the repository should contain:

- `LICENSE-MIT`
- `LICENSE-APACHE`
- `COPYRIGHT`
- a README license section
- a contribution statement that contributions are submitted under both terms
- an explicit statement that Dudu claims no copyright over user source or
  generated program output

Dudu-owned runtime, prelude, and support headers that may enter generated
programs should use the same dual license. Third-party components retain their
own licenses. If a release bundles libclang, LLVM, or another native component,
the release must include its required license and third-party notices.

The LLVM license exception is not required merely because Dudu dynamically
links against libclang. Bundling or statically linking LLVM artifacts does
create notice and redistribution work that must be handled by release
packaging.

## Ownership Boundary

Dudu owns:

- `dudu`, `duc`, and `dudu-lsp`
- Dudu parsing, semantic analysis, modules, and C++ generation
- Dudu formatting and editor protocol behavior
- Dudu source dependencies and lockfiles
- Dudu toolchain release versions
- generated Dudu support files and CMake integration

The host native ecosystem owns:

- the C++ compiler and linker
- libc, the platform SDK, and system headers
- C and C++ libraries
- pkg-config and native package discovery
- user-owned CMake projects and toolchain files
- CUDA, Vulkan, SDL, raylib, and other optional SDKs

The installer should detect missing native prerequisites and print concrete
instructions. It must not silently invoke `sudo` or attempt to own the entire
native SDK.

## Release Authority

Public installs and updates must resolve only immutable tagged releases. They
must never build or download the current `master` branch.

Each release has one canonical version that feeds:

- `dudu --version`
- `duc --version`
- `dudu-lsp` server metadata
- the VS Code/Open VSX extension
- generated C++ schema metadata when present
- release archive names
- package metadata

The source of truth should live in one repository file or CMake-generated
version definition. Hard-coded copies in individual binaries are not
acceptable.

Pre-release versions follow semantic versioning, for example:

```text
0.1.0-alpha.1
0.1.0-alpha.2
0.1.0-beta.1
0.1.0
```

VS Code Marketplace pre-release publishing uses its own pre-release channel
with a numeric `major.minor.patch` extension version. The release process must
map the Dudu toolchain version to that constraint explicitly rather than
inventing several unrelated version numbers.

## Development Validation Policy

Remote jobs are not the normal Dudu development loop. Humans and agents should
run focused tests locally before committing and broader tests locally before a
release milestone.

Normal compiler work:

```sh
./scripts/test_fast.sh
```

Before a significant stable push:

```sh
./scripts/test_full.sh
./scripts/test_dogfood.sh
git diff --check
```

Before a release tag:

```sh
./scripts/release-check.sh
```

`release-check.sh` should become the single local release-candidate entry point.
It should run the release subset from
[Validation Matrix](validation-matrix.md), verify a clean source tree, verify
version/changelog consistency, build release artifacts, install into a clean
temporary prefix, and compile/run a fresh hello project through the installed
tools.

GitHub automation policy:

- no release or deployment from ordinary `master` pushes
- no required remote check in the normal local iteration loop
- no push-wait-fix workflow as a substitute for local validation
- optional manual clean-environment validation for release candidates
- tag-triggered artifact production only after local release checks pass
- optional scheduled compatibility probes only when their failures are useful
  reports and do not block development
- cancel superseded remote jobs rather than queueing stale work

Remote release jobs repeat critical checks because packaging environments can
differ from the developer machine. They are a final clean-environment guard,
not the primary debugger.

## Primary Installation Model

The primary developer-facing installation should be:

```sh
curl --proto '=https' --tlsv1.2 -sSf https://dudulang.org/install.sh | sh
```

The bootstrap script should be small and auditable. It should:

1. detect the host operating system and architecture
2. resolve a tagged Dudu release
3. download an immutable release manifest and artifact
4. verify the artifact checksum before executing or installing it
5. check native prerequisites and explain missing packages
6. install without root privileges
7. install `dudu`, `duc`, and `dudu-lsp` as one atomic toolchain
8. report PATH changes clearly
9. record the installation method and exact version
10. provide uninstall and rollback instructions

Required installer options:

```text
--version VERSION
--prefix PATH
--source
--no-modify-path
--help
```

The initial alpha installer may build a tagged source archive locally. This
avoids promising a universal binary before Dudu has controlled libclang/LLVM
artifacts for each host. It requires CMake, a C++ compiler, and libclang
development files, and must diagnose each missing prerequisite separately.

As reproducible host artifacts mature, the installer should prefer a matching
binary toolchain and retain `--source` for unsupported systems and developers.

## Installed Toolchain Layout

Installer-owned toolchains use versioned directories and an atomic active
pointer under the selected prefix. With the default `~/.local` prefix:

```text
~/.local/share/dudu/
    installs.json
    current -> toolchains/0.1.0-alpha.2
    previous -> toolchains/0.1.0-alpha.1
    toolchains/
        0.1.0-alpha.1/
            bin/
            lib/
            share/
        0.1.0-alpha.2/
            bin/
            lib/
            share/
~/.local/bin/
    dudu -> ../share/dudu/current/bin/dudu
    duc -> ../share/dudu/current/bin/duc
    dudu-lsp -> ../share/dudu/current/bin/dudu-lsp
```

The exact layout may change, but these properties may not:

- compiler, project driver, LSP, formatter behavior, and support files update
  together
- an interrupted update leaves the previous toolchain usable
- activation is one atomic switch
- rollback does not rebuild or redownload the previous version
- uninstall removes only installer-owned files

## Self-Update

Installer-owned Dudu supports:

```sh
dudu update --check
dudu update
dudu update --version 0.1.0-alpha.1
dudu update --source
dudu update --rollback
```

An update should:

1. read installation ownership metadata
2. resolve an immutable tagged release
3. download and verify into a new version directory
4. build there when source mode is selected
5. run a minimal installed-toolchain smoke check
6. atomically activate the new directory
7. retain the previous working version for rollback

Package-manager-owned installations must not self-update. In that case,
`dudu update` should identify the owner and print the correct command, for
example `brew upgrade dudu` or `sudo pacman -Syu dudu`.

Project toolchain pinning is desirable before stable `1.0` and should use
project metadata rather than a second source-level import mechanism. An alpha
may initially warn when a project was last built with a different compiler
version, but release builds and lockfiles must always record the compiler
version used.

Do not add stable/beta/nightly channels, dated toolchains, independently
versioned components, or cross-target standard-library downloads until real
users need them. Dudu should not copy rustup's storage and component complexity
before it has that problem.

## Binary Toolchains

The eventual binary release matrix starts with:

```text
x86_64-unknown-linux-gnu
aarch64-unknown-linux-gnu
x86_64-apple-darwin
aarch64-apple-darwin
```

Windows follows when the compiler, generated CMake path, native scanning, and
editor integration are validated there.

Each binary toolchain must be built in a controlled environment with a stated
minimum OS/libc baseline. It should bundle or statically link a compatible
pinned libclang/LLVM runtime rather than loading an arbitrary system LLVM ABI.
The host C++ compiler, linker, SDK, and project libraries remain external.

Binary releases require:

- reproducible release configuration
- explicit minimum glibc/macOS versions
- architecture-specific smoke tests
- runtime dependency inspection
- license and third-party notices
- SHA-256 checksums
- signatures when release signing is established
- installed hello/native-import smoke tests

Source installation remains supported even after binary releases exist.

## Editor Distribution

The Dudu editor client is distributed independently from the native toolchain:

- Visual Studio Marketplace pre-release
- Open VSX pre-release
- `.vsix` attached to GitHub releases

The first-alpha Visual Studio Marketplace upload is manual and uses the exact
VSIX verified against the GitHub release manifest. Do not create a billed Azure
subscription solely to obtain a global PAT; Microsoft retires those tokens on
December 1, 2026. Open VSX publication remains automated with its dedicated
registry token. Entra-based Marketplace automation can be added when release
frequency justifies the Azure infrastructure.

The extension must not bundle `dudu`, `duc`, libclang, or `dudu-lsp`. It should
locate `dudu-lsp` from the active Dudu toolchain and show a useful installation
or version-mismatch message when it cannot.

Before first publication, the extension needs:

- a real publisher identity
- README and changelog
- PNG icon and marketplace metadata
- repository, license, and issue links
- bundled production JavaScript rather than a shipped development
  `node_modules` tree
- VSIX packaging and clean-install smoke tests
- matching Open VSX metadata
- an explicit minimum compatible Dudu toolchain version

## Package Channels

Package channels complement the primary installer. They are not independent
release authorities: every recipe must consume an immutable tagged release.

Recommended order:

1. GitHub tagged source release and checksums
2. source-building bootstrap installer
3. VS Code Marketplace/Open VSX pre-release and GitHub VSIX
4. Arch AUR source package
5. personal Homebrew tap
6. downloadable `.deb` artifact
7. prebuilt Linux/macOS toolchains
8. official distribution repositories after external adoption

Package behavior:

- AUR `dudu` builds a tagged source release
- an optional `dudu-git` package may track `master`, but it is not an official
  Dudu release and should not be the first published package
- a personal Homebrew tap can ship alpha versions before Dudu qualifies for
  `homebrew/core`
- a downloadable `.deb` is useful before maintaining an apt repository
- official Debian/Ubuntu submission is separate community packaging work and
  requires long-term maintenance
- package recipes disable Dudu self-update and preserve package-manager
  ownership

Do not maintain a custom apt repository for the first alpha. Repository signing,
key rotation, distro matrices, and metadata hosting add maintenance without
improving the first user test enough to justify it.

## Pre-Distribution Alpha Gate

Complete this product gate before implementing public distribution channels.
The first alpha should be intentionally small. It does not require every item
in `le_plan.md` or every speculative language feature.

### Legal And Identity

- [x] Add MIT OR Apache-2.0 licensing files and notices.
- [x] Centralize the Dudu toolchain version.
- [x] Make `dudu`, `duc`, `dudu-lsp`, extension metadata, release metadata, and
      generated schema metadata agree on that version.
- [ ] Define `0.1.0-alpha.5` and update `CHANGELOG.md` from `[Unreleased]` during
      the release cut.

### Compiler And Project Driver

- [x] Compile and run multi-file projects through generated CMake.
- [x] Emit separate generated module artifacts.
- [x] Support normal `dudu check/build/run/test/fmt/clean` workflows.
- [x] Resolve path/Git Dudu dependencies through a stable lockfile.
- [x] Preserve diagnostics, semantic highlighting, hover, and inlay hints
      through invalid editor edits and repair.
- [x] Close any reproducible P0/P1 compiler or LSP failure found in the three
      maintained dogfood projects.
- [x] Run a final source audit for unreleased compatibility paths or
      library-name special cases introduced after the existing cleanup guards.

Module-level Dudu analysis invalidation, complete C++ template compatibility,
and every planned macro feature are valuable post-alpha work unless a concrete
dogfood program proves one is a release blocker.

### Validation

- [x] Add `scripts/release-check.sh` as the authoritative local release gate.
- [x] Pass fast, full, negative, dependency, LSP, example, and dogfood checks
      from a clean tree.
- [x] Verify clean release-mode build and install into a temporary prefix.
- [x] Build and run a fresh generated hello project using only installed files.
- [x] Verify one native C import and one C++ standard-library import through
      the installed toolchain.
- [x] Validate Linux x86_64 as the first required host.

### User Experience And Documentation

- [x] Make README status, prerequisites, install, update, uninstall, first
      project, editor setup, and known limitations accurate for the tag.
- [x] Ensure compiler errors identify Dudu source rather than generated C++ for
      ordinary language mistakes covered by the public examples.
- [x] Ensure missing compiler, CMake, libclang, native header, and pkg-config
      dependencies have distinct actionable diagnostics.
- [x] Ensure the public examples do not depend on private absolute paths.
- [x] Publish known limitations instead of implying stable-language coverage.

Passing these sections means the language and local toolchain are ready to be
packaged as an alpha. It does not mean the alpha has been published.

## Alpha Distribution Gate

Complete this after the pre-distribution product gate passes:

- [x] Produce an immutable source archive and SHA-256 checksum from the tag.
- [x] Implement the source-building bootstrap installer against tagged
      releases only.
- [x] Record install ownership and exact version.
- [x] Implement installer-owned `dudu update --check`, `dudu update`, and
      rollback without overwriting package-manager installations.
- [x] Package and clean-install a pre-release VSIX.
- [x] Create a tag/manual-only release workflow; do not add ordinary
      push-to-master deployment or release jobs.
- [ ] Validate macOS Apple Silicon before advertising it as supported; Intel
      macOS may remain best-effort until tested.

The Apple Silicon job, package builders, editor publication workflow, and
two-version lifecycle fixture are implemented. The final unchecked item needs
evidence from the real tagged `macos-14` hosted run, not a Linux simulation.

## Not Alpha Blockers

The following should not delay the first public alpha by themselves:

- official Debian or Ubuntu repository inclusion
- `homebrew/core`
- a custom apt repository
- Windows support
- every optional native compatibility probe
- a central Dudu package registry
- stable/beta/nightly toolchain channels
- cross-compilation component management
- a complete user-defined derive/serde macro system
- complete NumPy/PyTorch-equivalent libraries
- perfect support for every template-heavy C++ library
- stable `1.0` compatibility guarantees

## Alpha Release Sequence

1. Complete the pre-distribution legal, versioning, compiler, validation, and
   documentation gate.
2. Implement and test source installation, ownership, update, rollback, and
   uninstall locally.
3. Cut `0.1.0-alpha.5` from a clean commit and locally run the exact release
   gate.
4. Push the immutable tag.
5. Produce the source archive, checksum, VSIX, and release notes.
6. Run clean Linux and macOS release-environment validation without using the
   remote job as the development loop.
7. Publish the GitHub alpha and editor pre-release.
8. Add AUR, Homebrew tap, and `.deb` channels from the same tag after the first
   install path is proven.
9. Resume `le_plan.md` using real user reports to prioritize language,
   interoperability, incremental-build, and editor work.
