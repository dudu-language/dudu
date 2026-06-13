# Changelog

## [Unreleased]

### Added

- Added the Dudu project driver plan.
- Added `dudu init` and `dudu new` project scaffolding commands.
- Added support for the newer `dudu.toml` project-driver shape:
  `entry`, `[cxx]`, `[include]`, `[sources]`, `[pkg]`, `[link]`, and
  `[build].dir`.
- Added C and C++ source passthrough for native builds.

### Changed

- Improved native header scanning for direct SIMD intrinsic imports.

### Fixed

- Fixed native signature parsing for signatures with suffixes such as
  `noexcept(true)`.
- Fixed `size_t` native type mapping to `usize`.
