#!/usr/bin/env bash
set -euo pipefail
trap 'echo "test_codegen_shapes.sh failed near line $LINENO" >&2' ERR

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

"$repo_root/build/dudu" "$repo_root/examples/compile_time.dd" --emit-cpp \
    "$repo_root/build/compile_time_raylib.cpp" -DDEBUG=true -DRENDER_BACKEND=raylib
grep -q "inline constexpr bool DEBUG = true;" "$repo_root/build/compile_time_raylib.cpp"
grep -q 'inline constexpr std::string_view RENDER_BACKEND = "raylib";' \
    "$repo_root/build/compile_time_raylib.cpp"

"$repo_root/build/duc" emit "$repo_root/examples/cuda_kernel.dd" \
    -o "$repo_root/build/cuda_kernel.cpp"
grep -q "#define DUDU_CUDA_GLOBAL" "$repo_root/build/cuda_kernel.cpp"
grep -q "DUDU_CUDA_GLOBAL void saxpy_kernel" "$repo_root/build/cuda_kernel.cpp"
grep -q "float\\* dev_x = nullptr;" "$repo_root/build/cuda_kernel.cpp"
grep -q "err = cudaMalloc" "$repo_root/build/cuda_kernel.cpp"
! grep -q "dudu::dudu" "$repo_root/build/cuda_kernel.cpp"

"$repo_root/build/duc" emit "$repo_root/examples/cuda_shared_memory_tile.dd" \
    -o "$repo_root/build/cuda_shared_memory_tile.cpp"
grep -q "DUDU_CUDA_GLOBAL void tile_sum" "$repo_root/build/cuda_shared_memory_tile.cpp"
grep -q "std::array<float, 256> tile" "$repo_root/build/cuda_shared_memory_tile.cpp"
grep -q "threadIdx::x" "$repo_root/build/cuda_shared_memory_tile.cpp"

"$repo_root/build/duc" emit "$repo_root/examples/shader_compute.dd" \
    -o "$repo_root/build/shader_compute.cpp"
grep -q "#define DUDU_SHADER_COMPUTE" "$repo_root/build/shader_compute.cpp"
grep -q "#define DUDU_WORKGROUP_SIZE" "$repo_root/build/shader_compute.cpp"
grep -q "DUDU_SHADER_COMPUTE DUDU_WORKGROUP_SIZE(8, 8, 1) void blur_x" \
    "$repo_root/build/shader_compute.cpp"

"$repo_root/build/duc" emit "$repo_root/examples/layout_hardware.dd" \
    -o "$repo_root/build/layout_hardware.cpp"
grep -Fq "struct __attribute__((packed)) PacketHeader" "$repo_root/build/layout_hardware.cpp"
grep -Fq "struct alignas(16) Mat4Block" "$repo_root/build/layout_hardware.cpp"
grep -Fq "inline volatile UartRegs* const UART0" "$repo_root/build/layout_hardware.cpp"
grep -Fq "offsetof(PacketHeader, flags)" "$repo_root/build/layout_hardware.cpp"

"$repo_root/build/duc" emit "$repo_root/examples/interrupt_handler.dd" \
    -o "$repo_root/build/interrupt_handler.cpp"
grep -Fq 'extern "C" __attribute__((section(".isr_vector"))) void SysTick_Handler()' \
    "$repo_root/build/interrupt_handler.cpp"
grep -Fq "inline volatile TimerRegs* const TIMER0" \
    "$repo_root/build/interrupt_handler.cpp"
grep -Fq "regs->clear = 1;" "$repo_root/build/interrupt_handler.cpp"

"$repo_root/build/duc" emit "$repo_root/examples/web_server.dd" -o "$repo_root/build/web_server.cpp"
grep -Fq "app.Get(\"/todos" "$repo_root/build/web_server.cpp"
grep -Fq "[&](auto&& req, auto&& res)" "$repo_root/build/web_server.cpp"

