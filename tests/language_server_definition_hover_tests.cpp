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

void write_file(const std::filesystem::path& path, const std::string& text) {
    std::ofstream file(path);
    if (!file) {
        throw std::runtime_error("could not write " + path.string());
    }
    file << text;
}

void test_lsp_definition_jumps_to_native_header_type() {
    const std::filesystem::path dir =
        std::filesystem::temp_directory_path() / "dudu_lsp_native_definition_unit_test";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    write_file(dir / "native_point.h", "typedef struct NativePoint {\n"
                                       "    int x;\n"
                                       "    int y;\n"
                                       "} NativePoint;\n");

    const dudu::Document doc{.uri = dudu::file_uri(dir / "main.dd"),
                             .path = dir / "main.dd",
                             .text = "from c.path import native_point.h\n"
                                     "\n"
                                     "def main() -> i32:\n"
                                     "    point: NativePoint\n"
                                     "    return point.x\n"};
    dudu::Json params = dudu::JsonParser("{\"position\":{\"line\":3,\"character\":16}}").parse();
    const std::string definition = dudu::definition_json(doc, &params);
    assert(definition.find(dudu::file_uri(dir / "native_point.h")) != std::string::npos);
    assert(definition.find("\"line\":0") != std::string::npos);
}

void test_lsp_definition_opens_native_header_from_path_segments_and_alias() {
    const std::filesystem::path dir =
        std::filesystem::temp_directory_path() / "dudu_lsp_native_import_path_test";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir / "vendor");
    write_file(dir / "vendor" / "math.hpp", "#pragma once\n");

    const dudu::Document doc{.uri = dudu::file_uri(dir / "main.dd"),
                             .path = dir / "main.dd",
                             .text = "from cpp.path import ./vendor/math.hpp as math\n"};
    for (const int character : {22, 25, 32, 43}) {
        dudu::Json params = dudu::JsonParser("{\"position\":{\"line\":0,\"character\":" +
                                             std::to_string(character) + "}}")
                                .parse();
        const std::string definition = dudu::definition_json(doc, &params);
        assert(definition.find(dudu::file_uri(dir / "vendor" / "math.hpp")) != std::string::npos);
    }
}

void test_lsp_definition_jumps_to_parameter_and_inferred_local() {
    const dudu::Document doc{.uri = "file:///local_definition.dd",
                             .path = "local_definition.dd",
                             .text = "def add(value: i32, extra: i32) -> i32:\n"
                                     "    total = value + extra\n"
                                     "    return total\n"};

    dudu::Json value_params =
        dudu::JsonParser("{\"position\":{\"line\":1,\"character\":14}}").parse();
    const std::string value_definition = dudu::definition_json(doc, &value_params);
    assert(value_definition.find("\"line\":0") != std::string::npos);

    dudu::Json total_params =
        dudu::JsonParser("{\"position\":{\"line\":2,\"character\":13}}").parse();
    const std::string total_definition = dudu::definition_json(doc, &total_params);
    assert(total_definition.find("\"line\":1") != std::string::npos);
}

void test_lsp_definition_jumps_to_loop_binding() {
    const dudu::Document doc{.uri = "file:///loop_definition.dd",
                             .path = "loop_definition.dd",
                             .text = "def sum_values(values: list[i32]) -> i32:\n"
                                     "    total = 0\n"
                                     "    for value in values:\n"
                                     "        total += value\n"
                                     "    return total\n"};

    dudu::Json params = dudu::JsonParser("{\"position\":{\"line\":3,\"character\":18}}").parse();
    const std::string definition = dudu::definition_json(doc, &params);
    assert(definition.find("\"line\":2") != std::string::npos);
}

void test_lsp_definition_jumps_to_destructured_binding() {
    const dudu::Document doc{.uri = "file:///destructure_definition.dd",
                             .path = "destructure_definition.dd",
                             .text = "def pair() -> tuple[i32, i32]:\n"
                                     "    return 1, 2\n"
                                     "\n"
                                     "def main() -> i32:\n"
                                     "    left, right = pair()\n"
                                     "    return right\n"};

    dudu::Json params = dudu::JsonParser("{\"position\":{\"line\":5,\"character\":13}}").parse();
    const std::string definition = dudu::definition_json(doc, &params);
    assert(definition.find("\"line\":4") != std::string::npos);
}

