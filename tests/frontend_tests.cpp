#include "dudu/cpp_emit.hpp"
#include "dudu/cpp_lower.hpp"
#include "dudu/format.hpp"
#include "dudu/lexer.hpp"
#include "dudu/native_headers.hpp"
#include "dudu/parser.hpp"
#include "dudu/project_config.hpp"
#include "dudu/sema.hpp"

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

void test_ast_constructor_assignment_compatibility() {
    const dudu::ModuleAst module = dudu::parse_source("class Bag:\n"
                                                      "    names: dict[str, i32]\n"
                                                      "    values: list[i32]\n"
                                                      "\n"
                                                      "def make_bag() -> Bag:\n"
                                                      "    first: Bag = Bag({}, [])\n"
                                                      "    second: Bag = Bag(names={}, values=[])\n"
                                                      "    return first\n",
                                                      "ast_constructor_assignment.dd");
    dudu::analyze_module(module, {.check_bodies = true});
}

void test_statement_ast_shape() {
    const dudu::ModuleAst module = dudu::parse_source("def main() -> i32:\n"
                                                      "    total: i32 = 0\n"
                                                      "    for item: i32 in values:\n"
                                                      "        total += item\n"
                                                      "    if total == 0:\n"
                                                      "        total += 42\n"
                                                      "    else:\n"
                                                      "        total = 1\n"
                                                      "    return total\n",
                                                      "statement_ast.dd");
    assert(module.functions.size() == 1);
    const dudu::FunctionDecl& main = module.functions.front();
    assert(main.statements.size() == 5);
    assert(main.statements[0].kind == dudu::StmtKind::VarDecl);
    assert(main.statements[0].name == "total");
    assert(main.statements[0].type == "i32");
    assert(main.statements[0].value == "0");
    assert(main.statements[1].kind == dudu::StmtKind::For);
    assert(main.statements[1].name == "item");
    assert(main.statements[1].type == "i32");
    assert(main.statements[1].iterable == "values");
    assert(main.statements[1].children.size() == 1);
    assert(main.statements[1].children[0].kind == dudu::StmtKind::CompoundAssign);
    assert(main.statements[1].children[0].target == "total");
    assert(main.statements[1].children[0].op == "+");
    assert(main.statements[1].children[0].value == "item");
    assert(main.statements[2].kind == dudu::StmtKind::If);
    assert(main.statements[2].condition == "total == 0");
    assert(main.statements[2].children.size() == 1);
    assert(main.statements[2].children[0].kind == dudu::StmtKind::CompoundAssign);
    assert(main.statements[3].kind == dudu::StmtKind::Else);
    assert(main.statements[3].children.size() == 1);
    assert(main.statements[3].children[0].kind == dudu::StmtKind::Assign);
    assert(main.statements[3].children[0].target == "total");
    assert(main.statements[3].children[0].value == "1");
    assert(main.statements[4].kind == dudu::StmtKind::Return);
    assert(main.statements[4].value == "total");
}

