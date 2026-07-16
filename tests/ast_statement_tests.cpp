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
#include "dudu/lsp/language_server_signature_help.hpp"
#include "dudu/native/native_header_identity.hpp"
#include "dudu/native/native_header_types.hpp"
#include "dudu/native/native_signature_match.hpp"
#include "dudu/native/native_signature_substitution.hpp"
#include "dudu/native/native_signature_templates.hpp"
#include "dudu/parser/parser.hpp"
#include "dudu/project/project_index.hpp"
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
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace {

void test_ast_constructor_assignment_aliases() {
    const dudu::ModuleAst module = dudu::parse_source("type Scores = dict[str, i32]\n"
                                                      "type Values = list[i32]\n"
                                                      "\n"
                                                      "class Bag:\n"
                                                      "    names: Scores\n"
                                                      "    values: Values\n"
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
    assert(dudu::has_stmt_type_ref(main.statements[0]));
    assert(dudu::substitute_type_ref_text(dudu::stmt_type_ref(main.statements[0]), {}) == "i32");
    assert(main.statements[0].value_expr.kind == dudu::ExprKind::IntLiteral);
    assert(main.statements[0].value_expr.value == "0");
    assert(main.statements[1].kind == dudu::StmtKind::For);
    assert(main.statements[1].name == "item");
    assert(dudu::has_stmt_type_ref(main.statements[1]));
    assert(dudu::substitute_type_ref_text(dudu::stmt_type_ref(main.statements[1]), {}) == "i32");
    assert(dudu::stmt_iterable_expr(main.statements[1]).kind == dudu::ExprKind::Name);
    assert(dudu::stmt_iterable_expr(main.statements[1]).name == "values");
    assert(main.statements[1].children.size() == 1);
    assert(main.statements[1].children[0].kind == dudu::StmtKind::CompoundAssign);
    assert(dudu::stmt_target_expr(main.statements[1].children[0]).kind == dudu::ExprKind::Name);
    assert(dudu::stmt_target_expr(main.statements[1].children[0]).name == "total");
    assert(main.statements[1].children[0].compound_op == dudu::CompoundAssignOp::Add);
    assert(main.statements[1].children[0].value_expr.kind == dudu::ExprKind::Name);
    assert(main.statements[1].children[0].value_expr.name == "item");
    assert(main.statements[2].kind == dudu::StmtKind::If);
    assert(dudu::stmt_condition_expr(main.statements[2]).kind == dudu::ExprKind::Binary);
    assert(dudu::stmt_condition_expr(main.statements[2]).op == "==");
    assert(dudu::stmt_condition_expr(main.statements[2]).children.size() == 2);
    assert(dudu::stmt_condition_expr(main.statements[2]).children[0].kind == dudu::ExprKind::Name);
    assert(dudu::stmt_condition_expr(main.statements[2]).children[0].name == "total");
    assert(dudu::stmt_condition_expr(main.statements[2]).children[1].kind ==
           dudu::ExprKind::IntLiteral);
    assert(dudu::stmt_condition_expr(main.statements[2]).children[1].value == "0");
    assert(main.statements[2].children.size() == 1);
    assert(main.statements[2].children[0].kind == dudu::StmtKind::CompoundAssign);
    assert(main.statements[3].kind == dudu::StmtKind::Else);
    assert(main.statements[3].children.size() == 1);
    assert(main.statements[3].children[0].kind == dudu::StmtKind::Assign);
    assert(dudu::stmt_target_expr(main.statements[3].children[0]).kind == dudu::ExprKind::Name);
    assert(dudu::stmt_target_expr(main.statements[3].children[0]).name == "total");
    assert(main.statements[3].children[0].value_expr.kind == dudu::ExprKind::IntLiteral);
    assert(main.statements[3].children[0].value_expr.value == "1");
    assert(main.statements[4].kind == dudu::StmtKind::Return);
    assert(main.statements[4].value_expr.kind == dudu::ExprKind::Name);
    assert(main.statements[4].value_expr.name == "total");
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
    assert(dudu::has_stmt_type_ref(except));
    assert(dudu::type_ref_text(dudu::stmt_type_ref(except)) == "Error");

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

void test_unsupported_python_diagnostics_include_guidance() {
    struct Case {
        std::string source;
        std::string expected;
    };
    const std::vector<Case> cases = {
        {"def main():\n"
         "    with open(\"data\"):\n"
         "        pass\n",
         "rely on RAII object lifetime"},
        {"def main():\n"
         "    def helper():\n"
         "        pass\n",
         "move the function to top-level or class scope"},
        {"def main():\n"
         "    values = [x for x in items]\n",
         "use an explicit loop"},
        {"def main():\n"
         "    return getattr(value, \"x\")\n",
         "use statically known fields or methods"},
        {"def main():\n"
         "    yield 1\n",
         "explicit iterator/state type or callback"},
    };
    for (const Case& item : cases) {
        bool rejected = false;
        try {
            const dudu::ModuleAst module =
                dudu::parse_source(item.source, "unsupported_guidance.dd");
            dudu::analyze_module(module, {.check_bodies = true});
        } catch (const dudu::CompileError& error) {
            rejected = std::string(error.what()).find(item.expected) != std::string::npos;
        }
        assert(rejected);
    }
}

void test_malformed_static_field_type_is_rejected() {
    bool threw = false;
    try {
        (void)dudu::parse_source("class Counter:\n"
                                 "    count: static[] = 0\n",
                                 "bad_static_type.dd");
    } catch (const dudu::CompileError& error) {
        threw = std::string(error.what()).find("malformed static field type") != std::string::npos;
    }
    assert(threw);
}

void test_malformed_declaration_type_syntax_is_rejected() {
    bool threw = false;
    try {
        (void)dudu::parse_source("def bad_type() -> i32:\n"
                                 "    value: * = 1\n"
                                 "    return value\n",
                                 "bad_decl_type_syntax.dd");
    } catch (const dudu::CompileError& error) {
        threw = std::string(error.what()).find("malformed type syntax") != std::string::npos;
    }
    assert(threw);
}

} // namespace

int main() {
    try {
        test_ast_constructor_assignment_aliases();
        test_ast_index_receiver_type_inference();
        test_statement_ast_shape();
        test_var_decl_name_must_be_identifier();
        test_except_binding_name_must_be_identifier();
        test_unsupported_statement_ast_shape();
        test_unsupported_def_expression_ast_shape();
        test_unsupported_comprehension_ast_shape();
        test_unsupported_dynamic_call_ast_shape();
        test_unsupported_python_diagnostics_include_guidance();
        test_malformed_static_field_type_is_rejected();
        test_malformed_declaration_type_syntax_is_rejected();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
