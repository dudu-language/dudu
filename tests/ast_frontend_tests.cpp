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

void write_file(const std::filesystem::path& path, const std::string& text) {
    std::ofstream file(path);
    if (!file) {
        throw std::runtime_error("could not write " + path.string());
    }
    file << text;
}

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

struct DecodedSemanticToken {
    int line = 0;
    int column = 0;
    int length = 0;
    int type = 0;
    int modifiers = 0;
    std::string text;
};

std::vector<std::string> split_lines(const std::string& source) {
    std::vector<std::string> lines;
    size_t start = 0;
    while (start <= source.size()) {
        const size_t end = source.find('\n', start);
        if (end == std::string::npos) {
            lines.push_back(source.substr(start));
            break;
        }
        lines.push_back(source.substr(start, end - start));
        start = end + 1;
    }
    return lines;
}

std::vector<DecodedSemanticToken> decoded_semantic_tokens(const std::string& source,
                                                          const std::string& json) {
    const std::vector<int> data = semantic_token_data(json);
    const std::vector<std::string> lines = split_lines(source);
    std::vector<DecodedSemanticToken> out;
    int line = 0;
    int column = 0;
    for (size_t i = 0; i + 4 < data.size(); i += 5) {
        const int delta_line = data[i];
        const int delta_column = data[i + 1];
        line += delta_line;
        column = delta_line == 0 ? column + delta_column : delta_column;
        std::string text;
        if (line >= 0 && static_cast<size_t>(line) < lines.size() && column >= 0 &&
            static_cast<size_t>(column) < lines[static_cast<size_t>(line)].size()) {
            text = lines[static_cast<size_t>(line)].substr(static_cast<size_t>(column),
                                                           static_cast<size_t>(data[i + 2]));
        }
        out.push_back({.line = line,
                       .column = column,
                       .length = data[i + 2],
                       .type = data[i + 3],
                       .modifiers = data[i + 4],
                       .text = text});
    }
    return out;
}

bool has_decoded_semantic_token(const std::vector<DecodedSemanticToken>& tokens,
                                std::string_view text, int type, int modifiers) {
    for (const DecodedSemanticToken& token : tokens) {
        if (token.text == text && token.type == type && token.modifiers == modifiers) {
            return true;
        }
    }
    return false;
}

void require_decoded_semantic_token(const std::vector<DecodedSemanticToken>& tokens,
                                    std::string_view text, int type, int modifiers) {
    if (has_decoded_semantic_token(tokens, text, type, modifiers)) {
        return;
    }
    std::ostringstream message;
    message << "missing semantic token text=" << text << " type=" << type
            << " modifiers=" << modifiers << "\n";
    for (const DecodedSemanticToken& token : tokens) {
        message << token.line << ':' << token.column << " text=" << token.text
                << " type=" << token.type << " modifiers=" << token.modifiers << '\n';
    }
    throw std::runtime_error(message.str());
}

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

void test_native_semantic_tokens() {
    const std::string source = "import c \"native.h\"\n"
                               "\n"
                               "def main() -> i32:\n"
                               "    event: DuduNativeEvent\n"
                               "    if DUDU_NATIVE_CHECK():\n"
                               "        return dudu_native_add(DUDU_NATIVE_MAGIC, event.type)\n"
                               "    return 0\n";
    dudu::ModuleAst module = dudu::parse_source(source, "native_semantic_tokens.dd");
    dudu::ModuleAst native_symbols = module;
    native_symbols.native_types.push_back({.name = "DuduNativeEvent",
                                           .native_spelling = "DuduNativeEvent",
                                           .type_ref = dudu::parse_type_text("DuduNativeEvent"),
                                           .location = {}});
    native_symbols.native_values.push_back({.name = "DUDU_NATIVE_MAGIC",
                                            .native_spelling = "i32",
                                            .type_ref = dudu::parse_type_text("i32"),
                                            .location = {}});
    native_symbols.native_functions.push_back(
        {.name = "dudu_native_add",
         .template_params = {},
         .param_native_spellings = {"i32", "i32"},
         .param_type_refs = {dudu::parse_type_text("i32"), dudu::parse_type_text("i32")},
         .return_native_spelling = "i32",
         .return_type_ref = dudu::parse_type_text("i32"),
         .location = {}});
    native_symbols.native_macros.push_back(
        {.name = "DUDU_NATIVE_CHECK", .arity = 0, .function_like = true, .location = {}});

    constexpr int native_modifier = 16;
    constexpr int readonly_modifier = 4;
    const std::vector<DecodedSemanticToken> tokens =
        decoded_semantic_tokens(source, dudu::semantic_tokens_json(module, native_symbols));
    require_decoded_semantic_token(tokens, "DuduNativeEvent", 1, native_modifier);
    require_decoded_semantic_token(tokens, "DUDU_NATIVE_CHECK", 10, native_modifier);
    require_decoded_semantic_token(tokens, "dudu_native_add", 4, native_modifier);
    require_decoded_semantic_token(tokens, "DUDU_NATIVE_MAGIC", 6,
                                   native_modifier | readonly_modifier);
}

