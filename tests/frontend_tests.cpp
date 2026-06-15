#include "dudu/cpp_emit.hpp"
#include "dudu/cpp_lower.hpp"
#include "dudu/cpp_stmt_types.hpp"
#include "dudu/format.hpp"
#include "dudu/lexer.hpp"
#include "dudu/native_headers.hpp"
#include "dudu/parser.hpp"
#include "dudu/project_config.hpp"
#include "dudu/sema.hpp"
#include "dudu/sema_alloc.hpp"
#include "dudu/sema_function_type.hpp"
#include "dudu/type_compat.hpp"

#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
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

void test_lexer_indentation() {
    const std::string source = "def main() -> i32:\n"
                               "    x: i32 = 1\n"
                               "    if x > 0:\n"
                               "        return x\n"
                               "    return 0\n";
    const std::vector<dudu::Token> tokens = dudu::lex_source(source, "inline.dd");

    int indents = 0;
    int dedents = 0;
    for (const dudu::Token& token : tokens) {
        indents += token.kind == dudu::TokenKind::Indent ? 1 : 0;
        dedents += token.kind == dudu::TokenKind::Dedent ? 1 : 0;
    }
    assert(indents == 2);
    assert(dedents == 2);
}

void test_import_bindings() {
    const dudu::ModuleAst module = dudu::parse_source("import renderer.camera\n"
                                                      "import renderer.light\n"
                                                      "import renderer.camera as camera\n"
                                                      "from ui.button import Button as UiButton\n",
                                                      "imports.dd");
    assert(module.imports.size() == 4);
    assert(dudu::bound_import_name(module.imports[0]) == "renderer");
    assert(dudu::bound_import_name(module.imports[2]) == "camera");
    assert(dudu::bound_import_name(module.imports[3]) == "UiButton");

    bool collided = false;
    try {
        (void)dudu::parse_source("from ui.button import Button\n"
                                 "from game.input import Button\n",
                                 "collision.dd");
    } catch (const dudu::CompileError&) {
        collided = true;
    }
    assert(collided);

    const dudu::ModuleAst direct_native =
        dudu::parse_source("import cpp \"imgui.h\"\n", "direct_native.dd");
    assert(direct_native.imports.size() == 1);
    assert(direct_native.imports[0].alias.empty());
}

void test_canonical_examples_parse(const std::filesystem::path& root) {
    const std::vector<std::string> examples = {
        "allocators.dd",           "audio_synth.dd",     "compile_time.dd",
        "cpp_library.dd",          "cuda_kernel.dd",     "cuda_shared_memory_tile.dd",
        "ffmpeg_probe_decode.dd",  "fibonacci.dd",       "function_pointers.dd",
        "glfw_opengl_triangle.dd", "image_filter.dd",    "layout_hardware.dd",
        "modules_visibility.dd",   "native_escape.dd",   "numerics_kmeans.dd",
        "opencl_kernel_host.dd",   "raylib_game.dd",     "sdl3_imgui.dd",
        "sdl3_window.dd",          "shader_compute.dd",  "systems_mmap.dd",
        "threading_atomics.dd",    "vulkan_triangle.dd", "web_server.dd",
    };

    for (const std::string& example : examples) {
        const std::filesystem::path path = root / "examples" / example;
        const dudu::ModuleAst module = dudu::parse_source(read_file(path), path);
        assert(!module.classes.empty() || !module.functions.empty() || !module.imports.empty() ||
               !module.constants.empty() || !module.static_asserts.empty() ||
               !module.enums.empty() || !module.aliases.empty());
    }
}

