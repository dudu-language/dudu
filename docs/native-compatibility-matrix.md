# Native Compatibility Matrix

Dudu's C/C++ interop promise should be checked against real libraries, not only
toy headers. This matrix tracks useful C/C++ APIs by domain and records whether
Dudu can import, typecheck, emit, build, and run small examples without wrapper
headers.

The fast test loop should stay small. Run this matrix periodically, before
version bumps, and whenever native header scanning, native type compatibility,
project-driver build behavior, or generated CMake changes.

Last local probe run: 2026-06-27 with `scripts/probe_optional.sh`. The default
run passed all available non-heavy probes on this machine; `spdlog` remained
the intentional default skip, and the manual
`DUDU_PROBE_HEAVY=1` spdlog smoke passed separately.

Last targeted tensor backend probe run: 2026-07-02 with
`scripts/probe_optional.sh openblas opencl` plus the external
`dudu-datascience` target API checker. OpenBLAS tensor comparison and OpenCL
tensor add/matmul probes passed locally.

Last targeted native callable-value probe run: 2026-07-12 with
`scripts/probe_optional.sh libxml2`. Function-pointer typedef aliases retain
their parameter and result types, and the fixture calls the imported
`xmlFree` allocator value directly without a wrapper.

Last local dogfood run: 2026-07-01 with `scripts/test_dogfood.sh`.
`raymarch-dd` built through the generated CMake backend, and
`dudu-webserver` built plus passed route smoke checks for `/`, `/health`,
`/echo`, and `/routes`. `dudu-datascience` built and its target API
graduation checker passed with 5 graduated specs and 0 pending specs.

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
| Eigen | math | header-only templates, vector typedef aliases and constructors | pass | `scripts/probe_optional.sh` / `eigen_vector.dd` | alias-target methods and inherited template methods/operators such as `Vector3f.dot` need deeper native base-template modeling |
| OpenBLAS / CBLAS | numeric | C ABI calls, fixed arrays, C enum parameters, pointer handoff, BLAS dot/matmul link behavior, reusable tensor comparison | pass | `scripts/probe_optional.sh` / `openblas_ddot.dd`, `openblas_sgemm.dd`, `tensor_dogfood/openblas_compare.dd` | no |
| OpenCV | image / CV | generated C++ build and tiny image write smoke | pass | `scripts/probe_optional.sh` / `examples/image_filter.dd` | no |
| sqlite3 | database | C API, pointers, result types, prepare/step/finalize | pass | `scripts/probe_optional.sh` / `sqlite_crud.dd` | no |
| Lua | scripting / embedding | multiple C headers, stack API, C strings, constants, lifecycle | pass | `scripts/probe_optional.sh` / `lua_stack.dd` | no |
| zlib | compression | C API, buffers, typedefs, constants, pointer output params | pass | `scripts/probe_optional.sh` / `zlib_roundtrip.dd` | no |
| liblzma | compression | C API, version query, nullable C string return, link | pass | `scripts/probe_optional.sh` / `lzma_version.dd` | no |
| libuuid | utility / systems | typedefed C array locals such as `uuid_t`, native array-to-pointer call handoff, mutable `char *out` buffer, constants, compare/copy, link | pass | `scripts/probe_optional.sh` / `uuid_parse.dd` | no |
| curl | network | C API, constants, pointer returns, struct field reads, link | pass | `scripts/probe_optional.sh` / `curl_version_info.dd` | no |
| OpenSSL | crypto / TLS | C API, const byte input, output buffers, link flags | pass | `scripts/probe_optional.sh` / `openssl_sha256.dd` | no |
| libevent | event loop / network | C API, opaque pointers, config/base lifecycle, `cstr` return | pass | `scripts/probe_optional.sh` / `libevent_base.dd` | no |
| libxml2 | XML | `import cxx`, C-style globals from a C++-aware C header, parser lifecycle, returned buffers, callable function-pointer allocator values, link flags | pass | `scripts/probe_optional.sh` / `libxml_parse_memory.dd` | no |
| Expat | XML | C API, opaque parser pointer, enum status, string input, lifecycle | pass | `scripts/probe_optional.sh` / `expat_parse.dd` | no |
| Cairo | 2D graphics | C API, opaque drawing context/surface pointers, enum constants, image buffer inspection, lifecycle | pass | `scripts/probe_optional.sh` / `cairo_image_surface.dd` | no |
| FreeType | font / text | C API, typedefed opaque pointer, output params, version query, lifecycle | pass | `scripts/probe_optional.sh` / `freetype_version.dd` | no |
| libpng | image | C API, typedef aliases, byte buffers, signature checks, link | pass | `scripts/probe_optional.sh` / `libpng_signature.dd` | no |
| libjpeg | image | C API, local C structs, macro-expanded common fields, address passing, C enum constants, setup function, link | pass | `scripts/probe_optional.sh` / `libjpeg_compress_setup.dd` | no |
| stb headers | media / utility | packaged single-header C API, byte buffers, output params, link | pass | `scripts/probe_optional.sh` / `stb_image_info.dd` | no |
| fmt | C++ utility | variadic templates, runtime format strings, `std::string` return | pass | `scripts/probe_optional.sh` / `fmt_format.dd` | no |
| spdlog | logging | template-heavy header stack, formatted logging call, link | heavy/manual pass | `DUDU_PROBE_HEAVY=1 scripts/probe_optional.sh` / `spdlog_basic.dd` | clang scanner currently exceeds the default optional-probe budget |
| Boost filesystem | C++ utility | namespace import, path construction, member methods, string-returning overloads, link | pass | `scripts/probe_optional.sh` / `boost_filesystem.dd` | no |
| raylib | game / media | window/game example, audio synth build | pass | `scripts/probe_optional.sh` / `examples/raylib_game.dd` | no |
| SDL3 | windowing | window example build through pkg-config | pass | `scripts/probe_optional.sh` / `examples/sdl3_window.dd` | no |
| GLFW | windowing | OpenGL triangle host build | pass | `scripts/probe_optional.sh` / `examples/glfw_opengl_triangle.dd` | no |
| Dear ImGui | UI | C++ namespace API, unaliased namespace visibility, context lifecycle, version query, link | pass | `scripts/probe_optional.sh` / `imgui_context.dd` | no |
| X11 | platform / windowing | C API, nullable `cstr`, opaque display pointer, XID return | pass | `scripts/probe_optional.sh` / `x11_display_probe.dd` | no |
| Wayland client | platform / windowing | C API, nullable `cstr`, opaque display pointer, link | pass | `scripts/probe_optional.sh` / `wayland_display_probe.dd` | no |
| OpenCL | compute | host API, kernel setup, run result, tensor buffer upload/download, elementwise and matmul kernels | pass | `scripts/probe_optional.sh` / `examples/opencl_kernel_host.dd`, `tensor_dogfood/opencl_tensor_add.dd`, `tensor_dogfood/opencl_tensor_matmul.dd` | no |
| Vulkan | graphics | header import, object setup smoke, link | pass | `scripts/probe_optional.sh` / `examples/vulkan_triangle.dd` | no |
| FFmpeg libavcodec | media | direct C header import, package link, packet alloc/free smoke | pass | `scripts/probe_optional.sh` / `examples/ffmpeg_probe_decode.dd` | no |
| C macros | C/C++ preprocessor | constants, function-like macros, variadics, lowercase macros | fixture | `cpp_macro_bomb.dd`, `c_macro_constants.dd` | no |
| Native C++ templates | C++ templates | explicit template calls, method templates, dependent fields and returns, nested and namespace alias templates, dependent type/non-type defaults, omitted-argument materialization, partial and concrete class specialization selection, warm scan cache | fixture | `native_template_function.dd`, `cpp_template_member.dd`, `native_dependent_template_return.dd`, `native_dependent_alias_metadata.dd`, `native_associated_type.dd`, `cpp_stdlib_interop.dd` | no |

