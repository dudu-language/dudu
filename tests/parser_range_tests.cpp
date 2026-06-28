#include "dudu/ast_expr.hpp"
#include "dudu/ast_type.hpp"
#include "dudu/parser.hpp"

#include <cassert>
#include <exception>
#include <iostream>

namespace {

void test_statement_source_range_uses_token_span() {
    const dudu::ModuleAst module = dudu::parse_source("def main() -> i32:\n"
                                                      "    value   =    add(1, 2)\n"
                                                      "    return value\n",
                                                      "statement_range.dd");
    const dudu::FunctionDecl& main = module.functions.front();
    const dudu::Stmt& assign = main.statements.front();
    assert(dudu::stmt_target_expr(assign).kind == dudu::ExprKind::Name);
    assert(dudu::stmt_target_expr(assign).name == "value");
    assert(assign.value_expr.kind == dudu::ExprKind::Call);
    assert(dudu::display_expr(assign.value_expr) == "add(1, 2)");
    assert(assign.range.start.line == 2);
    assert(assign.range.start.column == 5);
    assert(assign.range.end.line == 2);
    assert(assign.range.end.column == 27);
    assert(dudu::stmt_target_expr(assign).range.start.column == 5);
    assert(assign.value_expr.range.start.column == 18);
}

void test_digit_suffixed_member_receiver() {
    const dudu::ModuleAst module = dudu::parse_source("def main() -> i32:\n"
                                                      "    sdl3.InitForSDLRenderer()\n"
                                                      "    return 0\n",
                                                      "digit_member.dd");
    const dudu::Expr& expr = module.functions.front().statements.front().expr;
    assert(expr.kind == dudu::ExprKind::Call);
    assert(dudu::has_expr_callee(expr));
    assert(dudu::expr_callee(expr).front().kind == dudu::ExprKind::Member);
}

void test_keyword_statements_keep_token_ranges() {
    const dudu::ModuleAst module =
        dudu::parse_source("def main() -> i32:\n"
                           "    if   ready(value):\n"
                           "        return 1\n"
                           "    for item: i32 in values:\n"
                           "        debug_assert item > 0, \"positive\"\n"
                           "        debug_assert between(item, 0, 10), \"in range\"\n"
                           "    return 0\n",
                           "keyword_statement_ranges.dd");
    const dudu::FunctionDecl& main = module.functions.front();
    assert(main.statements.size() == 3);

    const dudu::Stmt& branch = main.statements[0];
    assert(branch.kind == dudu::StmtKind::If);
    assert(dudu::display_expr(dudu::stmt_condition_expr(branch)) == "ready(value)");
    assert(branch.range.start.column == 5);
    assert(branch.range.end.column == 23);
    assert(dudu::stmt_condition_expr(branch).kind == dudu::ExprKind::Call);
    assert(dudu::stmt_condition_expr(branch).range.start.column == 10);
    assert(branch.children.front().kind == dudu::StmtKind::Return);
    assert(branch.children.front().value_expr.range.start.column == 16);

    const dudu::Stmt& loop = main.statements[1];
    assert(loop.kind == dudu::StmtKind::For);
    assert(loop.name == "item");
    assert(dudu::has_stmt_type_ref(loop));
    assert(dudu::substitute_type_ref_text(dudu::stmt_type_ref(loop), {}) == "i32");
    assert(dudu::stmt_iterable_expr(loop).kind == dudu::ExprKind::Name);
    assert(dudu::stmt_iterable_expr(loop).name == "values");
    assert(dudu::stmt_iterable_expr(loop).range.start.column == 22);
    assert(loop.children.front().kind == dudu::StmtKind::DebugAssert);
    assert(dudu::display_expr(dudu::stmt_condition_expr(loop.children.front())) == "item > 0");
    assert(dudu::stmt_message_expr(loop.children.front()).kind == dudu::ExprKind::StringLiteral);
    assert(dudu::stmt_message_expr(loop.children.front()).value == "positive");
    const dudu::Stmt& call_assert = loop.children[1];
    assert(call_assert.kind == dudu::StmtKind::DebugAssert);
    assert(dudu::stmt_condition_expr(call_assert).kind == dudu::ExprKind::Call);
    assert(dudu::stmt_condition_expr(call_assert).children.size() == 3);
    assert(dudu::stmt_message_expr(call_assert).kind == dudu::ExprKind::StringLiteral);
    assert(dudu::stmt_message_expr(call_assert).value == "in range");
}

} // namespace

int main() {
    try {
        test_statement_source_range_uses_token_span();
        test_digit_suffixed_member_receiver();
        test_keyword_statements_keep_token_ranges();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
