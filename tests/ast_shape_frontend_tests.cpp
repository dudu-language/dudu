#include "dudu/codegen/cpp_emit.hpp"
#include "dudu/codegen/cpp_expr_emit.hpp"
#include "dudu/codegen/cpp_lower.hpp"
#include "dudu/codegen/cpp_stmt_types.hpp"
#include "dudu/core/array_shape.hpp"
#include "dudu/core/ast_expr.hpp"
#include "dudu/core/ast_type.hpp"
#include "dudu/core/match_patterns.hpp"
#include "dudu/lsp/language_server_completion.hpp"
#include "dudu/lsp/language_server_json.hpp"
#include "dudu/lsp/language_server_local_context.hpp"
#include "dudu/lsp/language_server_navigation.hpp"
#include "dudu/lsp/language_server_semantic_tokens.hpp"
#include "dudu/native/native_header_types.hpp"
#include "dudu/native/native_signature_match.hpp"
#include "dudu/native/native_signature_substitution.hpp"
#include "dudu/native/native_signature_templates.hpp"
#include "dudu/parser/parser.hpp"
#include "dudu/sema/sema.hpp"
#include "dudu/sema/sema_builtin_methods.hpp"
#include "dudu/sema/sema_context.hpp"
#include "dudu/sema/sema_expr_internal.hpp"
#include "dudu/sema/sema_function_type.hpp"
#include "dudu/sema/sema_generics.hpp"
#include "dudu/sema/sema_inheritance.hpp"
#include "dudu/sema/sema_method_templates.hpp"
#include "dudu/sema/sema_methods.hpp"
#include "dudu/sema/sema_methods_internal.hpp"
#include "dudu/sema/sema_native.hpp"
#include "dudu/sema/type_compat.hpp"
#include "dudu/sema/type_compat_native.hpp"

#include <cassert>
#include <cctype>
#include <exception>
#include <iostream>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace {

