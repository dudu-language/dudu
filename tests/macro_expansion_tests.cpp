#include "dudu/codegen/cpp_emit_modules.hpp"
#include "dudu/lsp/language_server_completion.hpp"
#include "dudu/lsp/language_server_definition.hpp"
#include "dudu/lsp/language_server_hover.hpp"
#include "dudu/lsp/language_server_json.hpp"
#include "dudu/lsp/language_server_macros.hpp"
#include "dudu/lsp/language_server_navigation.hpp"
#include "dudu/lsp/language_server_references.hpp"
#include "dudu/lsp/language_server_semantic_tokens.hpp"
#include "dudu/lsp/language_server_signature_help.hpp"
#include "dudu/lsp/language_server_support.hpp"
#include "dudu/project/project_config.hpp"
#include "dudu/project/project_index.hpp"

#include <algorithm>
#include <cassert>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>

namespace {

void write_file(const std::filesystem::path& path, const std::string& source) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream output(path);
    assert(output);
    output << source;
}

const dudu::ClassDecl& player_class(const dudu::ModuleAst& module) {
    for (const dudu::ModuleAst& unit : module.module_units) {
        for (const dudu::ClassDecl& klass : unit.classes) {
            if (klass.name == "Player")
                return klass;
        }
    }
    assert(false && "Player class is missing");
    return module.classes.front();
}

void require_semantic_token(const std::string& json, int expected_line, int expected_column,
                            int expected_type, int expected_modifiers) {
    std::vector<int> data;
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
        data.push_back(value);
    }
    int line = 0;
    int column = 0;
    for (size_t i = 0; i + 4 < data.size(); i += 5) {
        line += data[i];
        column = data[i] == 0 ? column + data[i + 1] : data[i + 1];
        if (line == expected_line && column == expected_column && data[i + 3] == expected_type &&
            data[i + 4] == expected_modifiers) {
            return;
        }
    }
    assert(false && "expected semantic token is missing");
}

const dudu::EnumDecl& find_enum(const dudu::ModuleAst& module, std::string_view name) {
    for (const dudu::ModuleAst& unit : module.module_units) {
        for (const dudu::EnumDecl& en : unit.enums) {
            if (en.name == name)
                return en;
        }
    }
    assert(false && "Color enum is missing");
    return module.enums.front();
}