void test_expression_ast_shape() {
    const dudu::ModuleAst module =
        dudu::parse_source("def main() -> i32:\n"
                           "    answer: i32 = add(20, values[0] + 2)\n"
                           "    if not ready or count < 3:\n"
                           "        player.inventory[slot].name = Vec4[f32](1.0, 0.0, 0.0, 1.0)\n"
                           "    values: list[i32] = [1, 2, 3]\n"
                           "    flags: i32 = mask & (1 << bit)\n"
                           "    *ptr = &values[0]\n"
                           "    point: Point = Point(x=1, y=2)\n"
                           "    hex_mask: i32 = 0x80\n"
                           "    view: span[i32] = values[1:3]\n"
                           "    choice: i32 = 1 if ready else 2\n"
                           "    callback = lambda left, right: left + right\n"
                           "    return answer\n",
                           "expression_ast.dd");
    assert(module.functions.size() == 1);
    const dudu::FunctionDecl& main = module.functions.front();
    assert(main.statements.size() == 11);

    const dudu::Stmt& answer = main.statements[0];
    assert(answer.kind == dudu::StmtKind::VarDecl);
    assert(answer.value_expr.kind == dudu::ExprKind::Call);
    assert(answer.value_expr.name == "add");
    assert(answer.value_expr.callee.size() == 1);
    assert(answer.value_expr.callee[0].kind == dudu::ExprKind::Name);
    assert(answer.value_expr.callee[0].name == "add");
    assert(answer.value_expr.range.start.line == 2);
    assert(answer.value_expr.range.start.column > answer.location.column);
    assert(answer.value_expr.children.size() == 2);
    assert(answer.value_expr.children[0].kind == dudu::ExprKind::IntLiteral);
    assert(answer.value_expr.children[0].range.start.line == 2);
    assert(answer.value_expr.children[0].range.start.column > answer.value_expr.range.start.column);
    assert(answer.value_expr.children[1].kind == dudu::ExprKind::Binary);
    assert(answer.value_expr.children[1].range.start.line == 2);
    assert(answer.value_expr.children[1].range.start.column >
           answer.value_expr.children[0].range.start.column);
    assert(answer.value_expr.children[1].op == "+");
    assert(answer.value_expr.children[1].children[0].kind == dudu::ExprKind::Index);

    const dudu::Stmt& branch = main.statements[1];
    assert(branch.kind == dudu::StmtKind::If);
    assert(branch.condition_expr.kind == dudu::ExprKind::Binary);
    assert(branch.condition_expr.op == "or");
    assert(branch.condition_expr.children[0].kind == dudu::ExprKind::Unary);
    assert(branch.condition_expr.children[0].op == "not");
    assert(branch.condition_expr.children[0].children[0].range.start.column >
           branch.condition_expr.children[0].range.start.column);
    assert(branch.condition_expr.children[1].kind == dudu::ExprKind::Binary);
    assert(branch.condition_expr.children[1].op == "<");
    assert(branch.condition_expr.children[1].children[1].range.start.column >
           branch.condition_expr.children[1].children[0].range.start.column);
    assert(branch.children.size() == 1);

    const dudu::Stmt& assign = branch.children[0];
    assert(assign.kind == dudu::StmtKind::Assign);
    assert(assign.target_expr.kind == dudu::ExprKind::Member);
    assert(assign.target_expr.name == "name");
    assert(assign.target_expr.children[0].kind == dudu::ExprKind::Index);
    assert(assign.value_expr.kind == dudu::ExprKind::TemplateCall);
    assert(assign.value_expr.name == "Vec4");
    assert(assign.value_expr.callee.size() == 1);
    assert(assign.value_expr.callee[0].kind == dudu::ExprKind::Name);
    assert(assign.value_expr.callee[0].name == "Vec4");
    assert(assign.value_expr.template_args.size() == 1);
    assert(assign.value_expr.template_args[0].kind == dudu::ExprKind::Name);
    assert(assign.value_expr.template_args[0].name == "f32");
    assert(assign.value_expr.template_args[0].range.start.column >
           assign.value_expr.range.start.column);
    assert(assign.value_expr.template_type_args.size() == 1);
    assert(assign.value_expr.template_type_args[0].kind == dudu::TypeKind::Named);
    assert(assign.value_expr.template_type_args[0].name == "f32");
    assert(assign.value_expr.template_type_args[0].range.start.column >
           assign.value_expr.range.start.column);
    assert(assign.value_expr.children.size() == 4);
    assert(assign.value_expr.children[0].range.start.column > assign.value_expr.range.start.column);

    const dudu::Stmt& values = main.statements[2];
    assert(values.kind == dudu::StmtKind::VarDecl);
    assert(values.value_expr.kind == dudu::ExprKind::ListLiteral);
    assert(values.value_expr.children.size() == 3);
    assert(values.value_expr.children[0].range.start.column > values.value_expr.range.start.column);
    assert(values.value_expr.children[1].range.start.column >
           values.value_expr.children[0].range.start.column);
    assert(values.value_expr.children[2].range.start.column >
           values.value_expr.children[1].range.start.column);
    assert(values.value_expr.range.start.line == 5);
    assert(values.value_expr.range.start.column > values.location.column);

    const dudu::Stmt& flags = main.statements[3];
    assert(flags.kind == dudu::StmtKind::VarDecl);
    assert(flags.value_expr.kind == dudu::ExprKind::Binary);
    assert(flags.value_expr.op == "&");
    assert(flags.value_expr.children[1].kind == dudu::ExprKind::Binary);
    assert(flags.value_expr.children[1].op == "<<");
    assert(flags.value_expr.children[1].range.start.column >
           flags.value_expr.children[0].range.start.column);
    assert(flags.value_expr.children[1].children[1].range.start.column >
           flags.value_expr.children[1].children[0].range.start.column);

    const dudu::Stmt& pointer_assign = main.statements[4];
    assert(pointer_assign.kind == dudu::StmtKind::Assign);
    assert(pointer_assign.target_expr.kind == dudu::ExprKind::Unary);
    assert(pointer_assign.target_expr.op == "*");
    assert(pointer_assign.target_expr.children[0].range.start.column >
           pointer_assign.target_expr.range.start.column);
    assert(pointer_assign.value_expr.kind == dudu::ExprKind::Unary);
    assert(pointer_assign.value_expr.op == "&");
    assert(pointer_assign.value_expr.children[0].kind == dudu::ExprKind::Index);
    assert(pointer_assign.value_expr.children[0].range.start.column >
           pointer_assign.value_expr.range.start.column);

    const dudu::Stmt& point = main.statements[5];
    assert(point.kind == dudu::StmtKind::VarDecl);
    assert(point.value_expr.kind == dudu::ExprKind::Call);
    assert(point.value_expr.callee.size() == 1);
    assert(point.value_expr.callee[0].kind == dudu::ExprKind::Name);
    assert(point.value_expr.callee[0].name == "Point");
    assert(point.value_expr.children.size() == 2);
    assert(point.value_expr.children[0].kind == dudu::ExprKind::NamedArg);
    assert(point.value_expr.children[0].name == "x");
    assert(point.value_expr.children[0].children.size() == 1);
    assert(point.value_expr.children[0].children[0].kind == dudu::ExprKind::IntLiteral);
    assert(point.value_expr.children[1].kind == dudu::ExprKind::NamedArg);
    assert(point.value_expr.children[1].name == "y");

    const dudu::Stmt& hex_mask = main.statements[6];
    assert(hex_mask.kind == dudu::StmtKind::VarDecl);
    assert(hex_mask.value_expr.kind == dudu::ExprKind::IntLiteral);
    assert(hex_mask.value_expr.text == "0x80");

    const dudu::Stmt& view = main.statements[7];
    assert(view.kind == dudu::StmtKind::VarDecl);
    assert(view.value_expr.kind == dudu::ExprKind::Index);
    assert(view.value_expr.children.size() == 2);
    assert(view.value_expr.children[1].kind == dudu::ExprKind::Slice);
    assert(view.value_expr.children[1].range.start.column > view.value_expr.range.start.column);
    assert(view.value_expr.children[1].children.size() == 2);
    assert(view.value_expr.children[1].children[0].kind == dudu::ExprKind::IntLiteral);
    assert(view.value_expr.children[1].children[1].kind == dudu::ExprKind::IntLiteral);
    assert(view.value_expr.children[1].children[0].range.start.column ==
           view.value_expr.children[1].range.start.column);
    assert(view.value_expr.children[1].children[1].range.start.column >
           view.value_expr.children[1].children[0].range.start.column);

    const dudu::Stmt& choice = main.statements[8];
    assert(choice.kind == dudu::StmtKind::VarDecl);
    assert(choice.value_expr.kind == dudu::ExprKind::Conditional);
    assert(choice.value_expr.children.size() == 3);
    assert(choice.value_expr.children[0].kind == dudu::ExprKind::IntLiteral);
    assert(choice.value_expr.children[1].kind == dudu::ExprKind::Name);
    assert(choice.value_expr.children[2].kind == dudu::ExprKind::IntLiteral);
    assert(choice.value_expr.children[1].range.start.column >
           choice.value_expr.children[0].range.start.column);
    assert(choice.value_expr.children[2].range.start.column >
           choice.value_expr.children[1].range.start.column);

    const dudu::Stmt& callback = main.statements[9];
    assert(callback.kind == dudu::StmtKind::Assign);
    assert(callback.value_expr.kind == dudu::ExprKind::Lambda);
    assert(callback.value_expr.params.size() == 2);
    assert(callback.value_expr.params[0].kind == dudu::ExprKind::Name);
    assert(callback.value_expr.params[0].name == "left");
    assert(callback.value_expr.params[1].kind == dudu::ExprKind::Name);
    assert(callback.value_expr.params[1].name == "right");
    assert(callback.value_expr.params[1].range.start.column >
           callback.value_expr.params[0].range.start.column);
    assert(callback.value_expr.children.size() == 1);
    assert(callback.value_expr.children[0].kind == dudu::ExprKind::Binary);
}