void test_literal_ast_values() {
    const dudu::ModuleAst module = dudu::parse_source("def main() -> i32:\n"
                                                      "    enabled = True\n"
                                                      "    mask = 0x80\n"
                                                      "    title = 'hi \"there\"'\n"
                                                      "    blob = \"\"\"line\nnext\"\"\"\n"
                                                      "    return mask\n",
                                                      "literal_values.dd");
    assert(module.functions.size() == 1);
    const dudu::FunctionDecl& main = module.functions.front();
    assert(main.statements.size() == 5);
    assert(main.statements[0].kind == dudu::StmtKind::Assign);
    assert(main.statements[0].value_expr.kind == dudu::ExprKind::BoolLiteral);
    assert(main.statements[0].value_expr.value == "True");
    assert(main.statements[1].kind == dudu::StmtKind::Assign);
    assert(main.statements[1].value_expr.kind == dudu::ExprKind::IntLiteral);
    assert(main.statements[1].value_expr.value == "0x80");
    assert(dudu::display_expr(main.statements[1].value_expr) == "0x80");
    assert(main.statements[2].value_expr.kind == dudu::ExprKind::StringLiteral);
    assert(main.statements[2].value_expr.value == "hi \"there\"");
    assert(dudu::display_expr(main.statements[2].value_expr) == "\"hi \\\"there\\\"\"");
    assert(dudu::lower_cpp_expr_ast(main.statements[2].value_expr, {}) == "\"hi \\\"there\\\"\"");
    assert(main.statements[3].value_expr.kind == dudu::ExprKind::StringLiteral);
    assert(main.statements[3].value_expr.value == "line\nnext");
    assert(dudu::display_expr(main.statements[3].value_expr) == "\"line\\nnext\"");
    assert(dudu::lower_cpp_expr_ast(main.statements[3].value_expr, {}) == "\"line\\nnext\"");
    dudu::Expr malformed_binary;
    malformed_binary.kind = dudu::ExprKind::Binary;
    assert(dudu::display_expr(malformed_binary) == "<malformed binary expression>");
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
                           "    pending = await fetch()\n"
                           "    produced = yield answer\n"
                           "    return answer\n",
                           "expression_ast.dd");
    assert(module.functions.size() == 1);
    const dudu::FunctionDecl& main = module.functions.front();
    assert(main.statements.size() == 11);

    const dudu::Stmt& answer = main.statements[0];
    assert(answer.kind == dudu::StmtKind::VarDecl);
    assert(answer.value_expr.kind == dudu::ExprKind::Call);
    assert(answer.value_expr.name.empty());
    assert(dudu::direct_callee_name(answer.value_expr) == "add");
    assert(dudu::expr_callee(answer.value_expr).size() == 1);
    assert(dudu::expr_callee(answer.value_expr)[0].kind == dudu::ExprKind::Name);
    assert(dudu::expr_callee(answer.value_expr)[0].name == "add");
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
    assert(dudu::stmt_condition_expr(branch).kind == dudu::ExprKind::Binary);
    assert(dudu::stmt_condition_expr(branch).op == "or");
    assert(dudu::stmt_condition_expr(branch).children[0].kind == dudu::ExprKind::Unary);
    assert(dudu::stmt_condition_expr(branch).children[0].op == "not");
    assert(dudu::stmt_condition_expr(branch).children[0].children[0].range.start.column >
           dudu::stmt_condition_expr(branch).children[0].range.start.column);
    assert(dudu::stmt_condition_expr(branch).children[1].kind == dudu::ExprKind::Binary);
    assert(dudu::stmt_condition_expr(branch).children[1].op == "<");
    assert(dudu::stmt_condition_expr(branch).children[1].children[1].range.start.column >
           dudu::stmt_condition_expr(branch).children[1].children[0].range.start.column);
    assert(branch.children.size() == 1);

    const dudu::Stmt& assign = branch.children[0];
    assert(assign.kind == dudu::StmtKind::Assign);
    assert(dudu::stmt_target_expr(assign).kind == dudu::ExprKind::Member);
    assert(dudu::stmt_target_expr(assign).name == "name");
    assert(dudu::stmt_target_expr(assign).children[0].kind == dudu::ExprKind::Index);
    assert(assign.value_expr.kind == dudu::ExprKind::TemplateCall);
    assert(assign.value_expr.name.empty());
    assert(dudu::direct_callee_name(assign.value_expr) == "Vec4");
    assert(dudu::expr_callee(assign.value_expr).size() == 1);
    assert(dudu::expr_callee(assign.value_expr)[0].kind == dudu::ExprKind::Name);
    assert(dudu::expr_callee(assign.value_expr)[0].name == "Vec4");
    assert(dudu::expr_template_args(assign.value_expr).size() == 1);
    assert(dudu::expr_template_args(assign.value_expr)[0].kind == dudu::ExprKind::Name);
    assert(dudu::expr_template_args(assign.value_expr)[0].name == "f32");
    assert(dudu::expr_template_args(assign.value_expr)[0].range.start.column >
           assign.value_expr.range.start.column);
    assert(dudu::expr_template_type_args(assign.value_expr).size() == 1);
    assert(dudu::expr_template_type_args(assign.value_expr)[0].kind == dudu::TypeKind::Named);
    assert(dudu::expr_template_type_args(assign.value_expr)[0].name == "f32");
    assert(dudu::expr_template_type_args(assign.value_expr)[0].range.start.column >
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
    assert(dudu::stmt_target_expr(pointer_assign).kind == dudu::ExprKind::Unary);
    assert(dudu::stmt_target_expr(pointer_assign).op == "*");
    assert(dudu::stmt_target_expr(pointer_assign).children[0].range.start.column >
           dudu::stmt_target_expr(pointer_assign).range.start.column);
    assert(pointer_assign.value_expr.kind == dudu::ExprKind::Unary);
    assert(pointer_assign.value_expr.op == "&");
    assert(pointer_assign.value_expr.children[0].kind == dudu::ExprKind::Index);
    assert(pointer_assign.value_expr.children[0].range.start.column >
           pointer_assign.value_expr.range.start.column);

    const dudu::Stmt& point = main.statements[5];
    assert(point.kind == dudu::StmtKind::VarDecl);
    assert(point.value_expr.kind == dudu::ExprKind::Call);
    assert(dudu::expr_callee(point.value_expr).size() == 1);
    assert(dudu::expr_callee(point.value_expr)[0].kind == dudu::ExprKind::Name);
    assert(dudu::expr_callee(point.value_expr)[0].name == "Point");
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
    assert(hex_mask.value_expr.value == "0x80");

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

    const dudu::Stmt& pending = main.statements[8];
    assert(pending.kind == dudu::StmtKind::Assign);
    assert(pending.value_expr.kind == dudu::ExprKind::Await);
    assert(pending.value_expr.children.size() == 1);
    assert(pending.value_expr.children[0].kind == dudu::ExprKind::Call);
    assert(dudu::direct_callee_name(pending.value_expr.children[0]) == "fetch");
    assert(pending.value_expr.children[0].range.start.column >
           pending.value_expr.range.start.column);

    const dudu::Stmt& produced = main.statements[9];
    assert(produced.kind == dudu::StmtKind::Assign);
    assert(produced.value_expr.kind == dudu::ExprKind::Yield);
    assert(produced.value_expr.children.size() == 1);
    assert(produced.value_expr.children[0].kind == dudu::ExprKind::Name);
    assert(produced.value_expr.children[0].name == "answer");
    assert(produced.value_expr.children[0].range.start.column >
           produced.value_expr.range.start.column);
}