void test_dudu_macro_expands_before_semantics_and_caches() {
    const std::filesystem::path dir =
        std::filesystem::temp_directory_path() / "dudu_macro_expansion_test";
    std::filesystem::remove_all(dir);
    write_file(dir / "dudu.toml", "name = \"macro_expansion_fixture\"\n"
                                  "entry = \"src/main.dd\"\n"
                                  "build_dir = \"build\"\n");
    write_file(dir / "src/macro_helper.dd",
               "import dudu.ast as ast\n"
               "\n"
               "def score_method(score: str) -> ast.FunctionDecl:\n"
               "    return_stmt = ast.return_statement(ast.int_expression(score))\n"
               "    return ast.FunctionDecl(\n"
               "        name=\"debug_score\",\n"
               "        return_type=ast.named_type(\"i32\"),\n"
               "        body=[return_stmt],\n"
               "    )\n");
    write_file(dir / "src/macros.dd",
               "from macro_helper import score_method\n"
               "import dudu.ast as ast\n"
               "\n"
               "class DebugOptions:\n"
               "    score: str = \"7\"\n"
               "\n"
               "@macro(attributes=DebugOptions)\n"
               "def Debug(item: ast.ClassDecl) -> ast.Expansion:\n"
               "    score: str = \"7\"\n"
               "    for field in item.fields:\n"
               "        options = ast.find_attribute(field.attributes, \"Debug\")\n"
               "        if options.has_value():\n"
               "            score = ast.string_argument(options.value(), \"score\", score)\n"
               "    out = ast.expansion()\n"
               "    out.add_method(score_method(score))\n"
               "    return out\n");
    write_file(dir / "src/main.dd", "from macros import Debug\n"
                                    "\n"
                                    "@derive(Debug)\n"
                                    "class Player:\n"
                                    "    @Debug(score=\"9\")\n"
                                    "    hp: i32\n"
                                    "\n"
                                    "def score(player: Player) -> i32:\n"
                                    "    return player.debug_score()\n");

    const dudu::ProjectConfig config = dudu::parse_project_config(dir / "dudu.toml");
    dudu::ProjectIndexOptions options;
    options.entry_path = dir / "src/main.dd";
    options.config = config;
    options.source_dir = dir / "src";
    options.force_module_tree = true;
    const dudu::ProjectIndex first = dudu::ProjectIndex::load(options);
    assert(first.macro_report().invocations == 1);
    assert(first.macro_report().worker_executions == 1);
    assert(first.macro_report().worker_starts == 1);
    assert(first.macro_report().timings.execute_ns > 0);
    assert(first.macro_report().definitions.size() == 1);
    const dudu::macro::ExpansionReport::Definition& debug =
        first.macro_report().definitions.front();
    assert(debug.name == "Debug");
    assert(debug.identity == "macros.Debug");
    assert(debug.module_path == "macros");
    assert(debug.accepted_kind == "class");
    assert(debug.attribute_schema.has_value());
    assert(debug.attribute_schema->name == "DebugOptions");
    assert(debug.attribute_schema->fields.size() == 1);
    assert(debug.attribute_schema->fields.front().name == "score");
    const dudu::ModuleAst& main = first.visible_unit_for_path(dir / "src/main.dd");
    const std::string semantic_tokens =
        dudu::semantic_tokens_json(first, dir / "src/main.dd", first);
    constexpr int token_parameter = 7;
    constexpr int token_macro = 10;
    constexpr int mod_declaration = 1;
    constexpr int mod_readonly = 4;
    require_semantic_token(semantic_tokens, 0, 19, token_macro, mod_declaration | mod_readonly);
    require_semantic_token(semantic_tokens, 2, 0, token_macro, mod_readonly);
    require_semantic_token(semantic_tokens, 2, 8, token_macro, mod_readonly);
    require_semantic_token(semantic_tokens, 4, 4, token_macro, mod_readonly);
    require_semantic_token(semantic_tokens, 4, 11, token_parameter, 0);
    dudu::Json derive_params =
        dudu::JsonParser("{\"position\":{\"line\":2,\"character\":9}}").parse();
    const std::optional<dudu::MacroEditorSelection> derive_selection =
        dudu::macro_selection_at(main, &derive_params);
    assert(derive_selection.has_value());
    assert(derive_selection->reference == "Debug");
    const std::optional<dudu::Symbol> derive_symbol =
        dudu::macro_symbol_for_reference(first, main, *derive_selection);
    assert(derive_symbol.has_value());
    assert(derive_symbol->detail.find("@macro Debug(class)") != std::string::npos);
    assert(derive_symbol->detail.find("score: str") != std::string::npos);

    dudu::Json option_params =
        dudu::JsonParser("{\"position\":{\"line\":4,\"character\":12}}").parse();
    const std::optional<dudu::MacroEditorSelection> option_selection =
        dudu::macro_selection_at(main, &option_params);
    assert(option_selection.has_value());
    assert(option_selection->kind == dudu::MacroEditorSelectionKind::HelperOption);
    assert(option_selection->option == "score");
    const std::optional<dudu::Symbol> option_symbol =
        dudu::macro_symbol_for_reference(first, main, *option_selection);
    assert(option_symbol.has_value());
    assert(option_symbol->detail == "score: str");

    const std::string main_source = "from macros import Debug\n"
                                    "\n"
                                    "@derive(Debug)\n"
                                    "class Player:\n"
                                    "    @Debug(score=\"9\")\n"
                                    "    hp: i32\n"
                                    "\n"
                                    "def score(player: Player) -> i32:\n"
                                    "    return player.debug_score()\n";
    const dudu::Document document{.uri = dudu::file_uri(dir / "src/main.dd"),
                                  .path = dir / "src/main.dd",
                                  .text = main_source};
    dudu::clear_language_server_module_cache();
    const std::string hover = dudu::hover_json(document, "", &derive_params);
    assert(hover.find("@macro Debug(class)") != std::string::npos);
    const std::string definition = dudu::definition_json(document, &derive_params);
    assert(definition.find("macros.dd") != std::string::npos);
    const std::string option_hover = dudu::hover_json(document, "", &option_params);
    assert(option_hover.find("score: str") != std::string::npos);
    dudu::Json generated_params =
        dudu::JsonParser("{\"position\":{\"line\":8,\"character\":21}}").parse();
    const std::string generated_hover = dudu::hover_json(document, "", &generated_params);
    assert(generated_hover.find("Generated by `@Debug`") != std::string::npos);
    const std::string generated_definition = dudu::definition_json(document, &generated_params);
    assert(generated_definition.find("\"line\":2") != std::string::npos);
    const std::string signature = dudu::signature_help_json(&document, &option_params);
    assert(signature.find("@Debug(score: str = ...)") != std::string::npos);
    const std::string completion = dudu::completion_json(&document, &option_params);
    assert(completion.find("\"label\":\"score\"") != std::string::npos);
    const std::string macro_source =
        "import dudu.ast as ast\n"
        "\n"
        "class DebugOptions:\n"
        "    score: str = \"7\"\n"
        "\n"
        "@macro(attributes=DebugOptions)\n"
        "def Debug(item: ast.ClassDecl) -> ast.Expansion:\n"
        "    score: str = \"7\"\n"
        "    for field in item.fields:\n"
        "        options = ast.find_attribute(field.attributes, \"Debug\")\n"
        "        if options.has_value():\n"
        "            score = ast.string_argument(options.value(), \"score\", score)\n"
        "    return_stmt = ast.return_statement(ast.int_expression(score))\n"
        "    method = ast.FunctionDecl(\n"
        "        name=\"debug_score\",\n"
        "        return_type=ast.named_type(\"i32\"),\n"
        "        body=[return_stmt],\n"
        "    )\n"
        "    out = ast.expansion()\n"
        "    out.add_method(method)\n"
        "    return out\n";
    const dudu::Document macro_document{.uri = dudu::file_uri(dir / "src/macros.dd"),
                                        .path = dir / "src/macros.dd",
                                        .text = macro_source};
    const std::map<std::string, dudu::Document> workspace = {{document.uri, document},
                                                             {macro_document.uri, macro_document}};
    const std::string references = dudu::references_json(document, &derive_params, workspace);
    assert(references.find("main.dd") != std::string::npos);
    assert(references.find("macros.dd") != std::string::npos);
    dudu::Json rename_params = dudu::JsonParser("{\"position\":{\"line\":2,\"character\":9},"
                                                "\"newName\":\"Inspectable\"}")
                                   .parse();
    const std::string rename = dudu::rename_json(document, &rename_params, workspace);
    assert(rename.find("\"newText\":\"Inspectable\"") != std::string::npos);
    assert(rename.find("main.dd") != std::string::npos);
    assert(rename.find("macros.dd") != std::string::npos);
    dudu::clear_language_server_module_cache();
    const dudu::ClassDecl& player = player_class(first.merged_module());
    assert(player.methods.size() == 1);
    assert(player.methods.front().name == "debug_score");
    const std::vector<dudu::CppModuleArtifact> artifacts =
        dudu::emit_cpp_module_artifacts(first.merged_module());
    assert(artifacts.size() == 3);
    assert(std::none_of(artifacts.begin(), artifacts.end(), [](const auto& artifact) {
        return artifact.module_path == "macros" || artifact.module_path == "macro_helper" ||
               artifact.module_path == "dudu.ast";
    }));
    assert(std::any_of(artifacts.begin(), artifacts.end(), [](const auto& artifact) {
        return artifact.content.find("debug_score") != std::string::npos;
    }));
    assert(std::any_of(artifacts.begin(), artifacts.end(), [](const auto& artifact) {
        return artifact.content.find("return 9") != std::string::npos;
    }));

    const dudu::ProjectIndex second = dudu::ProjectIndex::load(options);
    assert(second.macro_report().invocations == 1);
    assert(second.macro_report().worker_executions == 0);
    assert(second.macro_report().worker_starts == 0);
    assert(second.macro_report().worker_cache_hits == 1);
    assert(second.macro_report().expansion_cache_hits == 1);
    assert(player_class(second.merged_module()).methods.size() == 1);

    write_file(dir / "src/main.dd", "from macros import Debug\n"
                                    "\n"
                                    "@derive(Debug)\n"
                                    "class Player:\n"
                                    "    @Debug(score=\"9\")\n"
                                    "    hp: i32\n"
                                    "\n"
                                    "def score(player: Player) -> i32:\n"
                                    "    return player.debug_score() + 0\n");
    const dudu::ProjectIndex unrelated = dudu::ProjectIndex::load(options);
    assert(unrelated.macro_report().invocations == 1);
    assert(unrelated.macro_report().worker_executions == 0);
    assert(unrelated.macro_report().expansion_cache_hits == 1);

    write_file(dir / "src/main.dd", "from macros import Debug\n"
                                    "\n"
                                    "@derive(Debug)\n"
                                    "class Player:\n"
                                    "    @Debug(score=\"9\")\n"
                                    "    hp: i32\n"
                                    "    mana: i32\n"
                                    "\n"
                                    "def score(player: Player) -> i32:\n"
                                    "    return player.debug_score() + 0\n");
    const dudu::ProjectIndex declaration_changed = dudu::ProjectIndex::load(options);
    assert(declaration_changed.macro_report().invocations == 1);
    assert(declaration_changed.macro_report().worker_executions == 1);
    assert(declaration_changed.macro_report().worker_starts == 0);
    assert(declaration_changed.macro_report().expansion_cache_hits == 0);

    write_file(dir / "src/main.dd", "from macros import Debug\n"
                                    "\n"
                                    "@derive(Debug)\n"
                                    "class Player:\n"
                                    "    @Debug(score=\"10\")\n"
                                    "    hp: i32\n"
                                    "    mana: i32\n"
                                    "\n"
                                    "def score(player: Player) -> i32:\n"
                                    "    return player.debug_score() + 0\n");
    const dudu::ProjectIndex helper_changed = dudu::ProjectIndex::load(options);
    assert(helper_changed.macro_report().invocations == 1);
    assert(helper_changed.macro_report().worker_executions == 1);
    assert(helper_changed.macro_report().expansion_cache_hits == 0);
    const std::vector<dudu::CppModuleArtifact> changed_artifacts =
        dudu::emit_cpp_module_artifacts(helper_changed.merged_module());
    assert(
        std::any_of(changed_artifacts.begin(), changed_artifacts.end(), [](const auto& artifact) {
            return artifact.content.find("return 10") != std::string::npos;
        }));

    std::string changed_macro_source = macro_source;
    std::string::size_type position = 0;
    while ((position = changed_macro_source.find("\"7\"", position)) != std::string::npos) {
        changed_macro_source.replace(position, 3, "\"8\"");
        position += 3;
    }
    write_file(dir / "src/macros.dd", changed_macro_source);
    const dudu::ProjectIndex macro_changed = dudu::ProjectIndex::load(options);
    assert(macro_changed.macro_report().worker_cache_hits == 0);
    assert(macro_changed.macro_report().worker_executions == 1);
    assert(macro_changed.macro_report().worker_starts == 1);
    assert(macro_changed.macro_report().expansion_cache_hits == 0);
}