void test_decoded_semantic_tokens_cover_core_dudu_kinds() {
    const std::string source = "GLOBAL: i32 = 1\n"
                               "\n"
                               "enum Mode:\n"
                               "    Play\n"
                               "\n"
                               "class Player:\n"
                               "    hp: i32\n"
                               "    count: static[i32] = 0\n"
                               "\n"
                               "    def move(self, dx: i32) -> i32:\n"
                               "        next_hp = self.hp + dx\n"
                               "        return next_hp\n"
                               "\n"
                               "def make_player(seed: i32) -> Player:\n"
                               "    player: Player = Player(seed)\n"
                               "    player.move(2)\n"
                               "    mode: Mode = Mode.Play\n"
                               "    label = \"ok\"\n"
                               "    return player\n";
    const dudu::ModuleAst module = dudu::parse_source(source, "decoded_semantic_tokens.dd");
    const std::vector<DecodedSemanticToken> tokens =
        decoded_semantic_tokens(source, dudu::semantic_tokens_json(module));

    constexpr int token_type = 1;
    constexpr int token_class = 2;
    constexpr int token_enum = 3;
    constexpr int token_function = 4;
    constexpr int token_method = 5;
    constexpr int token_variable = 6;
    constexpr int token_parameter = 7;
    constexpr int token_property = 8;
    constexpr int token_enum_member = 9;
    constexpr int token_number = 12;
    constexpr int token_string = 13;

    constexpr int mod_declaration = 1;
    constexpr int mod_readonly = 4;
    constexpr int mod_static = 8;

    require_decoded_semantic_token(tokens, "GLOBAL", token_variable,
                                   mod_declaration | mod_readonly);
    require_decoded_semantic_token(tokens, "i32", token_type, 0);
    require_decoded_semantic_token(tokens, "Mode", token_enum, mod_declaration);
    require_decoded_semantic_token(tokens, "Play", token_enum_member, mod_declaration);
    require_decoded_semantic_token(tokens, "Player", token_class, mod_declaration);
    require_decoded_semantic_token(tokens, "hp", token_property, mod_declaration);
    require_decoded_semantic_token(tokens, "count", token_property, mod_declaration | mod_static);
    require_decoded_semantic_token(tokens, "move", token_method, mod_declaration);
    require_decoded_semantic_token(tokens, "self", token_parameter, mod_declaration);
    require_decoded_semantic_token(tokens, "dx", token_parameter, mod_declaration);
    require_decoded_semantic_token(tokens, "next_hp", token_variable, mod_declaration);
    require_decoded_semantic_token(tokens, "make_player", token_function, mod_declaration);
    require_decoded_semantic_token(tokens, "player", token_variable, mod_declaration);
    require_decoded_semantic_token(tokens, "move", token_method, 0);
    require_decoded_semantic_token(tokens, "Play", token_enum_member, mod_readonly);
    require_decoded_semantic_token(tokens, "2", token_number, 0);
    require_decoded_semantic_token(tokens, "\"ok\"", token_string, 0);
}

void test_unresolved_semantic_tokens_are_marked() {
    const std::string source = "def main() -> i32:\n"
                               "    local_value = 1\n"
                               "    local_value\n"
                               "    missing_obj.field\n"
                               "    missing_call()\n"
                               "    return missing_value\n";
    const dudu::ModuleAst module = dudu::parse_source(source, "unresolved_semantic_tokens.dd");
    const std::vector<DecodedSemanticToken> tokens =
        decoded_semantic_tokens(source, dudu::semantic_tokens_json(module));

    constexpr int token_function = 4;
    constexpr int token_variable = 6;
    constexpr int token_property = 8;
    constexpr int mod_declaration = 1;
    constexpr int mod_unresolved = 32;

    require_decoded_semantic_token(tokens, "local_value", token_variable, mod_declaration);
    require_decoded_semantic_token(tokens, "local_value", token_variable, 0);
    require_decoded_semantic_token(tokens, "missing_obj", token_variable, mod_unresolved);
    require_decoded_semantic_token(tokens, "field", token_property, mod_unresolved);
    require_decoded_semantic_token(tokens, "missing_call", token_function, mod_unresolved);
    require_decoded_semantic_token(tokens, "missing_value", token_variable, mod_unresolved);
}

void test_project_semantic_tokens_are_import_aware() {
    const std::filesystem::path dir =
        std::filesystem::temp_directory_path() / "dudu_project_semantic_tokens_test";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    write_file(dir / "math_utils.dd", "MAGIC: i32 = 9\n"
                                      "\n"
                                      "def mix(left: i32, right: i32) -> i32:\n"
                                      "    return left + right + MAGIC\n");
    write_file(dir / "entities.dd", "class Player:\n"
                                    "    hp: i32\n");
    const std::string main_source = "import math_utils as math\n"
                                    "from entities import Player\n"
                                    "\n"
                                    "def main() -> i32:\n"
                                    "    player: Player = Player(1)\n"
                                    "    return math.mix(player.hp, math.MAGIC)\n";
    write_file(dir / "main.dd", main_source);

    dudu::ProjectIndexOptions options;
    options.entry_path = dir / "main.dd";
    options.source_dir = dir;
    options.force_module_tree = true;
    const dudu::ProjectIndex index = dudu::ProjectIndex::load(options);

    dudu::ProjectIndexOptions native_options = options;
    native_options.include_native_headers = false;
    const dudu::ProjectIndex native_index = dudu::ProjectIndex::load(native_options);
    const std::vector<DecodedSemanticToken> tokens = decoded_semantic_tokens(
        main_source, dudu::semantic_tokens_json(index, dir / "main.dd", native_index));

    constexpr int token_namespace = 0;
    constexpr int token_class = 2;
    constexpr int token_function = 4;
    constexpr int token_variable = 6;
    constexpr int mod_readonly = 4;

    require_decoded_semantic_token(tokens, "math", token_namespace, 0);
    require_decoded_semantic_token(tokens, "Player", token_class, 0);
    require_decoded_semantic_token(tokens, "mix", token_function, 0);
    require_decoded_semantic_token(tokens, "MAGIC", token_variable, mod_readonly);
}

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
        test_native_semantic_tokens();
        test_decoded_semantic_tokens_cover_core_dudu_kinds();
        test_unresolved_semantic_tokens_are_marked();
        test_project_semantic_tokens_are_import_aware();
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
