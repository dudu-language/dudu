#include "dudu/core/ast_expr.hpp"
#include "dudu/core/ast_type.hpp"
#include "dudu/parser/parser.hpp"

#include <cassert>
#include <exception>
#include <iostream>
#include <string>
#include <vector>

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
    assert(loop.location.column == 9);
    assert(loop.range.start.column == 5);
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

void test_docstrings_attach_and_do_not_emit_as_statements() {
    const dudu::ModuleAst module = dudu::parse_source("'''Module docs.'''\n"
                                                      "\n"
                                                      "class Player:\n"
                                                      "    '''Runtime player docs.\n"
                                                      "\n"
                                                      "    Keeps indentation useful.\n"
                                                      "    '''\n"
                                                      "    hp: i32\n"
                                                      "\n"
                                                      "    def move(self) -> i32:\n"
                                                      "        '''Moves the player docs.'''\n"
                                                      "        return self.hp\n"
                                                      "\n"
                                                      "def helper() -> i32:\n"
                                                      "    '''Helper docs.'''\n"
                                                      "    return 1\n"
                                                      "\n"
                                                      "enum Mode:\n"
                                                      "    '''Mode docs.'''\n"
                                                      "    Play\n"
                                                      "    Pause\n",
                                                      "docstrings.dd");
    assert(module.doc_comment == "Module docs.");
    assert(module.classes.size() == 1);
    assert(module.classes.front().doc_comment ==
           "Runtime player docs.\n\nKeeps indentation useful.");
    assert(module.classes.front().methods.front().doc_comment == "Moves the player docs.");
    assert(module.classes.front().methods.front().statements.size() == 1);
    assert(module.classes.front().methods.front().statements.front().kind ==
           dudu::StmtKind::Return);
    assert(module.functions.front().doc_comment == "Helper docs.");
    assert(module.functions.front().statements.size() == 1);
    assert(module.functions.front().statements.front().kind == dudu::StmtKind::Return);
    assert(module.enums.front().doc_comment == "Mode docs.");
}

void test_misplaced_docstrings_are_rejected() {
    struct Case {
        std::string source;
        std::string expected;
    };
    const std::vector<Case> cases = {
        {"VALUE: i32 = 1\n"
         "'''Late module docs.'''\n",
         "module docstrings must be the first statement"},
        {"class Player:\n"
         "    hp: i32\n"
         "    '''Late class docs.'''\n",
         "class docstrings must be the first statement"},
        {"enum Mode:\n"
         "    Play\n"
         "    '''Late enum docs.'''\n",
         "enum docstrings must be the first statement"},
        {"def helper() -> i32:\n"
         "    value = 1\n"
         "    '''Late function docs.'''\n"
         "    return value\n",
         "function docstrings must be the first statement"},
    };
    for (const Case& item : cases) {
        bool rejected = false;
        try {
            (void)dudu::parse_source(item.source, "bad_docstring.dd");
        } catch (const dudu::CompileError& error) {
            rejected = std::string(error.what()).find(item.expected) != std::string::npos;
        }
        assert(rejected);
    }
}

} // namespace

int main() {
    try {
        test_statement_source_range_uses_token_span();
        test_digit_suffixed_member_receiver();
        test_keyword_statements_keep_token_ranges();
        test_docstrings_attach_and_do_not_emit_as_statements();
        test_misplaced_docstrings_are_rejected();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
