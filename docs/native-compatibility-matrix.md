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
| POSIX pthread | systems | pthread structs, mutex calls, function pointer callback, link flags, run | pass | `scripts/probe_optional.sh` / `posix_threads_mutex.dd` | no |
| C++ standard library algorithms | C++ stdlib | containers, algorithms, pairs, tuples, `std.get` | pass | `scripts/probe_cpp_stdlib_algorithms.sh` | no |
| C++ standard library interop mix | C++ stdlib | `std.optional`, `std.array`, `std.span`, `std.map`, `std.unordered_set`, `std.function`, smart pointers, mutex guard | fixture | `tests/fixtures/cpp_stdlib_interop.dd` | no |
| C++ standard library variants | C++ stdlib | `std.variant`, `std.holds_alternative`, `std.get` | fixture | `tests/fixtures/cpp_std_variant.dd` | no |
| glm | math | header import, constructors, functions such as `glm.dot` | pass | `scripts/probe_optional.sh` / `glm_math.dd` | no |
| Eigen | math | header-only templates, vector constructors, methods, operators | pass | `scripts/probe_optional.sh` / `eigen_vector.dd` | no |
| OpenBLAS / CBLAS | numeric | C ABI calls, fixed arrays, pointer handoff, link behavior | pass | `scripts/probe_optional.sh` / `openblas_ddot.dd` | no |
| OpenCV | image / CV | generated C++ build and tiny image write smoke | pass | `scripts/probe_optional.sh` / `examples/image_filter.dd` | no |
| sqlite3 | database | C API, pointers, result types, prepare/step/finalize | pass | `scripts/probe_optional.sh` / `sqlite_crud.dd` | no |
| Lua | scripting / embedding | multiple C headers, stack API, C strings, constants, lifecycle | pass | `scripts/probe_optional.sh` / `lua_stack.dd` | no |
| zlib | compression | C API, buffers, typedefs, constants, pointer output params | pass | `scripts/probe_optional.sh` / `zlib_roundtrip.dd` | no |
| curl | network | C API, constants, pointer returns, struct field reads, link | pass | `scripts/probe_optional.sh` / `curl_version_info.dd` | no |
| OpenSSL | crypto / TLS | C API, const byte input, output buffers, link flags | pass | `scripts/probe_optional.sh` / `openssl_sha256.dd` | no |
| libevent | event loop / network | C API, opaque pointers, config/base lifecycle, `cstr` return | pass | `scripts/probe_optional.sh` / `libevent_base.dd` | no |
| libxml2 | XML | `import cxx`, C-style globals from a C++-aware C header, parser lifecycle, returned buffers, link flags | pass | `scripts/probe_optional.sh` / `libxml_parse_memory.dd` | no |
| Expat | XML | C API, opaque parser pointer, enum status, string input, lifecycle | pass | `scripts/probe_optional.sh` / `expat_parse.dd` | no |
| Cairo | 2D graphics | C API, opaque drawing context/surface pointers, enum constants, image buffer inspection, lifecycle | pass | `scripts/probe_optional.sh` / `cairo_image_surface.dd` | no |
| FreeType | font / text | C API, typedefed opaque pointer, output params, version query, lifecycle | pass | `scripts/probe_optional.sh` / `freetype_version.dd` | no |
| libpng | image | C API, typedef aliases, byte buffers, signature checks, link | pass | `scripts/probe_optional.sh` / `libpng_signature.dd` | no |
| libjpeg | image | C API, local C struct, address passing, C enum constants, setup function, link | pass | `scripts/probe_optional.sh` / `libjpeg_compress_setup.dd` | no |
| stb headers | media / utility | packaged single-header C API, byte buffers, output params, link | pass | `scripts/probe_optional.sh` / `stb_image_info.dd` | no |
| fmt | C++ utility | variadic templates, runtime format strings, `std::string` return | pass | `scripts/probe_optional.sh` / `fmt_format.dd` | no |
| spdlog | logging | template-heavy header stack, formatted logging call, link | pass | `scripts/probe_optional.sh` / `spdlog_basic.dd` | no |
| Boost filesystem | C++ utility | namespace import, path construction, member methods, string-returning overloads, link | pass | `scripts/probe_optional.sh` / `boost_filesystem.dd` | no |
| raylib | game / media | window/game example, audio synth build | pass | `scripts/probe_optional.sh` / `examples/raylib_game.dd` | no |
| SDL3 | windowing | window example build through pkg-config | pass | `scripts/probe_optional.sh` / `examples/sdl3_window.dd` | no |
| GLFW | windowing | OpenGL triangle host build | pass | `scripts/probe_optional.sh` / `examples/glfw_opengl_triangle.dd` | no |
| Dear ImGui | UI | C++ namespace API, unaliased namespace visibility, context lifecycle, version query, link | pass | `scripts/probe_optional.sh` / `imgui_context.dd` | no |
| X11 | platform / windowing | C API, nullable `cstr`, opaque display pointer, XID return | pass | `scripts/probe_optional.sh` / `x11_display_probe.dd` | no |
| Wayland client | platform / windowing | C API, nullable `cstr`, opaque display pointer, link | pass | `scripts/probe_optional.sh` / `wayland_display_probe.dd` | no |
| OpenCL | compute | host API, kernel setup, run result | pass | `scripts/probe_optional.sh` / `examples/opencl_kernel_host.dd` | no |
| Vulkan | graphics | header import, object setup smoke, link | pass | `scripts/probe_optional.sh` / `examples/vulkan_triangle.dd` | no |
| FFmpeg libavcodec | media | direct C header import, package link, packet alloc/free smoke | pass | `scripts/probe_optional.sh` / `examples/ffmpeg_probe_decode.dd` | no |
| C macros | C/C++ preprocessor | constants, function-like macros, variadics, lowercase macros | fixture | `cpp_macro_bomb.dd`, `c_macro_constants.dd` | no |
| Native C++ templates | C++ templates | explicit template calls, method templates, dependent returns | fixture | `native_template_function.dd`, `cpp_template_member.dd`, `native_dependent_template_return.dd` | no |

## Planned Matrix Targets

These are important enough to become official probes or fixtures as the
compiler matures.

| API / Library | Domain | Target Coverage | Status | Notes |
| --- | --- | --- | --- | --- |
| CUDA / CUBLAS | GPU / ML | host API, device buffers, BLAS-like calls | skip | Needs NVIDIA tooling/hardware. |
| platform APIs | OS | Win32, Cocoa subsets | planned | Platform-specific; X11 and Wayland have Linux probes. |
| macro-expanded C struct fields | C interop | fields introduced through C preprocessor member macros | planned | Found while probing libjpeg: `jpeg_compress_struct.err` comes from `jpeg_common_fields`, and Dudu's scanner does not model that field yet. |

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
