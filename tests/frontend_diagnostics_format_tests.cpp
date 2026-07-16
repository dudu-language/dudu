#include "dudu/codegen/cpp_emit.hpp"
#include "dudu/codegen/cpp_emit_modules.hpp"
#include "dudu/codegen/cpp_lower.hpp"
#include "dudu/codegen/cpp_stmt_types.hpp"
#include "dudu/core/ast_expr.hpp"
#include "dudu/core/ast_type.hpp"
#include "dudu/format/format.hpp"
#include "dudu/format/format_path.hpp"
#include "dudu/lsp/language_server_completion.hpp"
#include "dudu/lsp/language_server_definition.hpp"
#include "dudu/lsp/language_server_diagnostics.hpp"
#include "dudu/lsp/language_server_hover.hpp"
#include "dudu/lsp/language_server_json.hpp"
#include "dudu/lsp/language_server_navigation.hpp"
#include "dudu/lsp/language_server_reference_collect.hpp"
#include "dudu/lsp/language_server_references.hpp"
#include "dudu/lsp/language_server_support.hpp"
#include "dudu/native/native_headers.hpp"
#include "dudu/parser/lexer.hpp"
#include "dudu/parser/parser.hpp"
#include "dudu/project/module_loader.hpp"
#include "dudu/project/project_config.hpp"
#include "dudu/project/project_index.hpp"
#include "dudu/sema/sema.hpp"
#include "dudu/sema/sema_alloc.hpp"
#include "dudu/sema/sema_expr_cpp_escape_calls.hpp"
#include "dudu/sema/sema_expr_internal.hpp"
#include "dudu/sema/sema_function_type.hpp"
#include "dudu/sema/sema_index.hpp"
#include "dudu/sema/sema_ops.hpp"
#include "dudu/sema/type_compat.hpp"

#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <string>
#include <vector>