void test_type_ast_shape() {
    const dudu::ModuleAst module =
        dudu::parse_source("type PlayerList = list[*Player]\n"
                           "\n"
                           "MAX_PLAYERS: i32 = 4 * 2\n"
                           "static_assert(MAX_PLAYERS == 8)\n"
                           "\n"
                           "enum Mode: u8\n"
                           "    Idle = 0\n"
                           "    Running = 1 + 1\n"
                           "\n"
                           "class Player:\n"
                           "    count: static[i32] = 0\n"
                           "    DEFAULT_HP: i32 = MAX_PLAYERS + 34\n"
                           "    transform: array[f32][4, 4]\n"
                           "\n"
                           "def update(player: &Player, names: list[str]) -> *Player:\n"
                           "    local: const[list[i32]] = [1, 2]\n"
                           "    return None\n",
                           "type_ast.dd");
    assert(module.aliases.size() == 1);
    assert(module.aliases[0].type_ref.kind == dudu::TypeKind::Template);
    assert(module.aliases[0].type_ref.name == "list");
    assert(module.aliases[0].type_ref.children.size() == 1);
    assert(module.aliases[0].type_ref.children[0].kind == dudu::TypeKind::Pointer);

    assert(module.constants.size() == 1);
    assert(module.constants[0].value_expr.kind == dudu::ExprKind::Binary);
    assert(module.constants[0].value_expr.op == "*");
    assert(module.static_asserts.size() == 1);
    assert(module.static_asserts[0].expression_expr.kind == dudu::ExprKind::Binary);
    assert(module.static_asserts[0].expression_expr.op == "==");

    assert(module.enums.size() == 1);
    assert(module.enums[0].underlying_type_ref.kind == dudu::TypeKind::Named);
    assert(module.enums[0].underlying_type_ref.name == "u8");
    assert(module.enums[0].values.size() == 2);
    assert(module.enums[0].values[0].value_expr.kind == dudu::ExprKind::IntLiteral);
    assert(module.enums[0].values[1].value_expr.kind == dudu::ExprKind::Binary);
    assert(module.enums[0].values[1].value_expr.op == "+");

    assert(module.classes.size() == 1);
    const dudu::ClassDecl& player = module.classes[0];
    assert(player.static_fields.size() == 1);
    assert(player.static_fields[0].type == "i32");
    assert(player.static_fields[0].type_ref.kind == dudu::TypeKind::Named);
    assert(player.static_fields[0].value_expr.kind == dudu::ExprKind::IntLiteral);
    assert(player.constants.size() == 1);
    assert(player.constants[0].value_expr.kind == dudu::ExprKind::Binary);
    assert(player.constants[0].value_expr.op == "+");
    assert(player.fields.size() == 1);
    assert(player.fields[0].type_ref.kind == dudu::TypeKind::FixedArray);
    assert(player.fields[0].type_ref.value == "4, 4");
    assert(player.fields[0].type_ref.children[0].kind == dudu::TypeKind::Template);
    assert(player.fields[0].type_ref.children[0].name == "array");

    assert(module.functions.size() == 1);
    const dudu::FunctionDecl& update = module.functions[0];
    assert(update.return_type_ref.kind == dudu::TypeKind::Pointer);
    assert(update.params[0].type_ref.kind == dudu::TypeKind::Reference);
    assert(update.params[1].type_ref.kind == dudu::TypeKind::Template);
    assert(update.params[1].type_ref.children[0].name == "str");
    assert(update.statements[0].type_ref.kind == dudu::TypeKind::Const);
    assert(update.statements[0].type_ref.range.start.line == 16);
    assert(update.statements[0].type_ref.range.start.column > update.statements[0].location.column);
    assert(update.statements[0].type_ref.children[0].kind == dudu::TypeKind::Template);

    assert(dudu::lower_cpp_type(dudu::parse_type_text("list[*Player]")) == "std::vector<Player*>");
    assert(dudu::lower_cpp_type(dudu::parse_type_text("&const[Player]")) == "const Player&");
    assert(dudu::lower_cpp_type(dudu::parse_type_text("fn(i32, f32) -> bool")) ==
           "std::add_pointer_t<bool(int32_t, float)>");
    assert(dudu::lower_cpp_type(player.fields[0].type_ref) ==
           "std::array<std::array<float, 4>, 4>");
    assert(dudu::lower_cpp_type("array[i32][3]") == "std::array<int32_t, 3>");
    assert(dudu::lower_cpp_type("array[f32][4, 4]") == "std::array<std::array<float, 4>, 4>");
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

void test_native_type_declaration_emission() {
    const dudu::ModuleAst module =
        dudu::parse_source("import c \"SDL3/SDL.h\" as sdl\n"
                           "\n"
                           "type SDL_Event\n"
                           "\n"
                           "def main() -> i32:\n"
                           "    event: SDL_Event\n"
                           "    while sdl.SDL_PollEvent(&event):\n"
                           "        if event.type == sdl.SDL_EVENT_QUIT:\n"
                           "            return 0\n"
                           "    return 1\n",
                           "native_type.dd");
    dudu::analyze_module(module, {.check_bodies = true});
    const std::string cpp = dudu::emit_cpp_source(module);
    assert(cpp.find("#include \"SDL3/SDL.h\"") != std::string::npos);
    assert(cpp.find("SDL_Event event{};") != std::string::npos);
    assert(cpp.find("struct SDL_Event") == std::string::npos);
}

void test_native_header_type_scan(const std::filesystem::path& root) {
    dudu::ModuleAst module =
        dudu::parse_source("import c \"native_headers/simple_c.h\"\n"
                           "import cpp \"native_headers/simple_cpp.hpp\"\n"
                           "\n"
                           "def main() -> i32:\n"
                           "    event: DuduNativeEvent\n"
                           "    window: *DuduNativeWindow = None\n"
                           "    widget: DuduWidgetAlias\n"
                           "    other: Widget = Widget(5)\n"
                           "    namespaced: dudu_native.Widget = "
                           "dudu_native.Widget(6)\n"
                           "    derived: dudu_native.DerivedWidget = "
                           "dudu_native.DerivedWidget(9)\n"
                           "    nested: dudu_native.Outer.Inner = "
                           "dudu_native.Outer.Inner(21)\n"
                           "    amount: f32 = 2.0\n"
                           "    if derived.base_scaled(2) != 18:\n"
                           "        return 1\n"
                           "    if dudu_native.use_base_widget(&derived) != 9:\n"
                           "        return 2\n"
                           "    if dudu_native.read_const_ref(namespaced) != 6:\n"
                           "        return 5\n"
                           "    if nested.doubled() != 42:\n"
                           "        return 4\n"
                           "    proc = dudu_native_proc()\n"
                           "    if proc == None:\n"
                           "        return 3\n"
                           "    if DUDU_NATIVE_CHECK():\n"
                           "        return dudu_native.add(20, 22) + "
                           "DUDU_NATIVE_MAGIC\n"
                           "    if dudu_native_ready(&event):\n"
                           "        return DUDU_NATIVE_SCALE(other.scaled(3)) + "
                           "i32(dudu_native.overloaded(amount))\n"
                           "    dudu_native_format(\"%d %d\", event.type, "
                           "dudu_native_kind_ok)\n"
                           "    return event.type + widget.value + "
                           "other.value + dudu_native_kind_ok\n",
                           root / "tests/fixtures/native_scan.dd");
    dudu::ProjectConfig config;
    config.build_dir = root / "build" / "native-header-test-cache";
    std::filesystem::remove_all(config.build_dir);
    dudu::merge_native_header_types(module,
                                    {.config = config, .source_dir = root / "tests/fixtures"});
    dudu::analyze_module(module, {.check_bodies = true});
    const std::string cpp = dudu::emit_cpp_source(module);
    assert(cpp.find("DuduNativeEvent event{};") != std::string::npos);
    assert(cpp.find("DuduNativeWindow* window = nullptr;") != std::string::npos);
    assert(cpp.find("DuduWidgetAlias widget{};") != std::string::npos);
    assert(cpp.find("Widget other = Widget(5);") != std::string::npos);
    assert(cpp.find("dudu_native::Widget namespaced = dudu_native::Widget(6);") !=
           std::string::npos);
    assert(cpp.find("dudu_native::DerivedWidget derived = dudu_native::DerivedWidget(9);") !=
           std::string::npos);
    assert(cpp.find("dudu_native::Outer::Inner nested = dudu_native::Outer::Inner(21);") !=
           std::string::npos);
    assert(cpp.find("derived.base_scaled(2)") != std::string::npos);
    assert(cpp.find("nested.doubled()") != std::string::npos);
    assert(cpp.find("dudu_native::use_base_widget((&derived))") != std::string::npos);
    assert(cpp.find("dudu_native::read_const_ref(namespaced)") != std::string::npos);
    assert(std::filesystem::exists(config.build_dir / "dudu-header-cache"));
}

void test_native_call_arity(const std::filesystem::path& root) {
    dudu::ModuleAst module = dudu::parse_source("import c \"native_headers/simple_c.h\"\n"
                                                "\n"
                                                "def main() -> i32:\n"
                                                "    return dudu_native_add()\n",
                                                root / "tests/fixtures/native_bad_arity.dd");
    dudu::merge_native_header_types(
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
        dudu::ModuleAst module = dudu::parse_source("import c \"native_headers/simple_c.h\"\n"
                                                    "DUDU_NATIVE_MAGIC: i32 = 1\n",
                                                    root / "tests/fixtures/native_collision.dd");
        dudu::merge_native_header_types(
            module, {.config = dudu::ProjectConfig{}, .source_dir = root / "tests/fixtures"});
        dudu::analyze_module(module, {.check_bodies = true});
    } catch (const dudu::CompileError&) {
        failed = true;
    }
    assert(failed);
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
    dudu::ModuleAst first = dudu::parse_source("import cpp \"./cache_probe.hpp\"\n"
                                               "\n"
                                               "def main() -> i32:\n"
                                               "    if cache_probe(True):\n"
                                               "        return 42\n"
                                               "    return 0\n",
                                               source_dir / "main.dd");
    dudu::merge_native_header_types(first, {.config = config, .source_dir = source_dir});
    dudu::analyze_module(first, {.check_bodies = true});

    {
        std::ofstream out(header);
        out << "#pragma once\n"
               "inline int cache_probe(int value, int salt) { return value + salt; }\n";
    }
    bool failed = false;
    try {
        dudu::ModuleAst second = dudu::parse_source("import cpp \"./cache_probe.hpp\"\n"
                                                    "\n"
                                                    "def main() -> i32:\n"
                                                    "    if cache_probe(True):\n"
                                                    "        return 42\n"
                                                    "    return 0\n",
                                                    source_dir / "main.dd");
        dudu::merge_native_header_types(second, {.config = config, .source_dir = source_dir});
        dudu::analyze_module(second, {.check_bodies = true});
    } catch (const dudu::CompileError& error) {
        failed = std::string(error.what()).find("cache_probe") != std::string::npos;
    }
    assert(failed);
}

void test_native_header_pointer_diagnostics(const std::filesystem::path& root) {
    bool failed = false;
    try {
        dudu::ModuleAst module = dudu::parse_source("import c \"native_headers/simple_c.h\"\n"
                                                    "\n"
                                                    "def main() -> i32:\n"
                                                    "    event: DuduNativeEvent\n"
                                                    "    if dudu_native_ready(event):\n"
                                                    "        return 1\n"
                                                    "    return 0\n",
                                                    root / "tests/fixtures/native_pointer.dd");
        dudu::merge_native_header_types(
            module, {.config = dudu::ProjectConfig{}, .source_dir = root / "tests/fixtures"});
        dudu::analyze_module(module, {.check_bodies = true});
    } catch (const dudu::CompileError&) {
        failed = true;
    }
    assert(failed);
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
    assert(config.targets.at("tests").target_mode == "hosted");
    const dudu::ProjectConfig tests_config = dudu::apply_project_target(config, "tests");
    assert(tests_config.name == "tests");
    assert(tests_config.main == "tests/main.dd");
    assert(tests_config.target_kind == "executable");
    assert(tests_config.target_mode == "hosted");
}

void test_image_filter_emission(const std::filesystem::path& root) {
    const std::filesystem::path path = root / "examples" / "image_filter.dd";
    const dudu::ModuleAst module = dudu::parse_source(read_file(path), path);
    dudu::analyze_module(module, {.check_bodies = true});
    const std::string cpp = dudu::emit_cpp_source(module);
    assert(cpp.find("dst.at<cv::Vec3b>(y, x)") != std::string::npos);
    assert(cpp.find("edges.at<uint8_t>(y, x)") != std::string::npos);
    assert(cpp.find("pixel[0]") != std::string::npos);
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
        test_ast_constructor_assignment_compatibility();
        test_statement_ast_shape();
        test_expression_ast_shape();
        test_type_ast_shape();
        test_formatter();
        test_typed_for_emission();
        test_class_field_defaults_and_static_fields();
        test_native_type_declaration_emission();
        test_native_header_type_scan(root);
        test_native_call_arity(root);
        test_native_header_collision(root);
        test_native_header_cache_invalidates_local_header(root);
        test_native_header_pointer_diagnostics(root);
        test_project_driver_config(root);
        test_image_filter_emission(root);
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
