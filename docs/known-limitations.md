# Known Limitations

This document describes the boundaries of the first Dudu alpha. They are
published constraints, not promises of silent compatibility.

## Stability

- The language, generated C++ ABI, project manifest, lockfile, and editor
  protocol may change between alpha releases.
- Dudu does not preserve compatibility with unreleased syntax. Removed forms
  produce migration diagnostics where that is useful.
- Generated C++ is readable and inspectable, but it is a build artifact. Code
  should not depend on generated symbol spellings remaining stable.

## Hosts And Installation

- Linux x86_64 is the only validated host for `0.1.0-alpha.7`.
- macOS Apple Silicon is a release target but is not advertised as validated
  until the release gate passes on real Apple hardware.
- Windows is not supported by the first alpha.
- Both checkout and tagged bootstrap installation build locally and require
  CMake, a C++20 compiler, and libclang development files. The bootstrap,
  atomic update/rollback/uninstall, package recipes, and `.deb` builder are
  implemented and locally validated but are unavailable to users until the
  first immutable tag and GitHub prerelease are published.
- Binary toolchains are not shipped by the first alpha. Host compiler, SDK,
  libc, and libclang compatibility therefore remain local build concerns.

## Native Interop

- Native header awareness is broad, not complete C++ language emulation.
  Common C APIs, standard-library templates, and the libraries in
  [the native compatibility matrix](native-compatibility-matrix.md) are
  exercised locally.
- Token-pasting, stringizing, declaration-generating, and partial-syntax macros
  can still require a normal C/C++ adapter header.
- Very template-heavy headers can make the first scan noticeably slower.
  Scans are cached and subsequent editor/compiler operations are expected to
  be interactive.
- Native libraries, include paths, linker flags, and platform toolchains remain
  CMake/pkg-config concerns. Dudu source dependencies do not replace native
  package management.

## Projects And Packages

- CMake is the supported build backend. Simple projects use generated CMake;
  complex native projects may use a user-owned `CMakeLists.txt` while keeping
  `dudu build`, `run`, and `test` as the front door.
- Dudu packages currently use path or immutable Git dependencies recorded in
  `dudu.lock`. There is no central Dudu package registry.
- The public source-dependency model carries `.dd` packages. It does not try to
  download arbitrary system C/C++ dependencies.

## Language And Tooling

- Dudu is a statically typed Python-shaped systems language, not a Python
  implementation. Dynamic Python behavior is outside its compatibility goal.
- Compiler and LSP behavior is covered by deterministic fixtures and three
  maintained dogfood projects, but alpha users should report minimized cases
  where diagnostics, hover, navigation, or recovery after invalid edits fail.
- CUDA validation is unavailable on the current release host. OpenCL and
  OpenBLAS paths are covered; CUDA/CUBLAS remains hardware/toolchain-dependent.

Run `./scripts/release-check.sh` from a clean checkout for the exact local
pre-release evidence.