void test_imported_macro_definition_is_reported_without_an_invocation() {
    const std::filesystem::path dir =
        std::filesystem::temp_directory_path() / "dudu_macro_definition_catalog_test";
    std::filesystem::remove_all(dir);
    write_file(dir / "dudu.toml", "name = \"macro_definition_catalog\"\n"
                                  "entry = \"src/main.dd\"\n"
                                  "build_dir = \"build\"\n");
    write_file(dir / "src/macros.dd", "import dudu.ast as ast\n"
                                      "\n"
                                      "@macro\n"
                                      "def Debug(item: ast.ClassDecl) -> ast.Expansion:\n"
                                      "    return ast.expansion()\n");
    write_file(dir / "src/main.dd", "from macros import Debug\n"
                                    "\n"
                                    "class Player:\n"
                                    "    hp: i32\n");

    const dudu::ProjectConfig config = dudu::parse_project_config(dir / "dudu.toml");
    dudu::ProjectIndexOptions options;
    options.entry_path = dir / "src/main.dd";
    options.config = config;
    options.source_dir = dir / "src";
    options.force_module_tree = true;
    const dudu::ProjectIndex index = dudu::ProjectIndex::load(options);
    assert(index.macro_report().invocations == 0);
    assert(index.macro_report().definitions.size() == 1);
    assert(index.macro_report().definitions.front().identity == "macros.Debug");
}

