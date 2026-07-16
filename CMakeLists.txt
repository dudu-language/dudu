cmake_minimum_required(VERSION 3.20)

project(dudu LANGUAGES CXX)

include(GNUInstallDirs)

file(READ "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" DUDU_VERSION)
string(STRIP "${DUDU_VERSION}" DUDU_VERSION)
if(NOT DUDU_VERSION MATCHES "^[0-9]+[.][0-9]+[.][0-9]+(-[0-9A-Za-z.-]+)?$")
    message(FATAL_ERROR "VERSION is not a valid Dudu semantic version: ${DUDU_VERSION}")
endif()
set(DUDU_CPP_SCHEMA_VERSION 1)
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/src/dudu/core/version.hpp.in"
    "${CMAKE_CURRENT_BINARY_DIR}/generated/dudu/core/version.hpp"
    @ONLY
)

file(GLOB DUDU_LLVM_PREFIXES LIST_DIRECTORIES true
    "/usr/lib/llvm-*"
    "/opt/homebrew/opt/llvm"
    "/usr/local/opt/llvm"
)
find_path(DUDU_LIBCLANG_INCLUDE_DIR clang-c/Index.h
    HINTS $ENV{LLVM_ROOT} ${DUDU_LLVM_PREFIXES}
    PATH_SUFFIXES include
)
find_library(DUDU_LIBCLANG_LIBRARY
    NAMES clang libclang
    HINTS $ENV{LLVM_ROOT} ${DUDU_LLVM_PREFIXES}
    PATH_SUFFIXES lib
)
if(NOT DUDU_LIBCLANG_INCLUDE_DIR OR NOT DUDU_LIBCLANG_LIBRARY)
    message(FATAL_ERROR
        "Dudu native header awareness requires libclang development files. "
        "Install libclang-dev/llvm (Linux) or Homebrew llvm (macOS), or set LLVM_ROOT."
    )
endif()

option(DUDU_STRICT "Enable strict compiler warnings" ON)
option(DUDU_WARN_AS_ERROR "Treat warnings as errors" OFF)
option(DUDU_BUILD_TESTS "Build Dudu compiler tests" OFF)
set(DUDU_INSTALL_OWNER "source" CACHE STRING
    "Installation owner: source, local, installer, homebrew, aur, or deb")
set_property(CACHE DUDU_INSTALL_OWNER PROPERTY STRINGS source local installer homebrew aur deb)
if(NOT DUDU_INSTALL_OWNER MATCHES "^(source|local|installer|homebrew|aur|deb)$")
    message(FATAL_ERROR "invalid DUDU_INSTALL_OWNER: ${DUDU_INSTALL_OWNER}")
endif()
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/packaging/install-owner.in"
    "${CMAKE_CURRENT_BINARY_DIR}/generated/install-owner"
    @ONLY
)

set(DUDU_SUBSYSTEM_DIRS
    core
    frontend
    parser
    sema
    codegen
    native
    macro
    project
    lsp
    format
    testing
    support
)

