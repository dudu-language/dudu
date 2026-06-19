# Native Compatibility Matrix

Dudu's C/C++ interop promise should be checked against real libraries, not only
toy headers. This matrix tracks useful C/C++ APIs by domain and records whether
Dudu can import, typecheck, emit, build, and run small examples without wrapper
headers.

The fast test loop should stay small. Run this matrix periodically, before
version bumps, and whenever native header scanning, native type compatibility,
project-driver build behavior, or generated CMake changes.

Last local probe run: 2026-06-19 with `scripts/probe_optional.sh`.

## Status Key

- `pass`: currently covered by an optional probe or fixture and passed locally.
- `fixture`: covered by frontend/codegen tests, but not necessarily linked
  against the system library in the optional probe suite.
- `planned`: desired compatibility target with no official check yet.
- `skip`: intentionally not covered on this machine or requires unavailable
  hardware/tooling.

## Current Probes

| API / Library | Domain | Coverage | Status | Command / Fixture | Wrapper |
| --- | --- | --- | --- | --- | --- |
| C standard library | libc / systems | callbacks, qsort, malloc/free-style APIs | fixture | `tests/fixtures/c_qsort_callback.dd` | no |
| POSIX mmap | systems | headers, constants, pointers, file APIs, run | pass | `scripts/probe_optional.sh` / `posix_mmap_hash.dd` | no |
| POSIX pthread | systems | pthread structs, mutex calls, link flags, run | pass | `scripts/probe_optional.sh` / `posix_threads_mutex.dd` | small helper header for callback shape |
| C++ standard library algorithms | C++ stdlib | containers, algorithms, pairs, tuples, `std.get` | pass | `scripts/probe_cpp_stdlib_algorithms.sh` | no |
| C++ standard library variants | C++ stdlib | `std.variant`, `std.holds_alternative`, `std.get` | fixture | `tests/fixtures/cpp_std_variant.dd` | no |
| glm | math | header import, constructors, functions such as `glm.dot` | pass | `scripts/probe_optional.sh` / `glm_math.dd` | no |
| Eigen | math | header-only templates, vector constructors, methods, operators | pass | `scripts/probe_optional.sh` / `eigen_vector.dd` | no |
| OpenBLAS / CBLAS | numeric | C ABI calls, fixed arrays, pointer handoff, link behavior | pass | `scripts/probe_optional.sh` / `openblas_ddot.dd` | no |
| OpenCV | image / CV | generated C++ build and tiny image write smoke | pass | `scripts/probe_optional.sh` / `examples/image_filter.dd` | no |
| sqlite3 | database | C API, pointers, result types, prepare/step/finalize | pass | `scripts/probe_optional.sh` / `sqlite_crud.dd` | no |
| zlib | compression | C API, buffers, typedefs, constants, pointer output params | pass | `scripts/probe_optional.sh` / `zlib_roundtrip.dd` | no |
| curl | network | C API, constants, pointer returns, struct field reads, link | pass | `scripts/probe_optional.sh` / `curl_version_info.dd` | no |
| OpenSSL | crypto / TLS | C API, const byte input, output buffers, link flags | pass | `scripts/probe_optional.sh` / `openssl_sha256.dd` | no |
| libevent | event loop / network | C API, opaque pointers, config/base lifecycle, `cstr` return | pass | `scripts/probe_optional.sh` / `libevent_base.dd` | no |
| libpng | image | C API, typedef aliases, byte buffers, signature checks, link | pass | `scripts/probe_optional.sh` / `libpng_signature.dd` | no |
| fmt | C++ utility | variadic templates, runtime format strings, `std::string` return | pass | `scripts/probe_optional.sh` / `fmt_format.dd` | no |
| spdlog | logging | template-heavy header stack, formatted logging call, link | pass | `scripts/probe_optional.sh` / `spdlog_basic.dd` | no |
| raylib | game / media | window/game example, audio synth build | pass | `scripts/probe_optional.sh` / `examples/raylib_game.dd` | no |
| SDL3 | windowing | window example build through pkg-config | pass | `scripts/probe_optional.sh` / `examples/sdl3_window.dd` | no |
| GLFW | windowing | OpenGL triangle host build | pass | `scripts/probe_optional.sh` / `examples/glfw_opengl_triangle.dd` | no |
| OpenCL | compute | host API, kernel setup, run result | pass | `scripts/probe_optional.sh` / `examples/opencl_kernel_host.dd` | no |
| Vulkan | graphics | header import, object setup smoke, link | pass | `scripts/probe_optional.sh` / `examples/vulkan_triangle.dd` | no |
| FFmpeg libavcodec | media | header import, package link, probe/decode smoke | pass | `scripts/probe_optional.sh` / `examples/ffmpeg_probe_decode.dd` | small wrapper for normal FFmpeg include shape |
| C macros | C/C++ preprocessor | constants, function-like macros, variadics, lowercase macros | fixture | `cpp_macro_bomb.dd`, `c_macro_constants.dd` | no |
| Native C++ templates | C++ templates | explicit template calls, method templates, dependent returns | fixture | `native_template_function.dd`, `cpp_template_member.dd`, `native_dependent_template_return.dd` | no |

## Planned Matrix Targets

These are important enough to become official probes or fixtures as the
compiler matures.

| API / Library | Domain | Target Coverage | Status | Notes |
| --- | --- | --- | --- | --- |
| Dear ImGui | UI | C++ namespace API, backends, frame loop | planned | Prefer direct ImGui headers; wrappers only for backend/platform glue users normally write in C++. |
| stb headers | media / utility | single-header C, macro configuration | planned | May require explicit macro configuration policy. |
| Boost subset | C++ utility | selected non-huge components | planned | Avoid trying all of Boost at once. |
| CUDA / CUBLAS | GPU / ML | host API, device buffers, BLAS-like calls | skip | Needs NVIDIA tooling/hardware. |
| platform APIs | OS | Win32, Cocoa, X11/Wayland subsets | planned | Platform-specific; keep optional. |

## Rules

- A normal C/C++ user should not need a Dudu-specific wrapper for ordinary
  declarations, functions, templates, constants, or types.
- Wrapper headers are acceptable for macro-generated declarations, token
  pasting, partial-syntax macros, backend glue, or platform setup that C/C++
  users commonly hide behind adapter code too.
- A new wrapper should explain what Dudu cannot model yet. If the wrapper exists
  only because of a Dudu compiler gap, add a compiler issue/plan item.
- Failures in this matrix should become language/compiler work, not permanent
  example workarounds.
