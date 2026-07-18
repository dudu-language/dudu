#include "dudu/lsp/language_server_completion.hpp"
#include "dudu/lsp/language_server_definition.hpp"
#include "dudu/lsp/language_server_diagnostics.hpp"
#include "dudu/lsp/language_server_hover.hpp"
#include "dudu/lsp/language_server_json.hpp"
#include "dudu/lsp/language_server_member_references.hpp"
#include "dudu/lsp/language_server_navigation.hpp"
#include "dudu/lsp/language_server_reference_collect.hpp"
#include "dudu/lsp/language_server_reference_query.hpp"
#include "dudu/lsp/language_server_references.hpp"
#include "dudu/lsp/language_server_signature_help.hpp"
#include "dudu/lsp/language_server_support.hpp"
#include "dudu/lsp/language_server_symbols.hpp"
#include "dudu/native/native_headers.hpp"
#include "dudu/parser/parser.hpp"
#include "dudu/project/module_loader.hpp"
#include "dudu/project/project_index.hpp"
#include "dudu/sema/sema.hpp"

#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <string>
#include <vector>

namespace {

std::string read_file(const std::filesystem::path& path) {
    std::ifstream file(path);
    if (!file) {
        throw std::runtime_error("could not open " + path.string());
    }
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

void write_file(const std::filesystem::path& path, const std::string& text) {
    std::ofstream file(path);
    if (!file) {
        throw std::runtime_error("could not write " + path.string());
    }
    file << text;
}

void test_lsp_references_keep_unbound_member_query_dotted() {
    const std::filesystem::path dir =
        std::filesystem::temp_directory_path() / "dudu_lsp_member_reference_query_test";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    write_file(dir / "native_methods.hpp", "namespace left {\n"
                                           "struct Thing {\n"
                                           "    int shared() const {\n"
                                           "        return 1;\n"
                                           "    }\n"
                                           "};\n"
                                           "}\n"
                                           "\n"
                                           "namespace right {\n"
                                           "struct Thing {\n"
                                           "    int shared() const {\n"
                                           "        return 2;\n"
                                           "    }\n"
                                           "};\n"
                                           "}\n");

    const dudu::Document doc{.uri = dudu::file_uri(dir / "main.dd"),
                             .path = dir / "main.dd",
                             .text = "from cpp.path import ./native_methods.hpp\n"
                                     "\n"
                                     "def main() -> i32:\n"
                                     "    first: left.Thing\n"
                                     "    second: right.Thing\n"
                                     "    return second.shared() + first.shared()\n"};
    const std::map<std::string, dudu::Document> workspace{{doc.uri, doc}};
    dudu::Json params = dudu::JsonParser("{\"position\":{\"line\":5,\"character\":20}}").parse();
    const std::string refs = dudu::references_json(doc, &params, workspace);
    assert(refs.find("\"start\":{\"line\":5,\"character\":18}") != std::string::npos);
    assert(refs.find("\"start\":{\"line\":5,\"character\":35}") == std::string::npos);
}

void test_lsp_native_field_references_filter_by_receiver_type() {
    const std::filesystem::path dir =
        std::filesystem::temp_directory_path() / "dudu_lsp_native_field_reference_test";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    write_file(dir / "native_widgets.hpp", "struct MatrixWidget {\n"
                                           "    int value;\n"
                                           "};\n"
                                           "\n"
                                           "struct OtherWidget {\n"
                                           "    int value;\n"
                                           "};\n");
    write_file(dir / "main.dd", "from cpp.path import ./native_widgets.hpp\n"
                                "\n"
                                "def main() -> i32:\n"
                                "    widget: MatrixWidget\n"
                                "    return widget.value\n");
    write_file(dir / "same.dd", "from cpp.path import ./native_widgets.hpp\n"
                                "\n"
                                "def same() -> i32:\n"
                                "    widget: MatrixWidget\n"
                                "    return widget.value\n");
    write_file(dir / "other.dd", "from cpp.path import ./native_widgets.hpp\n"
                                 "\n"
                                 "def other() -> i32:\n"
                                 "    widget: OtherWidget\n"
                                 "    return widget.value\n");

    const dudu::Document main_doc{.uri = dudu::file_uri(dir / "main.dd"),
                                  .path = dir / "main.dd",
                                  .text = read_file(dir / "main.dd")};
    const std::map<std::string, dudu::Document> workspace{
        {main_doc.uri, main_doc},
        {dudu::file_uri(dir / "same.dd"),
         {.uri = dudu::file_uri(dir / "same.dd"),
          .path = dir / "same.dd",
          .text = read_file(dir / "same.dd")}},
        {dudu::file_uri(dir / "other.dd"),
         {.uri = dudu::file_uri(dir / "other.dd"),
          .path = dir / "other.dd",
          .text = read_file(dir / "other.dd")}},
    };
    dudu::Json params = dudu::JsonParser("{\"position\":{\"line\":4,\"character\":18}}").parse();
    const std::string refs = dudu::references_json(main_doc, &params, workspace);
    assert(refs.find(dudu::file_uri(dir / "main.dd")) != std::string::npos);
    assert(refs.find(dudu::file_uri(dir / "same.dd")) != std::string::npos);
    assert(refs.find(dudu::file_uri(dir / "other.dd")) == std::string::npos);
}

void test_lsp_references_track_assignment_bindings() {
    const dudu::Document doc{.uri = "file:///refs.dd",
                             .path = "refs.dd",
                             .text = "def pair() -> tuple[i32, i32]:\n"
                                     "    return 1, 2\n"
                                     "\n"
                                     "def main() -> i32:\n"
                                     "    used = 1\n"
                                     "    left, right = pair()\n"
                                     "    return used + left + right\n"};

    const dudu::ModuleAst module = dudu::parse_source(doc.text, doc.path);
    const std::vector<dudu::ReferenceLocation> used_refs = dudu::references_in(module, doc, "used");
    assert(used_refs.size() == 2);
    const std::vector<dudu::ReferenceLocation> left_refs = dudu::references_in(module, doc, "left");
    assert(left_refs.size() == 2);
    const std::vector<dudu::ReferenceLocation> right_refs =
        dudu::references_in(module, doc, "right");
    assert(right_refs.size() == 2);
}

void test_lsp_references_scope_same_named_locals_by_function() {
    const dudu::Document doc{.uri = "file:///scoped_local_refs.dd",
                             .path = "scoped_local_refs.dd",
                             .text = "def first() -> i32:\n"
                                     "    value = 1\n"
                                     "    return value\n"
                                     "\n"
                                     "def second() -> i32:\n"
                                     "    value = 2\n"
                                     "    return value\n"};
    const std::map<std::string, dudu::Document> workspace{{doc.uri, doc}};
    dudu::Json params = dudu::JsonParser("{\"position\":{\"line\":6,\"character\":13}}").parse();
    const std::string refs = dudu::references_json(doc, &params, workspace);
    assert(refs.find("\"start\":{\"line\":5,\"character\":4}") != std::string::npos);
    assert(refs.find("\"start\":{\"line\":6,\"character\":11}") != std::string::npos);
    assert(refs.find("\"start\":{\"line\":1,\"character\":4}") == std::string::npos);
    assert(refs.find("\"start\":{\"line\":2,\"character\":11}") == std::string::npos);
}

void test_lsp_references_track_qualified_type_refs() {
    const dudu::Document doc{.uri = "file:///qualified_type_refs.dd",
                             .path = "qualified_type_refs.dd",
                             .text = "from cpp.path import raylib.h as rl\n"
                                     "\n"
                                     "def length(value: rl.Vector2) -> f32:\n"
                                     "    other: rl.Vector2\n"
                                     "    return value.x + other.x\n"};

    const dudu::ModuleAst module = dudu::parse_source(doc.text, doc.path);
    const std::vector<dudu::ReferenceLocation> refs =
        dudu::references_in(module, doc, "rl.Vector2");
    assert(refs.size() == 2);

    const std::map<std::string, dudu::Document> workspace{{doc.uri, doc}};
    dudu::Json params = dudu::JsonParser("{\"position\":{\"line\":2,\"character\":23}}").parse();
    const std::string refs_json = dudu::references_json(doc, &params, workspace);
    assert(refs_json.find("\"start\":{\"line\":2,\"character\":18}") != std::string::npos);
    assert(refs_json.find("\"start\":{\"line\":3,\"character\":11}") != std::string::npos);
}

void test_lsp_references_use_member_identity() {
    const dudu::Document doc{.uri = "file:///member_identity_refs.dd",
                             .path = "member_identity_refs.dd",
                             .text = "class Player:\n"
                                     "    hp: i32\n"
                                     "\n"
                                     "    def heal(self) -> i32:\n"
                                     "        return self.hp\n"
                                     "\n"
                                     "class Enemy:\n"
                                     "    hp: i32\n"
                                     "\n"
                                     "    def hurt(self) -> i32:\n"
                                     "        return self.hp\n"
                                     "\n"
                                     "def main() -> i32:\n"
                                     "    player: Player = Player(1)\n"
                                     "    enemy: Enemy = Enemy(2)\n"
                                     "    return player.hp + enemy.hp\n"};
    const std::map<std::string, dudu::Document> workspace{{doc.uri, doc}};
    dudu::Json params = dudu::JsonParser("{\"position\":{\"line\":1,\"character\":5}}").parse();
    const std::string refs = dudu::references_json(doc, &params, workspace);
    assert(refs.find("\"start\":{\"line\":1,\"character\":4}") != std::string::npos);
    assert(refs.find("\"start\":{\"line\":4,\"character\":20}") != std::string::npos);
    assert(refs.find("\"start\":{\"line\":15,\"character\":18}") != std::string::npos);
    assert(refs.find("\"start\":{\"line\":7,\"character\":4}") == std::string::npos);
    assert(refs.find("\"start\":{\"line\":10,\"character\":20}") == std::string::npos);
    assert(refs.find("\"start\":{\"line\":15,\"character\":29}") == std::string::npos);

    dudu::Json use_params =
        dudu::JsonParser("{\"position\":{\"line\":15,\"character\":20}}").parse();
    const std::string use_refs = dudu::references_json(doc, &use_params, workspace);
    assert(use_refs.find("\"start\":{\"line\":1,\"character\":4}") != std::string::npos);
    assert(use_refs.find("\"start\":{\"line\":4,\"character\":20}") != std::string::npos);
    assert(use_refs.find("\"start\":{\"line\":15,\"character\":18}") != std::string::npos);
    assert(use_refs.find("\"start\":{\"line\":7,\"character\":4}") == std::string::npos);
    assert(use_refs.find("\"start\":{\"line\":10,\"character\":20}") == std::string::npos);
    assert(use_refs.find("\"start\":{\"line\":15,\"character\":29}") == std::string::npos);
}

void test_lsp_references_use_imported_member_identity_from_use_site() {
    const std::filesystem::path dir =
        std::filesystem::temp_directory_path() / "dudu_lsp_imported_member_refs_test";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    const std::filesystem::path entities_path = dir / "entities.dd";
    const std::filesystem::path main_path = dir / "main.dd";
    const std::string entities_source = "class Player:\n"
                                        "    hp: i32\n"
                                        "\n"
                                        "    def heal(self) -> i32:\n"
                                        "        return self.hp\n"
                                        "\n"
                                        "class Enemy:\n"
                                        "    hp: i32\n"
                                        "\n"
                                        "    def hurt(self) -> i32:\n"
                                        "        return self.hp\n";
    const std::string main_source = "from entities import Player\n"
                                    "from entities import Enemy\n"
                                    "\n"
                                    "def main() -> i32:\n"
                                    "    player: Player = Player(1)\n"
                                    "    enemy: Enemy = Enemy(2)\n"
                                    "    return player.hp + enemy.hp\n";
    write_file(entities_path, entities_source);
    write_file(main_path, main_source);
    const dudu::Document main_doc{
        .uri = dudu::file_uri(main_path), .path = main_path, .text = main_source};
    const dudu::Document entities_doc{
        .uri = dudu::file_uri(entities_path), .path = entities_path, .text = entities_source};
    const std::map<std::string, dudu::Document> workspace{{main_doc.uri, main_doc},
                                                          {entities_doc.uri, entities_doc}};
    dudu::clear_language_server_module_cache();
    dudu::set_language_server_open_documents(workspace);
    const dudu::ProjectIndexSnapshot index_snapshot =
        dudu::project_index_for_document(main_doc, true);
    const dudu::ProjectIndex& index = *index_snapshot;
    const dudu::ModuleAst* entity_unit = index.unit_for_path(entities_path);
    assert(entity_unit != nullptr);
    assert(!dudu::references_in(*entity_unit, entities_doc, "Player.hp").empty());
    dudu::Json params = dudu::JsonParser("{\"position\":{\"line\":6,\"character\":20}}").parse();
    const dudu::ModuleAst& main_unit = index.visible_unit_for_path(main_path);
    const dudu::AstSelection selection = dudu::ast_selection_at(main_unit, &params);
    assert(selection.expr_path.has_value());
    const std::optional<std::string> member_query =
        dudu::member_use_reference_query_at(main_unit, main_doc, *selection.expr_path, &params);
    assert(member_query == "Player.hp");
    const std::string refs = dudu::references_json(main_doc, &params, workspace);
    assert(refs.find(dudu::file_uri(main_path)) != std::string::npos);
    assert(refs.find(dudu::file_uri(entities_path)) != std::string::npos);
    assert(refs.find("\"start\":{\"line\":1,\"character\":4}") != std::string::npos);
    assert(refs.find("\"start\":{\"line\":4,\"character\":20}") != std::string::npos);
    assert(refs.find("\"start\":{\"line\":6,\"character\":18}") != std::string::npos);
    assert(refs.find("\"start\":{\"line\":7,\"character\":4}") == std::string::npos);
    assert(refs.find("\"start\":{\"line\":10,\"character\":20}") == std::string::npos);
    assert(refs.find("\"start\":{\"line\":6,\"character\":29}") == std::string::npos);
    dudu::clear_language_server_module_cache();
}

void test_lsp_references_use_imported_method_identity_from_use_site() {
    const std::filesystem::path dir =
        std::filesystem::temp_directory_path() / "dudu_lsp_imported_method_refs_test";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    const std::filesystem::path entities_path = dir / "entities.dd";
    const std::filesystem::path main_path = dir / "main.dd";
    const std::string entities_source = "class Player:\n"
                                        "    hp: i32\n"
                                        "\n"
                                        "    def move(self, dx: i32) -> i32:\n"
                                        "        self.hp += dx\n"
                                        "        return self.hp\n"
                                        "\n"
                                        "class Enemy:\n"
                                        "    hp: i32\n"
                                        "\n"
                                        "    def move(self, dx: i32) -> i32:\n"
                                        "        self.hp -= dx\n"
                                        "        return self.hp\n";
    const std::string main_source = "from entities import Player\n"
                                    "from entities import Enemy\n"
                                    "\n"
                                    "def main() -> i32:\n"
                                    "    player: Player = Player(1)\n"
                                    "    enemy: Enemy = Enemy(2)\n"
                                    "    return player.move(3) + enemy.move(4)\n";
    write_file(entities_path, entities_source);
    write_file(main_path, main_source);
    const dudu::Document main_doc{
        .uri = dudu::file_uri(main_path), .path = main_path, .text = main_source};
    const dudu::Document entities_doc{
        .uri = dudu::file_uri(entities_path), .path = entities_path, .text = entities_source};
    const std::map<std::string, dudu::Document> workspace{{main_doc.uri, main_doc},
                                                          {entities_doc.uri, entities_doc}};
    dudu::clear_language_server_module_cache();
    dudu::set_language_server_open_documents(workspace);
    const dudu::ProjectIndexSnapshot index_snapshot =
        dudu::project_index_for_document(main_doc, true);
    const dudu::ProjectIndex& index = *index_snapshot;
    const dudu::ModuleAst& main_unit = index.visible_unit_for_path(main_path);
    dudu::Json params = dudu::JsonParser("{\"position\":{\"line\":6,\"character\":20}}").parse();
    const dudu::AstSelection selection = dudu::ast_selection_at(main_unit, &params);
    assert(selection.expr_path.has_value());
    const std::optional<std::string> member_query =
        dudu::member_use_reference_query_at(main_unit, main_doc, *selection.expr_path, &params);
    assert(member_query == "Player.move");
    const std::string refs = dudu::references_json(main_doc, &params, workspace);
    assert(refs.find(dudu::file_uri(main_path)) != std::string::npos);
    assert(refs.find(dudu::file_uri(entities_path)) != std::string::npos);
    assert(refs.find("\"start\":{\"line\":3,\"character\":8}") != std::string::npos);
    assert(refs.find("\"start\":{\"line\":6,\"character\":18}") != std::string::npos);
    assert(refs.find("\"start\":{\"line\":10,\"character\":8}") == std::string::npos);
    assert(refs.find("\"start\":{\"line\":6,\"character\":34}") == std::string::npos);
    dudu::clear_language_server_module_cache();
}

void test_lsp_references_use_enum_value_identity() {
    const dudu::Document doc{.uri = "file:///enum_identity_refs.dd",
                             .path = "enum_identity_refs.dd",
                             .text = "enum Mode:\n"
                                     "    Play\n"
                                     "    Pause\n"
                                     "\n"
                                     "enum Other:\n"
                                     "    Play\n"
                                     "\n"
                                     "def main() -> i32:\n"
                                     "    mode: Mode = Mode.Play\n"
                                     "    other: Other = Other.Play\n"
                                     "    return 0\n"};
    const std::map<std::string, dudu::Document> workspace{{doc.uri, doc}};
    dudu::Json params = dudu::JsonParser("{\"position\":{\"line\":1,\"character\":5}}").parse();
    const std::string refs = dudu::references_json(doc, &params, workspace);
    assert(refs.find("\"start\":{\"line\":1,\"character\":4}") != std::string::npos);
    assert(refs.find("\"start\":{\"line\":8,\"character\":22}") != std::string::npos);
    assert(refs.find("\"start\":{\"line\":5,\"character\":4}") == std::string::npos);
    assert(refs.find("\"start\":{\"line\":9,\"character\":25}") == std::string::npos);
}

void test_lsp_sum_type_variant_hover_and_references_use_identity() {
    const dudu::Document doc{.uri = "file:///sum_type_variant_refs.dd",
                             .path = "sum_type_variant_refs.dd",
                             .text = "enum Token:\n"
                                     "    # Integer token docs.\n"
                                     "    IntLit:\n"
                                     "        value: i64\n"
                                     "\n"
                                     "enum OtherToken:\n"
                                     "    IntLit:\n"
                                     "        value: i64\n"
                                     "\n"
                                     "def main() -> i32:\n"
                                     "    token: Token = Token.IntLit(i64(1))\n"
                                     "    other: OtherToken = OtherToken.IntLit(i64(2))\n"
                                     "    return 0\n"};
    const std::map<std::string, dudu::Document> workspace{{doc.uri, doc}};
    dudu::Json hover_params =
        dudu::JsonParser("{\"position\":{\"line\":10,\"character\":26}}").parse();
    const std::string hover = dudu::hover_json(doc, "", &hover_params);
    assert(hover.find("enum variant Token.IntLit") != std::string::npos);
    assert(hover.find("Integer token docs.") != std::string::npos);

    dudu::Json ref_params = dudu::JsonParser("{\"position\":{\"line\":2,\"character\":5}}").parse();
    const std::string refs = dudu::references_json(doc, &ref_params, workspace);
    assert(refs.find("\"start\":{\"line\":2,\"character\":4}") != std::string::npos);
    assert(refs.find("\"start\":{\"line\":10,\"character\":25}") != std::string::npos);
    assert(refs.find("\"start\":{\"line\":6,\"character\":4}") == std::string::npos);
    assert(refs.find("\"start\":{\"line\":11,\"character\":35}") == std::string::npos);
}

void test_lsp_local_definitions_follow_lexical_shadowing() {
    const dudu::Document doc{.uri = "file:///local_shadow_definition.dd",
                             .path = "local_shadow_definition.dd",
                             .text = "def choose(flag: bool) -> i32:\n"
                                     "    value = 1\n"
                                     "    if flag:\n"
                                     "        value = 2\n"
                                     "        return value\n"
                                     "    return value\n"};

    dudu::Json inner_params =
        dudu::JsonParser("{\"position\":{\"line\":4,\"character\":16}}").parse();
    const std::string inner = dudu::definition_json(doc, &inner_params);
    assert(inner.find("\"start\":{\"line\":3,\"character\":8}") != std::string::npos);

    dudu::Json outer_params =
        dudu::JsonParser("{\"position\":{\"line\":5,\"character\":12}}").parse();
    const std::string outer = dudu::definition_json(doc, &outer_params);
    assert(outer.find("\"start\":{\"line\":1,\"character\":4}") != std::string::npos);
    assert(outer.find("\"start\":{\"line\":3,\"character\":8}") == std::string::npos);
}

} // namespace

int main() {
    try {
        test_lsp_references_keep_unbound_member_query_dotted();
        test_lsp_native_field_references_filter_by_receiver_type();
        test_lsp_references_track_assignment_bindings();
        test_lsp_references_scope_same_named_locals_by_function();
        test_lsp_references_track_qualified_type_refs();
        test_lsp_references_use_member_identity();
        test_lsp_references_use_imported_member_identity_from_use_site();
        test_lsp_references_use_imported_method_identity_from_use_site();
        test_lsp_references_use_enum_value_identity();
        test_lsp_sum_type_variant_hover_and_references_use_identity();
        test_lsp_local_definitions_follow_lexical_shadowing();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