void test_cpp_escape_ast_payloads() {
    const dudu::ModuleAst module = dudu::parse_source("def main() -> i32:\n"
                                                      "    value: i32 = cpp(\"19\")\n"
                                                      "    cpp(\"value += 23;\")\n"
                                                      "    return value\n",
                                                      "cpp_escape_ast.dd");
    assert(module.functions.size() == 1);
    const dudu::FunctionDecl& main = module.functions.front();
    assert(main.statements.size() == 3);

    const dudu::Stmt& value = main.statements[0];
    assert(value.kind == dudu::StmtKind::VarDecl);
    assert(value.value_expr.kind == dudu::ExprKind::CppEscape);
    assert(value.value_expr.value == "19");

    const dudu::Stmt& statement_escape = main.statements[1];
    assert(statement_escape.kind == dudu::StmtKind::CppEscape);
    assert(statement_escape.cpp_lines.size() == 1);
    assert(statement_escape.cpp_lines[0] == "value += 23;");
    assert(statement_escape.range.start.column == 5);
    assert(statement_escape.range.end.column > statement_escape.range.start.column);
}

void test_dereference_postfix_expression_shape() {
    const dudu::Expr deref = dudu::parse_expr_text("*self.out");
    assert(deref.kind == dudu::ExprKind::Unary);
    assert(deref.op == "*");
    assert(deref.children.size() == 1);
    assert(deref.children[0].kind == dudu::ExprKind::Member);
    assert(deref.children[0].name == "out");

    const dudu::Expr cast = dudu::parse_expr_text("*struct State(user_data)");
    assert(cast.kind == dudu::ExprKind::Call);
    assert(cast.name.empty());
    assert(dudu::direct_callee_name(cast) == "*");
    assert(dudu::type_ref_head_name(dudu::expr_type_ref(cast)) == "struct State");
    assert(!dudu::has_expr_template_args(cast));
    assert(!dudu::has_expr_template_type_args(cast));

    const dudu::Expr template_cast = dudu::parse_expr_text("*list[MissingType](ptr)");
    assert(template_cast.kind == dudu::ExprKind::Call);
    assert(template_cast.name.empty());
    assert(dudu::direct_callee_name(template_cast) == "*");
    const dudu::TypeRef& template_cast_type = dudu::expr_type_ref(template_cast);
    assert(template_cast_type.kind == dudu::TypeKind::Template);
    assert(template_cast_type.name == "list");
    assert(template_cast_type.children.size() == 1);
    assert(template_cast_type.children[0].name == "MissingType");
    assert(!dudu::has_expr_template_args(template_cast));
    assert(!dudu::has_expr_template_type_args(template_cast));

    const dudu::Expr qualified_template_cast = dudu::parse_expr_text("*std.vector[i32](raw_data)");
    assert(qualified_template_cast.kind == dudu::ExprKind::Call);
    assert(dudu::direct_callee_name(qualified_template_cast) == "*");
    const dudu::TypeRef& qualified_template_cast_type =
        dudu::expr_type_ref(qualified_template_cast);
    assert(qualified_template_cast_type.kind == dudu::TypeKind::Template);
    assert(qualified_template_cast_type.name == "std.vector");
    assert(qualified_template_cast_type.children.size() == 1);
    assert(!dudu::has_expr_template_args(qualified_template_cast));
    assert(!dudu::has_expr_template_type_args(qualified_template_cast));
    assert(dudu::substitute_type_ref_text(qualified_template_cast_type.children[0], {}) == "i32");
}

