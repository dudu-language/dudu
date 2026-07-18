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

void test_parser_recovery_preserves_top_level_declarations() {
    const std::string source = "def before() -> i32:\n"
                               "    return 1\n"
                               "\n"
                               "this is not a declaration\n"
                               "\n"
                               "def after() -> i32:\n"
                               "    return 2\n";
    const dudu::ParseResult result = dudu::parse_source_recovering(source, "recover_top_level.dd");
    assert(result.diagnostics.size() == 1);
    assert(result.module.functions.size() == 2);
    assert(result.module.functions[0].name == "before");
    assert(result.module.functions[1].name == "after");
}

void test_parser_recovery_preserves_function_around_bad_statement() {
    const std::string source = "def main() -> i32:\n"
                               "    before = 1\n"
                               "    if before\n"
                               "        ignored = 2\n"
                               "    after = 3\n"
                               "    return after\n";
    const dudu::ParseResult result = dudu::parse_source_recovering(source, "recover_statement.dd");
    assert(result.diagnostics.size() == 1);
    assert(result.module.functions.size() == 1);
    const std::vector<dudu::Stmt>& statements = result.module.functions.front().statements;
    assert(statements.size() == 3);
    assert(dudu::stmt_target_expr(statements[0]).name == "before");
    assert(dudu::stmt_target_expr(statements[1]).name == "after");
    assert(statements[2].kind == dudu::StmtKind::Return);
}

void test_parser_recovery_preserves_class_around_bad_member() {
    const std::string source = "class Player:\n"
                               "    hp: i32\n"
                               "    broken i32\n"
                               "    speed: f32\n";
    const dudu::ParseResult result = dudu::parse_source_recovering(source, "recover_class.dd");
    assert(result.diagnostics.size() == 1);
    assert(result.module.classes.size() == 1);
    assert(result.module.classes.front().fields.size() == 2);
    assert(result.module.classes.front().fields[0].name == "hp");
    assert(result.module.classes.front().fields[1].name == "speed");
}

void test_parser_recovery_preserves_enum_around_bad_value() {
    const std::string source = "enum Message:\n"
                               "    Ready\n"
                               "    Broken i32\n"
                               "    Data:\n"
                               "        value: i32\n";
    const dudu::ParseResult result = dudu::parse_source_recovering(source, "recover_enum.dd");
    assert(result.diagnostics.size() == 1);
    assert(result.module.enums.size() == 1);
    assert(result.module.enums.front().values.size() == 2);
    assert(result.module.enums.front().values[0].name == "Ready");
    assert(result.module.enums.front().values[1].name == "Data");
}

void test_parser_recovery_skips_nested_bad_enum_payload() {
    const std::string source = "enum Message:\n"
                               "    Broken:\n"
                               "        value i32\n"
                               "        ignored: i32\n"
                               "    Ready\n"
                               "\n"
                               "def after() -> i32:\n"
                               "    return 1\n";
    const dudu::ParseResult result =
        dudu::parse_source_recovering(source, "recover_enum_payload.dd");
    assert(result.diagnostics.size() == 1);
    assert(result.module.enums.size() == 1);
    assert(result.module.enums.front().values.size() == 1);
    assert(result.module.enums.front().values.front().name == "Ready");
    assert(result.module.functions.size() == 1);
    assert(result.module.functions.front().name == "after");
}

void test_parser_recovery_stops_unfinished_call_at_statement_boundary() {
    const std::string source = "def main() -> i32:\n"
                               "    before = 1\n"
                               "    broken = call(\n"
                               "    after = 2\n"
                               "    return after\n"
                               "\n"
                               "def later() -> i32:\n"
                               "    return 3\n";
    const dudu::ParseResult result =
        dudu::parse_source_recovering(source, "recover_unfinished_call.dd");
    assert(result.diagnostics.size() == 1);
    assert(result.module.functions.size() == 2);
    assert(result.module.functions[0].statements.size() == 3);
    assert(dudu::stmt_target_expr(result.module.functions[0].statements[0]).name == "before");
    assert(dudu::stmt_target_expr(result.module.functions[0].statements[1]).name == "after");
    assert(result.module.functions[0].statements[2].kind == dudu::StmtKind::Return);
    assert(result.module.functions[1].name == "later");
}

void test_parser_recovery_stops_unfinished_index_at_statement_boundary() {
    const std::string source = "def main() -> i32:\n"
                               "    values: list[i32] = []\n"
                               "    broken = values[\n"
                               "    after = 2\n"
                               "    return after\n";
    const dudu::ParseResult result =
        dudu::parse_source_recovering(source, "recover_unfinished_index.dd");
    assert(result.diagnostics.size() == 1);
    assert(result.diagnostics.front().code == "dudu.parser.unfinished_group");
    assert(result.module.functions.size() == 1);
    assert(result.module.functions.front().statements.size() == 3);
    assert(dudu::stmt_target_expr(result.module.functions.front().statements[1]).name == "after");
}

void test_parser_recovery_preserves_declaration_after_incomplete_import() {
    const std::string source = "from helper import\n"
                               "\n"
                               "def usable() -> i32:\n"
                               "    return 2\n";
    const dudu::ParseResult result =
        dudu::parse_source_recovering(source, "recover_incomplete_import.dd");
    assert(result.diagnostics.size() == 1);
    assert(result.module.functions.size() == 1);
    assert(result.module.functions.front().name == "usable");
}