void test_lsp_definition_uses_receiver_for_ambiguous_native_methods() {
    const std::filesystem::path dir =
        std::filesystem::temp_directory_path() / "dudu_lsp_native_method_definition_test";
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
                                     "    return second.shared()\n"};
    dudu::Json params = dudu::JsonParser("{\"position\":{\"line\":5,\"character\":20}}").parse();
    const std::string definition = dudu::definition_json(doc, &params);
    assert(definition.find(dudu::file_uri(dir / "native_methods.hpp")) != std::string::npos);
    assert(definition.find("\"line\":10") != std::string::npos);
}

void test_lsp_definition_jumps_to_native_member_field() {
    const std::filesystem::path dir =
        std::filesystem::temp_directory_path() / "dudu_lsp_native_field_definition_test";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    write_file(dir / "native_widget.hpp", "class NativeWidget {\n"
                                          "  public:\n"
                                          "    int scaled(int factor) const {\n"
                                          "        return value * factor;\n"
                                          "    }\n"
                                          "    int value = 0;\n"
                                          "};\n");

    const dudu::Document doc{.uri = dudu::file_uri(dir / "main.dd"),
                             .path = dir / "main.dd",
                             .text = "from cpp.path import ./native_widget.hpp\n"
                                     "\n"
                                     "def main() -> i32:\n"
                                     "    widget: NativeWidget\n"
                                     "    return widget.value\n"};
    dudu::Json params = dudu::JsonParser("{\"position\":{\"line\":4,\"character\":18}}").parse();
    const std::string definition = dudu::definition_json(doc, &params);
    assert(definition.find(dudu::file_uri(dir / "native_widget.hpp")) != std::string::npos);
    assert(definition.find("\"line\":5") != std::string::npos);
}

void test_lsp_hover_uses_receiver_for_ambiguous_native_methods() {
    const std::filesystem::path dir =
        std::filesystem::temp_directory_path() / "dudu_lsp_native_method_hover_test";
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
                                           "    float shared() const {\n"
                                           "        return 2.0f;\n"
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
                                     "    return i32(second.shared())\n"};
    dudu::Json params = dudu::JsonParser("{\"position\":{\"line\":5,\"character\":24}}").parse();
    const std::string hover = dudu::hover_json(doc, "second.shared", &params);
    assert(hover.find("shared() -> f32") != std::string::npos);
    assert(hover.find("shared() -> i32") == std::string::npos);
}

void test_lsp_hover_infers_local_from_native_call() {
    const std::filesystem::path dir =
        std::filesystem::temp_directory_path() / "dudu_lsp_native_call_local_hover_test";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    write_file(dir / "native_factory.h", "typedef struct NativeThing {\n"
                                         "    int value;\n"
                                         "} NativeThing;\n"
                                         "NativeThing* make_thing(void);\n");

    const dudu::Document doc{.uri = dudu::file_uri(dir / "main.dd"),
                             .path = dir / "main.dd",
                             .text = "from c.path import ./native_factory.h as native\n"
                                     "\n"
                                     "def main() -> i32:\n"
                                     "    thing = native.make_thing()\n"
                                     "    thing\n"
                                     "    return 0\n"};
    dudu::Json params = dudu::JsonParser("{\"position\":{\"line\":4,\"character\":6}}").parse();
    const std::string hover = dudu::hover_json(doc, "", &params);
    assert(hover.find("thing: *native.NativeThing") != std::string::npos);
}

void test_lsp_hover_uses_loaded_module_units() {
    const std::filesystem::path dir =
        std::filesystem::temp_directory_path() / "dudu_lsp_module_hover_unit_test";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    write_file(dir / "maths.dd", "def inc(value: i32) -> i32:\n"
                                 "    return value + 1\n");
    write_file(dir / "main.dd", "def main() -> i32:\n"
                                "    return 0\n");

    const dudu::Document doc{.uri = dudu::file_uri(dir / "main.dd"),
                             .path = dir / "main.dd",
                             .text = "import maths\n"
                                     "\n"
                                     "def main() -> i32:\n"
                                     "    return maths.inc(1)\n"};
    const std::string hover = dudu::hover_json(doc, "maths.inc");
    assert(hover.find("def inc(value: i32) -> i32") != std::string::npos);
}

