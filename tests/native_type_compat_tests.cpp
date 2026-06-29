#include "dudu/codegen/cpp_emit.hpp"
#include "dudu/native/native_headers.hpp"
#include "dudu/parser/parser.hpp"
#include "dudu/sema/sema.hpp"

#include <cassert>
#include <exception>
#include <filesystem>
#include <iostream>

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

} // namespace

int main() {
    try {
        const std::filesystem::path root = DUDU_REPO_ROOT;
        test_native_string_literal_to_string_view(root);
        test_none_to_native_cstr(root);
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