void test_header_emission() {
    const dudu::ModuleAst module = dudu::parse_source("import cpp \"raylib.h\" as rl\n"
                                                      "\n"
                                                      "private class Hidden:\n"
                                                      "    z: i32\n"
                                                      "\n"
                                                      "class Vec3:\n"
                                                      "    x: f32\n"
                                                      "    y: f32\n"
                                                      "\n"
                                                      "private def helper(a: Vec3) -> f32:\n"
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
    assert(header.find("Hidden") == std::string::npos);
    assert(header.find("helper") == std::string::npos);

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

void test_allocation_type_ref_diagnostics() {
    dudu::Symbols symbols;
    const dudu::SourceLocation location{.file = "cpp_escape_alloc.dd", .line = 7, .column = 12};

    bool rejected = false;
    try {
        (void)dudu::infer_cpp_escape_allocation_call(symbols, &location, "new[list[MissingType]]",
                                                     std::vector<dudu::Expr>{});
    } catch (const dudu::CompileError& error) {
        assert(error.location().line == 7);
        assert(error.location().column > location.column);
        assert(std::string(error.what()).find("unknown allocation type: MissingType") !=
               std::string::npos);
        rejected = true;
    }
    assert(rejected);
}

void test_emitted_local_index_type_inference() {
    const std::map<std::string, std::string> locals = {
        {"flag", "bool"},
        {"count", "i32"},
        {"scale", "f32"},
        {"values", "list[i32]"},
        {"names", "dict[str, Player]"},
        {"matrix", "array[f32][3, 4]"},
    };
    const std::map<std::string, std::string> functions;

    assert(dudu::infer_emitted_local_type(dudu::parse_expr_text("values[0]"), locals, functions) ==
           "i32");
    assert(dudu::infer_emitted_local_type(dudu::parse_expr_text("names[key]"), locals, functions) ==
           "Player");
    assert(dudu::infer_emitted_local_type(dudu::parse_expr_text("matrix[1]"), locals, functions) ==
           "array[f32][4]");
    assert(dudu::infer_emitted_local_type(dudu::parse_expr_text("matrix[1, 2]"), locals,
                                          functions) == "f32");
}

void test_emitted_local_expression_type_inference() {
    const std::map<std::string, std::string> locals = {
        {"flag", "bool"},
        {"count", "i32"},
        {"player", "*const[Player]"},
        {"queue", "storage[Queue[i32]]"},
        {"scale", "f32"},
    };
    const std::map<std::string, std::string> functions = {
        {"make_matrix", "array[i32][2, 2]"},
        {"make_values", "list[i32]"},
        {"make_count", "i32"},
        {"Player.hp", "i32"},
        {"Queue.pop", "i32"},
    };

    assert(dudu::infer_emitted_local_type(dudu::parse_expr_text("True"), locals, functions) ==
           "bool");
    assert(dudu::infer_emitted_local_type(dudu::parse_expr_text("1_024"), locals, functions) ==
           "i32");
    assert(dudu::infer_emitted_local_type(dudu::parse_expr_text("1.5"), locals, functions) ==
           "f64");
    assert(dudu::infer_emitted_local_type(dudu::parse_expr_text("\"hi\""), locals, functions) ==
           "str");
    assert(dudu::infer_emitted_local_type(dudu::parse_expr_text("&count"), locals, functions) ==
           "*i32");
    assert(dudu::infer_emitted_local_type(dudu::parse_expr_text("*&count"), locals, functions) ==
           "i32");
    assert(dudu::infer_emitted_local_type(dudu::parse_expr_text("not flag"), locals, functions) ==
           "bool");
    assert(dudu::infer_emitted_local_type(dudu::parse_expr_text("count + 2"), locals, functions) ==
           "i32");
    assert(dudu::infer_emitted_local_type(dudu::parse_expr_text("scale + 2"), locals, functions) ==
           "f32");
    assert(dudu::infer_emitted_local_type(dudu::parse_expr_text("count < 4"), locals, functions) ==
           "bool");
    assert(dudu::infer_emitted_local_type(dudu::parse_expr_text("make_count()"), locals,
                                          functions) == "i32");
    assert(dudu::infer_emitted_local_type(dudu::parse_expr_text("player.hp()"), locals,
                                          functions) == "i32");
    assert(dudu::infer_emitted_local_type(dudu::parse_expr_text("queue.pop()"), locals,
                                          functions) == "i32");
    assert(dudu::infer_emitted_local_type(dudu::parse_expr_text("make_values()[0]"), locals,
                                          functions) == "i32");
    assert(dudu::infer_emitted_local_type(dudu::parse_expr_text("make_matrix()[1][0]"), locals,
                                          functions) == "i32");
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
}

void test_list_iterator_methods() {
    const dudu::ModuleAst module = dudu::parse_source("def main() -> i32:\n"
                                                      "    values: list[i32] = [1, 2]\n"
                                                      "    values.begin()\n"
                                                      "    values.end()\n"
                                                      "    return 0\n",
                                                      "list_iter.dd");
    dudu::analyze_module(module, {.check_bodies = true});
}

void test_reference_list_indexing() {
    const dudu::ModuleAst module = dudu::parse_source("def write(values: &list[i32]):\n"
                                                      "    values[0] = 5\n"
                                                      "\n"
                                                      "def main() -> i32:\n"
                                                      "    values: list[i32] = [1]\n"
                                                      "    write(values)\n"
                                                      "    return values[0]\n",
                                                      "reference_list_indexing.dd");
    dudu::analyze_module(module, {.check_bodies = true});
}

void test_bare_void_return() {
    const dudu::ModuleAst module = dudu::parse_source("def done():\n"
                                                      "    return\n"
                                                      "\n"
                                                      "def main() -> i32:\n"
                                                      "    done()\n"
                                                      "    return 0\n",
                                                      "bare_void_return.dd");
    dudu::analyze_module(module, {.check_bodies = true});
}

void test_typed_for_emission() {
    const dudu::ModuleAst module = dudu::parse_source("class Item:\n"
                                                      "    value: i32\n"
                                                      "\n"
                                                      "def sum_items(items: list[Item]) -> i32:\n"
                                                      "    total: i32 = 0\n"
                                                      "    for item: &Item in items:\n"
                                                      "        total += item.value\n"
                                                      "    for copy in items:\n"
                                                      "        total += copy.value\n"
                                                      "    return total\n",
                                                      "typed_for.dd");
    const std::string cpp = dudu::emit_cpp_source(module);
    assert(cpp.find("for (Item& item : items)") != std::string::npos);
    assert(cpp.find("for (auto&& copy : items)") != std::string::npos);
}

void test_class_field_defaults_and_static_fields() {
    const dudu::ModuleAst module = dudu::parse_source("class Counter:\n"
                                                      "    value: i32 = 7\n"
                                                      "    count: static[i32] = 0\n"
                                                      "\n"
                                                      "    def bump() -> i32:\n"
                                                      "        Counter.count += 1\n"
                                                      "        return Counter.count\n",
                                                      "class_defaults.dd");
    assert(module.classes.size() == 1);
    const dudu::ClassDecl& counter = module.classes.front();
    assert(counter.fields.size() == 1);
    assert(counter.fields[0].name == "value");
    assert(counter.fields[0].value == "7");
    assert(counter.static_fields.size() == 1);
    assert(counter.static_fields[0].name == "count");
    assert(counter.static_fields[0].type == "i32");
    assert(counter.methods.size() == 1);

    const std::string cpp = dudu::emit_cpp_source(module);
    assert(cpp.find("int32_t value = 7;") != std::string::npos);
    assert(cpp.find("inline static int32_t count = 0;") != std::string::npos);
    assert(cpp.find("static int32_t bump()") != std::string::npos);
}

void test_project_driver_config(const std::filesystem::path& root) {
    const std::filesystem::path dir = root / "build" / "project-driver-config-test";
    std::filesystem::create_directories(dir);
    const std::filesystem::path config_path = dir / "dudu.toml";
    {
        std::ofstream out(config_path);
        out << "name = \"tool\"\n"
               "entry = \"src/main.dd\"\n"
               "\n"
               "[cxx]\n"
               "standard = \"c++23\"\n"
               "compiler = \"clang++\"\n"
               "\n"
               "[include]\n"
               "paths = [\n"
               "    \"src\",\n"
               "    \"include\",\n"
               "]\n"
               "\n"
               "[sources]\n"
               "cpp = [\"src/native.cpp\"]\n"
               "c = [\"src/native.c\"]\n"
               "\n"
               "[pkg]\n"
               "libs = [\"raylib\"]\n"
               "\n"
               "[link]\n"
               "paths = [\"lib\"]\n"
               "libs = [\"m\"]\n"
               "flags = [\"-pthread\"]\n"
               "\n"
               "[build]\n"
               "dir = \"out\"\n"
               "\n"
               "[targets.tool]\n"
               "entry = \"tools/tool.dd\"\n"
               "kind = \"executable\"\n"
               "\n"
               "[targets.tool.pkg]\n"
               "libs = [\"sqlite3\"]\n"
               "\n"
               "[targets.tests]\n"
               "entry = \"tests/main.dd\"\n"
               "kind = \"executable\"\n"
               "mode = \"hosted\"\n";
    }
    const dudu::ProjectConfig config = dudu::parse_project_config(config_path);
    assert(config.name == "tool");
    assert(config.main == "src/main.dd");
    assert(config.cpp_std == "c++23");
    assert(config.compiler == "clang++");
    assert(config.include_dirs.size() == 2);
    assert(config.cpp_sources.size() == 1);
    assert(config.c_sources.size() == 1);
    assert(config.pkg_config_packages.size() == 1);
    assert(config.lib_dirs.size() == 1);
    assert(config.libs.size() == 1);
    assert(config.link_flags.size() == 1);
    assert(config.build_dir == "out");
    assert(config.targets.size() == 2);
    assert(config.targets.at("tool").main == "tools/tool.dd");
    assert(config.targets.at("tool").pkg_config_packages.size() == 1);
    assert(config.targets.at("tests").target_mode == "hosted");
    const dudu::ProjectConfig tests_config = dudu::apply_project_target(config, "tests");
    assert(tests_config.name == "tests");
    assert(tests_config.main == "tests/main.dd");
    assert(tests_config.target_kind == "executable");
    assert(tests_config.target_mode == "hosted");
}

} // namespace

int main() {
    try {
        const std::filesystem::path root = DUDU_REPO_ROOT;
        test_lexer_indentation();
        test_import_bindings();
        test_canonical_examples_parse(root);
        test_header_emission();
        test_semantic_diagnostics();
        test_allocation_type_ref_diagnostics();
        test_emitted_local_index_type_inference();
        test_emitted_local_expression_type_inference();
        test_formatter();
        test_list_iterator_methods();
        test_reference_list_indexing();
        test_bare_void_return();
        test_typed_for_emission();
        test_class_field_defaults_and_static_fields();
        test_project_driver_config(root);
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