void test_lsp_hover_uses_ast_doc_comments() {
    const dudu::Document doc{.uri = "file:///doc_hover.dd",
                             .path = "doc_hover.dd",
                             .text = "# Adds two numbers.\n"
                                     "# Keeps hover docs on the AST.\n"
                                     "def add(left: i32, right: i32) -> i32:\n"
                                     "    return left + right\n"};
    const std::string hover = dudu::hover_json(doc, "add");
    assert(hover.find("def add(left: i32, right: i32) -> i32") != std::string::npos);
    assert(hover.find("Adds two numbers.") != std::string::npos);
    assert(hover.find("Keeps hover docs on the AST.") != std::string::npos);
}

void test_lsp_hover_uses_constant_and_alias_docs() {
    const dudu::Document doc{.uri = "file:///constant_alias_docs.dd",
                             .path = "constant_alias_docs.dd",
                             .text = "# Player id docs.\n"
                                     "type PlayerId = i32\n"
                                     "\n"
                                     "# Default player id docs.\n"
                                     "DEFAULT_PLAYER_ID: PlayerId = 7\n"
                                     "\n"
                                     "def main() -> i32:\n"
                                     "    value: PlayerId = DEFAULT_PLAYER_ID\n"
                                     "    return value\n"};

    dudu::Json alias_params =
        dudu::JsonParser("{\"position\":{\"line\":7,\"character\":12}}").parse();
    const std::string alias_hover = dudu::hover_json(doc, "", &alias_params);
    assert(alias_hover.find("type PlayerId = i32") != std::string::npos);
    assert(alias_hover.find("Player id docs.") != std::string::npos);

    dudu::Json constant_params =
        dudu::JsonParser("{\"position\":{\"line\":7,\"character\":25}}").parse();
    const std::string constant_hover = dudu::hover_json(doc, "", &constant_params);
    assert(constant_hover.find("DEFAULT_PLAYER_ID: PlayerId") != std::string::npos);
    assert(constant_hover.find("Default player id docs.") != std::string::npos);

    const std::string alias_definition = dudu::definition_json(doc, &alias_params);
    assert(alias_definition.find("\"line\":1") != std::string::npos);

    const std::string constant_definition = dudu::definition_json(doc, &constant_params);
    assert(constant_definition.find("\"line\":4") != std::string::npos);
}

void test_lsp_hover_uses_imported_ast_doc_comments() {
    const std::filesystem::path dir =
        std::filesystem::temp_directory_path() / "dudu_lsp_imported_doc_hover_test";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    write_file(dir / "maths.dd", "# Imported increment helper.\n"
                                 "def inc(value: i32) -> i32:\n"
                                 "    return value + 1\n");

    const dudu::Document doc{.uri = dudu::file_uri(dir / "main.dd"),
                             .path = dir / "main.dd",
                             .text = "import maths\n"
                                     "\n"
                                     "def main() -> i32:\n"
                                     "    return maths.inc(1)\n"};
    const std::string hover = dudu::hover_json(doc, "maths.inc");
    assert(hover.find("def inc(value: i32) -> i32") != std::string::npos);
    assert(hover.find("Imported increment helper.") != std::string::npos);
}

void test_lsp_hover_uses_imported_enum_value_identity() {
    const std::filesystem::path dir =
        std::filesystem::temp_directory_path() / "dudu_lsp_imported_enum_hover_test";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    write_file(dir / "entities.dd", "enum Mode:\n"
                                    "    # Player mode docs.\n"
                                    "    Play\n"
                                    "\n"
                                    "enum OtherMode:\n"
                                    "    # Other mode docs.\n"
                                    "    Play\n");

    const dudu::Document doc{.uri = dudu::file_uri(dir / "main.dd"),
                             .path = dir / "main.dd",
                             .text = "from entities import Mode\n"
                                     "\n"
                                     "def main() -> i32:\n"
                                     "    mode: Mode = Mode.Play\n"
                                     "    return 0\n"};
    dudu::Json params = dudu::JsonParser("{\"position\":{\"line\":3,\"character\":22}}").parse();
    const std::string hover = dudu::hover_json(doc, "", &params);
    assert(hover.find("enum variant Mode.Play") != std::string::npos);
    assert(hover.find("Player mode docs.") != std::string::npos);
    assert(hover.find("Other mode docs.") == std::string::npos);
}

