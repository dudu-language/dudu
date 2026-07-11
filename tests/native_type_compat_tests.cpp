#include "dudu/codegen/cpp_emit.hpp"
#include "dudu/native/native_headers.hpp"
#include "dudu/parser/parser.hpp"
#include "dudu/sema/sema.hpp"
#include "dudu/sema/sema_context.hpp"
#include "dudu/sema/type_compat.hpp"

#include <cassert>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace {

void test_native_string_literal_to_string_view(const std::filesystem::path& root) {
    dudu::ModuleAst module =
        dudu::parse_source("import cpp \"native_headers/string_view_accept.hpp\" as native\n"
                           "\n"
                           "def main() -> i32:\n"
                           "    return native.dudu_native_string.size_of(\"dudu\")\n",
                           root / "tests/fixtures/native_string_view.dd");
    dudu::ProjectConfig config;
    config.build_dir = root / "build" / "native-string-view-cache";
    std::filesystem::remove_all(config.build_dir);
    dudu::merge_native_header_types(module,
                                    {.config = config, .source_dir = root / "tests/fixtures"});
    dudu::analyze_module(module, {.check_bodies = true});
    const std::string cpp = dudu::emit_cpp_source(module);
    assert(cpp.find("dudu_native_string::size_of(\"dudu\")") != std::string::npos);
}

void test_none_to_native_cstr(const std::filesystem::path& root) {
    dudu::ModuleAst module =
        dudu::parse_source("import cpp \"native_headers/cstr_accept.hpp\" as native\n"
                           "\n"
                           "def main() -> i32:\n"
                           "    return native.dudu_native_cstr.accepts_cstr(None)\n",
                           root / "tests/fixtures/native_cstr_none.dd");
    dudu::ProjectConfig config;
    config.build_dir = root / "build" / "native-cstr-none-cache";
    std::filesystem::remove_all(config.build_dir);
    dudu::merge_native_header_types(module,
                                    {.config = config, .source_dir = root / "tests/fixtures"});
    dudu::analyze_module(module, {.check_bodies = true});
    const std::string cpp = dudu::emit_cpp_source(module);
    assert(cpp.find("dudu_native_cstr::accepts_cstr(nullptr)") != std::string::npos);
}

void test_native_identity_controls_type_compatibility(const std::filesystem::path& root) {
    dudu::Symbols identity_symbols;
    identity_symbols.native_type_identity_keys["left.Widget"] = "usr:c:@S@Widget";
    identity_symbols.native_type_identity_keys["right.Widget"] = "usr:c:@S@Widget";
    assert(dudu::type_assignment_allowed(identity_symbols,
                                         dudu::parse_type_text("left.Widget"),
                                         dudu::parse_type_text("right.Widget")));
    identity_symbols.native_type_identity_keys["right.Widget"] = "usr:c:@N@right@S@Widget";
    assert(!dudu::type_assignment_allowed(identity_symbols,
                                          dudu::parse_type_text("left.Widget"),
                                          dudu::parse_type_text("right.Widget")));
    identity_symbols.native_type_identity_keys["left.iterator"] = "usr:left-iterator";
    identity_symbols.native_type_identity_keys["right.iterator"] = "usr:right-iterator";
    assert(!dudu::type_assignment_allowed(identity_symbols,
                                          dudu::parse_type_text("left.iterator"),
                                          dudu::parse_type_text("right.iterator")));

    const std::filesystem::path source_dir = root / "build" / "native-identity-type-compat";
    std::filesystem::remove_all(source_dir);
    std::filesystem::create_directories(source_dir);
    {
        std::ofstream out(source_dir / "identity_types.hpp");
        out << "#pragma once\n"
               "struct SharedWidget { int value; };\n"
               "namespace first { struct Widget { int value; }; }\n"
               "namespace second { struct Widget { int value; }; }\n";
    }

    dudu::ProjectConfig config;
    config.project_dir = source_dir;
    config.build_dir = source_dir / "cache";
    dudu::ModuleAst same = dudu::parse_source(
        "from cpp.path import ./identity_types.hpp as left\n"
        "from cpp.path import ./identity_types.hpp as right\n"
        "\n"
        "def preserve(value: left.SharedWidget) -> right.SharedWidget:\n"
        "    return value\n",
        source_dir / "same.dd");
    dudu::merge_native_header_types(same, {.config = config, .source_dir = source_dir});
    dudu::analyze_module(same, {.check_bodies = true});

    dudu::ModuleAst distinct = dudu::parse_source(
        "from cpp.path import ./identity_types.hpp\n"
        "\n"
        "def reject(value: first.Widget) -> second.Widget:\n"
        "    return value\n",
        source_dir / "distinct.dd");
    dudu::merge_native_header_types(distinct, {.config = config, .source_dir = source_dir});
    bool rejected = false;
    try {
        dudu::analyze_module(distinct, {.check_bodies = true});
    } catch (const dudu::CompileError& error) {
        const std::string message = error.what();
        rejected = message.find("first.Widget") != std::string::npos &&
                   message.find("second.Widget") != std::string::npos;
    }
    assert(rejected);
}

} // namespace

int main() {
    try {
        const std::filesystem::path root = DUDU_REPO_ROOT;
        test_native_string_literal_to_string_view(root);
        test_none_to_native_cstr(root);
        test_native_identity_controls_type_compatibility(root);
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
