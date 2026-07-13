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

add_library(dudu_frontend ${DUDU_FRONTEND_SOURCES})
target_include_directories(dudu_frontend PUBLIC
    src
    "${CMAKE_CURRENT_BINARY_DIR}/generated"
)
target_include_directories(dudu_frontend PRIVATE ${DUDU_LIBCLANG_INCLUDE_DIR})
target_compile_definitions(dudu_frontend PRIVATE
    DUDU_SOURCE_LIBRARY_DIR="${CMAKE_CURRENT_SOURCE_DIR}/lib"
)
target_link_libraries(dudu_frontend PRIVATE ${DUDU_LIBCLANG_LIBRARY})
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

if(DUDU_BUILD_TESTS)
    add_executable(dudu_frontend_tests tests/frontend_tests.cpp)
    target_link_libraries(dudu_frontend_tests PRIVATE dudu_frontend)
    target_compile_features(dudu_frontend_tests PRIVATE cxx_std_20)
    target_compile_definitions(dudu_frontend_tests PRIVATE DUDU_REPO_ROOT="${CMAKE_SOURCE_DIR}")

    add_executable(dudu_ast_frontend_tests tests/ast_frontend_tests.cpp)
    target_link_libraries(dudu_ast_frontend_tests PRIVATE dudu_frontend)
    target_compile_features(dudu_ast_frontend_tests PRIVATE cxx_std_20)

    add_executable(dudu_ast_type_frontend_tests tests/ast_type_frontend_tests.cpp)
    target_link_libraries(dudu_ast_type_frontend_tests PRIVATE dudu_frontend)
    target_compile_features(dudu_ast_type_frontend_tests PRIVATE cxx_std_20)

    add_executable(dudu_ast_shape_frontend_tests tests/ast_shape_frontend_tests.cpp)
    target_link_libraries(dudu_ast_shape_frontend_tests PRIVATE dudu_frontend)
    target_compile_features(dudu_ast_shape_frontend_tests PRIVATE cxx_std_20)

    add_executable(dudu_language_server_tests tests/language_server_tests.cpp)
    target_link_libraries(dudu_language_server_tests PRIVATE dudu_frontend)
    target_compile_features(dudu_language_server_tests PRIVATE cxx_std_20)
    target_compile_definitions(dudu_language_server_tests PRIVATE DUDU_REPO_ROOT="${CMAKE_SOURCE_DIR}")

    add_executable(dudu_language_server_navigation_tests tests/language_server_navigation_tests.cpp)
    target_link_libraries(dudu_language_server_navigation_tests PRIVATE dudu_frontend)
    target_compile_features(dudu_language_server_navigation_tests PRIVATE cxx_std_20)
    target_compile_definitions(dudu_language_server_navigation_tests PRIVATE DUDU_REPO_ROOT="${CMAKE_SOURCE_DIR}")

    add_executable(dudu_language_server_text_sync_tests tests/language_server_text_sync_tests.cpp)
    target_link_libraries(dudu_language_server_text_sync_tests PRIVATE dudu_frontend)
    target_compile_features(dudu_language_server_text_sync_tests PRIVATE cxx_std_20)

    add_executable(dudu_member_type_frontend_tests tests/member_type_frontend_tests.cpp)
    target_link_libraries(dudu_member_type_frontend_tests PRIVATE dudu_frontend)
    target_compile_features(dudu_member_type_frontend_tests PRIVATE cxx_std_20)

    add_executable(dudu_module_frontend_tests tests/module_frontend_tests.cpp)
    target_link_libraries(dudu_module_frontend_tests PRIVATE dudu_frontend)
    target_compile_features(dudu_module_frontend_tests PRIVATE cxx_std_20)
    target_compile_definitions(dudu_module_frontend_tests PRIVATE DUDU_REPO_ROOT="${CMAKE_SOURCE_DIR}")

    add_executable(dudu_type_inference_tests tests/type_inference_tests.cpp)
    target_link_libraries(dudu_type_inference_tests PRIVATE dudu_frontend)
    target_compile_features(dudu_type_inference_tests PRIVATE cxx_std_20)

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

    add_executable(dudu_format_tests tests/format_tests.cpp)
    target_link_libraries(dudu_format_tests PRIVATE dudu_frontend)
    target_compile_features(dudu_format_tests PRIVATE cxx_std_20)

    add_executable(dudu_macro_protocol_tests tests/macro_protocol_tests.cpp)
    target_link_libraries(dudu_macro_protocol_tests PRIVATE dudu_frontend)
    target_compile_features(dudu_macro_protocol_tests PRIVATE cxx_std_20)

    add_executable(dudu_macro_syntax_tests tests/macro_syntax_tests.cpp)
    target_link_libraries(dudu_macro_syntax_tests PRIVATE dudu_frontend)
    target_compile_features(dudu_macro_syntax_tests PRIVATE cxx_std_20)
    target_compile_definitions(dudu_macro_syntax_tests PRIVATE DUDU_REPO_ROOT="${CMAKE_SOURCE_DIR}")
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
    if(DUDU_BUILD_TESTS)
        target_compile_options(dudu_frontend_tests PRIVATE -Wall -Wextra -Wpedantic)
        target_compile_options(dudu_ast_frontend_tests PRIVATE -Wall -Wextra -Wpedantic)
        target_compile_options(dudu_ast_shape_frontend_tests PRIVATE -Wall -Wextra -Wpedantic)
        target_compile_options(dudu_ast_type_frontend_tests PRIVATE -Wall -Wextra -Wpedantic)
        target_compile_options(dudu_language_server_tests PRIVATE -Wall -Wextra -Wpedantic)
        target_compile_options(dudu_language_server_navigation_tests PRIVATE -Wall -Wextra -Wpedantic)
        target_compile_options(dudu_language_server_text_sync_tests PRIVATE -Wall -Wextra -Wpedantic)
        target_compile_options(dudu_member_type_frontend_tests PRIVATE -Wall -Wextra -Wpedantic)
        target_compile_options(dudu_module_frontend_tests PRIVATE -Wall -Wextra -Wpedantic)
        target_compile_options(dudu_type_inference_tests PRIVATE -Wall -Wextra -Wpedantic)
        target_compile_options(dudu_native_frontend_tests PRIVATE -Wall -Wextra -Wpedantic)
        target_compile_options(dudu_native_type_compat_tests PRIVATE -Wall -Wextra -Wpedantic)
        target_compile_options(dudu_emit_frontend_tests PRIVATE -Wall -Wextra -Wpedantic)
        target_compile_options(dudu_parser_range_tests PRIVATE -Wall -Wextra -Wpedantic)
        target_compile_options(dudu_project_config_tests PRIVATE -Wall -Wextra -Wpedantic)
        target_compile_options(dudu_format_tests PRIVATE -Wall -Wextra -Wpedantic)
        target_compile_options(dudu_macro_protocol_tests PRIVATE -Wall -Wextra -Wpedantic)
        target_compile_options(dudu_macro_syntax_tests PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    if(DUDU_WARN_AS_ERROR)
        target_compile_options(dudu PRIVATE -Werror)
        target_compile_options(duc PRIVATE -Werror)
        target_compile_options(dudu-lsp PRIVATE -Werror)
        target_compile_options(dudu_frontend PRIVATE -Werror)
        if(DUDU_BUILD_TESTS)
            target_compile_options(dudu_frontend_tests PRIVATE -Werror)
            target_compile_options(dudu_ast_frontend_tests PRIVATE -Werror)
            target_compile_options(dudu_ast_shape_frontend_tests PRIVATE -Werror)
            target_compile_options(dudu_ast_type_frontend_tests PRIVATE -Werror)
            target_compile_options(dudu_language_server_tests PRIVATE -Werror)
            target_compile_options(dudu_language_server_navigation_tests PRIVATE -Werror)
            target_compile_options(dudu_language_server_text_sync_tests PRIVATE -Werror)
            target_compile_options(dudu_member_type_frontend_tests PRIVATE -Werror)
            target_compile_options(dudu_module_frontend_tests PRIVATE -Werror)
            target_compile_options(dudu_type_inference_tests PRIVATE -Werror)
            target_compile_options(dudu_native_frontend_tests PRIVATE -Werror)
            target_compile_options(dudu_native_type_compat_tests PRIVATE -Werror)
            target_compile_options(dudu_emit_frontend_tests PRIVATE -Werror)
            target_compile_options(dudu_parser_range_tests PRIVATE -Werror)
            target_compile_options(dudu_project_config_tests PRIVATE -Werror)
            target_compile_options(dudu_format_tests PRIVATE -Werror)
            target_compile_options(dudu_macro_protocol_tests PRIVATE -Werror)
            target_compile_options(dudu_macro_syntax_tests PRIVATE -Werror)
        endif()
    endif()
endif()

if(DUDU_BUILD_TESTS)
    enable_testing()
    add_test(NAME dudu_frontend_tests COMMAND dudu_frontend_tests)
    add_test(NAME dudu_ast_frontend_tests COMMAND dudu_ast_frontend_tests)
    add_test(NAME dudu_ast_shape_frontend_tests COMMAND dudu_ast_shape_frontend_tests)
    add_test(NAME dudu_ast_type_frontend_tests COMMAND dudu_ast_type_frontend_tests)
    add_test(NAME dudu_language_server_tests COMMAND dudu_language_server_tests)
    add_test(NAME dudu_language_server_navigation_tests COMMAND dudu_language_server_navigation_tests)
    add_test(NAME dudu_language_server_text_sync_tests COMMAND dudu_language_server_text_sync_tests)
    add_test(NAME dudu_member_type_frontend_tests COMMAND dudu_member_type_frontend_tests)
    add_test(NAME dudu_module_frontend_tests COMMAND dudu_module_frontend_tests)
    add_test(NAME dudu_type_inference_tests COMMAND dudu_type_inference_tests)
    add_test(NAME dudu_native_frontend_tests COMMAND dudu_native_frontend_tests)
    add_test(NAME dudu_native_type_compat_tests COMMAND dudu_native_type_compat_tests)
    add_test(NAME dudu_emit_frontend_tests COMMAND dudu_emit_frontend_tests)
    add_test(NAME dudu_parser_range_tests COMMAND dudu_parser_range_tests)
    add_test(NAME dudu_project_config_tests COMMAND dudu_project_config_tests)
    add_test(NAME dudu_format_tests COMMAND dudu_format_tests)
    add_test(NAME dudu_macro_protocol_tests COMMAND dudu_macro_protocol_tests)
    add_test(NAME dudu_macro_syntax_tests COMMAND dudu_macro_syntax_tests)
endif()
