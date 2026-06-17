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
    assert(assign.source_text == "value   =    add(1, 2)");
    assert(assign.target_expr.text == "value");
    assert(assign.value_expr.text == "add(1, 2)");
    assert(assign.range.start.line == 2);
    assert(assign.range.start.column == 5);
    assert(assign.range.end.line == 2);
    assert(assign.range.end.column == 27);
    assert(assign.target_expr.range.start.column == 5);
    assert(assign.value_expr.range.start.column == 18);
}

void test_digit_suffixed_member_receiver() {
    const dudu::ModuleAst module = dudu::parse_source("def main() -> i32:\n"
                                                      "    sdl3.InitForSDLRenderer()\n"
                                                      "    return 0\n",
                                                      "digit_member.dd");
    const dudu::Expr& expr = module.functions.front().statements.front().expr;
    assert(expr.kind == dudu::ExprKind::Call);
    assert(!expr.callee.empty());
    assert(expr.callee.front().kind == dudu::ExprKind::Member);
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
    assert(branch.condition_expr.text == "ready(value)");
    assert(branch.source_text == "if   ready(value):");
    assert(branch.condition_expr.kind == dudu::ExprKind::Call);
    assert(branch.condition_expr.range.start.column == 10);
    assert(branch.children.front().kind == dudu::StmtKind::Return);
    assert(branch.children.front().value_expr.range.start.column == 16);

    const dudu::Stmt& loop = main.statements[1];
    assert(loop.kind == dudu::StmtKind::For);
    assert(loop.name == "item");
    assert(loop.type == "i32");
    assert(loop.iterable_expr.text == "values");
    assert(loop.iterable_expr.range.start.column == 22);
    assert(loop.children.front().kind == dudu::StmtKind::DebugAssert);
    assert(loop.children.front().condition_expr.text == "item > 0");
    assert(loop.children.front().message_expr.text == "\"positive\"");
    const dudu::Stmt& call_assert = loop.children[1];
    assert(call_assert.kind == dudu::StmtKind::DebugAssert);
    assert(call_assert.condition_expr.kind == dudu::ExprKind::Call);
    assert(call_assert.condition_expr.children.size() == 3);
    assert(call_assert.message_expr.text == "\"in range\"");
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