void test_lsp_hover_uses_imported_member_identity_for_field_docs() {
    const std::filesystem::path dir =
        std::filesystem::temp_directory_path() / "dudu_lsp_imported_member_hover_test";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    write_file(dir / "entities.dd", "class Player:\n"
                                    "    # Player hit point docs.\n"
                                    "    hp: i32\n"
                                    "\n"
                                    "class Enemy:\n"
                                    "    # Enemy hit point docs.\n"
                                    "    hp: i32\n");

    const dudu::Document doc{.uri = dudu::file_uri(dir / "main.dd"),
                             .path = dir / "main.dd",
                             .text = "from entities import Player\n"
                                     "\n"
                                     "def main() -> i32:\n"
                                     "    player: Player = Player(10)\n"
                                     "    return player.hp\n"};
    dudu::Json params = dudu::JsonParser("{\"position\":{\"line\":4,\"character\":18}}").parse();
    const std::string hover = dudu::hover_json(doc, "", &params);
    assert(hover.find("hp: i32") != std::string::npos);
    assert(hover.find("Player hit point docs.") != std::string::npos);
    assert(hover.find("Enemy hit point docs.") == std::string::npos);
}

void test_lsp_index_operator_definition_and_hover() {
    const dudu::Document doc{.uri = "file:///index_operator_lsp.dd",
                             .path = "index_operator_lsp.dd",
                             .text = "class Tensor:\n"
                                     "    data: list[i32]\n"
                                     "\n"
                                     "    @operator(\"[]\")\n"
                                     "    def at(self, index: i32) -> i32:\n"
                                     "        '''Indexed read docs.'''\n"
                                     "        return self.data[index]\n"
                                     "\n"
                                     "def main() -> i32:\n"
                                     "    tensor = Tensor([10])\n"
                                     "    value = tensor[0]\n"
                                     "    return value\n"};

    dudu::Json bracket_params =
        dudu::JsonParser("{\"position\":{\"line\":10,\"character\":18}}").parse();
    const std::string definition = dudu::definition_json(doc, &bracket_params);
    assert(definition.find("\"line\":4") != std::string::npos);

    const std::string hover = dudu::hover_json(doc, "", &bracket_params);
    assert(hover.find("def at(self: &Self, index: i32) -> i32") != std::string::npos);
    assert(hover.find("Selected `@operator(\\\"[]\\\")` overload.") != std::string::npos);
    assert(hover.find("Result type: `i32`.") != std::string::npos);
    assert(hover.find("Indexed read docs.") != std::string::npos);

    dudu::Json index_arg_params =
        dudu::JsonParser("{\"position\":{\"line\":10,\"character\":19}}").parse();
    const std::string index_arg_definition = dudu::definition_json(doc, &index_arg_params);
    assert(index_arg_definition.find("\"line\":4") == std::string::npos);
}

} // namespace

int main() {
    try {
        test_lsp_definition_jumps_to_native_header_type();
        test_lsp_definition_opens_native_header_from_path_segments_and_alias();
        test_lsp_definition_jumps_to_parameter_and_inferred_local();
        test_lsp_definition_jumps_to_loop_binding();
        test_lsp_definition_jumps_to_destructured_binding();
        test_lsp_definition_uses_receiver_for_ambiguous_native_methods();
        test_lsp_definition_jumps_to_native_member_field();
        test_lsp_hover_uses_receiver_for_ambiguous_native_methods();
        test_lsp_hover_infers_local_from_native_call();
        test_lsp_hover_uses_loaded_module_units();
        test_lsp_hover_uses_ast_doc_comments();
        test_lsp_hover_uses_constant_and_alias_docs();
        test_lsp_hover_uses_imported_ast_doc_comments();
        test_lsp_hover_uses_imported_enum_value_identity();
        test_lsp_hover_uses_imported_member_identity_for_field_docs();
        test_lsp_index_operator_definition_and_hover();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
