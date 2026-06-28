cmake_minimum_required(VERSION 3.20)

project(dudu LANGUAGES CXX)

include(GNUInstallDirs)

option(DUDU_STRICT "Enable strict compiler warnings" ON)
option(DUDU_WARN_AS_ERROR "Treat warnings as errors" OFF)
option(DUDU_BUILD_TESTS "Build Dudu compiler tests" OFF)

add_library(dudu_frontend
    src/dudu/array_shape.cpp
    src/dudu/ast.cpp
    src/dudu/ast_expr.cpp
    src/dudu/ast_expr_parse.cpp
    src/dudu/ast_expr_token_core.cpp
    src/dudu/ast_expr_token_postfix.cpp
    src/dudu/ast_parse_scan.cpp
    src/dudu/ast_parse_utils.cpp
    src/dudu/ast_type.cpp
    src/dudu/ast_type_parse.cpp
    src/dudu/ast_type_token_parser.cpp
    src/dudu/build_flags.cpp
    src/dudu/build_backend_select.cpp
    src/dudu/cli_command.cpp
    src/dudu/cli_options.cpp
    src/dudu/cli_usage.cpp
    src/dudu/cmake_backend.cpp
    src/dudu/cmake_emit.cpp
    src/dudu/control_flow.cpp
    src/dudu/cpp_emit.cpp
    src/dudu/cpp_emit_classes.cpp
    src/dudu/cpp_emit_enums.cpp
    src/dudu/cpp_emit_modules.cpp
    src/dudu/cpp_emit_prelude.cpp
    src/dudu/cpp_emit_tests.cpp
    src/dudu/cpp_expr_call_emit.cpp
    src/dudu/cpp_expr_escape_emit.cpp
    src/dudu/cpp_expr_emit.cpp
    src/dudu/cpp_expr_index.cpp
    src/dudu/cpp_expr_offsetof_emit.cpp
    src/dudu/cpp_expr_pointer_receiver.cpp
    src/dudu/cpp_expr_builtins.cpp
    src/dudu/cpp_expr_slices.cpp
    src/dudu/cpp_expr_swizzles.cpp
    src/dudu/cpp_lower.cpp
    src/dudu/cpp_match_emit.cpp
    src/dudu/cpp_pointer_members.cpp
    src/dudu/cpp_raw_escape.cpp
    src/dudu/cpp_raw_escape_templates.cpp
    src/dudu/cpp_stmt_emit.cpp
    src/dudu/cpp_stmt_emit_support.cpp
    src/dudu/cpp_stmt_generic_methods.cpp
    src/dudu/cpp_stmt_helpers.cpp
    src/dudu/cpp_stmt_types.cpp
    src/dudu/cpp_type.cpp
    src/dudu/cpp_type_arrays.cpp
    src/dudu/cpp_type_ref.cpp
    src/dudu/cpp_type_templates.cpp
    src/dudu/decorators.cpp
    src/dudu/escapes.cpp
    src/dudu/file_io.cpp
    src/dudu/format.cpp
    src/dudu/format_imports.cpp
    src/dudu/format_path.cpp
    src/dudu/language_server.cpp
    src/dudu/language_server_ast_lints.cpp
    src/dudu/language_server_code_actions.cpp
    src/dudu/language_server_completion.cpp
    src/dudu/language_server_definition.cpp
    src/dudu/language_server_diagnostics.cpp
    src/dudu/language_server_hover.cpp
    src/dudu/language_server_import_references.cpp
    src/dudu/language_server_json.cpp
    src/dudu/language_server_lint_cpp_escape.cpp
    src/dudu/language_server_lint_common.cpp
    src/dudu/language_server_lint_scope.cpp
    src/dudu/language_server_lint_suspicious_cast.cpp
    src/dudu/language_server_lint_unreachable.cpp
    src/dudu/language_server_local_context.cpp
    src/dudu/language_server_navigation.cpp
    src/dudu/language_server_native_lookup.cpp
    src/dudu/language_server_reference_collect.cpp
    src/dudu/language_server_references.cpp
    src/dudu/language_server_selection.cpp
    src/dudu/language_server_semantic_tokens.cpp
    src/dudu/language_server_symbols.cpp
    src/dudu/language_server_symbol_results.cpp
    src/dudu/language_server_support.cpp
    src/dudu/language_server_workspace.cpp
    src/dudu/lexer.cpp
    src/dudu/match_patterns.cpp
    src/dudu/module_import_aliases.cpp
    src/dudu/module_loader.cpp
    src/dudu/module_names.cpp
    src/dudu/native_build.cpp
    src/dudu/native_header_cache.cpp
    src/dudu/native_header_cache_deps.cpp
    src/dudu/native_header_cache_format.cpp
    src/dudu/native_header_collision.cpp
    src/dudu/native_header_dedupe.cpp
    src/dudu/native_header_merge.cpp
    src/dudu/native_header_parse.cpp
    src/dudu/native_header_parse_macros.cpp
    src/dudu/native_header_scan_command.cpp
    src/dudu/native_header_scope.cpp
    src/dudu/native_header_types.cpp
    src/dudu/native_headers.cpp
    src/dudu/native_signature_match.cpp
    src/dudu/native_signature_substitution.cpp
    src/dudu/native_signature_templates.cpp
    src/dudu/naming.cpp
    src/dudu/parser.cpp
    src/dudu/parser_decls.cpp
    src/dudu/parser_statement_continuation.cpp
    src/dudu/parser_statement_ops.cpp
    src/dudu/parser_statements.cpp
    src/dudu/parser_utils.cpp
    src/dudu/project_config.cpp
    src/dudu/project_driver.cpp
    src/dudu/sema.cpp
    src/dudu/sema_alloc.cpp
    src/dudu/sema_assignment.cpp
    src/dudu/sema_bindings.cpp
    src/dudu/sema_body.cpp
    src/dudu/sema_body_helpers.cpp
    src/dudu/sema_body_substitution.cpp
    src/dudu/sema_builtin_methods.cpp
    src/dudu/sema_common.cpp
    src/dudu/sema_context.cpp
    src/dudu/sema_constexpr.cpp
    src/dudu/sema_constructors.cpp
    src/dudu/sema_declarations.cpp
    src/dudu/sema_enum.cpp
    src/dudu/sema_expr.cpp
    src/dudu/sema_expr_cpp_escape.cpp
    src/dudu/sema_expr_cpp_escape_calls.cpp
    src/dudu/sema_expr_cpp_escape_infer.cpp
    src/dudu/sema_expr_support.cpp
    src/dudu/sema_expr_template.cpp
    src/dudu/sema_expr_type_calls.cpp
    src/dudu/sema_expr_type_member_calls.cpp
    src/dudu/sema_expr_type_members.cpp
    src/dudu/sema_expr_type_ops.cpp
    src/dudu/sema_function_type.cpp
    src/dudu/sema_generics.cpp
    src/dudu/sema_index.cpp
    src/dudu/sema_index_type_ref.cpp
    src/dudu/sema_inheritance.cpp
    src/dudu/sema_inheritance_internal.cpp
    src/dudu/sema_iterable.cpp
    src/dudu/sema_match.cpp
    src/dudu/sema_member_paths.cpp
    src/dudu/sema_method_templates.cpp
    src/dudu/sema_methods.cpp
    src/dudu/sema_native.cpp
    src/dudu/sema_ops.cpp
    src/dudu/sema_scan.cpp
    src/dudu/sema_super.cpp
    src/dudu/sema_swizzles.cpp
    src/dudu/source.cpp
    src/dudu/test_driver.cpp
    src/dudu/token.cpp
    src/dudu/type_compat.cpp
    src/dudu/type_compat_literals.cpp
    src/dudu/type_compat_native.cpp
    src/dudu/type_compat_structural.cpp
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

install(TARGETS dudu duc
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

install(DIRECTORY editors/
    DESTINATION ${CMAKE_INSTALL_DATADIR}/dudu/editors
)

install(DIRECTORY docs/
    DESTINATION ${CMAKE_INSTALL_DOCDIR}/docs
    FILES_MATCHING PATTERN "*.md"
)

install(FILES README.md CHANGELOG.md
    DESTINATION ${CMAKE_INSTALL_DOCDIR}
)

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/include")
    install(DIRECTORY include/
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    )
endif()

if(DUDU_BUILD_TESTS)
    add_executable(dudu_frontend_tests tests/frontend_tests.cpp)
    target_link_libraries(dudu_frontend_tests PRIVATE dudu_frontend)
    target_compile_features(dudu_frontend_tests PRIVATE cxx_std_20)
    target_compile_definitions(dudu_frontend_tests PRIVATE DUDU_REPO_ROOT="${CMAKE_SOURCE_DIR}")

    add_executable(dudu_ast_frontend_tests tests/ast_frontend_tests.cpp)
    target_link_libraries(dudu_ast_frontend_tests PRIVATE dudu_frontend)
    target_compile_features(dudu_ast_frontend_tests PRIVATE cxx_std_20)

    add_executable(dudu_native_frontend_tests tests/native_frontend_tests.cpp)
    target_link_libraries(dudu_native_frontend_tests PRIVATE dudu_frontend)
    target_compile_features(dudu_native_frontend_tests PRIVATE cxx_std_20)
    target_compile_definitions(dudu_native_frontend_tests PRIVATE DUDU_REPO_ROOT="${CMAKE_SOURCE_DIR}")

    add_executable(dudu_native_type_compat_tests tests/native_type_compat_tests.cpp)
    target_link_libraries(dudu_native_type_compat_tests PRIVATE dudu_frontend)
    target_compile_features(dudu_native_type_compat_tests PRIVATE cxx_std_20)
    target_compile_definitions(dudu_native_type_compat_tests PRIVATE DUDU_REPO_ROOT="${CMAKE_SOURCE_DIR}")

    add_executable(dudu_emit_frontend_tests tests/emit_frontend_tests.cpp)
    target_link_libraries(dudu_emit_frontend_tests PRIVATE dudu_frontend)
    target_compile_features(dudu_emit_frontend_tests PRIVATE cxx_std_20)
    target_compile_definitions(dudu_emit_frontend_tests PRIVATE DUDU_REPO_ROOT="${CMAKE_SOURCE_DIR}")

    add_executable(dudu_parser_range_tests tests/parser_range_tests.cpp)
    target_link_libraries(dudu_parser_range_tests PRIVATE dudu_frontend)
    target_compile_features(dudu_parser_range_tests PRIVATE cxx_std_20)

    add_executable(dudu_project_config_tests tests/project_config_tests.cpp)
    target_link_libraries(dudu_project_config_tests PRIVATE dudu_frontend)
    target_compile_features(dudu_project_config_tests PRIVATE cxx_std_20)
    target_compile_definitions(dudu_project_config_tests PRIVATE DUDU_REPO_ROOT="${CMAKE_SOURCE_DIR}")
endif()

if(DUDU_STRICT)
    target_compile_options(dudu PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(duc PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(dudu_frontend PRIVATE -Wall -Wextra -Wpedantic)
    if(DUDU_BUILD_TESTS)
        target_compile_options(dudu_frontend_tests PRIVATE -Wall -Wextra -Wpedantic)
        target_compile_options(dudu_ast_frontend_tests PRIVATE -Wall -Wextra -Wpedantic)
        target_compile_options(dudu_native_frontend_tests PRIVATE -Wall -Wextra -Wpedantic)
        target_compile_options(dudu_native_type_compat_tests PRIVATE -Wall -Wextra -Wpedantic)
        target_compile_options(dudu_emit_frontend_tests PRIVATE -Wall -Wextra -Wpedantic)
        target_compile_options(dudu_parser_range_tests PRIVATE -Wall -Wextra -Wpedantic)
        target_compile_options(dudu_project_config_tests PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    if(DUDU_WARN_AS_ERROR)
        target_compile_options(dudu PRIVATE -Werror)
        target_compile_options(duc PRIVATE -Werror)
        target_compile_options(dudu_frontend PRIVATE -Werror)
        if(DUDU_BUILD_TESTS)
            target_compile_options(dudu_frontend_tests PRIVATE -Werror)
            target_compile_options(dudu_ast_frontend_tests PRIVATE -Werror)
            target_compile_options(dudu_native_frontend_tests PRIVATE -Werror)
            target_compile_options(dudu_native_type_compat_tests PRIVATE -Werror)
            target_compile_options(dudu_emit_frontend_tests PRIVATE -Werror)
            target_compile_options(dudu_parser_range_tests PRIVATE -Werror)
            target_compile_options(dudu_project_config_tests PRIVATE -Werror)
        endif()
    endif()
endif()

if(DUDU_BUILD_TESTS)
    enable_testing()
    add_test(NAME dudu_frontend_tests COMMAND dudu_frontend_tests)
    add_test(NAME dudu_ast_frontend_tests COMMAND dudu_ast_frontend_tests)
    add_test(NAME dudu_native_frontend_tests COMMAND dudu_native_frontend_tests)
    add_test(NAME dudu_native_type_compat_tests COMMAND dudu_native_type_compat_tests)
    add_test(NAME dudu_emit_frontend_tests COMMAND dudu_emit_frontend_tests)
    add_test(NAME dudu_parser_range_tests COMMAND dudu_parser_range_tests)
    add_test(NAME dudu_project_config_tests COMMAND dudu_project_config_tests)
endif()
