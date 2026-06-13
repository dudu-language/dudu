# Changelog

## [Unreleased]

### Added

- Added the Dudu project driver plan.
- Added `dudu init` and `dudu new` project scaffolding commands.
- Added `dudu init <path>` support.
- Added Cargo-style git initialization for project scaffolds.
- Added support for the newer `dudu.toml` project-driver shape:
  `entry`, `[cxx]`, `[include]`, `[sources]`, `[pkg]`, `[link]`, and
  `[build].dir`.
- Added C and C++ source passthrough for native builds.
- Added `@test` functions and generated native `dudu test` harnesses.
- Added runtime `assert` lowering for test-friendly failures.

### Changed

- Improved native header scanning for direct SIMD intrinsic imports.
- Removed generated `CHANGELOG.md` files from `dudu init` and `dudu new`
  scaffolds.

### Fixed

- Fixed native signature parsing for signatures with suffixes such as
  `noexcept(true)`.
- Fixed `size_t` native type mapping to `usize`.
