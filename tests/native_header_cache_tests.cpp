#include "dudu/codegen/cpp_emit.hpp"
#include "dudu/core/ast_type.hpp"
#include "dudu/lsp/language_server_symbols.hpp"
#include "dudu/native/native_header_cache_deps.hpp"
#include "dudu/native/native_header_cache_format.hpp"
#include "dudu/native/native_header_merge.hpp"
#include "dudu/native/native_header_parse.hpp"
#include "dudu/native/native_header_scan_command.hpp"
#include "dudu/native/native_header_scope.hpp"
#include "dudu/native/native_header_types.hpp"
#include "dudu/native/native_header_usr.hpp"
#include "dudu/native/native_headers.hpp"
#include "dudu/native/native_signature_substitution.hpp"
#include "dudu/parser/parser.hpp"
#include "dudu/sema/sema.hpp"
#include "dudu/sema/sema_context.hpp"
#include "dudu/sema/sema_function_type.hpp"
#include "dudu/sema/sema_method_templates.hpp"
#include "dudu/sema/sema_ops.hpp"
#include "dudu/sema/type_compat.hpp"
#include "dudu/sema/type_compat_native.hpp"

#include <algorithm>
#include <cassert>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>

namespace {
void test_native_cache_record_round_trip() {
    std::ostringstream encoded;
    dudu::write_record(encoded, "DOC",
                       {"line one\nline two", "template<class T>\nT convert(T value)"});
    std::istringstream input(encoded.str());
    const auto decoded = dudu::read_record(input);
    assert(decoded.has_value());
    assert(decoded->first == "DOC");
    assert(decoded->second == std::vector<std::string>({"line one\nline two",
                                                        "template<class T>\nT convert(T value)"}));
    assert(input.peek() == std::char_traits<char>::eof());
}

void test_native_cursor_metadata_round_trip() {
    const dudu::SourceLocation location = {
        .file = dudu::SourceFileName("rich_docs.hpp"), .line = 17, .column = 9};
    dudu::NativeCursorIdentityIndex source;
    source.insert(dudu::NativeCursorKind::Function, "convert", location,
                  "c:@FT@>1#Tconvert#t0.0#I#", dudu::TypeLayout{.size = 8, .alignment = 8},
                  "native.convert");
    source.insert_metadata(
        dudu::NativeCursorKind::Function, "convert", location,
        {.declaration = "template<class T> int convert(T value = T{})",
         .summary_doc_comment = "Convert a value using a compile-time policy.",
         .return_doc_comment = "The converted score.",
         .deprecated_message = "Use convert_v2.",
         .parameters = {{.name = "value",
                         .default_value = "T{}",
                         .doc_comment = "Value to convert."}},
         .template_parameters = {
             {.name = "T", .default_value = "int", .doc_comment = "Converted value type."}}});

    const dudu::NativeCursorIdentityIndex restored =
        dudu::NativeCursorIdentityIndex::deserialize(source.serialize());
    const std::optional<dudu::NativeDeclarationMetadata> metadata =
        restored.find_metadata(dudu::NativeCursorKind::Function, "convert", location);
    assert(metadata.has_value());
    assert(metadata->declaration == "template<class T> int convert(T value = T{})");
    assert(metadata->summary_doc_comment == "Convert a value using a compile-time policy.");
    assert(metadata->return_doc_comment == "The converted score.");
    assert(metadata->deprecated_message == "Use convert_v2.");
    assert(metadata->parameters.size() == 1);
    assert(metadata->parameters[0].name == "value");
    assert(metadata->parameters[0].default_value == "T{}");
    assert(metadata->parameters[0].doc_comment == "Value to convert.");
    assert(metadata->template_parameters.size() == 1);
    assert(metadata->template_parameters[0].name == "T");
    assert(metadata->template_parameters[0].default_value == "int");
    assert(metadata->template_parameters[0].doc_comment == "Converted value type.");
}

void test_native_single_underscore_function_macros(const std::filesystem::path& root) {
    const std::filesystem::path source_dir = root / "build" / "native-macro-scan";
    const std::filesystem::path header_dir = root / "build" / "native-macro-include";
    const std::filesystem::path header = header_dir / "single_underscore_macro.hpp";
    std::filesystem::remove_all(source_dir);
    std::filesystem::remove_all(header_dir);
    std::filesystem::create_directories(source_dir);
    std::filesystem::create_directories(header_dir);

    {
        std::ofstream out(header);
        out << "#pragma once\n"
               "#define _dudu_macro_call(a, b) ((a) + (b))\n"
               "#define __dudu_private_call(a) (a)\n"
               "#define _DUDU_PRIVATE_VALUE 3\n";
    }
    dudu::ModuleAst module = dudu::parse_source("from cpp.path import single_underscore_macro.hpp\n"
                                                "\n"
                                                "def main() -> i32:\n"
                                                "    return _dudu_macro_call(20, 22)\n",
                                                source_dir / "main.dd");
    dudu::ProjectConfig config;
    config.project_dir = source_dir;
    config.build_dir = source_dir / "build";
    config.include_dirs = {"../native-macro-include"};
    dudu::merge_native_headers(module, {.config = config, .source_dir = source_dir});
    dudu::analyze_module(module, {.check_bodies = true});
    const std::string cpp = dudu::emit_cpp_source(module);
    assert(cpp.find("_dudu_macro_call(20, 22)") != std::string::npos);
}

void test_native_call_arity(const std::filesystem::path& root) {
    dudu::ModuleAst module = dudu::parse_source("from c.path import native_headers/simple_c.h\n"
                                                "\n"
                                                "def main() -> i32:\n"
                                                "    return dudu_native_add()\n",
                                                root / "tests/fixtures/native_bad_arity.dd");
    dudu::merge_native_headers(
        module, {.config = dudu::ProjectConfig{}, .source_dir = root / "tests/fixtures"});
    bool rejected = false;
    try {
        dudu::analyze_module(module, {.check_bodies = true});
    } catch (const dudu::CompileError&) {
        rejected = true;
    }
    assert(rejected);
}

void test_native_header_collision(const std::filesystem::path& root) {
    bool failed = false;
    try {
        dudu::ModuleAst module = dudu::parse_source("from c.path import native_headers/simple_c.h\n"
                                                    "DUDU_NATIVE_MAGIC: i32 = 1\n",
                                                    root / "tests/fixtures/native_collision.dd");
        dudu::merge_native_headers(
            module, {.config = dudu::ProjectConfig{}, .source_dir = root / "tests/fixtures"});
        dudu::analyze_module(module, {.check_bodies = true});
    } catch (const dudu::CompileError&) {
        failed = true;
    }
    assert(failed);
}

void test_native_header_cache_ignores_generated_scanner_source(const std::filesystem::path& root) {
    const std::filesystem::path source_dir = root / "build" / "native-cache-generated-source";
    const std::filesystem::path generated = source_dir / "dudu_native_headers_123.cpp";
    const std::filesystem::path header = source_dir / "real.hpp";
    std::filesystem::remove_all(source_dir);
    std::filesystem::create_directories(source_dir);
    {
        std::ofstream out(generated);
        out << "#include \"real.hpp\"\n";
    }
    {
        std::ofstream out(header);
        out << "#pragma once\ninline int real_answer(void) { return 42; }\n";
    }

    const std::string make_deps =
        "dudu_native_scan: " + generated.string() + " " + header.string() + "\n";
    const std::string stamps =
        dudu::native_header_dependency_stamps_from_makefile(make_deps, generated);
    assert(stamps.find(generated.string()) == std::string::npos);
    assert(stamps.find(header.string()) != std::string::npos);
    assert(dudu::native_header_dependency_stamps_current(stamps));
}

void test_native_header_cache_invalidates_local_header(const std::filesystem::path& root) {
    const std::filesystem::path source_dir = root / "build" / "native-cache-invalidation";
    const std::filesystem::path header = source_dir / "cache_probe.hpp";
    dudu::ProjectConfig config;
    config.build_dir = source_dir / "build";
    std::filesystem::remove_all(source_dir);
    std::filesystem::create_directories(source_dir);

    {
        std::ofstream out(header);
        out << "#pragma once\ninline bool cache_probe(bool value) { return value; }\n";
    }
    dudu::ModuleAst first = dudu::parse_source("from cpp.path import ./cache_probe.hpp\n"
                                               "\n"
                                               "def main() -> i32:\n"
                                               "    if cache_probe(True):\n"
                                               "        return 42\n"
                                               "    return 0\n",
                                               source_dir / "main.dd");
    dudu::merge_native_headers(first, {.config = config, .source_dir = source_dir});
    dudu::analyze_module(first, {.check_bodies = true});

    {
        std::ofstream out(header);
        out << "#pragma once\n"
               "inline int cache_probe(int value, int salt) { return value + salt; }\n";
    }
    bool failed = false;
    try {
        dudu::ModuleAst second = dudu::parse_source("from cpp.path import ./cache_probe.hpp\n"
                                                    "\n"
                                                    "def main() -> i32:\n"
                                                    "    if cache_probe(True):\n"
                                                    "        return 42\n"
                                                    "    return 0\n",
                                                    source_dir / "main.dd");
        dudu::merge_native_headers(second, {.config = config, .source_dir = source_dir});
        dudu::analyze_module(second, {.check_bodies = true});
    } catch (const dudu::CompileError& error) {
        failed = std::string(error.what()).find("cache_probe") != std::string::npos;
    }
    assert(failed);
}

void test_native_header_cache_invalidates_included_header(const std::filesystem::path& root) {
    const std::filesystem::path source_dir = root / "build" / "native-cache-transitive";
    const std::filesystem::path wrapper = source_dir / "wrapper.hpp";
    const std::filesystem::path detail = source_dir / "detail.hpp";
    dudu::ProjectConfig config;
    config.build_dir = source_dir / "build";
    std::filesystem::remove_all(source_dir);
    std::filesystem::create_directories(source_dir);

    {
        std::ofstream out(wrapper);
        out << "#pragma once\n#include \"detail.hpp\"\n";
    }
    {
        std::ofstream out(detail);
        out << "#pragma once\ninline int included_answer(void) { return 42; }\n";
    }
    dudu::ModuleAst first = dudu::parse_source("from cpp.path import ./wrapper.hpp as wrap\n"
                                               "\n"
                                               "def main() -> i32:\n"
                                               "    return wrap.included_answer()\n",
                                               source_dir / "main.dd");
    dudu::merge_native_headers(first, {.config = config, .source_dir = source_dir});
    dudu::analyze_module(first, {.check_bodies = true});

    {
        std::ofstream out(detail);
        out << "#pragma once\ninline int replacement_answer(void) { return 42; }\n";
    }
    bool failed = false;
    try {
        dudu::ModuleAst second = dudu::parse_source("from cpp.path import ./wrapper.hpp as wrap\n"
                                                    "\n"
                                                    "def main() -> i32:\n"
                                                    "    return wrap.included_answer()\n",
                                                    source_dir / "main.dd");
        dudu::merge_native_headers(second, {.config = config, .source_dir = source_dir});
        dudu::analyze_module(second, {.check_bodies = true});
    } catch (const dudu::CompileError& error) {
        failed = std::string(error.what()).find("included_answer") != std::string::npos;
    }
    assert(failed);
}

void test_native_header_pointer_diagnostics(const std::filesystem::path& root) {
    bool failed = false;
    try {
        dudu::ModuleAst module = dudu::parse_source("from c.path import native_headers/simple_c.h\n"
                                                    "\n"
                                                    "def main() -> i32:\n"
                                                    "    event: DuduNativeEvent\n"
                                                    "    if dudu_native_ready(event):\n"
                                                    "        return 1\n"
                                                    "    return 0\n",
                                                    root / "tests/fixtures/native_pointer.dd");
        dudu::merge_native_headers(
            module, {.config = dudu::ProjectConfig{}, .source_dir = root / "tests/fixtures"});
        dudu::analyze_module(module, {.check_bodies = true});
    } catch (const dudu::CompileError&) {
        failed = true;
    }
    assert(failed);
}

} // namespace

int main() {
    try {
        const std::filesystem::path root = DUDU_REPO_ROOT;
        test_native_cache_record_round_trip();
        test_native_cursor_metadata_round_trip();
        test_native_single_underscore_function_macros(root);
        test_native_call_arity(root);
        test_native_header_collision(root);
        test_native_header_cache_ignores_generated_scanner_source(root);
        test_native_header_cache_invalidates_local_header(root);
        test_native_header_cache_invalidates_included_header(root);
        test_native_header_pointer_diagnostics(root);
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
