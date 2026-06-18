#include "dudu/array_shape.hpp"
#include "dudu/ast_expr.hpp"
#include "dudu/ast_type.hpp"
#include "dudu/cpp_emit.hpp"
#include "dudu/cpp_lower.hpp"
#include "dudu/cpp_stmt_types.hpp"
#include "dudu/language_server_semantic_tokens.hpp"
#include "dudu/match_patterns.hpp"
#include "dudu/native_header_types.hpp"
#include "dudu/parser.hpp"
#include "dudu/sema.hpp"
#include "dudu/sema_builtin_methods.hpp"
#include "dudu/sema_context.hpp"
#include "dudu/sema_function_type.hpp"
#include "dudu/sema_inheritance.hpp"
#include "dudu/sema_method_templates.hpp"
#include "dudu/type_compat.hpp"

#include <cassert>
#include <cctype>
#include <exception>
#include <iostream>
#include <string>
#include <vector>

namespace {

std::vector<int> semantic_token_data(const std::string& json) {
    std::vector<int> out;
    for (size_t i = 0; i < json.size();) {
        if (std::isdigit(static_cast<unsigned char>(json[i])) == 0) {
            ++i;
            continue;
        }
        int value = 0;
        while (i < json.size() && std::isdigit(static_cast<unsigned char>(json[i])) != 0) {
            value = value * 10 + json[i] - '0';
            ++i;
        }
        out.push_back(value);
    }
    return out;
}

bool has_semantic_token(const std::vector<int>& data, int type, int modifier) {
    for (size_t i = 0; i + 4 < data.size(); i += 5) {
        if (data[i + 3] == type && (data[i + 4] & modifier) == modifier) {
            return true;
        }
    }
    return false;
}

void test_ast_assignment_display_types() {
    assert(dudu::assignment_error("bool", dudu::parse_expr_text("123"), "") ==
           "cannot assign number to bool without an explicit cast");
    assert(dudu::assignment_error("bool", dudu::parse_expr_text("\"hi\""), "") ==
           "cannot assign str to bool without an explicit cast");
    assert(dudu::assignment_error("bool", dudu::parse_expr_text("value"), "") ==
           "cannot assign  to bool without an explicit cast");

    dudu::Expr unknown;
    unknown.kind = dudu::ExprKind::Unknown;
    unknown.text = "123";
    assert(dudu::assignment_error("bool", unknown, "") ==
           "cannot assign  to bool without an explicit cast");
}

void test_missing_expression_is_not_unknown() {
    const dudu::Expr parsed_empty = dudu::parse_expr_text("");
    assert(parsed_empty.kind == dudu::ExprKind::Missing);
    assert(dudu::expr_missing(parsed_empty));

    dudu::Expr unknown;
    unknown.kind = dudu::ExprKind::Unknown;
    assert(!dudu::expr_missing(unknown));
}

void test_type_compat_uses_type_ast_for_pointers() {
    assert(dudu::type_assignment_allowed("*const[void]", "*i32"));
    assert(dudu::type_assignment_allowed("* const[void]", "*i32"));
    assert(dudu::type_assignment_allowed("*const[i32]", "*i32"));
    assert(dudu::type_assignment_allowed("*i32", "*&i32"));
    assert(dudu::type_assignment_allowed("&const[i32]", "i32"));
    assert(dudu::type_assignment_allowed("i32", "&const[i32]"));
    assert(dudu::type_assignment_allowed("fn(i32) -> void", "fn(i32)"));

    assert(dudu::type_assignment_allowed(dudu::parse_type_text("*const[void]"),
                                         dudu::parse_type_text("*list[i32]")));
    assert(dudu::type_assignment_allowed(dudu::parse_type_text("*const[list[i32]]"),
                                         dudu::parse_type_text("*list[i32]")));
    assert(dudu::type_assignment_allowed(dudu::parse_type_text("&const[list[i32]]"),
                                         dudu::parse_type_text("list[i32]")));
    assert(dudu::type_assignment_allowed(dudu::parse_type_text("list[fn(i32)]"),
                                         dudu::parse_type_text("list[fn(i32) -> void]")));
    assert(!dudu::type_assignment_allowed(dudu::parse_type_text("list[i32]"),
                                          dudu::parse_type_text("list[str]")));
    assert(dudu::type_assignment_allowed(dudu::parse_type_text("array[list[i32]][4]"),
                                         dudu::parse_type_text("array[list[i32]][4]")));
    assert(!dudu::type_assignment_allowed(dudu::parse_type_text("array[list[i32]][4]"),
                                          dudu::parse_type_text("array[list[str]][4]")));
    assert(!dudu::type_assignment_allowed(dudu::parse_type_text("array[list[i32]][4]"),
                                          dudu::parse_type_text("array[list[i32]][8]")));

    const dudu::Expr name_expr = dudu::parse_expr_text("value");
    assert(dudu::assignment_type_allowed(dudu::parse_type_text("array[list[i32]][4]"), name_expr,
                                         "array[list[i32]][4]"));
    assert(!dudu::assignment_type_allowed(dudu::parse_type_text("array[list[i32]][4]"), name_expr,
                                          "array[list[str]][4]"));
    assert(dudu::assignment_type_allowed(dudu::parse_type_text("std.unique_ptr[Node]"), name_expr,
                                         "__detail.__unique_ptr_t[Node]"));
    assert(!dudu::assignment_type_allowed(dudu::parse_type_text("f32"), name_expr, "__m128"));
}

void test_core_type_helpers_use_type_ast() {
    assert(dudu::base_type("*const[i32]") == "const");
    assert(dudu::base_type("&Player") == "Player");
    assert(dudu::base_type("array[f32][4, 4]") == "array");
    assert(dudu::base_type("fn(i32) -> bool") == "fn");
    assert(dudu::base_type("struct sqlite3") == "struct sqlite3");
    assert(dudu::substitute_type_ref_text(
               dudu::explicit_array_element_type_ref(dudu::parse_type_text("array[list[i32]][4]")),
               {}) == "list[i32]");

    const std::vector<dudu::TypeRef> tuple =
        dudu::template_type_arg_refs(dudu::parse_type_text("tuple[i32, list[str]]"), "tuple");
    assert(tuple.size() == 2);
    assert(dudu::substitute_type_ref_text(tuple[0], {}) == "i32");
    assert(dudu::substitute_type_ref_text(tuple[1], {}) == "list[str]");

    const std::vector<dudu::TypeRef> aliased_tuple = dudu::template_type_arg_refs_resolved(
        dudu::parse_type_text("Pair"), "tuple", {{"Pair", "tuple[i32, f32]"}});
    assert(aliased_tuple.size() == 2);
    assert(dudu::substitute_type_ref_text(aliased_tuple[0], {}) == "i32");
    assert(dudu::substitute_type_ref_text(aliased_tuple[1], {}) == "f32");

    assert(dudu::first_template_type_arg_text("list[*Player]") == "*Player");
    assert(dudu::first_template_type_arg_text("dict[str, list[i32]]") == "str");
    assert(!dudu::first_template_type_arg_text("*Player"));

    const dudu::TypeRef cpp_vector = dudu::parse_type_text("std::vector<std::string>");
    assert(cpp_vector.kind == dudu::TypeKind::Template);
    assert(cpp_vector.name == "std::vector");
    assert(cpp_vector.children.size() == 1);
    assert(dudu::substitute_type_ref_text(cpp_vector.children[0], {}) == "std::string");

    const dudu::TypeRef cpp_atomic = dudu::parse_type_text("std::atomic<uint64_t>");
    assert(cpp_atomic.kind == dudu::TypeKind::Template);
    assert(cpp_atomic.name == "std::atomic");
    assert(cpp_atomic.children.size() == 1);
    assert(dudu::substitute_type_ref_text(cpp_atomic.children[0], {}) == "uint64_t");
}

void test_builtin_method_signature_uses_type_ast() {
    dudu::Symbols symbols;
    dudu::FunctionSignature signature;
    assert(dudu::builtin_cpp_method_signature(symbols, "list[*Player]", "append", signature));
    assert(signature.params.size() == 1);
    assert(signature.params[0] == "*Player");
    assert(signature.return_type == "void");

    signature = {};
    assert(dudu::builtin_cpp_method_signature(symbols, "std::vector<std::string>", "push_back",
                                              signature));
    assert(signature.params.size() == 1);
    assert(signature.params[0] == "std::string");
    assert(signature.return_type == "void");

    signature = {};
    assert(dudu::builtin_cpp_method_signature(symbols, "std::atomic<uint64_t>", "load", signature));
    assert(signature.return_type == "uint64_t");
}

void test_native_header_types_split_cpp_templates() {
    assert(dudu::dudu_type("const vec<L, T, Q> &") == "&const[vec[L, T, Q]]");
    assert(dudu::signature_params("T (const vec<L, T, Q> &, const vec<L, T, Q> &)") ==
           std::vector<std::string>({"&const[vec[L, T, Q]]", "&const[vec[L, T, Q]]"}));
}

void test_receiver_template_substitution_uses_type_ast() {
    assert(dudu::substitute_receiver_template_type("list[value_type]", {"i32"}) == "list[i32]");
    assert(dudu::substitute_receiver_template_type("fn(value_type) -> element_type", {"f32"}) ==
           "fn(f32) -> f32");
    assert(dudu::substitute_receiver_template_type("std::vector<value_type>", {"i32"}) ==
           "std::vector<i32>");

    const dudu::TypeRef vector_type = dudu::parse_type_text("std::vector<value_type>");
    const dudu::TypeRef vector_substituted =
        dudu::substitute_receiver_template_type(vector_type, {"i32"});
    assert(dudu::substitute_type_ref_text(vector_substituted, {}) == "std::vector<i32>");

    const std::vector<std::string> receiver_args =
        dudu::template_args_from_type(dudu::parse_type_text("dict[str, i32]"));
    assert(receiver_args == std::vector<std::string>({"str", "i32"}));
}

void test_inherited_method_signature_uses_type_ast() {
    dudu::ClassDecl owner;
    owner.name = "Box";
    owner.generic_params = {"T"};

    dudu::FunctionDecl method;
    method.name = "replace";
    method.params.push_back({"self", dudu::parse_type_text("Box[T]"), {}});
    method.params.push_back({"value", dudu::parse_type_text("T"), {}});
    method.return_type_ref = dudu::parse_type_text("T");

    const dudu::FunctionSignature signature =
        dudu::inherited_method_signature_for_type(owner, dudu::parse_type_text("Box[i32]"), method);
    assert(signature.params == std::vector<std::string>({"i32"}));
    assert(signature.param_type_refs.size() == 1);
    assert(dudu::substitute_type_ref_text(signature.param_type_refs[0], {}) == "i32");
    assert(signature.return_type == "i32");
    assert(dudu::substitute_type_ref_text(signature.return_type_ref, {}) == "i32");
}

void test_native_semantic_tokens() {
    dudu::ModuleAst module =
        dudu::parse_source("import c \"native.h\"\n"
                           "\n"
                           "def main() -> i32:\n"
                           "    event: DuduNativeEvent\n"
                           "    if DUDU_NATIVE_CHECK():\n"
                           "        return dudu_native_add(DUDU_NATIVE_MAGIC, event.type)\n"
                           "    return 0\n",
                           "native_semantic_tokens.dd");
    dudu::ModuleAst native_symbols = module;
    native_symbols.native_types.push_back(
        {.name = "DuduNativeEvent", .type = "DuduNativeEvent", .location = {}});
    native_symbols.native_values.push_back(
        {.name = "DUDU_NATIVE_MAGIC", .type = "i32", .location = {}});
    native_symbols.native_functions.push_back(
        {.name = "dudu_native_add",
         .template_params = {},
         .params = {"i32", "i32"},
         .param_type_refs = {dudu::parse_type_text("i32"), dudu::parse_type_text("i32")},
         .return_type = "i32",
         .return_type_ref = dudu::parse_type_text("i32"),
         .location = {}});
    native_symbols.native_macros.push_back(
        {.name = "DUDU_NATIVE_CHECK", .arity = 0, .function_like = true, .location = {}});

    const std::vector<int> data =
        semantic_token_data(dudu::semantic_tokens_json(module, native_symbols));
    constexpr int native_modifier = 16;
    assert(has_semantic_token(data, 1, native_modifier));
    assert(has_semantic_token(data, 4, native_modifier));
    assert(has_semantic_token(data, 6, native_modifier | 4));
    assert(has_semantic_token(data, 10, native_modifier));
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

void test_ast_index_receiver_type_inference() {
    const dudu::ModuleAst module = dudu::parse_source("def make_values() -> list[i32]:\n"
                                                      "    return [1, 2]\n"
                                                      "\n"
                                                      "def make_matrix() -> array[i32][2, 2]:\n"
                                                      "    matrix: array[i32] = [[1, 2], [3, 4]]\n"
                                                      "    return matrix\n"
                                                      "\n"
                                                      "def main() -> i32:\n"
                                                      "    first: i32 = make_values()[0]\n"
                                                      "    second: i32 = make_matrix()[1][0]\n"
                                                      "    return first + second\n",
                                                      "ast_index_receiver.dd");
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
    assert(dudu::substitute_type_ref_text(main.statements[0].type_ref, {}) == "i32");
    assert(main.statements[0].value_expr.text == "0");
    assert(main.statements[1].kind == dudu::StmtKind::For);
    assert(main.statements[1].name == "item");
    assert(dudu::substitute_type_ref_text(main.statements[1].type_ref, {}) == "i32");
    assert(main.statements[1].iterable_expr.text == "values");
    assert(main.statements[1].children.size() == 1);
    assert(main.statements[1].children[0].kind == dudu::StmtKind::CompoundAssign);
    assert(main.statements[1].children[0].target_expr.text == "total");
    assert(main.statements[1].children[0].compound_op == dudu::CompoundAssignOp::Add);
    assert(main.statements[1].children[0].value_expr.text == "item");
    assert(main.statements[2].kind == dudu::StmtKind::If);
    assert(main.statements[2].condition_expr.text == "total == 0");
    assert(main.statements[2].children.size() == 1);
    assert(main.statements[2].children[0].kind == dudu::StmtKind::CompoundAssign);
    assert(main.statements[3].kind == dudu::StmtKind::Else);
    assert(main.statements[3].children.size() == 1);
    assert(main.statements[3].children[0].kind == dudu::StmtKind::Assign);
    assert(main.statements[3].children[0].target_expr.text == "total");
    assert(main.statements[3].children[0].value_expr.text == "1");
    assert(main.statements[4].kind == dudu::StmtKind::Return);
    assert(main.statements[4].value_expr.text == "total");
}

void test_var_decl_name_must_be_identifier() {
    bool rejected = false;
    try {
        (void)dudu::parse_source("def main() -> i32:\n"
                                 "    player.hp: i32 = 1\n"
                                 "    return 0\n",
                                 "invalid_decl_name.dd");
    } catch (const dudu::CompileError& error) {
        assert(error.location().line == 2);
        assert(std::string(error.what()).find("expected : after declaration name") !=
               std::string::npos);
        rejected = true;
    }
    assert(rejected);
}

void test_except_binding_name_must_be_identifier() {
    const dudu::ModuleAst module = dudu::parse_source("def main() -> i32:\n"
                                                      "    try:\n"
                                                      "        return 1\n"
                                                      "    except err: Error:\n"
                                                      "        return 0\n",
                                                      "except_binding.dd");
    const dudu::Stmt& except = module.functions.front().statements[1];
    assert(except.kind == dudu::StmtKind::Except);
    assert(except.name == "err");
    assert(dudu::type_ref_text(except.type_ref) == "Error");

    bool rejected = false;
    try {
        (void)dudu::parse_source("def main() -> i32:\n"
                                 "    try:\n"
                                 "        return 1\n"
                                 "    except err.value: Error:\n"
                                 "        return 0\n",
                                 "invalid_except_binding.dd");
    } catch (const dudu::CompileError& error) {
        assert(error.location().line == 4);
        assert(std::string(error.what()).find("expected : after declaration name") !=
               std::string::npos);
        rejected = true;
    }
    assert(rejected);
}

void test_unsupported_statement_ast_shape() {
    const dudu::ModuleAst module = dudu::parse_source("def main() -> i32:\n"
                                                      "    with open(\"data\"):\n"
                                                      "        return 0\n",
                                                      "unsupported_statement_shape.dd");
    assert(module.functions.size() == 1);
    const dudu::FunctionDecl& main = module.functions.front();
    assert(main.statements.size() == 1);
    assert(main.statements[0].kind == dudu::StmtKind::Unsupported);
    assert(main.statements[0].unsupported_feature == dudu::UnsupportedFeature::ContextManagers);
}

void test_unsupported_def_expression_ast_shape() {
    const dudu::ModuleAst module = dudu::parse_source("def main() -> i32:\n"
                                                      "    return def local(): 1\n",
                                                      "unsupported_def_expression_shape.dd");
    assert(module.functions.size() == 1);
    const dudu::FunctionDecl& main = module.functions.front();
    assert(main.statements.size() == 1);
    assert(main.statements[0].kind == dudu::StmtKind::Return);
    assert(main.statements[0].value_expr.kind == dudu::ExprKind::DefExpression);
}

void test_unsupported_comprehension_ast_shape() {
    const dudu::ModuleAst module = dudu::parse_source("def main() -> i32:\n"
                                                      "    values = [x for x in items]\n"
                                                      "    names = {x: x for x in items}\n",
                                                      "unsupported_comprehension_shape.dd");
    assert(module.functions.size() == 1);
    const dudu::FunctionDecl& main = module.functions.front();
    assert(main.statements.size() == 2);
    assert(main.statements[0].kind == dudu::StmtKind::Assign);
    assert(main.statements[0].value_expr.kind == dudu::ExprKind::Comprehension);
    assert(main.statements[1].kind == dudu::StmtKind::Assign);
    assert(main.statements[1].value_expr.kind == dudu::ExprKind::Comprehension);
}

void test_unsupported_dynamic_call_ast_shape() {
    const dudu::ModuleAst module = dudu::parse_source("def main() -> i32:\n"
                                                      "    value = eval(\"1\")\n"
                                                      "    exec(\"value = 1\")\n"
                                                      "    return getattr(value, \"x\")\n",
                                                      "unsupported_dynamic_call_shape.dd");
    assert(module.functions.size() == 1);
    const dudu::FunctionDecl& main = module.functions.front();
    assert(main.statements.size() == 3);
    assert(main.statements[0].value_expr.kind == dudu::ExprKind::Call);
    assert(dudu::direct_callee_name(main.statements[0].value_expr) == "eval");
    assert(main.statements[1].expr.kind == dudu::ExprKind::Call);
    assert(dudu::direct_callee_name(main.statements[1].expr) == "exec");
    assert(main.statements[2].value_expr.kind == dudu::ExprKind::Call);
    assert(dudu::direct_callee_name(main.statements[2].value_expr) == "getattr");
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
    assert(assign.value_expr.name.empty());
    assert(dudu::direct_callee_name(assign.value_expr) == "Vec4");
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
    assert(value.value_expr.text == "cpp(\"19\")");
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
    assert(dudu::direct_callee_name(cast) == "*struct State");

    const dudu::Expr template_cast = dudu::parse_expr_text("*list[MissingType](ptr)");
    assert(template_cast.kind == dudu::ExprKind::TemplateCall);
    assert(template_cast.name.empty());
    assert(dudu::direct_callee_name(template_cast) == "*list");
    assert(template_cast.template_type_args.size() == 1);

    const dudu::Expr qualified_template_cast = dudu::parse_expr_text("*std.vector[i32](raw_data)");
    assert(qualified_template_cast.kind == dudu::ExprKind::TemplateCall);
    assert(dudu::direct_callee_name(qualified_template_cast) == "*std.vector");
    assert(qualified_template_cast.template_args.size() == 1);
    assert(qualified_template_cast.template_args[0].text == "i32");
    assert(qualified_template_cast.template_type_args.size() == 1);
    assert(qualified_template_cast.template_type_args[0].text == "i32");
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
    assert(!panic.callee.empty());
    assert(panic.callee.front().kind == dudu::ExprKind::Member);
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
    assert(dudu::lower_cpp_type(dudu::parse_type_text("*const[i32]")) == "const int32_t*");
    assert(dudu::lower_cpp_type(dudu::parse_type_text("const[*i32]")) == "int32_t* const");
    assert(dudu::lower_cpp_type(dudu::parse_type_text("const[*const[i32]]")) ==
           "const int32_t* const");
    assert(dudu::lower_cpp_type(dudu::parse_type_text("&const[Player]")) == "const Player&");
    assert(dudu::lower_cpp_type(dudu::parse_type_text("const[&Player]")) == "Player&");
    assert(dudu::lower_cpp_type("*const[i32]") == "const int32_t*");
    assert(dudu::lower_cpp_type("const[*i32]") == "int32_t* const");
    assert(dudu::lower_cpp_type(dudu::parse_type_text("fn(i32, f32) -> bool")) ==
           "std::add_pointer_t<bool(int32_t, float)>");
    assert(dudu::lower_cpp_type(dudu::parse_type_text("fn(i32)")) ==
           "std::add_pointer_t<void(int32_t)>");
    dudu::FunctionSignature signature;
    assert(dudu::parse_function_type(dudu::parse_type_text("fn(i32, f32) -> bool"), signature));
    assert(signature.params.size() == 2);
    assert(signature.param_type_refs.size() == 2);
    assert(signature.params[0] == "i32");
    assert(signature.param_type_refs[0].name == "i32");
    assert(signature.params[1] == "f32");
    assert(signature.param_type_refs[1].name == "f32");
    assert(signature.return_type == "bool");
    assert(signature.return_type_ref.name == "bool");
    const dudu::TypeRef signature_ref = dudu::function_type_ref(signature);
    assert(signature_ref.kind == dudu::TypeKind::Function);
    assert(signature_ref.children.size() == 3);
    assert(signature_ref.children[0].name == "bool");
    assert(signature_ref.children[1].name == "i32");
    assert(signature_ref.children[2].name == "f32");
    assert(dudu::substitute_type_ref_text(signature_ref, {}) == "fn(i32, f32) -> bool");
    assert(dudu::parse_function_type(dudu::parse_type_text("fn(i32)"), signature));
    assert(signature.params.size() == 1);
    assert(signature.params[0] == "i32");
    assert(signature.return_type == "void");
    assert(signature.return_type_ref.name == "void");
    assert(dudu::parse_function_type(dudu::parse_type_text("std.function[fn(i32) -> i32]"),
                                     signature));
    assert(signature.params.size() == 1);
    assert(signature.params[0] == "i32");
    assert(signature.return_type == "i32");
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
    const dudu::TypeRef nested =
        dudu::substitute_type_ref(dudu::parse_type_text("fn(list[T]) -> T"), {{"T", "f32"}});
    assert(dudu::substitute_type_ref_text(nested, {}) == "fn(list[f32]) -> f32");
    assert(dudu::lower_cpp_type(player.fields[0].type_ref) ==
           "std::array<std::array<float, 4>, 4>");
    const dudu::ArrayShapeInference inferred_array = dudu::infer_array_literal_shape_type(
        dudu::parse_type_text("array[i32]"), dudu::parse_expr_text("[[1, 2], [3, 4]]"));
    assert(inferred_array.status == dudu::ArrayShapeStatus::Inferred);
    assert(inferred_array.type == "array[i32][2, 2]");
    assert(inferred_array.type_ref.kind == dudu::TypeKind::FixedArray);
    assert(inferred_array.type_ref.children.size() == 1);
    assert(inferred_array.type_ref.children[0].kind == dudu::TypeKind::Template);
    assert(inferred_array.type_ref.children[0].name == "array");
    assert(dudu::lower_cpp_type(inferred_array.type_ref) ==
           "std::array<std::array<int32_t, 2>, 2>");
    assert(dudu::lower_cpp_type("array[i32][3]") == "std::array<int32_t, 3>");
    assert(dudu::lower_cpp_type("array[f32][4, 4]") == "std::array<std::array<float, 4>, 4>");
}

void test_generic_decl_ast_shape() {
    const dudu::ModuleAst module = dudu::parse_source("class Box[T]:\n"
                                                      "    value: T\n"
                                                      "\n"
                                                      "    def get(self) -> T:\n"
                                                      "        return self.value\n"
                                                      "\n"
                                                      "def id[T](value: T) -> T:\n"
                                                      "    return value\n",
                                                      "generic_decl_shape.dd");
    assert(module.classes.size() == 1);
    assert(module.classes[0].name == "Box");
    assert(module.classes[0].generic_params == std::vector<std::string>{"T"});
    assert(module.classes[0].methods.size() == 1);
    assert(dudu::type_ref_text(module.classes[0].methods[0].return_type_ref) == "T");
    assert(module.functions.size() == 1);
    assert(module.functions[0].name == "id");
    assert(module.functions[0].generic_params == std::vector<std::string>{"T"});
}

void test_payload_enum_ast_shape() {
    const dudu::ModuleAst module = dudu::parse_source("enum Message:\n"
                                                      "    Quit\n"
                                                      "\n"
                                                      "    Move:\n"
                                                      "        x: i32\n"
                                                      "        y: i32\n"
                                                      "\n"
                                                      "    Write(str)\n",
                                                      "payload_enum_shape.dd");
    assert(module.enums.size() == 1);
    const dudu::EnumDecl& message = module.enums[0];
    assert(message.values.size() == 3);
    assert(message.values[0].name == "Quit");
    assert(message.values[0].payload_fields.empty());
    assert(message.values[1].name == "Move");
    assert(!message.values[1].tuple_payload);
    assert(message.values[1].payload_fields.size() == 2);
    assert(message.values[1].payload_fields[0].name == "x");
    assert(message.values[1].payload_fields[0].type_ref.kind == dudu::TypeKind::Named);
    assert(message.values[2].name == "Write");
    assert(message.values[2].tuple_payload);
    assert(message.values[2].payload_fields.size() == 1);
    assert(message.values[2].payload_fields[0].name == "_0");
    assert(dudu::type_ref_text(message.values[2].payload_fields[0].type_ref) == "str");
}

void test_match_case_ast_shape() {
    const dudu::ModuleAst module = dudu::parse_source("def handle(msg: Message) -> i32:\n"
                                                      "    match msg:\n"
                                                      "        case Message.Quit:\n"
                                                      "            return 0\n"
                                                      "        case Message.Move(x, y) if x > 0:\n"
                                                      "            return y\n"
                                                      "        case _:\n"
                                                      "            return 1\n",
                                                      "match_case_shape.dd");
    assert(module.functions.size() == 1);
    const dudu::FunctionDecl& handle = module.functions.front();
    assert(handle.statements.size() == 1);
    const dudu::Stmt& match = handle.statements[0];
    assert(match.kind == dudu::StmtKind::Match);
    assert(match.condition_expr.text == "msg");
    assert(match.condition_expr.kind == dudu::ExprKind::Name);
    assert(match.children.size() == 3);

    const dudu::Stmt& quit = match.children[0];
    assert(quit.kind == dudu::StmtKind::Case);
    assert(quit.pattern_expr.text == "Message.Quit");
    assert(quit.pattern_expr.kind == dudu::ExprKind::Member);
    assert(quit.children.size() == 1);
    assert(quit.children[0].kind == dudu::StmtKind::Return);

    const dudu::Stmt& move = match.children[1];
    assert(move.kind == dudu::StmtKind::Case);
    assert(move.pattern_expr.text == "Message.Move(x, y)");
    assert(move.guard_expr.text == "x > 0");
    assert(move.pattern_expr.kind == dudu::ExprKind::Call);
    assert(move.guard_expr.kind == dudu::ExprKind::Binary);
    assert(move.guard_expr.op == ">");

    const dudu::Stmt& wildcard = match.children[2];
    assert(wildcard.kind == dudu::StmtKind::Case);
    assert(wildcard.pattern_expr.text == "_");
    assert(wildcard.guard_expr.kind == dudu::ExprKind::Missing);
    assert(wildcard.pattern_expr.kind == dudu::ExprKind::Name);
}

void test_wrapper_match_type_uses_type_ast() {
    const dudu::WrapperMatchType result =
        dudu::wrapper_match_type(dudu::parse_type_text("Result[list[i32], Option[str]]"));
    assert(result.kind == dudu::WrapperMatchKind::Result);
    assert(result.args.size() == 2);
    assert(result.args[0] == "list[i32]");
    assert(result.args[1] == "Option[str]");

    const dudu::WrapperMatchType option =
        dudu::wrapper_match_type(dudu::parse_type_text("Option[Result[i32, str]]"));
    assert(option.kind == dudu::WrapperMatchKind::Option);
    assert(option.args.size() == 1);
    assert(option.args[0] == "Result[i32, str]");
}

} // namespace

int main() {
    try {
        test_ast_assignment_display_types();
        test_missing_expression_is_not_unknown();
        test_type_compat_uses_type_ast_for_pointers();
        test_core_type_helpers_use_type_ast();
        test_builtin_method_signature_uses_type_ast();
        test_native_header_types_split_cpp_templates();
        test_receiver_template_substitution_uses_type_ast();
        test_inherited_method_signature_uses_type_ast();
        test_native_semantic_tokens();
        test_ast_constructor_assignment_compatibility();
        test_ast_index_receiver_type_inference();
        test_statement_ast_shape();
        test_var_decl_name_must_be_identifier();
        test_except_binding_name_must_be_identifier();
        test_unsupported_statement_ast_shape();
        test_unsupported_def_expression_ast_shape();
        test_unsupported_comprehension_ast_shape();
        test_unsupported_dynamic_call_ast_shape();
        test_expression_ast_shape();
        test_cpp_escape_ast_payloads();
        test_dereference_postfix_expression_shape();
        test_decorator_expression_ast_shape();
        test_type_ast_shape();
        test_generic_decl_ast_shape();
        test_payload_enum_ast_shape();
        test_match_case_ast_shape();
        test_wrapper_match_type_uses_type_ast();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
