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
    assert(assign.text == "value = add(1, 2)");
    assert(assign.source_text == "value   =    add(1, 2)");
    assert(assign.range.start.line == 2);
    assert(assign.range.start.column == 5);
    assert(assign.range.end.line == 2);
    assert(assign.range.end.column == 27);
    assert(assign.target_expr.range.start.column == 5);
    assert(assign.value_expr.range.start.column == 18);
}

} // namespace

int main() {
    try {
        test_statement_source_range_uses_token_span();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