void test_recovering_parser_accepts_valid_multiline_groups() {
    const std::string source = "def add(a: i32, b: i32) -> i32:\n"
                               "    return a + b\n"
                               "\n"
                               "def main() -> i32:\n"
                               "    value = add(\n"
                               "        20,\n"
                               "        22,\n"
                               "    )\n"
                               "    return value\n";
    const dudu::ParseResult result =
        dudu::parse_source_recovering(source, "recover_valid_multiline.dd");
    assert(result.diagnostics.empty());
    assert(result.module.functions.size() == 2);
    assert(result.module.functions.back().statements.size() == 2);
}

void test_parser_recovery_skips_bad_declaration_body() {
    const std::string source = "def broken() -> i32\n"
                               "    return 1\n"
                               "\n"
                               "def usable() -> i32:\n"
                               "    return 2\n";
    const dudu::ParseResult result =
        dudu::parse_source_recovering(source, "recover_declaration.dd");
    assert(result.diagnostics.size() == 1);
    assert(result.module.functions.size() == 1);
    assert(result.module.functions.front().name == "usable");
}

void test_parser_recovery_preserves_declaration_after_unfinished_decorator() {
    const std::string source = "@operator(\n"
                               "def damaged() -> i32:\n"
                               "    return 1\n"
                               "\n"
                               "def usable() -> i32:\n"
                               "    return 2\n";
    const dudu::ParseResult result = dudu::parse_source_recovering(source, "recover_decorator.dd");
    assert(result.diagnostics.size() == 1);
    assert(result.module.functions.size() == 2);
    assert(result.module.functions[0].name == "damaged");
    assert(result.module.functions[1].name == "usable");
}

void test_parser_recovery_rejects_incomplete_member_expression() {
    const std::string source = "class Player:\n"
                               "    hp: i32\n"
                               "\n"
                               "def damaged(player: Player) -> i32:\n"
                               "    player.\n"
                               "    return 0\n"
                               "\n"
                               "def usable() -> i32:\n"
                               "    return 2\n";
    const dudu::ParseResult result = dudu::parse_source_recovering(source, "recover_member.dd");
    assert(result.diagnostics.size() == 1);
    assert(result.module.functions.size() == 2);
    assert(result.module.functions[0].name == "damaged");
    assert(result.module.functions[1].name == "usable");
}

void test_parser_recovery_rejects_missing_binary_operand() {
    const std::string source = "def damaged() -> i32:\n"
                               "    value = 1 +\n"
                               "    return 0\n"
                               "\n"
                               "def usable() -> i32:\n"
                               "    return 2\n";
    const dudu::ParseResult result = dudu::parse_source_recovering(source, "recover_operand.dd");
    assert(result.diagnostics.size() == 1);
    assert(result.diagnostics.front().code == "dudu.parser.incomplete_expression");
    assert(result.module.functions.size() == 2);
    assert(result.module.functions[1].name == "usable");
}

void test_parser_recovery_rejects_missing_return_type() {
    const std::string source = "def damaged() ->:\n"
                               "    return 0\n"
                               "\n"
                               "def usable() -> i32:\n"
                               "    return 2\n";
    const dudu::ParseResult result =
        dudu::parse_source_recovering(source, "recover_return_type.dd");
    assert(result.diagnostics.size() == 1);
    assert(result.module.functions.size() == 1);
    assert(result.module.functions[0].name == "usable");
}

void test_lexer_recovery_preserves_later_declarations() {
    const std::string source = "def before() -> i32:\n"
                               "    return 1\n"
                               "\n"
                               "BROKEN: str = \"unterminated\n"
                               "\n"
                               "def after() -> i32:\n"
                               "    return 2\n";
    const dudu::ParseResult result = dudu::parse_source_recovering(source, "recover_lexer.dd");
    assert(result.diagnostics.size() == 1);
    assert(result.diagnostics.front().code == "dudu.lexer.syntax");
    assert(result.module.functions.size() == 2);
    assert(result.module.functions[0].name == "before");
    assert(result.module.functions[1].name == "after");
}

void test_lexer_recovery_preserves_function_body_after_bad_indent() {
    const std::string source = "def main() -> i32:\n"
                               "    before = 1\n"
                               "  broken = 2\n"
                               "    after = 3\n"
                               "    return after\n";
    const dudu::ParseResult result = dudu::parse_source_recovering(source, "recover_indent.dd");
    assert(result.diagnostics.size() == 1);
    assert(result.diagnostics.front().code == "dudu.lexer.syntax");
    assert(result.module.functions.size() == 1);
    const std::vector<dudu::Stmt>& statements = result.module.functions.front().statements;
    assert(statements.size() == 3);
    assert(dudu::stmt_target_expr(statements[0]).name == "before");
    assert(dudu::stmt_target_expr(statements[1]).name == "after");
}

} // namespace

int main() {
    try {
        test_parser_recovery_preserves_top_level_declarations();
        test_parser_recovery_preserves_function_around_bad_statement();
        test_parser_recovery_preserves_class_around_bad_member();
        test_parser_recovery_preserves_enum_around_bad_value();
        test_parser_recovery_skips_nested_bad_enum_payload();
        test_parser_recovery_stops_unfinished_call_at_statement_boundary();
        test_parser_recovery_stops_unfinished_index_at_statement_boundary();
        test_parser_recovery_preserves_declaration_after_incomplete_import();
        test_recovering_parser_accepts_valid_multiline_groups();
        test_parser_recovery_skips_bad_declaration_body();
        test_parser_recovery_preserves_declaration_after_unfinished_decorator();
        test_parser_recovery_rejects_incomplete_member_expression();
        test_parser_recovery_rejects_missing_binary_operand();
        test_parser_recovery_rejects_missing_return_type();
        test_lexer_recovery_preserves_later_declarations();
        test_lexer_recovery_preserves_function_body_after_bad_indent();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