void test_macro_helper_schema_accepts_typed_collection_literals() {
    const std::filesystem::path dir =
        std::filesystem::temp_directory_path() / "dudu_macro_collection_options_test";
    std::filesystem::remove_all(dir);
    write_file(dir / "dudu.toml", "name = \"macro_collection_options\"\n"
                                  "entry = \"src/main.dd\"\n"
                                  "build_dir = \"build\"\n");
    write_file(dir / "src/macros.dd",
               "import dudu.ast as ast\n"
               "\n"
               "class Options:\n"
               "    aliases: list[str] = []\n"
               "    labels: set[str] = {}\n"
               "    scores: dict[str, i32] = {}\n"
               "    choice: variant[bool, str] = False\n"
               "\n"
               "@macro(attributes=Options)\n"
               "def Metadata(item: ast.ClassDecl) -> ast.Expansion:\n"
               "    return ast.expansion()\n");
    write_file(dir / "src/main.dd",
               "from macros import Metadata\n"
               "\n"
               "@derive(Metadata)\n"
               "class Player:\n"
               "    @Metadata(\n"
               "        aliases=[\"old\", \"older\"],\n"
               "        labels={\"stable\"},\n"
               "        scores={\"v1\": 1},\n"
               "        choice=\"named\",\n"
               "    )\n"
               "    hp: i32\n");

    const dudu::ProjectConfig config = dudu::parse_project_config(dir / "dudu.toml");
    dudu::ProjectIndexOptions options;
    options.entry_path = dir / "src/main.dd";
    options.config = config;
    options.source_dir = dir / "src";
    options.force_module_tree = true;
    const dudu::ProjectIndex index = dudu::ProjectIndex::load(options);
    assert(index.macro_report().invocations == 1);

    write_file(dir / "src/main.dd",
               "from macros import Metadata\n"
               "\n"
               "@derive(Metadata)\n"
               "class Player:\n"
               "    @Metadata(aliases=[1])\n"
               "    hp: i32\n");
    bool rejected = false;
    try {
        (void)dudu::ProjectIndex::load(options);
    } catch (const dudu::CompileError& error) {
        rejected = std::string(error.what()).find("wrong value type for Metadata.aliases") !=
                   std::string::npos;
    }
    assert(rejected);
}