"$repo_root/build/duc" emit "$repo_root/tests/fixtures/sqlite_crud.dd" \
    -o "$repo_root/build/sqlite_crud.cpp"
grep -Fq '#include <sqlite3.h>' "$repo_root/build/sqlite_crud.cpp"
grep -Fq "sqlite3_prepare_v2" "$repo_root/build/sqlite_crud.cpp"
grep -Fq "dudu::Result<Todo, DbError> fetch_todo" "$repo_root/build/sqlite_crud.cpp"
grep -Fq "std::string(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1)))" \
    "$repo_root/build/sqlite_crud.cpp"

"$repo_root/build/duc" emit "$repo_root/tests/fixtures/c_macro_constants.dd" \
    -o "$repo_root/build/c_macro_constants.cpp"
grep -Fq '#include "c_macro_wrap.h"' "$repo_root/build/c_macro_constants.cpp"
grep -Fq "DUDU_WRAP_SCALE(5)" "$repo_root/build/c_macro_constants.cpp"
grep -Fq "return ((total + DUDU_WRAP_MAGIC) - 7);" "$repo_root/build/c_macro_constants.cpp"
"$repo_root/build/duc" emit "$repo_root/tests/fixtures/c_variadic_macro.dd" \
    -o "$repo_root/build/c_variadic_macro.cpp"
grep -Fq "DUDU_WRAP_FIRST(20, 1, 2, 3)" "$repo_root/build/c_variadic_macro.cpp"
grep -Fq "DUDU_WRAP_COUNT(first, 22, 99)" "$repo_root/build/c_variadic_macro.cpp"
"$repo_root/build/duc" emit "$repo_root/tests/fixtures/cpp_macro_bomb.dd" \
    -o "$repo_root/build/cpp_macro_bomb.cpp"
grep -Fq "DUDU_MACRO_BOMB_ASSIGN(scratch, 40);" "$repo_root/build/cpp_macro_bomb.cpp"
grep -Fq "DUDU_MACRO_BOMB_SUM2(7, 13, 99)" "$repo_root/build/cpp_macro_bomb.cpp"
! grep -Fq "macros::DUDU_MACRO_BOMB" "$repo_root/build/cpp_macro_bomb.cpp"

"$repo_root/build/duc" emit "$repo_root/tests/fixtures/c_lowercase_macro.dd" \
    -o "$repo_root/build/c_lowercase_macro.cpp"
grep -Fq "#include \"assert.h\"" "$repo_root/build/c_lowercase_macro.cpp"
grep -Fq "assert((value == 42));" "$repo_root/build/c_lowercase_macro.cpp"
"$repo_root/build/duc" emit "$repo_root/tests/fixtures/c_direct_lowercase_macro.dd" \
    -o "$repo_root/build/c_direct_lowercase_macro.cpp"
grep -Fq "if (!((value == 42))) { throw std::runtime_error(\"assert failed: value == 42\"); }" \
    "$repo_root/build/c_direct_lowercase_macro.cpp"

"$repo_root/build/duc" emit "$repo_root/tests/fixtures/named_callback.dd" \
    -o "$repo_root/build/function_pointer.cpp"
grep -Fq "std::add_pointer_t<int32_t(int32_t)> callback" \
    "$repo_root/build/function_pointer.cpp"
! grep -q "std::function" "$repo_root/build/function_pointer.cpp"

"$repo_root/build/duc" emit "$repo_root/tests/fixtures/cpp_move_unique_ptr.dd" \
    -o "$repo_root/build/cpp_move_unique_ptr.cpp"
grep -Fq "std::move(first)" "$repo_root/build/cpp_move_unique_ptr.cpp"
"$repo_root/build/duc" emit "$repo_root/tests/fixtures/static_fields.dd" \
    -o "$repo_root/build/static_fields.cpp"
