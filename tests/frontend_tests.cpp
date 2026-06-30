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
    assert(module.imports[0].range.start.line == 1);
    assert(module.imports[0].range.end.line == 1);

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

void write_file(const std::filesystem::path& path, const std::string& text) {
    std::ofstream file(path);
    if (!file) {
        throw std::runtime_error("could not write " + path.string());
    }
    file << text;
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

void test_rejected_oop_surface_fixtures(const std::filesystem::path& root) {
    struct Case {
        std::string path;
        std::string expected;
    };
    const std::vector<Case> cases = {
        {"bad_dunder_init.dd", "reserved Python-style dunder"},
        {"bad_dunder_del.dd", "reserved Python-style dunder"},
        {"bad_dunder_operator.dd", "reserved Python-style dunder"},
        {"bad_dunder_free.dd", "reserved Python-style dunder"},
        {"bad_staticmethod_self.dd", "@staticmethod is not supported"},
        {"bad_staticmethod_free.dd", "@staticmethod is not supported"},
        {"bad_classmethod.dd", "@classmethod is not supported"},
        {"bad_property.dd", "@property is not supported"},
        {"bad_class_member_visibility.dd", "explicit visibility keywords are not supported"},
    };

    for (const Case& item : cases) {
        bool rejected = false;
        try {
            const std::filesystem::path path = root / "tests" / "fixtures" / item.path;
            const dudu::ModuleAst module = dudu::parse_source(read_file(path), path);
            dudu::analyze_module(module, {.check_bodies = true});
        } catch (const dudu::CompileError& error) {
            rejected = std::string(error.what()).find(item.expected) != std::string::npos;
        }
        assert(rejected);
    }
}

void test_header_emission() {
    const dudu::ModuleAst module = dudu::parse_source("import cpp \"raylib.h\" as rl\n"
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

void test_negative_numeric_literals_contextualize_as_f32_args() {
    const dudu::ModuleAst module = dudu::parse_source("def take_f32(x: f32, y: f32):\n"
                                                      "    pass\n"
                                                      "\n"
                                                      "class Player:\n"
                                                      "    x: f32\n"
                                                      "    y: f32\n"
                                                      "\n"
                                                      "    def move(self, dx: f32, dy: f32):\n"
                                                      "        self.x += dx\n"
                                                      "        self.y += dy\n"
                                                      "\n"
                                                      "def main() -> i32:\n"
                                                      "    take_f32(-2.0, -1.0)\n"
                                                      "    player = Player(x=1.0, y=2.0)\n"
                                                      "    player.move(2.0, -1.0)\n"
                                                      "    return 0\n",
                                                      "negative_numeric_literal_args.dd");
    dudu::analyze_module(module, {.check_bodies = true});
}

void test_receiver_reference_semantics() {
    const dudu::ModuleAst ok = dudu::parse_source("class Vec3:\n"
                                                  "    x: f32\n"
                                                  "\n"
                                                  "    def length(self: &const[Self]) -> f32:\n"
                                                  "        return self.x\n"
                                                  "\n"
                                                  "    def normalize(self) -> &Self:\n"
                                                  "        return self\n",
                                                  "receiver_reference_semantics.dd");
    dudu::analyze_module(ok, {.check_bodies = true});

    bool rejected = false;
    try {
        const dudu::ModuleAst bad = dudu::parse_source("class Vec3:\n"
                                                       "    x: f32\n"
                                                       "\n"
                                                       "    def bad(self: Vec3) -> f32:\n"
                                                       "        return self.x\n",
                                                       "bad_receiver_value.dd");
        dudu::analyze_module(bad, {.check_bodies = true});
    } catch (const dudu::CompileError& error) {
        rejected = std::string(error.what()).find("self must be a receiver reference") !=
                   std::string::npos;
    }
    assert(rejected);
}

void test_pointer_dereference_uses_type_ast() {
    const dudu::ModuleAst module = dudu::parse_source("def read(ptr: *const[i32]) -> const[i32]:\n"
                                                      "    return *ptr\n"
                                                      "\n"
                                                      "def write(ptr: *i32):\n"
                                                      "    *ptr = 5\n",
                                                      "pointer_dereference_type_ast.dd");
    dudu::analyze_module(module, {.check_bodies = true});
}

void test_extern_c_signature_uses_type_ast() {
    const dudu::ModuleAst ok =
        dudu::parse_source("@extern_c\n"
                           "def take_struct(value: *struct NativeThing) -> void:\n"
                           "    return\n",
                           "extern_c_struct_pointer.dd");
    dudu::analyze_module(ok, {.check_bodies = true});

    bool rejected = false;
    try {
        const dudu::ModuleAst bad = dudu::parse_source("@extern_c\n"
                                                       "def bad_ref(value: &i32) -> void:\n"
                                                       "    return\n",
                                                       "extern_c_ref.dd");
        dudu::analyze_module(bad, {.check_bodies = true});
    } catch (const dudu::CompileError& error) {
        rejected =
            std::string(error.what()).find("@extern_c parameter type is not C ABI safe: &") !=
            std::string::npos;
    }
    assert(rejected);

    rejected = false;
    try {
        const dudu::ModuleAst bad = dudu::parse_source("class Player:\n"
                                                       "    hp: i32\n"
                                                       "\n"
                                                       "@extern_c\n"
                                                       "def bad_class(value: Player) -> void:\n"
                                                       "    return\n",
                                                       "extern_c_class.dd");
        dudu::analyze_module(bad, {.check_bodies = true});
    } catch (const dudu::CompileError& error) {
        rejected =
            std::string(error.what()).find("@extern_c parameter type is not C ABI safe: Player") !=
            std::string::npos;
    }
    assert(rejected);
}

void test_pointer_arithmetic_uses_type_ast() {
    const dudu::ModuleAst module = dudu::parse_source("def next_ptr(ptr: *i32) -> *i32:\n"
                                                      "    return ptr + 1\n",
                                                      "pointer_arithmetic_type_ast.dd");
    dudu::analyze_module(module, {.check_bodies = true});
}

void test_base_pointer_assignment_uses_type_ast() {
    const dudu::ModuleAst module = dudu::parse_source("class Base:\n"
                                                      "    @virtual\n"
                                                      "    def id(self) -> i32:\n"
                                                      "        return 1\n"
                                                      "\n"
                                                      "class Derived(Base):\n"
                                                      "    @override\n"
                                                      "    def id(self) -> i32:\n"
                                                      "        return 2\n"
                                                      "\n"
                                                      "def assign(value: *Derived) -> *Base:\n"
                                                      "    base: *Base = value\n"
                                                      "    return base\n",
                                                      "base_pointer_assignment_type_ast.dd");
    dudu::analyze_module(module, {.check_bodies = true});
}

void test_duplicate_base_check_resolves_type_aliases() {
    bool rejected = false;
    try {
        const dudu::ModuleAst module = dudu::parse_source("class Base:\n"
                                                          "    id: i32\n"
                                                          "\n"
                                                          "type BaseAlias = Base\n"
                                                          "\n"
                                                          "class Derived(Base, BaseAlias):\n"
                                                          "    value: i32\n",
                                                          "duplicate_base_alias.dd");
        dudu::analyze_module(module, {.check_bodies = true});
    } catch (const dudu::CompileError& error) {
        rejected =
            std::string(error.what()).find("duplicate base class: BaseAlias") != std::string::npos;
    }
    assert(rejected);
}

void test_inherited_method_identity_resolves_type_aliases() {
    bool rejected = false;
    try {
        const dudu::ModuleAst module =
            dudu::parse_source("class Node:\n"
                               "    id: i32\n"
                               "\n"
                               "class Payload:\n"
                               "    value: i32\n"
                               "\n"
                               "type PayloadAlias = Payload\n"
                               "\n"
                               "class Named:\n"
                               "    @abstract\n"
                               "    def name(self) -> str:\n"
                               "\n"
                               "    def label(self, payload: Payload) -> str:\n"
                               "        return self.name()\n"
                               "\n"
                               "class Titled:\n"
                               "    @abstract\n"
                               "    def title(self) -> str:\n"
                               "\n"
                               "    def label(self, payload: PayloadAlias) -> str:\n"
                               "        return self.title()\n"
                               "\n"
                               "class Sprite(Node, Named, Titled):\n"
                               "    @override\n"
                               "    def name(self) -> str:\n"
                               "        return \"sprite\"\n"
                               "\n"
                               "    @override\n"
                               "    def title(self) -> str:\n"
                               "        return \"sprite\"\n",
                               "ambiguous_inherited_alias_signature.dd");
        dudu::analyze_module(module, {.check_bodies = true});
    } catch (const dudu::CompileError& error) {
        rejected =
            std::string(error.what()).find("ambiguous inherited concrete method: label(Payload)") !=
            std::string::npos;
    }
    assert(rejected);
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
                                                      "type ItemAlias = Item\n"
                                                      "\n"
                                                      "def sum_items(items: list[Item]) -> i32:\n"
                                                      "    total: i32 = 0\n"
                                                      "    for index in range(3):\n"
                                                      "        total += index\n"
                                                      "    for item: &Item in items:\n"
                                                      "        total += item.value\n"
                                                      "    for alias_item: ItemAlias in items:\n"
                                                      "        total += alias_item.value\n"
                                                      "    for copy in items:\n"
                                                      "        total += copy.value\n"
                                                      "    return total\n",
                                                      "typed_for.dd");
    dudu::analyze_module(module, {.check_bodies = true});
    const std::string cpp = dudu::emit_cpp_source(module);
    assert(cpp.find("for (auto index = 0; index < 3; index += 1)") != std::string::npos);
    assert(cpp.find("for (Item& item : items)") != std::string::npos);
    assert(cpp.find("for (ItemAlias alias_item : items)") != std::string::npos);
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
    assert(counter.fields[0].value_expr.kind == dudu::ExprKind::IntLiteral);
    assert(counter.fields[0].value_expr.value == "7");
    assert(counter.static_fields.size() == 1);
    assert(counter.static_fields[0].name == "count");
    assert(dudu::type_ref_text(counter.static_fields[0].type_ref) == "i32");
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
        test_rejected_oop_surface_fixtures(root);
        test_header_emission();
        test_semantic_diagnostics();
        test_formatter();
        test_list_iterator_methods();
        test_reference_list_indexing();
        test_negative_numeric_literals_contextualize_as_f32_args();
        test_receiver_reference_semantics();
        test_pointer_dereference_uses_type_ast();
        test_extern_c_signature_uses_type_ast();
        test_pointer_arithmetic_uses_type_ast();
        test_base_pointer_assignment_uses_type_ast();
        test_duplicate_base_check_resolves_type_aliases();
        test_inherited_method_identity_resolves_type_aliases();
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