set(DUDU_FRONTEND_SOURCES)
foreach(subsystem IN LISTS DUDU_SUBSYSTEM_DIRS)
    file(GLOB subsystem_sources CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/src/dudu/${subsystem}/*.cpp"
    )
    list(APPEND DUDU_FRONTEND_SOURCES ${subsystem_sources})
endforeach()

set(DUDU_MACRO_RUNTIME_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/src/dudu/macro/macro_capabilities.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/dudu/macro/macro_hash.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/dudu/macro/macro_protocol_generated.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/dudu/macro/macro_wire.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/dudu/macro/macro_worker_runtime.cpp"
)
list(REMOVE_ITEM DUDU_FRONTEND_SOURCES ${DUDU_MACRO_RUNTIME_SOURCES})
list(REMOVE_ITEM DUDU_FRONTEND_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/src/dudu/macro/macro_sdk_bridge_generated.cpp"
)

add_library(dudu_macro_runtime STATIC ${DUDU_MACRO_RUNTIME_SOURCES})
target_include_directories(dudu_macro_runtime PUBLIC src)
target_compile_features(dudu_macro_runtime PUBLIC cxx_std_20)

add_library(dudu_frontend ${DUDU_FRONTEND_SOURCES})
target_include_directories(dudu_frontend PUBLIC
    src
    "${CMAKE_CURRENT_BINARY_DIR}/generated"
)
target_include_directories(dudu_frontend PRIVATE ${DUDU_LIBCLANG_INCLUDE_DIR})
target_compile_definitions(dudu_frontend PRIVATE
    DUDU_SOURCE_LIBRARY_DIR="${CMAKE_CURRENT_SOURCE_DIR}/lib"
)
target_link_libraries(dudu_frontend PUBLIC dudu_macro_runtime PRIVATE ${DUDU_LIBCLANG_LIBRARY})
target_compile_features(dudu_frontend PUBLIC cxx_std_20)

add_executable(dudu src/tools/dudu.cpp)
target_link_libraries(dudu PRIVATE dudu_frontend)
target_compile_features(dudu PRIVATE cxx_std_20)

add_executable(duc src/tools/duc.cpp)
target_link_libraries(duc PRIVATE dudu_frontend)
target_compile_features(duc PRIVATE cxx_std_20)

add_executable(dudu-lsp src/tools/dudu_lsp.cpp)
target_link_libraries(dudu-lsp PRIVATE dudu_frontend)
target_compile_features(dudu-lsp PRIVATE cxx_std_20)

install(TARGETS dudu duc dudu-lsp
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

install(TARGETS dudu_macro_runtime
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

install(FILES
    src/dudu/macro/macro_protocol_generated.hpp
    src/dudu/macro/macro_capabilities.hpp
    src/dudu/macro/macro_sdk_bridge_generated.hpp
    src/dudu/macro/macro_wire.hpp
    src/dudu/macro/macro_worker_runtime.hpp
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/dudu/macro
)

install(FILES src/dudu/macro/macro_sdk_bridge_generated.cpp
    DESTINATION ${CMAKE_INSTALL_DATADIR}/dudu/macro
)

install(PROGRAMS install.sh
    DESTINATION ${CMAKE_INSTALL_LIBEXECDIR}/dudu
    RENAME dudu-toolchain-manager
)

install(FILES "${CMAKE_CURRENT_BINARY_DIR}/generated/install-owner"
    DESTINATION ${CMAKE_INSTALL_DATADIR}/dudu
)

install(DIRECTORY editors/vim editors/nvim
    DESTINATION ${CMAKE_INSTALL_DATADIR}/dudu/editors
)

install(DIRECTORY docs/
    DESTINATION ${CMAKE_INSTALL_DOCDIR}/docs
    FILES_MATCHING PATTERN "*.md"
)

install(DIRECTORY lib/
    DESTINATION ${CMAKE_INSTALL_DATADIR}/dudu/lib
    FILES_MATCHING PATTERN "*.dd"
)

install(FILES schema/macro_ast.json
    DESTINATION ${CMAKE_INSTALL_DATADIR}/dudu/schema
)

install(FILES
    README.md
    CHANGELOG.md
    COPYRIGHT
    LICENSE-APACHE
    LICENSE-MIT
    VERSION
    DESTINATION ${CMAKE_INSTALL_DOCDIR}
)

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/include")
    install(DIRECTORY include/
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    )
endif()

function(dudu_add_test target source library)
    add_executable(${target} ${source})
    target_link_libraries(${target} PRIVATE ${library})
    target_compile_features(${target} PRIVATE cxx_std_20)
    if(DUDU_STRICT)
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
        if(DUDU_WARN_AS_ERROR)
            target_compile_options(${target} PRIVATE -Werror)
        endif()
    endif()
    add_test(NAME ${target} COMMAND ${target})
endfunction()

if(DUDU_BUILD_TESTS)
    enable_testing()

    dudu_add_test(dudu_frontend_syntax_fixture_tests
        tests/frontend_syntax_fixture_tests.cpp dudu_frontend)
    dudu_add_test(dudu_frontend_diagnostics_format_tests
        tests/frontend_diagnostics_format_tests.cpp dudu_frontend)
    dudu_add_test(dudu_frontend_semantics_codegen_tests
        tests/frontend_semantics_codegen_tests.cpp dudu_frontend)
    target_compile_definitions(
        dudu_frontend_syntax_fixture_tests PRIVATE DUDU_REPO_ROOT="${CMAKE_SOURCE_DIR}")

    dudu_add_test(dudu_language_server_semantic_token_tests
        tests/language_server_semantic_token_tests.cpp dudu_frontend)
    dudu_add_test(dudu_parser_recovery_tests tests/parser_recovery_tests.cpp dudu_frontend)
    dudu_add_test(dudu_ast_statement_tests tests/ast_statement_tests.cpp dudu_frontend)
    dudu_add_test(dudu_ast_declaration_query_tests
        tests/ast_declaration_query_tests.cpp dudu_frontend)
    dudu_add_test(dudu_ast_type_core_tests tests/ast_type_core_tests.cpp dudu_frontend)
    dudu_add_test(dudu_native_template_type_tests tests/native_template_type_tests.cpp dudu_frontend)
    dudu_add_test(dudu_ast_expression_shape_tests tests/ast_expression_shape_tests.cpp dudu_frontend)
    dudu_add_test(dudu_ast_type_shape_tests tests/ast_type_shape_tests.cpp dudu_frontend)
    dudu_add_test(dudu_language_server_diagnostics_tests
        tests/language_server_diagnostics_tests.cpp dudu_frontend)
    dudu_add_test(dudu_language_server_completion_inlay_tests
        tests/language_server_completion_inlay_tests.cpp dudu_frontend)
    dudu_add_test(dudu_language_server_native_editor_tests
        tests/language_server_native_editor_tests.cpp dudu_frontend)
    dudu_add_test(dudu_language_server_project_index_tests
        tests/language_server_project_index_tests.cpp dudu_frontend)
    target_compile_definitions(
        dudu_language_server_diagnostics_tests PRIVATE DUDU_REPO_ROOT="${CMAKE_SOURCE_DIR}")
    target_compile_definitions(
        dudu_language_server_completion_inlay_tests
        PRIVATE DUDU_REPO_ROOT="${CMAKE_SOURCE_DIR}")
    target_compile_definitions(
        dudu_language_server_native_editor_tests PRIVATE DUDU_REPO_ROOT="${CMAKE_SOURCE_DIR}")
    target_compile_definitions(
        dudu_language_server_project_index_tests PRIVATE DUDU_REPO_ROOT="${CMAKE_SOURCE_DIR}")

    dudu_add_test(dudu_language_server_definition_hover_tests
        tests/language_server_definition_hover_tests.cpp dudu_frontend)
    dudu_add_test(dudu_language_server_symbol_references_tests
        tests/language_server_symbol_references_tests.cpp dudu_frontend)
    dudu_add_test(dudu_language_server_module_native_references_tests
        tests/language_server_module_native_references_tests.cpp dudu_frontend)
    target_compile_definitions(
        dudu_language_server_definition_hover_tests
        PRIVATE DUDU_REPO_ROOT="${CMAKE_SOURCE_DIR}")
    target_compile_definitions(
        dudu_language_server_symbol_references_tests
        PRIVATE DUDU_REPO_ROOT="${CMAKE_SOURCE_DIR}")
    target_compile_definitions(
        dudu_language_server_module_native_references_tests
        PRIVATE DUDU_REPO_ROOT="${CMAKE_SOURCE_DIR}")

    dudu_add_test(dudu_language_server_text_sync_tests
        tests/language_server_text_sync_tests.cpp dudu_frontend)
    dudu_add_test(dudu_language_server_workspace_tests
        tests/language_server_workspace_tests.cpp dudu_frontend)
    dudu_add_test(dudu_member_type_substitution_tests
        tests/member_type_substitution_tests.cpp dudu_frontend)
    dudu_add_test(dudu_member_type_lookup_tests tests/member_type_lookup_tests.cpp dudu_frontend)
    dudu_add_test(dudu_module_loader_tests tests/module_loader_tests.cpp dudu_frontend)
    dudu_add_test(dudu_project_index_module_tests tests/project_index_module_tests.cpp dudu_frontend)
    dudu_add_test(dudu_cpp_module_artifact_tests tests/cpp_module_artifact_tests.cpp dudu_frontend)
    target_compile_definitions(
        dudu_project_index_module_tests PRIVATE DUDU_REPO_ROOT="${CMAKE_SOURCE_DIR}")

    dudu_add_test(dudu_type_inference_tests tests/type_inference_tests.cpp dudu_frontend)
    dudu_add_test(dudu_native_scan_basics_tests tests/native_scan_basics_tests.cpp dudu_frontend)
    dudu_add_test(dudu_native_import_identity_tests
        tests/native_import_identity_tests.cpp dudu_frontend)
    dudu_add_test(dudu_native_scan_dedupe_tests
        tests/native_scan_dedupe_tests.cpp dudu_frontend)
    dudu_add_test(dudu_native_header_cache_tests
        tests/native_header_cache_tests.cpp dudu_frontend)
    dudu_add_test(dudu_native_template_metadata_tests
        tests/native_template_metadata_tests.cpp dudu_frontend)
    dudu_add_test(dudu_native_import_projection_tests
        tests/native_import_projection_tests.cpp dudu_frontend)
    target_compile_definitions(
        dudu_native_scan_basics_tests PRIVATE DUDU_REPO_ROOT="${CMAKE_SOURCE_DIR}")
    target_compile_definitions(
        dudu_native_import_identity_tests PRIVATE DUDU_REPO_ROOT="${CMAKE_SOURCE_DIR}")
    target_compile_definitions(
        dudu_native_scan_dedupe_tests PRIVATE DUDU_REPO_ROOT="${CMAKE_SOURCE_DIR}")
    target_compile_definitions(
        dudu_native_header_cache_tests PRIVATE DUDU_REPO_ROOT="${CMAKE_SOURCE_DIR}")
    target_compile_definitions(
        dudu_native_template_metadata_tests PRIVATE DUDU_REPO_ROOT="${CMAKE_SOURCE_DIR}")
    target_compile_definitions(
        dudu_native_import_projection_tests PRIVATE DUDU_REPO_ROOT="${CMAKE_SOURCE_DIR}")

    dudu_add_test(dudu_native_type_compat_tests tests/native_type_compat_tests.cpp dudu_frontend)
    target_compile_definitions(dudu_native_type_compat_tests PRIVATE DUDU_REPO_ROOT="${CMAKE_SOURCE_DIR}")

    dudu_add_test(dudu_emit_native_pointer_tests
        tests/emit_native_pointer_tests.cpp dudu_frontend)
    dudu_add_test(dudu_emit_collection_match_tests
        tests/emit_collection_match_tests.cpp dudu_frontend)
    dudu_add_test(dudu_emit_class_function_tests
        tests/emit_class_function_tests.cpp dudu_frontend)
    target_compile_definitions(
        dudu_emit_native_pointer_tests PRIVATE DUDU_REPO_ROOT="${CMAKE_SOURCE_DIR}")
    target_compile_definitions(
        dudu_emit_collection_match_tests PRIVATE DUDU_REPO_ROOT="${CMAKE_SOURCE_DIR}")

    dudu_add_test(dudu_parser_range_tests tests/parser_range_tests.cpp dudu_frontend)
    dudu_add_test(dudu_project_config_tests tests/project_config_tests.cpp dudu_frontend)
    target_compile_definitions(dudu_project_config_tests PRIVATE DUDU_REPO_ROOT="${CMAKE_SOURCE_DIR}")

    dudu_add_test(dudu_format_tests tests/format_tests.cpp dudu_frontend)
    dudu_add_test(dudu_macro_protocol_tests tests/macro_protocol_tests.cpp dudu_frontend)
    dudu_add_test(dudu_macro_syntax_tests tests/macro_syntax_tests.cpp dudu_frontend)
    target_compile_definitions(dudu_macro_syntax_tests PRIVATE DUDU_REPO_ROOT="${CMAKE_SOURCE_DIR}")

    dudu_add_test(dudu_macro_worker_runtime_tests
        tests/macro_worker_runtime_tests.cpp dudu_macro_runtime)
    dudu_add_test(dudu_macro_worker_codegen_tests
        tests/macro_worker_codegen_tests.cpp dudu_frontend)
    dudu_add_test(dudu_macro_worker_process_tests
        tests/macro_worker_process_tests.cpp dudu_frontend)
    dudu_add_test(dudu_macro_worker_binary_tests
        tests/macro_worker_binary_tests.cpp dudu_frontend)
    target_compile_definitions(
        dudu_macro_worker_binary_tests PRIVATE DUDU_REPO_ROOT="${CMAKE_SOURCE_DIR}"
                                               DUDU_BUILD_DIR="${CMAKE_BINARY_DIR}"
                                               DUDU_TEST_CXX="${CMAKE_CXX_COMPILER}")

    dudu_add_test(dudu_macro_ast_bridge_tests tests/macro_ast_bridge_tests.cpp dudu_frontend)
    dudu_add_test(dudu_macro_expansion_tests tests/macro_expansion_tests.cpp dudu_frontend)
    dudu_add_test(dudu_macro_capability_tests tests/macro_capability_tests.cpp dudu_frontend)
    dudu_add_test(dudu_macro_expansion_render_tests
        tests/macro_expansion_render_tests.cpp dudu_frontend)
    dudu_add_test(dudu_macro_package_fixture_tests
        tests/macro_package_fixture_tests.cpp dudu_frontend)
    target_compile_definitions(
        dudu_macro_package_fixture_tests PRIVATE DUDU_REPO_ROOT="${CMAKE_SOURCE_DIR}")

    dudu_add_test(dudu_enum_method_tests tests/enum_method_tests.cpp dudu_frontend)
endif()

option(DUDU_PACKAGE_DEB "Enable Debian package generation through CPack" OFF)
if(DUDU_PACKAGE_DEB)
    string(REPLACE "-" "~" DUDU_DEBIAN_VERSION "${DUDU_VERSION}")
    set(CPACK_GENERATOR DEB)
    set(CPACK_PACKAGE_NAME dudu)
    set(CPACK_PACKAGE_VENDOR "Dudu Language")
    set(CPACK_PACKAGE_CONTACT "https://github.com/dudu-language/dudu/issues")
    set(CPACK_PACKAGE_DESCRIPTION_SUMMARY
        "Python-shaped systems language with direct C and C++ interop")
    set(CPACK_PACKAGE_VERSION "${DUDU_DEBIAN_VERSION}")
    set(CPACK_PACKAGE_HOMEPAGE_URL "https://github.com/dudu-language/dudu")
    set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE-MIT")
    set(CPACK_DEBIAN_PACKAGE_SECTION devel)
    set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
    set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)
    include(CPack)
endif()

if(DUDU_STRICT)
    target_compile_options(dudu PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(duc PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(dudu-lsp PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(dudu_frontend PRIVATE -Wall -Wextra -Wpedantic)
    if(DUDU_WARN_AS_ERROR)
        target_compile_options(dudu PRIVATE -Werror)
        target_compile_options(duc PRIVATE -Werror)
        target_compile_options(dudu-lsp PRIVATE -Werror)
        target_compile_options(dudu_frontend PRIVATE -Werror)
    endif()
endif()

if(DUDU_BUILD_TESTS)
    add_test(NAME dudu_macro_sdk_compile_tests
        COMMAND bash ${CMAKE_SOURCE_DIR}/scripts/test_macro_sdk_compile.sh $<TARGET_FILE:duc>)
endif()
