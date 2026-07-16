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

dudu::Json completion_params(int line, int character) {
    dudu::Json line_json;
    line_json.value = static_cast<double>(line);
    dudu::Json character_json;
    character_json.value = static_cast<double>(character);
    dudu::Json position_json;
    position_json.value = dudu::JsonObject{{"line", line_json}, {"character", character_json}};
    dudu::Json params;
    params.value = dudu::JsonObject{{"position", position_json}};
    return params;
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

void test_non_type_generic_param_shape() {
    const dudu::ModuleAst module = dudu::parse_source("class SmallVec[T, N]:\n"
                                                      "    items: array[T][N]\n",
                                                      "generic_non_type_shape.dd");
    assert(module.classes.size() == 1);
    const dudu::ClassDecl& small_vec = module.classes[0];
    const std::set<std::string> values = dudu::generic_value_params_for_class(small_vec);
    assert(values.size() == 1);
    assert(values.contains("N"));

    const dudu::TypeRef substituted = dudu::substitute_type_ref(
        small_vec.fields[0].type_ref,
        {{"T", dudu::parse_type_text("i32")}, {"N", dudu::parse_type_text("3")}});
    assert(dudu::type_ref_text(substituted) == "array[i32][3]");
}

void test_payload_enum_ast_shape() {
    const dudu::ModuleAst module = dudu::parse_source("enum Message:\n"
                                                      "    Quit\n"
                                                      "\n"
                                                      "    Move:\n"
                                                      "        x: i32\n"
                                                      "        y: i32\n"
                                                      "\n"
                                                      "    Write:\n"
                                                      "        text: str\n",
                                                      "payload_enum_shape.dd");
    assert(module.enums.size() == 1);
    const dudu::EnumDecl& message = module.enums[0];
    assert(message.values.size() == 3);
    assert(message.values[0].name == "Quit");
    assert(message.values[0].payload_fields.empty());
    assert(message.values[1].name == "Move");
    assert(message.values[1].payload_fields.size() == 2);
    assert(message.values[1].payload_fields[0].name == "x");
    assert(message.values[1].payload_fields[0].type_ref.kind == dudu::TypeKind::Named);
    assert(message.values[2].name == "Write");
    assert(message.values[2].payload_fields.size() == 1);
    assert(message.values[2].payload_fields[0].name == "text");
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
    assert(dudu::stmt_condition_expr(match).kind == dudu::ExprKind::Name);
    assert(dudu::stmt_condition_expr(match).name == "msg");
    assert(match.children.size() == 3);

    const dudu::Stmt& quit = match.children[0];
    assert(quit.kind == dudu::StmtKind::Case);
    assert(dudu::stmt_pattern_expr(quit).kind == dudu::ExprKind::Member);
    assert(dudu::stmt_pattern_expr(quit).name == "Quit");
    assert(dudu::stmt_pattern_expr(quit).children.size() == 1);
    assert(dudu::stmt_pattern_expr(quit).children[0].kind == dudu::ExprKind::Name);
    assert(dudu::stmt_pattern_expr(quit).children[0].name == "Message");
    assert(quit.children.size() == 1);
    assert(quit.children[0].kind == dudu::StmtKind::Return);

    const dudu::Stmt& move = match.children[1];
    assert(move.kind == dudu::StmtKind::Case);
    assert(dudu::stmt_pattern_expr(move).kind == dudu::ExprKind::Call);
    assert(dudu::direct_callee_name(dudu::stmt_pattern_expr(move)) == "Message.Move");
    assert(dudu::stmt_pattern_expr(move).children.size() == 2);
    assert(dudu::stmt_pattern_expr(move).children[0].kind == dudu::ExprKind::Name);
    assert(dudu::stmt_pattern_expr(move).children[0].name == "x");
    assert(dudu::stmt_pattern_expr(move).children[1].kind == dudu::ExprKind::Name);
    assert(dudu::stmt_pattern_expr(move).children[1].name == "y");
    assert(dudu::stmt_guard_expr(move).kind == dudu::ExprKind::Binary);
    assert(dudu::stmt_guard_expr(move).op == ">");
    assert(dudu::stmt_guard_expr(move).children.size() == 2);
    assert(dudu::stmt_guard_expr(move).children[0].kind == dudu::ExprKind::Name);
    assert(dudu::stmt_guard_expr(move).children[0].name == "x");
    assert(dudu::stmt_guard_expr(move).children[1].kind == dudu::ExprKind::IntLiteral);
    assert(dudu::stmt_guard_expr(move).children[1].value == "0");

    const dudu::Stmt& wildcard = match.children[2];
    assert(wildcard.kind == dudu::StmtKind::Case);
    assert(dudu::stmt_guard_expr(wildcard).kind == dudu::ExprKind::Missing);
    assert(dudu::stmt_pattern_expr(wildcard).kind == dudu::ExprKind::Name);
    assert(dudu::stmt_pattern_expr(wildcard).name == "_");
}

void test_wrapper_match_type_uses_type_ast() {
    const dudu::WrapperMatchType result =
        dudu::wrapper_match_type(dudu::parse_type_text("Result[list[i32], Option[str]]"));
    assert(result.kind == dudu::WrapperMatchKind::Result);
    assert(result.arg_refs.size() == 2);
    assert(result.arg_refs[0].kind == dudu::TypeKind::Template);
    assert(result.arg_refs[0].name == "list");
    assert(result.arg_refs[1].kind == dudu::TypeKind::Template);
    assert(result.arg_refs[1].name == "Option");

    const dudu::WrapperMatchType option =
        dudu::wrapper_match_type(dudu::parse_type_text("Option[Result[i32, str]]"));
    assert(option.kind == dudu::WrapperMatchKind::Option);
    assert(option.arg_refs.size() == 1);
    assert(option.arg_refs[0].kind == dudu::TypeKind::Template);
    assert(option.arg_refs[0].name == "Result");
}

void test_member_completion_target_uses_tokens() {
    const std::string source = "def main() -> i32:\n"
                               "    player: Player = Player()\n"
                               "    player.\n"
                               "    module.value.\n"
                               "    player.hp\n";
    const dudu::Document doc{
        .uri = "file:///completion.dd",
        .path = "/tmp/completion.dd",
        .text = source,
    };

    dudu::Json params = completion_params(2, 11);
    assert(dudu::member_completion_target(doc, &params) == "player");

    params = completion_params(3, 17);
    assert(dudu::member_completion_target(doc, &params) == "module.value");

    params = completion_params(4, 13);
    assert(dudu::member_completion_target(doc, &params) == "player");
}

void test_member_candidate_types_use_type_refs() {
    dudu::ModuleAst module;
    module.aliases.push_back(dudu::TypeAliasDecl{.name = "ViewCamera",
                                                 .cpp_name = "",
                                                 .type_ref = dudu::named_type_ref("Camera"),
                                                 .origin_module = "",
                                                 .location = {}});
    module.native_types.push_back(
        dudu::NativeTypeDecl{.name = "NativeView",
                             .native_spelling = "",
                             .type_ref = dudu::named_type_ref("NativeCamera"),
                             .location = {}});
    module.native_types.push_back(
        dudu::NativeTypeDecl{.name = "TaggedView",
                             .native_spelling = "",
                             .type_ref = dudu::named_type_ref("struct NativeTaggedCamera"),
                             .location = {}});

    const std::set<std::string> dudu_candidates =
        dudu::member_candidate_types(module, dudu::named_type_ref("ViewCamera"));
    assert(dudu_candidates.contains("ViewCamera"));
    assert(dudu_candidates.contains("Camera"));

    const std::set<std::string> native_candidates =
        dudu::member_candidate_types(module, dudu::named_type_ref("NativeView"));
    assert(native_candidates.contains("NativeView"));
    assert(native_candidates.contains("NativeCamera"));

    const std::set<std::string> tagged_candidates =
        dudu::member_candidate_types(module, dudu::named_type_ref("TaggedView"));
    assert(tagged_candidates.contains("TaggedView"));
    assert(tagged_candidates.contains("struct NativeTaggedCamera"));
    assert(tagged_candidates.contains("NativeTaggedCamera"));
}

void test_signature_help_call_site_uses_tokens() {
    const std::string source = "def add(a: i32, b: i32) -> i32:\n"
                               "    return a + b\n"
                               "\n"
                               "def main() -> i32:\n"
                               "    return add(max(1, 2), 3)\n";
    const dudu::Document doc{
        .uri = "file:///signature.dd",
        .path = "/tmp/signature.dd",
        .text = source,
    };

    dudu::Json params = completion_params(4, 26);
    const std::string help = dudu::signature_help_json(&doc, &params);
    assert(help.find("add(a: i32, b: i32) -> i32") != std::string::npos);
    assert(help.find("\"activeParameter\":1") != std::string::npos);
}

void test_ast_expr_path_at_cursor() {
    const std::string source = "def main() -> i32:\n"
                               "    player.hp.current\n";
    const dudu::Document doc{
        .uri = "file:///path.dd",
        .path = "/tmp/path.dd",
        .text = source,
    };

    dudu::Json params = completion_params(1, 17);
    const dudu::ModuleAst module = dudu::parse_source(doc.text, doc.path);
    const std::optional<dudu::ExprPath> path = dudu::ast_selection_at(module, &params).expr_path;
    assert(path);
    assert(path->segments.size() == 3);
    assert(path->segments[0].text == "player");
    assert(path->segments[1].text == "hp");
    assert(path->segments[2].text == "current");
}

} // namespace

int main() {
    try {
        test_generic_decl_ast_shape();
        test_non_type_generic_param_shape();
        test_payload_enum_ast_shape();
        test_match_case_ast_shape();
        test_wrapper_match_type_uses_type_ast();
        test_member_completion_target_uses_tokens();
        test_member_candidate_types_use_type_refs();
        test_signature_help_call_site_uses_tokens();
        test_ast_expr_path_at_cursor();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