void test_enum_derive_generates_a_callable_method() {
    const std::filesystem::path dir =
        std::filesystem::temp_directory_path() / "dudu_macro_enum_derive_test";
    std::filesystem::remove_all(dir);
    write_file(dir / "dudu.toml", "name = \"macro_enum_derive_fixture\"\n"
                                  "entry = \"src/main.dd\"\n"
                                  "build_dir = \"build\"\n");
    write_file(dir / "src/macros.dd",
               "import dudu.ast as ast\n"
               "\n"
               "@macro\n"
               "def StringEnum(item: ast.EnumDecl) -> ast.Expansion:\n"
               "    self_type = ast.TypeRef(\n"
               "        kind=ast.TypeKind.Reference,\n"
               "        children=[ast.named_type(\"Self\")],\n"
               "    )\n"
               "    self_param = ast.Parameter(name=\"self\", type=self_type)\n"
               "    value = ast.Expression(\n"
               "        kind=ast.ExpressionKind.StringLiteral, value=item.name,\n"
               "    )\n"
               "    body = ast.Statement(kind=ast.StatementKind.Return, value=value)\n"
               "    method = ast.FunctionDecl(\n"
               "        name=\"enum_name\",\n"
               "        parameters=[self_param],\n"
               "        return_type=ast.named_type(\"str\"),\n"
               "        body=[body],\n"
               "    )\n"
               "    return ast.Expansion(\n"
               "        members=[ast.generated(ast.function_declaration(method))],\n"
               "    )\n");
    write_file(dir / "src/main.dd", "from macros import StringEnum\n"
                                    "\n"
                                    "@derive(StringEnum)\n"
                                    "enum Color:\n"
                                    "    Red\n"
                                    "    Green\n"
                                    "\n"
                                    "def color_name(color: Color) -> str:\n"
                                    "    return color.enum_name()\n");

    const dudu::ProjectConfig config = dudu::parse_project_config(dir / "dudu.toml");
    dudu::ProjectIndexOptions options;
    options.entry_path = dir / "src/main.dd";
    options.config = config;
    options.source_dir = dir / "src";
    options.force_module_tree = true;
    const dudu::ProjectIndex index = dudu::ProjectIndex::load(options);
    assert(index.macro_report().invocations == 1);
    const dudu::EnumDecl& color = find_enum(index.merged_module(), "Color");
    assert(color.methods.size() == 1);
    assert(color.methods.front().name == "enum_name");
    const std::vector<dudu::CppModuleArtifact> artifacts =
        dudu::emit_cpp_module_artifacts(index.merged_module());
    assert(std::any_of(artifacts.begin(), artifacts.end(), [](const auto& artifact) {
        return artifact.content.find("dudu_main_Color_enum_name") != std::string::npos &&
               artifact.content.find("dudu_main_Color_enum_name(color)") != std::string::npos;
    }));
}