## External Dogfood Repos

These repos are not required for the normal test loop, but they should be run
periodically because they catch whole-project friction that small fixtures miss.

| Repo | Domain | Current Use | Status |
| --- | --- | --- | --- |
| `/home/vega/Coding/Graphics/raymarch-dd` | graphics / SDL / generated modules | Real-time app with vector math, C library calls, generated CMake, LSP navigation pressure, and runtime rendering. | local dogfood via `scripts/test_dogfood.sh` |
| `/home/vega/Coding/Web/dudu-webserver` | POSIX networking / C++ stdlib | Multi-file blocking HTTP server using sockets, polling, libc, C strings, and `std.string` without a C++ webserver shim. | local dogfood via `scripts/test_dogfood.sh`; build and route smoke passed on 2026-06-27 |
| `/home/vega/Coding/ML/dudu-datascience` | tensor indexing / BLAS / OpenCL / ML-shaped API | Data-science indexing tour plus target API graduation manifest for tensor surface, advanced indexing, BLAS backend, OpenCL GPU backend, and `mald` autograd training examples. CPU/OpenBLAS demos import package-shaped `ndad`; optional OpenCL remains isolated in the older `dudu_tensor` target until backend extension modules are cleaner. | local dogfood via `dudu run --timings` and `./scripts/check_target_api.sh`; passed on 2026-07-02 |

## Planned Matrix Targets

These are important enough to become official probes or fixtures as the
compiler matures.

| API / Library | Domain | Target Coverage | Status | Notes |
| --- | --- | --- | --- | --- |
| CUDA / CUBLAS | GPU / ML | host API, device buffers, BLAS-like calls | skip | Needs NVIDIA tooling/hardware. |
| platform APIs | OS | Win32, Cocoa subsets | planned | Platform-specific; X11 and Wayland have Linux probes. |

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