grep -Fq "inline static int32_t count = 0;" "$repo_root/build/static_fields.cpp"
grep -Fq "Counter::count += 1;" "$repo_root/build/static_fields.cpp"

"$repo_root/build/duc" emit "$repo_root/tests/fixtures/cpp_filesystem_path.dd" \
    -o "$repo_root/build/cpp_filesystem_path.cpp"
grep -Fq "std::filesystem::path path" "$repo_root/build/cpp_filesystem_path.cpp"
"$repo_root/build/duc" emit "$repo_root/tests/fixtures/cpp_chrono_timer.dd" \
    -o "$repo_root/build/cpp_chrono_timer.cpp"
grep -Fq "std::chrono::duration_cast<std::chrono::milliseconds>" \
    "$repo_root/build/cpp_chrono_timer.cpp"

"$repo_root/build/duc" emit "$repo_root/tests/fixtures/std_vector_map_string.dd" \
    -o "$repo_root/build/std_vector_map_string.cpp"
grep -Fq "std::vector<std::string> items" "$repo_root/build/std_vector_map_string.cpp"
grep -Fq "std::unordered_map<std::string, int32_t> scores" \
    "$repo_root/build/std_vector_map_string.cpp"
grep -Fq "std::string label = items[0]" "$repo_root/build/std_vector_map_string.cpp"

"$repo_root/build/duc" emit "$repo_root/tests/fixtures/binary_packet_parser.dd" \
    -o "$repo_root/build/binary_packet_parser.cpp"
grep -Fq "struct __attribute__((packed)) PacketHeader" "$repo_root/build/binary_packet_parser.cpp"
grep -Fq "offsetof(PacketHeader, flags)" "$repo_root/build/binary_packet_parser.cpp"
grep -Fq "std::array<uint8_t, 8> bytes" "$repo_root/build/binary_packet_parser.cpp"
grep -Fq "<< 24" "$repo_root/build/binary_packet_parser.cpp"

"$repo_root/build/duc" emit "$repo_root/tests/fixtures/debug_asserts.dd" \
    -o "$repo_root/build/debug_asserts.cpp"
grep -Fq "#include <cassert>" "$repo_root/build/debug_asserts.cpp"
grep -Fq "assert(((value == 42)));" "$repo_root/build/debug_asserts.cpp"
grep -Fq "assert(((value > 0)) && (\"value should be positive\"));" \
    "$repo_root/build/debug_asserts.cpp"

"$repo_root/build/duc" emit "$repo_root/tests/fixtures/bitwise_ops.dd" \
    -o "$repo_root/build/bitwise_ops.cpp"
grep -Fq "1 << 5" "$repo_root/build/bitwise_ops.cpp"
grep -Fq "flags |= (1 << 3)" "$repo_root/build/bitwise_ops.cpp"
grep -Fq "flags ^= (1 << 3)" "$repo_root/build/bitwise_ops.cpp"
grep -Fq "flags >>= 1" "$repo_root/build/bitwise_ops.cpp"

"$repo_root/build/duc" emit "$repo_root/tests/fixtures/tuple_return.dd" \
    -o "$repo_root/build/tuple_return.cpp"
grep -Fq "dudu::Tuple2<int32_t, int32_t> divmod_i32" "$repo_root/build/tuple_return.cpp"
grep -Fq "return {(value / divisor), (value % divisor)};" "$repo_root/build/tuple_return.cpp"
! grep -q "std::tuple" "$repo_root/build/tuple_return.cpp"
! grep -q "#include <tuple>" "$repo_root/build/tuple_return.cpp"

"$repo_root/build/duc" emit "$repo_root/tests/fixtures/generic_method_return_context.dd" \
    -o "$repo_root/build/generic_method_return_context.cpp"
grep -Fq "box.make<int32_t>()" "$repo_root/build/generic_method_return_context.cpp"
grep -Fq "box.pair<int32_t>(22)" "$repo_root/build/generic_method_return_context.cpp"
