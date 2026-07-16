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

void test_native_semantic_tokens() {
    const std::string source = "from c.path import native.h\n"
                               "\n"
                               "def main() -> i32:\n"
                               "    event: DuduNativeEvent\n"
                               "    if DUDU_NATIVE_CHECK():\n"
                               "        return dudu_native_add(DUDU_NATIVE_MAGIC, event.type)\n"
                               "    return 0\n";
    dudu::ModuleAst module = dudu::parse_source(source, "native_semantic_tokens.dd");
    dudu::ModuleAst native_symbols = module;
    native_symbols.native_types.push_back(
        {.name = "DuduNativeEvent",
         .native_spelling = "DuduNativeEvent",
         .type_ref = dudu::parse_type_text("DuduNativeEvent"),
         .identity = dudu::native_identity("DuduNativeEvent", "native_semantic_tokens.h"),
         .location = {}});
    native_symbols.native_values.push_back({.name = "DUDU_NATIVE_MAGIC",
                                            .native_spelling = "i32",
                                            .type_ref = dudu::parse_type_text("i32"),
                                            .location = {}});
    native_symbols.native_functions.push_back(
        {.name = "dudu_native_add",
         .template_params = {},
         .template_param_is_value = {},
         .param_names = {"left", "right"},
         .param_native_spellings = {"i32", "i32"},
         .param_type_refs = {dudu::parse_type_text("i32"), dudu::parse_type_text("i32")},
         .return_native_spelling = "i32",
         .return_type_ref = dudu::parse_type_text("i32"),
         .identity = dudu::native_identity("dudu_native_add", "native_semantic_tokens.h"),
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

void test_native_import_semantic_token_ranges() {
    const std::string source = "from cpp import thread\n"
                               "from c import SDL3/SDL_pixels.h as sdl\n"
                               "from cpp.path import vendor/foo.hpp as foo\n";
    const dudu::ModuleAst module = dudu::parse_source(source, "native_import_tokens.dd");
    const std::vector<DecodedSemanticToken> tokens =
        decoded_semantic_tokens(source, dudu::semantic_tokens_json(module));

    constexpr int token_namespace = 0;
    constexpr int token_string = 13;
    constexpr int mod_declaration = 1;
    constexpr int mod_native = 16;
    require_decoded_semantic_token(tokens, "thread", token_string, mod_native);
    require_decoded_semantic_token(tokens, "SDL3/SDL_pixels.h", token_string, mod_native);
    require_decoded_semantic_token(tokens, "vendor/foo.hpp", token_string, mod_native);
    require_decoded_semantic_token(tokens, "sdl", token_namespace, mod_declaration | mod_native);
    require_decoded_semantic_token(tokens, "foo", token_namespace, mod_declaration | mod_native);
}

void test_lexical_semantic_tokens_survive_invalid_source() {
    const std::string source = "from renderer import launch_render_workers\n"
                               "\n"
                               "@operator(\"+\")\n"
                               "class Vec3:\n"
                               "    x: f32\n"
                               "\n"
                               "def main() -> i32:\n"
                               "    if fps_elapsed_ms: i32 >= 250:\n"
                               "        return launch_render_workers(1)\n";
    const std::vector<DecodedSemanticToken> tokens =
        decoded_semantic_tokens(source, dudu::lexical_semantic_tokens_json(source));

    constexpr int token_class = 2;
    constexpr int token_function = 4;
    constexpr int token_variable = 6;
    constexpr int token_macro = 10;
    constexpr int token_keyword = 11;
    constexpr int token_type = 1;
    constexpr int token_number = 12;
    constexpr int token_string = 13;
    require_decoded_semantic_token(tokens, "from", token_keyword, 0);
    require_decoded_semantic_token(tokens, "launch_render_workers", token_variable, 0);
    require_decoded_semantic_token(tokens, "@operator", token_macro, 0);
    require_decoded_semantic_token(tokens, "\"+\"", token_string, 0);
    require_decoded_semantic_token(tokens, "Vec3", token_class, 0);
    require_decoded_semantic_token(tokens, "f32", token_type, 0);
    require_decoded_semantic_token(tokens, "main", token_function, 0);
    require_decoded_semantic_token(tokens, "if", token_keyword, 0);
    require_decoded_semantic_token(tokens, "250", token_number, 0);
}

void test_recovered_ast_preserves_semantic_token_kinds() {
    const std::string source = "class Player:\n"
                               "    hp: i32\n"
                               "\n"
                               "def update(player: Player, amount: i32) -> i32:\n"
                               "    before = player.hp\n"
                               "    if amount\n"
                               "        ignored = 1\n"
                               "    after = before + amount\n"
                               "    return after\n";
    const dudu::ParseResult result =
        dudu::parse_source_recovering(source, "recover_semantic_tokens.dd");
    assert(result.diagnostics.size() == 1);
    const std::vector<DecodedSemanticToken> tokens =
        decoded_semantic_tokens(source, dudu::semantic_tokens_json(result.module));
    constexpr int token_class = 2;
    constexpr int token_function = 4;
    constexpr int token_variable = 6;
    constexpr int token_parameter = 7;
    constexpr int token_property = 8;
    require_decoded_semantic_token(tokens, "Player", token_class, 1);
    require_decoded_semantic_token(tokens, "hp", token_property, 1);
    require_decoded_semantic_token(tokens, "update", token_function, 1);
    require_decoded_semantic_token(tokens, "player", token_parameter, 1);
    require_decoded_semantic_token(tokens, "amount", token_parameter, 1);
    require_decoded_semantic_token(tokens, "before", token_variable, 1);
    require_decoded_semantic_token(tokens, "after", token_variable, 1);
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
                               "    for i in range(2):\n"
                               "        player.move(i)\n"
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
    require_decoded_semantic_token(tokens, "i", token_variable, mod_declaration);
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

} // namespace

int main() {
    try {
        test_native_semantic_tokens();
        test_native_import_semantic_token_ranges();
        test_lexical_semantic_tokens_survive_invalid_source();
        test_recovered_ast_preserves_semantic_token_kinds();
        test_decoded_semantic_tokens_cover_core_dudu_kinds();
        test_unresolved_semantic_tokens_are_marked();
        test_project_semantic_tokens_are_import_aware();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