namespace {

std::string read_file(const std::filesystem::path& path) {
    std::ifstream file(path);
    if (!file) {
        throw std::runtime_error("could not open " + path.string());
    }
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

void write_file(const std::filesystem::path& path, const std::string& text) {
    std::ofstream file(path);
    if (!file) {
        throw std::runtime_error("could not write " + path.string());
    }
    file << text;
}

void test_header_emission() {
    const dudu::ModuleAst module = dudu::parse_source("from cpp.path import raylib.h as rl\n"
                                                      "\n"
                                                      "class Vec3:\n"
                                                      "    x: f32\n"
                                                      "    y: f32\n"
                                                      "\n"
                                                      "def _helper(a: Vec3) -> f32:\n"
                                                      "    return a.x\n"
                                                      "\n"
                                                      "def dot(a: Vec3, b: Vec3) -> f32:\n"
                                                      "    return a.x * b.x + a.y * b.y\n",
                                                      "header.dd");
    const std::string header = dudu::emit_cpp_header(module);
    assert(header.find("#include \"raylib.h\"") != std::string::npos);
    assert(header.find("struct Vec3") != std::string::npos);
    assert(header.find("struct Vec3") < header.find("float dot"));
    assert(header.find("float x{};") != std::string::npos);
    assert(header.find("float dot(Vec3 a, Vec3 b);") != std::string::npos);
    assert(header.find("_helper") == std::string::npos);

    const dudu::ModuleAst alias_module =
        dudu::parse_source("type PlayerId = u64\n"
                           "\n"
                           "def next_id(id: PlayerId) -> PlayerId:\n"
                           "    return id + 1\n",
                           "alias_header.dd");
    const std::string alias_header = dudu::emit_cpp_header(alias_module);
    assert(alias_header.find("using PlayerId = uint64_t;") != std::string::npos);
    assert(alias_header.find("PlayerId next_id(PlayerId id);") != std::string::npos);
}

void test_semantic_diagnostics() {
    bool duplicate = false;
    try {
        const dudu::ModuleAst module = dudu::parse_source("class Vec:\n"
                                                          "    x: i32\n"
                                                          "\n"
                                                          "class Vec:\n"
                                                          "    y: i32\n",
                                                          "duplicate.dd");
        dudu::analyze_module(module);
    } catch (const dudu::CompileError&) {
        duplicate = true;
    }
    assert(duplicate);

    bool bad_return = false;
    try {
        const dudu::ModuleAst module = dudu::parse_source("def bad() -> i32:\n"
                                                          "    return True\n",
                                                          "bad_return.dd");
        dudu::analyze_module(module, {.check_bodies = true});
    } catch (const dudu::CompileError& error) {
        assert(error.location().line == 2);
        assert(error.location().column > 5);
        bad_return = true;
    }
    assert(bad_return);

    bool bad_local_value = false;
    try {
        const dudu::ModuleAst module = dudu::parse_source("def bad_value():\n"
                                                          "    value: i32 = \"nope\"\n",
                                                          "bad_value.dd");
        dudu::analyze_module(module, {.check_bodies = true});
    } catch (const dudu::CompileError& error) {
        assert(error.location().line == 2);
        assert(error.location().column > 5);
        bad_local_value = true;
    }
    assert(bad_local_value);

    bool bad_local_type = false;
    try {
        const dudu::ModuleAst module = dudu::parse_source("def bad_type_name():\n"
                                                          "    value: MissingType = 1\n",
                                                          "bad_type_name.dd");
        dudu::analyze_module(module, {.check_bodies = true});
    } catch (const dudu::CompileError& error) {
        assert(error.location().line == 2);
        assert(error.location().column > 5);
        bad_local_type = true;
    }
    assert(bad_local_type);

    bool bad_generic_value = false;
    try {
        const dudu::ModuleAst module = dudu::parse_source("def bad_generic_value[T]() -> i32:\n"
                                                          "    return T\n",
                                                          "bad_generic_value.dd");
        dudu::analyze_module(module, {.check_bodies = true});
    } catch (const dudu::CompileError& error) {
        assert(error.location().line == 2);
        assert(error.location().column > 5);
        assert(std::string(error.what()).find("type parameter used as a value: T") !=
               std::string::npos);
        bad_generic_value = true;
    }
    assert(bad_generic_value);

    const dudu::ModuleAst generic_named_field =
        dudu::parse_source("class Weird[t]:\n"
                           "    t: i32\n"
                           "    value: t\n"
                           "\n"
                           "def make_weird[t](value: t) -> Weird[t]:\n"
                           "    return Weird[t](t=3, value=value)\n"
                           "\n"
                           "def use_weird() -> i32:\n"
                           "    weird = make_weird[i32](7)\n"
                           "    return weird.t\n",
                           "generic_named_field.dd");
    dudu::analyze_module(generic_named_field, {.check_bodies = true});

    bool bad_value_as_type = false;
    try {
        const dudu::ModuleAst module = dudu::parse_source("def bad_value_as_type(value: i32):\n"
                                                          "    other: value = 1\n",
                                                          "bad_value_as_type.dd");
        dudu::analyze_module(module, {.check_bodies = true});
    } catch (const dudu::CompileError& error) {
        assert(error.location().line == 2);
        assert(error.location().column > 5);
        assert(std::string(error.what()).find("value used as a type: value") != std::string::npos);
        bad_value_as_type = true;
    }
    assert(bad_value_as_type);

    bool bad_prior_local_as_type = false;
    try {
        const dudu::ModuleAst module = dudu::parse_source("def bad_prior_local_as_type():\n"
                                                          "    value: i32 = 1\n"
                                                          "    other: value = 2\n",
                                                          "bad_prior_local_as_type.dd");
        dudu::analyze_module(module, {.check_bodies = true});
    } catch (const dudu::CompileError& error) {
        assert(error.location().line == 3);
        assert(error.location().column > 5);
        assert(std::string(error.what()).find("value used as a type: value") != std::string::npos);
        bad_prior_local_as_type = true;
    }
    assert(bad_prior_local_as_type);

    for (const std::string type : {"int", "float", "double"}) {
        bool rejected = false;
        try {
            const dudu::ModuleAst module = dudu::parse_source("def bad_type():\n"
                                                              "    value: " +
                                                                  type + " = 0\n",
                                                              "bad_type.dd");
            dudu::analyze_module(module, {.check_bodies = true});
        } catch (const dudu::CompileError&) {
            rejected = true;
        }
        assert(rejected);
    }
}

void test_formatter() {
    const std::string formatted = dudu::format_source("def main() -> i32:   \n"
                                                      "    return 0\t\n"
                                                      "\n"
                                                      "\n"
                                                      "\n"
                                                      "def other():\n");
    assert(formatted == "def main() -> i32:\n"
                        "    return 0\n"
                        "\n"
                        "\n"
                        "def other():\n");

    const std::string sorted = dudu::format_source("import zeta\n"
                                                   "from beta import Thing\n"
                                                   "import alpha\n"
                                                   "\n"
                                                   "def main() -> i32:\n"
                                                   "    return 0\n");
    assert(sorted == "from beta import Thing\n"
                     "import alpha\n"
                     "import zeta\n"
                     "\n"
                     "def main() -> i32:\n"
                     "    return 0\n");

    const std::string tabs = dudu::format_source("def main() -> i32:\n"
                                                 "\tvalue: str = \"\\tkept\"\n"
                                                 "\treturn 0\n");
    assert(tabs == "def main() -> i32:\n"
                   "    value: str = \"\\tkept\"\n"
                   "    return 0\n");

    const std::filesystem::path dir =
        std::filesystem::temp_directory_path() / "dudu_format_path_test";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir / "src");
    std::filesystem::create_directories(dir / "build");
    write_file(dir / "src" / "main.dd", "def main() -> i32:   \n\treturn 0\n");
    write_file(dir / "build" / "generated.dd", "def generated() -> i32:   \n\treturn 1\n");

    dudu::format_path_in_place(dir / "src" / "main.dd");
    assert(read_file(dir / "src" / "main.dd") == "def main() -> i32:\n"
                                                 "    return 0\n");

    dudu::format_path_in_place(dir, {.excluded_dirs = {dir / "build"}});
    assert(read_file(dir / "build" / "generated.dd") == "def generated() -> i32:   \n"
                                                        "\treturn 1\n");
}

} // namespace

int main() {
    try {
        test_header_emission();
        test_semantic_diagnostics();
        test_formatter();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