void test_enum_derive_generated_match_binds_payloads() {
    const std::filesystem::path dir =
        std::filesystem::temp_directory_path() / "dudu_macro_enum_match_test";
    std::filesystem::remove_all(dir);
    write_file(dir / "dudu.toml", "name = \"macro_enum_match_fixture\"\n"
                                  "entry = \"src/main.dd\"\n"
                                  "build_dir = \"build\"\n");
    write_file(dir / "src/macros.dd",
               "import dudu.ast as ast\n"
               "\n"
               "@macro\n"
               "def PayloadScore(item: ast.EnumDecl) -> ast.Expansion:\n"
               "    self_param = ast.Parameter(\n"
               "        name=\"self\",\n"
               "        type=ast.reference_type(ast.named_type(\"Self\")),\n"
               "    )\n"
               "    payload_pattern = ast.call_expression(\n"
               "        ast.member_expression(ast.name_expression(item.name), \"Payload\"),\n"
               "        [ast.name_expression(\"value\")],\n"
               "    )\n"
               "    payload_case = ast.unguarded_case_statement(\n"
               "        payload_pattern,\n"
               "        [ast.return_statement(ast.name_expression(\"value\"))],\n"
               "    )\n"
               "    empty_case = ast.unguarded_case_statement(\n"
               "        ast.member_expression(ast.name_expression(item.name), \"Empty\"),\n"
               "        [ast.return_statement(ast.int_expression(\"0\"))],\n"
               "    )\n"
               "    method = ast.FunctionDecl(\n"
               "        name=\"payload_score\",\n"
               "        parameters=[self_param],\n"
               "        return_type=ast.named_type(\"i32\"),\n"
               "        body=[ast.match_statement(\n"
               "            ast.name_expression(\"self\"),\n"
               "            [payload_case, empty_case],\n"
               "        )],\n"
               "    )\n"
               "    out = ast.expansion()\n"
               "    out.add_method(method)\n"
               "    return out\n");
    write_file(dir / "src/main.dd", "from macros import PayloadScore\n"
                                    "\n"
                                    "@derive(PayloadScore)\n"
                                    "enum Message:\n"
                                    "    Payload:\n"
                                    "        value: i32\n"
                                    "    Empty\n"
                                    "\n"
                                    "def score(message: Message) -> i32:\n"
                                    "    return message.payload_score()\n");

    const dudu::ProjectConfig config = dudu::parse_project_config(dir / "dudu.toml");
    dudu::ProjectIndexOptions options;
    options.entry_path = dir / "src/main.dd";
    options.config = config;
    options.source_dir = dir / "src";
    options.force_module_tree = true;
    const dudu::ProjectIndex index = dudu::ProjectIndex::load(options);
    const dudu::EnumDecl& message = find_enum(index.merged_module(), "Message");
    assert(message.methods.size() == 1);
    assert(message.methods.front().name == "payload_score");
}

} // namespace

int main() {
    test_dudu_macro_expands_before_semantics_and_caches();
    test_imported_macro_definition_is_reported_without_an_invocation();
    test_macro_helper_schema_accepts_typed_collection_literals();
    test_enum_derive_generates_a_callable_method();
    test_enum_derive_generated_match_binds_payloads();
    return 0;
}