void test_decorator_expression_ast_shape() {
    const dudu::ModuleAst module = dudu::parse_source("@section(\".boot+fast\")\n"
                                                      "def boot() -> void:\n"
                                                      "    pass\n"
                                                      "\n"
                                                      "class Vec2:\n"
                                                      "    x: i32\n"
                                                      "\n"
                                                      "    @operator(\"+\")\n"
                                                      "    def add(self, other: Vec2) -> Vec2:\n"
                                                      "        return self\n"
                                                      "\n"
                                                      "@test.should_panic(\"bad + input\")\n"
                                                      "def panics():\n"
                                                      "    raise \"bad + input\"\n",
                                                      "decorator_ast.dd");

    assert(module.functions.size() == 2);
    assert(module.functions[0].decorators.size() == 1);
    const dudu::Expr& section = module.functions[0].decorators[0].expr;
    assert(section.kind == dudu::ExprKind::Call);
    assert(dudu::direct_callee_name(section) == "section");
    assert(section.range.start.line == 1);
    assert(section.range.start.column == 2);
    assert(section.children.size() == 1);
    assert(section.children[0].kind == dudu::ExprKind::StringLiteral);
    assert(section.children[0].value == ".boot+fast");

    assert(module.classes.size() == 1);
    assert(module.classes[0].methods.size() == 1);
    assert(dudu::type_ref_text(module.classes[0].methods[0].params.front().type_ref) == "&Self");
    assert(module.classes[0].methods[0].decorators.size() == 1);
    const dudu::Expr& op = module.classes[0].methods[0].decorators[0].expr;
    assert(op.kind == dudu::ExprKind::Call);
    assert(dudu::direct_callee_name(op) == "operator");
    assert(op.children.size() == 1);
    assert(op.children[0].kind == dudu::ExprKind::StringLiteral);
    assert(op.children[0].value == "+");

    assert(module.functions[1].decorators.size() == 1);
    const dudu::Expr& panic = module.functions[1].decorators[0].expr;
    assert(panic.kind == dudu::ExprKind::Call);
    assert(dudu::has_expr_callee(panic));
    assert(dudu::expr_callee(panic).front().kind == dudu::ExprKind::Member);
    assert(panic.children.size() == 1);
    assert(panic.children[0].kind == dudu::ExprKind::StringLiteral);
    assert(panic.children[0].value == "bad + input");
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
    assert(dudu::type_ref_text(player.static_fields[0].type_ref) == "i32");
    assert(player.static_fields[0].type_ref.kind == dudu::TypeKind::Named);
    assert(player.static_fields[0].value_expr.kind == dudu::ExprKind::IntLiteral);
    assert(player.constants.size() == 1);
    assert(player.constants[0].value_expr.kind == dudu::ExprKind::Binary);
    assert(player.constants[0].value_expr.op == "+");
    assert(player.fields.size() == 1);
    assert(player.fields[0].type_ref.kind == dudu::TypeKind::FixedArray);
    assert(player.fields[0].type_ref.value == "4, 4");
    assert(player.fields[0].type_ref.children.size() == 3);
    assert(player.fields[0].type_ref.children[0].kind == dudu::TypeKind::Template);
    assert(player.fields[0].type_ref.children[0].name == "array");
    assert(player.fields[0].type_ref.children[1].kind == dudu::TypeKind::Value);
    assert(player.fields[0].type_ref.children[1].value == "4");
    assert(player.fields[0].type_ref.children[2].kind == dudu::TypeKind::Value);
    assert(player.fields[0].type_ref.children[2].value == "4");

    assert(module.functions.size() == 1);
    const dudu::FunctionDecl& update = module.functions[0];
    assert(update.return_type_ref.kind == dudu::TypeKind::Pointer);
    assert(update.params[0].type_ref.kind == dudu::TypeKind::Reference);
    assert(update.params[1].type_ref.kind == dudu::TypeKind::Template);
    assert(update.params[1].type_ref.children[0].name == "str");
    assert(dudu::has_stmt_type_ref(update.statements[0]));
    const dudu::TypeRef& statement_type = dudu::stmt_type_ref(update.statements[0]);
    assert(statement_type.kind == dudu::TypeKind::Const);
    assert(statement_type.range.start.line == 16);
    assert(statement_type.range.start.column > update.statements[0].location.column);
    assert(statement_type.children[0].kind == dudu::TypeKind::Template);

    assert(dudu::lower_cpp_type(dudu::parse_type_text("list[*Player]")) == "std::vector<Player*>");
    assert(dudu::lower_cpp_type(dudu::parse_type_text("*const[i32]")) == "const int32_t*");
    assert(dudu::lower_cpp_type(dudu::parse_type_text("const[*i32]")) == "int32_t* const");
    assert(dudu::lower_cpp_type(dudu::parse_type_text("const[*const[i32]]")) ==
           "const int32_t* const");
    assert(dudu::lower_cpp_type(dudu::parse_type_text("&const[Player]")) == "const Player&");
    assert(dudu::lower_cpp_type(dudu::parse_type_text("const[&Player]")) == "Player&");
    dudu::TypeRef structured_named;
    structured_named.kind = dudu::TypeKind::Named;
    structured_named.name = "Player";
    assert(dudu::lower_cpp_type(structured_named) == "Player");
    dudu::TypeRef spelled_pointer_type;
    spelled_pointer_type.kind = dudu::TypeKind::Pointer;
    spelled_pointer_type.children.push_back(dudu::named_type_ref("Player"));
    assert(dudu::lower_cpp_type(spelled_pointer_type) == "Player*");
    assert(dudu::lower_cpp_type_spelling("*const[i32]") == "const int32_t*");
    assert(dudu::lower_cpp_type_spelling("const[*i32]") == "int32_t* const");
    assert(dudu::lower_cpp_type(dudu::parse_type_text("fn(i32, f32) -> bool")) ==
           "std::add_pointer_t<bool(int32_t, float)>");
    assert(dudu::lower_cpp_type(dudu::parse_type_text("fn(i32)")) ==
           "std::add_pointer_t<void(int32_t)>");
    assert(dudu::lower_cpp_type_spelling("fn(i32, f32) -> bool") ==
           "std::add_pointer_t<bool(int32_t, float)>");
    assert(dudu::lower_cpp_type_spelling("list[fn(i32) -> bool]") ==
           "std::vector<std::add_pointer_t<bool(int32_t)>>");
    assert(dudu::lower_cpp_type_spelling("dict[str, list[fn(i32) -> bool]]") ==
           "std::unordered_map<std::string, "
           "std::vector<std::add_pointer_t<bool(int32_t)>>>");
    assert(dudu::lower_cpp_type_spelling("std.function[fn(i32) -> bool]") ==
           "std::function<bool(int32_t)>");
    assert(dudu::lower_cpp_type_spelling("Box[list[i32]]") == "Box<std::vector<int32_t>>");
    assert(dudu::lower_cpp_type_spelling("array[Box[list[i32]]][3]") ==
           "std::array<Box<std::vector<int32_t>>, 3>");
    assert(dudu::parse_type_text("Player[3][4]").kind == dudu::TypeKind::Unknown);
    const dudu::TypeRef shaped_tensor = dudu::parse_type_text("Tensor[f32][dyn, 784]");
    assert(shaped_tensor.kind == dudu::TypeKind::Shaped);
    assert(dudu::type_ref_text(shaped_tensor) == "Tensor[f32][dyn, 784]");
    assert(dudu::lower_cpp_type(shaped_tensor) == "Tensor<float>");
    const dudu::TypeRef arithmetic_shape = dudu::parse_type_text("Tensor[T][B, C * H * W]");
    assert(arithmetic_shape.kind == dudu::TypeKind::Shaped);
    assert(arithmetic_shape.children.size() == 3);
    assert(arithmetic_shape.children[1].kind == dudu::TypeKind::Named);
    assert(arithmetic_shape.children[1].name == "B");
    assert(arithmetic_shape.children[2].kind == dudu::TypeKind::Value);
    assert(arithmetic_shape.children[2].value == "C * H * W");
    assert(dudu::type_ref_text(arithmetic_shape) == "Tensor[T][B, C * H * W]");
    const dudu::Expr shape_call = dudu::parse_expr_text("flatten_static[i32, 2, 3, 2, 2](x)");
    assert(shape_call.kind == dudu::ExprKind::TemplateCall);
    assert(dudu::expr_template_type_args(shape_call).size() == 5);
    assert(dudu::type_ref_text(dudu::expr_template_type_args(shape_call)[3]) == "2");
    dudu::FunctionDecl shape_fn;
    shape_fn.name = "flatten_static";
    shape_fn.generic_params = {"T", "B", "C", "H", "W"};
    shape_fn.return_type_ref = arithmetic_shape;
    const dudu::FunctionSignature shape_signature = dudu::instantiate_generic_signature(
        shape_fn,
        {dudu::parse_type_text("i32"), dudu::parse_type_text("2"), dudu::parse_type_text("3"),
         dudu::parse_type_text("2"), dudu::parse_type_text("2")});
    assert(dudu::type_ref_text(dudu::signature_return_type_ref(shape_signature)) ==
           "Tensor[i32][2, 12]");
    const std::map<std::string, dudu::TypeRef> module_type_substitutions{
        {"Tensor", dudu::named_type_ref("tensor.Tensor")},
        {"T", dudu::named_type_ref("f32")},
    };
    const dudu::TypeRef substituted_shaped_tensor = dudu::substitute_type_ref(
        dudu::parse_type_text("Tensor[T][dyn, 2]"), module_type_substitutions);
    assert(dudu::type_ref_text(substituted_shaped_tensor) == "tensor.Tensor[f32][dyn, 2]");
    assert(dudu::substitute_type_ref_text(dudu::parse_type_text("Tensor[T][dyn, 2]"),
                                          {{"Tensor", "tensor.Tensor"}, {"T", "f32"}}) ==
           "tensor.Tensor[f32][dyn, 2]");
    const dudu::TypeRef shaped_box = dudu::parse_type_text("Box[list[i32]][3]");
    assert(shaped_box.kind == dudu::TypeKind::Shaped);
    assert(dudu::lower_cpp_type(shaped_box) == "Box<std::vector<int32_t>>");
    bool rejected_array_shorthand = false;
    try {
        const dudu::ModuleAst bad_array = dudu::parse_source("class Player:\n"
                                                             "    hp: i32\n"
                                                             "\n"
                                                             "def bad():\n"
                                                             "    players: Player[3][4]\n",
                                                             "bad_array_shorthand.dd");
        dudu::analyze_module(bad_array, {.check_bodies = true});
    } catch (const dudu::CompileError& error) {
        rejected_array_shorthand =
            std::string(error.what()).find("malformed type syntax") != std::string::npos;
    }
    assert(rejected_array_shorthand);
    assert(dudu::lower_raw_template_call_arg("fn(i32) -> bool", {}) == "bool(int32_t)");
    dudu::FunctionSignature signature;
    assert(dudu::parse_function_type(dudu::parse_type_text("fn(i32, f32) -> bool"), signature));
    assert(dudu::signature_param_count(signature) == 2);
    assert(dudu::signature_param_type_ref(signature, 0).name == "i32");
    assert(dudu::signature_param_type_ref(signature, 1).name == "f32");
    assert(dudu::signature_return_type_ref(signature).name == "bool");
    const dudu::TypeRef signature_ref = dudu::function_type_ref(signature);
    assert(signature_ref.kind == dudu::TypeKind::Function);
    assert(signature_ref.children.size() == 3);
    assert(signature_ref.children[0].name == "bool");
    assert(signature_ref.children[1].name == "i32");
    assert(signature_ref.children[2].name == "f32");
    assert(dudu::substitute_type_ref_text(signature_ref, {}) == "fn(i32, f32) -> bool");
    assert(dudu::parse_function_type(dudu::parse_type_text("fn(i32)"), signature));
    assert(dudu::signature_param_count(signature) == 1);
    assert(dudu::signature_param_type_ref(signature, 0).name == "i32");
    assert(dudu::signature_return_type_ref(signature).name == "void");
    assert(dudu::parse_function_type(dudu::parse_type_text("std.function[fn(i32) -> i32]"),
                                     signature));
    assert(dudu::signature_param_count(signature) == 1);
    assert(dudu::signature_param_type_ref(signature, 0).name == "i32");
    assert(dudu::signature_return_type_ref(signature).name == "i32");
    const dudu::TypeRef c_tag = dudu::parse_type_text("*struct sqlite3");
    assert(c_tag.kind == dudu::TypeKind::Pointer);
    assert(c_tag.children[0].kind == dudu::TypeKind::Named);
    assert(c_tag.children[0].name == "struct sqlite3");
    const dudu::TypeRef nested_callback =
        dudu::parse_type_text("fn(fn(i32) -> i32, fn(i32) -> i32) -> fn(i32) -> i32");
    assert(nested_callback.kind == dudu::TypeKind::Function);
    assert(nested_callback.children.size() == 3);
    assert(nested_callback.children[0].kind == dudu::TypeKind::Function);
    assert(nested_callback.children[1].kind == dudu::TypeKind::Function);
    assert(nested_callback.children[2].kind == dudu::TypeKind::Function);
    const dudu::TypeRef nested = dudu::substitute_type_ref(
        dudu::parse_type_text("fn(list[T]) -> T"), {{"T", dudu::named_type_ref("f32")}});
    assert(dudu::substitute_type_ref_text(nested, {}) == "fn(list[f32]) -> f32");
    dudu::TypeRef malformed_placeholder;
    malformed_placeholder.kind = dudu::TypeKind::Unknown;
    const dudu::TypeRef malformed_substituted =
        dudu::substitute_type_ref(malformed_placeholder, {{"T", dudu::named_type_ref("f32")}});
    assert(malformed_substituted.kind == dudu::TypeKind::Unknown);
    assert(!dudu::has_type_ref(malformed_substituted));
    assert(dudu::lower_cpp_type(player.fields[0].type_ref) ==
           "std::array<std::array<float, 4>, 4>");
    const dudu::ArrayShapeInference inferred_array = dudu::infer_array_literal_shape_type(
        dudu::parse_type_text("array[i32]"), dudu::parse_expr_text("[[1, 2], [3, 4]]"));
    assert(inferred_array.status == dudu::ArrayShapeStatus::Inferred);
    assert(dudu::substitute_type_ref_text(inferred_array.type_ref, {}) == "array[i32][2, 2]");
    assert(dudu::substitute_type_ref_text(inferred_array.element_type_ref, {}) == "i32");
    assert(inferred_array.type_ref.kind == dudu::TypeKind::FixedArray);
    assert(inferred_array.type_ref.children.size() == 3);
    assert(inferred_array.type_ref.children[0].kind == dudu::TypeKind::Template);
    assert(inferred_array.type_ref.children[0].name == "array");
    assert(inferred_array.type_ref.children[1].kind == dudu::TypeKind::Value);
    assert(inferred_array.type_ref.children[1].value == "2");
    assert(inferred_array.type_ref.children[2].kind == dudu::TypeKind::Value);
    assert(inferred_array.type_ref.children[2].value == "2");
    assert(dudu::lower_cpp_type(inferred_array.type_ref) ==
           "std::array<std::array<int32_t, 2>, 2>");
    assert(dudu::lower_cpp_type(dudu::parse_type_text("array[f32][4, 4]")) ==
           "std::array<std::array<float, 4>, 4>");
    assert(dudu::lower_cpp_type(dudu::parse_type_text("array[f32][2 * 2, 3 + 1]")) ==
           "std::array<std::array<float, 4>, 4>");
    assert(dudu::type_ref_equivalent(dudu::parse_type_text("array[f32][2 * 2, 3 + 1]"),
                                     dudu::parse_type_text("array[f32][4, 4]")));
    assert(dudu::type_ref_equivalent(dudu::parse_type_text("array[f32][4, 4]"),
                                     dudu::parse_type_text("array[f32][4,4]")));
    assert(dudu::type_ref_same_shape(dudu::parse_type_text("array[f32][4, 4]"),
                                     dudu::parse_type_text("array[f32][4,4]")));
    assert(dudu::lower_cpp_type_spelling("array[i32][3]") == "std::array<int32_t, 3>");
    assert(dudu::lower_cpp_type_spelling("array[f32][4, 4]") ==
           "std::array<std::array<float, 4>, 4>");
}

} // namespace

int main() {
    try {
        test_literal_ast_values();
        test_expression_ast_shape();
        test_cpp_escape_ast_payloads();
        test_dereference_postfix_expression_shape();
        test_decorator_expression_ast_shape();
        test_type_ast_shape();
    } catch (const std::exception& error) {
        std::cerr << error.what() << "\n";
        return 1;
    }
    return 0;
}
