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
#include <utility>

namespace {

void test_native_string_literal_to_string_view(const std::filesystem::path& root) {
    dudu::ModuleAst module =
        dudu::parse_source("from cpp.path import native_headers/string_view_accept.hpp as native\n"
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
        dudu::parse_source("from cpp.path import native_headers/cstr_accept.hpp as native\n"
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
    identity_symbols.native_type_identity_by_binding["left.Widget"] = "usr:c:@S@Widget";
    identity_symbols.native_type_identity_by_binding["right.Widget"] = "usr:c:@S@Widget";
    assert(dudu::type_assignment_allowed(identity_symbols, dudu::parse_type_text("left.Widget"),
                                         dudu::parse_type_text("right.Widget")));
    identity_symbols.native_type_identity_by_binding["right.Widget"] = "usr:c:@N@right@S@Widget";
    assert(!dudu::type_assignment_allowed(identity_symbols, dudu::parse_type_text("left.Widget"),
                                          dudu::parse_type_text("right.Widget")));
    identity_symbols.native_type_identity_by_binding["left.iterator"] = "usr:left-iterator";
    identity_symbols.native_type_identity_by_binding["right.iterator"] = "usr:right-iterator";
    assert(!dudu::type_assignment_allowed(identity_symbols, dudu::parse_type_text("left.iterator"),
                                          dudu::parse_type_text("right.iterator")));

    const std::filesystem::path source_dir = root / "build" / "native-identity-type-compat";
    std::filesystem::remove_all(source_dir);
    std::filesystem::create_directories(source_dir);
    {
        std::ofstream out(source_dir / "identity_types.hpp");
        out << "#pragma once\n"
               "struct SharedWidget { int value; };\n"
               "inline int shared_value(SharedWidget value) { return value.value; }\n"
               "namespace first { struct Widget { int value; }; }\n"
               "namespace second { struct Widget { int value; }; }\n";
    }

    dudu::ProjectConfig config;
    config.project_dir = source_dir;
    config.build_dir = source_dir / "cache";
    dudu::ModuleAst same =
        dudu::parse_source("from cpp.path import ./identity_types.hpp as left\n"
                           "from cpp.path import ./identity_types.hpp as right\n"
                           "\n"
                           "def preserve(value: left.SharedWidget) -> right.SharedWidget:\n"
                           "    return value\n",
                           source_dir / "same.dd");
    dudu::merge_native_header_types(same, {.config = config, .source_dir = source_dir});
    dudu::analyze_module(same, {.check_bodies = true});
    const dudu::Symbols same_symbols = dudu::collect_symbols(same);
    const std::string& left_identity =
        same_symbols.native_type_identity_by_binding.at("left.SharedWidget");
    const std::string& right_identity =
        same_symbols.native_type_identity_by_binding.at("right.SharedWidget");
    assert(left_identity == right_identity);
    const auto& class_bindings = same_symbols.native_class_decls_by_identity.at(left_identity);
    assert(class_bindings.contains("left.SharedWidget"));
    assert(class_bindings.contains("right.SharedWidget"));
    const std::vector<const dudu::NativeFunctionDecl*> left_functions =
        dudu::native_function_decls_for_binding(same_symbols, "left.shared_value");
    const std::vector<const dudu::NativeFunctionDecl*> right_functions =
        dudu::native_function_decls_for_binding(same_symbols, "right.shared_value");
    assert(left_functions.size() == 1);
    assert(right_functions.size() == 1);
    const std::string& left_function_identity =
        same_symbols.native_function_identities_by_binding.at("left.shared_value").front();
    const std::string& right_function_identity =
        same_symbols.native_function_identities_by_binding.at("right.shared_value").front();
    assert(left_function_identity == right_function_identity);
    const auto& function_bindings =
        same_symbols.native_function_decls_by_identity.at(left_function_identity);
    assert(function_bindings.contains("left.shared_value"));
    assert(function_bindings.contains("right.shared_value"));

    dudu::ModuleAst distinct =
        dudu::parse_source("from cpp.path import ./identity_types.hpp\n"
                           "\n"
                           "def reject(value: first.Widget) -> second.Widget:\n"
                           "    return value\n",
                           source_dir / "distinct.dd");
    dudu::merge_native_header_types(distinct, {.config = config, .source_dir = source_dir});
    const dudu::Symbols distinct_symbols = dudu::collect_symbols(distinct);
    assert(distinct_symbols.native_type_identity_by_binding.at("first.Widget") !=
           distinct_symbols.native_type_identity_by_binding.at("second.Widget"));
    bool rejected = false;
    try {
        dudu::analyze_module(distinct, {.check_bodies = true});
    } catch (const dudu::CompileError& error) {
        const std::string message = error.what();
        rejected = message.find("first.Widget") != std::string::npos &&
                   message.find("second.Widget") != std::string::npos;
    }
    assert(rejected);

    dudu::ModuleAst missing_identity;
    dudu::NativeFunctionDecl missing_function;
    missing_function.name = "missing_identity";
    missing_identity.native_functions.push_back(std::move(missing_function));
    bool missing_rejected = false;
    try {
        (void)dudu::collect_symbols(missing_identity);
    } catch (const dudu::CompileError& error) {
        missing_rejected =
            std::string(error.what()).find("native function is missing canonical identity") !=
            std::string::npos;
    }
    assert(missing_rejected);
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
