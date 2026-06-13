cmake_minimum_required(VERSION 3.20)

project(dudu LANGUAGES CXX)

option(DUDU_STRICT "Enable strict compiler warnings" ON)
option(DUDU_WARN_AS_ERROR "Treat warnings as errors" ON)

add_library(dudu_frontend
    src/dudu/ast.cpp
    src/dudu/build_flags.cpp
    src/dudu/control_flow.cpp
    src/dudu/cpp_emit.cpp
    src/dudu/cpp_emit_prelude.cpp
    src/dudu/cpp_expr_builtins.cpp
    src/dudu/cpp_lower.cpp
    src/dudu/cpp_pointer_members.cpp
    src/dudu/cpp_stmt_emit.cpp
    src/dudu/cpp_stmt_types.cpp
    src/dudu/cpp_type.cpp
    src/dudu/escapes.cpp
    src/dudu/format.cpp
    src/dudu/format_path.cpp
    src/dudu/lexer.cpp
    src/dudu/module_loader.cpp
    src/dudu/native_build.cpp
    src/dudu/naming.cpp
    src/dudu/parser.cpp
    src/dudu/parser_utils.cpp
    src/dudu/project_config.cpp
    src/dudu/sema.cpp
    src/dudu/sema_alloc.cpp
    src/dudu/sema_bindings.cpp
    src/dudu/sema_context.cpp
    src/dudu/sema_constexpr.cpp
    src/dudu/sema_constructors.cpp
    src/dudu/sema_expr.cpp
    src/dudu/sema_function_type.cpp
    src/dudu/sema_index.cpp
    src/dudu/sema_methods.cpp
    src/dudu/sema_ops.cpp
    src/dudu/sema_scan.cpp
    src/dudu/source.cpp
    src/dudu/token.cpp
    src/dudu/type_compat.cpp
    src/dudu/unsupported.cpp
)
target_include_directories(dudu_frontend PUBLIC src)
target_compile_features(dudu_frontend PUBLIC cxx_std_20)

add_executable(dudu src/main.cpp)
target_link_libraries(dudu PRIVATE dudu_frontend)
target_compile_features(dudu PRIVATE cxx_std_20)

add_executable(duc src/main.cpp)
target_link_libraries(duc PRIVATE dudu_frontend)
target_compile_features(duc PRIVATE cxx_std_20)

add_executable(dudu_frontend_tests tests/frontend_tests.cpp)
target_link_libraries(dudu_frontend_tests PRIVATE dudu_frontend)
target_compile_features(dudu_frontend_tests PRIVATE cxx_std_20)
target_compile_definitions(dudu_frontend_tests PRIVATE DUDU_REPO_ROOT="${CMAKE_SOURCE_DIR}")

if(DUDU_STRICT)
    target_compile_options(dudu PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(duc PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(dudu_frontend PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(dudu_frontend_tests PRIVATE -Wall -Wextra -Wpedantic)
    if(DUDU_WARN_AS_ERROR)
        target_compile_options(dudu PRIVATE -Werror)
        target_compile_options(duc PRIVATE -Werror)
        target_compile_options(dudu_frontend PRIVATE -Werror)
        target_compile_options(dudu_frontend_tests PRIVATE -Werror)
    endif()
endif()

enable_testing()
add_test(NAME dudu_frontend_tests COMMAND dudu_frontend_tests)
