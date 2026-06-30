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
                             .text = "import c \"native_point.h\"\n"
                                     "\n"
                                     "def main() -> i32:\n"
                                     "    point: NativePoint\n"
                                     "    return point.x\n"};
    dudu::Json params = dudu::JsonParser("{\"position\":{\"line\":3,\"character\":16}}").parse();
    const std::string definition = dudu::definition_json(doc, &params);
    assert(definition.find(dudu::file_uri(dir / "native_point.h")) != std::string::npos);
    assert(definition.find("\"line\":0") != std::string::npos);
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
                             .text = "import cpp \"./native_methods.hpp\"\n"
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
                             .text = "import cpp \"./native_widget.hpp\"\n"
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
                             .text = "import cpp \"./native_methods.hpp\"\n"
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
                             .text = "import c \"./native_factory.h\" as native\n"
                                     "\n"
                                     "def main() -> i32:\n"
                                     "    thing = native.make_thing()\n"
                                     "    thing\n"
                                     "    return 0\n"};
    dudu::Json params = dudu::JsonParser("{\"position\":{\"line\":4,\"character\":6}}").parse();
    const std::string hover = dudu::hover_json(doc, "", &params);
    assert(hover.find("thing: *NativeThing") != std::string::npos);
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
                             .text = "import cpp \"./native_methods.hpp\"\n"
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
    write_file(dir / "main.dd", "import cpp \"./native_widgets.hpp\"\n"
                                "\n"
                                "def main() -> i32:\n"
                                "    widget: MatrixWidget\n"
                                "    return widget.value\n");
    write_file(dir / "same.dd", "import cpp \"./native_widgets.hpp\"\n"
                                "\n"
                                "def same() -> i32:\n"
                                "    widget: MatrixWidget\n"
                                "    return widget.value\n");
    write_file(dir / "other.dd", "import cpp \"./native_widgets.hpp\"\n"
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

void test_lsp_unreachable_lint_uses_branch_structure() {
    const dudu::Document doc{.uri = "",
                             .path = "lint_unreachable.dd",
                             .text = "def choose(x: i32) -> i32:\n"
                                     "    if x < 0:\n"
                                     "        return -1\n"
                                     "    elif x == 0:\n"
                                     "        return 0\n"
                                     "    else:\n"
                                     "        return 1\n"
                                     "    value: i32 = 4\n"
                                     "    return value\n"};
    const std::vector<dudu::Diagnostic> diags = dudu::diagnostics_for_document(doc);
    int unreachable_count = 0;
    for (const dudu::Diagnostic& diag : diags) {
        if (diag.code == "dudu.lint.unreachable") {
            ++unreachable_count;
            assert(diag.location.line == 8);
        }
    }
    assert(unreachable_count == 1);
}

void test_lsp_unreachable_lint_does_not_flag_partial_branch_return() {
    const dudu::Document doc{.uri = "",
                             .path = "lint_partial_branch_return.dd",
                             .text = "def choose(x: i32) -> i32:\n"
                                     "    if x < 0:\n"
                                     "        return -1\n"
                                     "    value: i32 = x + 1\n"
                                     "    return value\n"};
    const std::vector<dudu::Diagnostic> diags = dudu::diagnostics_for_document(doc);
    for (const dudu::Diagnostic& diag : diags) {
        assert(diag.code != "dudu.lint.unreachable");
    }
}

void test_lsp_unreachable_lint_does_not_flag_return_continuation() {
    const dudu::Document doc{.uri = "",
                             .path = "lint_return_continuation.dd",
                             .text = "def value(x: f32) -> f32:\n"
                                     "    return x\n"
                                     "        + 0.5\n"
                                     "        + 0.25\n"};
    const std::vector<dudu::Diagnostic> diags = dudu::diagnostics_for_document(doc);
    for (const dudu::Diagnostic& diag : diags) {
        assert(diag.code != "dudu.lint.unreachable");
    }
}

void test_lsp_scope_lint_tracks_inferred_assignment_locals() {
    const dudu::Document doc{.uri = "",
                             .path = "lint_inferred_locals.dd",
                             .text = "def main() -> i32:\n"
                                     "    used = 1\n"
                                     "    unused = 2\n"
                                     "    return used\n"};
    const std::vector<dudu::Diagnostic> diags = dudu::diagnostics_for_document(doc);
    int unused_count = 0;
    for (const dudu::Diagnostic& diag : diags) {
        if (diag.code == "dudu.lint.unused") {
            ++unused_count;
            assert(diag.location.line == 3);
        }
    }
    assert(unused_count == 1);
}

void test_lsp_suspicious_cast_lint_uses_type_refs() {
    const dudu::Document doc{.uri = "",
                             .path = "lint_suspicious_cast.dd",
                             .text = "def main() -> i32:\n"
                                     "    wide: f64 = 1.0\n"
                                     "    narrow = f32(wide)\n"
                                     "    return i32(narrow)\n"};
    const std::vector<dudu::Diagnostic> diags = dudu::diagnostics_for_document(doc);
    int cast_count = 0;
    for (const dudu::Diagnostic& diag : diags) {
        if (diag.code == "dudu.lint.suspicious_cast") {
            ++cast_count;
            assert(diag.location.line == 3);
        }
    }
    assert(cast_count == 1);
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
                             .text = "import cpp \"raylib.h\" as rl\n"
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
    const dudu::ProjectIndex& index = dudu::project_index_for_document(main_doc, true);
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
    const dudu::ProjectIndex& index = dudu::project_index_for_document(main_doc, true);
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
                                     "    IntLit(i64)\n"
                                     "\n"
                                     "enum OtherToken:\n"
                                     "    IntLit(i64)\n"
                                     "\n"
                                     "def main() -> i32:\n"
                                     "    token: Token = Token.IntLit(i64(1))\n"
                                     "    other: OtherToken = OtherToken.IntLit(i64(2))\n"
                                     "    return 0\n"};
    const std::map<std::string, dudu::Document> workspace{{doc.uri, doc}};
    dudu::Json hover_params =
        dudu::JsonParser("{\"position\":{\"line\":8,\"character\":26}}").parse();
    const std::string hover = dudu::hover_json(doc, "", &hover_params);
    assert(hover.find("enum variant Token.IntLit") != std::string::npos);
    assert(hover.find("Integer token docs.") != std::string::npos);

    dudu::Json ref_params = dudu::JsonParser("{\"position\":{\"line\":2,\"character\":5}}").parse();
    const std::string refs = dudu::references_json(doc, &ref_params, workspace);
    assert(refs.find("\"start\":{\"line\":2,\"character\":4}") != std::string::npos);
    assert(refs.find("\"start\":{\"line\":8,\"character\":25}") != std::string::npos);
    assert(refs.find("\"start\":{\"line\":5,\"character\":4}") == std::string::npos);
    assert(refs.find("\"start\":{\"line\":9,\"character\":35}") == std::string::npos);
}

void test_lsp_module_reference_filters_alias_target() {
    const std::filesystem::path dir =
        std::filesystem::temp_directory_path() / "dudu_lsp_module_reference_target_test";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    write_file(dir / "left.dd", "def answer() -> i32:\n"
                                "    return 1\n");
    write_file(dir / "right.dd", "def answer() -> i32:\n"
                                 "    return 2\n");
    write_file(dir / "main.dd", "import left as m\n"
                                "\n"
                                "def main() -> i32:\n"
                                "    return m.answer()\n");
    write_file(dir / "same.dd", "import left as m\n"
                                "\n"
                                "def same() -> i32:\n"
                                "    return m.answer()\n");
    write_file(dir / "other.dd", "import right as m\n"
                                 "\n"
                                 "def other() -> i32:\n"
                                 "    return m.answer()\n");

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
    dudu::Json params = dudu::JsonParser("{\"position\":{\"line\":3,\"character\":15}}").parse();
    const std::string refs = dudu::references_json(main_doc, &params, workspace);
    assert(refs.find(dudu::file_uri(dir / "main.dd")) != std::string::npos);
    assert(refs.find(dudu::file_uri(dir / "same.dd")) != std::string::npos);
    assert(refs.find(dudu::file_uri(dir / "other.dd")) == std::string::npos);
}

void test_lsp_module_references_include_target_declaration() {
    const std::filesystem::path dir =
        std::filesystem::temp_directory_path() / "dudu_lsp_module_reference_declaration_test";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    write_file(dir / "helper.dd", "def answer() -> i32:\n"
                                  "    return 42\n");
    write_file(dir / "main.dd", "import helper as h\n"
                                "\n"
                                "def main() -> i32:\n"
                                "    return h.answer()\n");

    const dudu::Document main_doc{.uri = dudu::file_uri(dir / "main.dd"),
                                  .path = dir / "main.dd",
                                  .text = read_file(dir / "main.dd")};
    const dudu::Document helper_doc{.uri = dudu::file_uri(dir / "helper.dd"),
                                    .path = dir / "helper.dd",
                                    .text = read_file(dir / "helper.dd")};
    const std::map<std::string, dudu::Document> workspace{{main_doc.uri, main_doc},
                                                          {helper_doc.uri, helper_doc}};
    dudu::Json params = dudu::JsonParser("{\"position\":{\"line\":3,\"character\":15}}").parse();
    const std::string refs = dudu::references_json(main_doc, &params, workspace);
    assert(refs.find(dudu::file_uri(dir / "main.dd")) != std::string::npos);
    assert(refs.find(dudu::file_uri(dir / "helper.dd")) != std::string::npos);
    assert(refs.find("\"start\":{\"line\":0,\"character\":4}") != std::string::npos);
}

void test_lsp_selective_import_references_include_target_declaration() {
    const std::filesystem::path dir =
        std::filesystem::temp_directory_path() / "dudu_lsp_selective_import_reference_test";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    write_file(dir / "helper.dd", "def answer() -> i32:\n"
                                  "    return 42\n");
    write_file(dir / "other_helper.dd", "def answer() -> i32:\n"
                                        "    return 7\n");
    write_file(dir / "main.dd", "from helper import answer as local_answer\n"
                                "\n"
                                "def main() -> i32:\n"
                                "    return local_answer()\n");
    write_file(dir / "same.dd", "from helper import answer as local_answer\n"
                                "\n"
                                "def same() -> i32:\n"
                                "    return local_answer()\n");
    write_file(dir / "other.dd", "from other_helper import answer as local_answer\n"
                                 "\n"
                                 "def other() -> i32:\n"
                                 "    return local_answer()\n");

    const dudu::Document main_doc{.uri = dudu::file_uri(dir / "main.dd"),
                                  .path = dir / "main.dd",
                                  .text = read_file(dir / "main.dd")};
    const dudu::Document helper_doc{.uri = dudu::file_uri(dir / "helper.dd"),
                                    .path = dir / "helper.dd",
                                    .text = read_file(dir / "helper.dd")};
    const std::map<std::string, dudu::Document> workspace{
        {main_doc.uri, main_doc},
        {helper_doc.uri, helper_doc},
        {dudu::file_uri(dir / "same.dd"),
         {.uri = dudu::file_uri(dir / "same.dd"),
          .path = dir / "same.dd",
          .text = read_file(dir / "same.dd")}},
        {dudu::file_uri(dir / "other.dd"),
         {.uri = dudu::file_uri(dir / "other.dd"),
          .path = dir / "other.dd",
          .text = read_file(dir / "other.dd")}},
    };
    dudu::Json params = dudu::JsonParser("{\"position\":{\"line\":3,\"character\":15}}").parse();
    const std::string refs = dudu::references_json(main_doc, &params, workspace);
    assert(refs.find(dudu::file_uri(dir / "main.dd")) != std::string::npos);
    assert(refs.find(dudu::file_uri(dir / "same.dd")) != std::string::npos);
    assert(refs.find(dudu::file_uri(dir / "helper.dd")) != std::string::npos);
    assert(refs.find(dudu::file_uri(dir / "other.dd")) == std::string::npos);
    assert(refs.find("\"start\":{\"line\":0,\"character\":4}") != std::string::npos);
}

void test_lsp_native_references_filter_by_identity() {
    const std::filesystem::path dir =
        std::filesystem::temp_directory_path() / "dudu_lsp_native_reference_identity_test";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    write_file(dir / "left.h", "int shared_name(void);\n");
    write_file(dir / "right.h", "int shared_name(void);\n");
    write_file(dir / "main.dd", "import c \"./left.h\" as n\n"
                                "\n"
                                "def main() -> i32:\n"
                                "    return n.shared_name()\n");
    write_file(dir / "same.dd", "import c \"./left.h\" as n\n"
                                "\n"
                                "def same() -> i32:\n"
                                "    return n.shared_name()\n");
    write_file(dir / "other.dd", "import c \"./right.h\" as n\n"
                                 "\n"
                                 "def other() -> i32:\n"
                                 "    return n.shared_name()\n");

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
    dudu::Json params = dudu::JsonParser("{\"position\":{\"line\":3,\"character\":14}}").parse();
    const std::string refs = dudu::references_json(main_doc, &params, workspace);
    assert(refs.find(dudu::file_uri(dir / "main.dd")) != std::string::npos);
    assert(refs.find(dudu::file_uri(dir / "same.dd")) != std::string::npos);
    assert(refs.find(dudu::file_uri(dir / "other.dd")) == std::string::npos);
}

void test_lsp_references_track_member_path_root_segments() {
    const dudu::Document doc{.uri = "file:///member_root.dd",
                             .path = "member_root.dd",
                             .text = "def main() -> i32:\n"
                                     "    return matrix_space.namespaced_add(2, 3)\n"};
    const dudu::ModuleAst module = dudu::parse_source(doc.text, doc.path);
    const std::vector<dudu::ReferenceLocation> refs =
        dudu::references_in(module, doc, "matrix_space");
    assert(!refs.empty());
    assert(refs.front().range.find("\"line\":1") != std::string::npos);
    assert(refs.front().range.find("\"character\":11") != std::string::npos);
}

void test_lsp_native_namespace_reference_query_uses_selected_segment() {
    const std::filesystem::path dir =
        std::filesystem::temp_directory_path() / "dudu_lsp_native_namespace_query_test";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    write_file(dir / "native_namespace.hpp",
               "#pragma once\n"
               "\n"
               "namespace matrix_space {\n"
               "    inline int namespaced_add(int left, int right) {\n"
               "        return left + right;\n"
               "    }\n"
               "}\n");
    write_file(dir / "main.dd", "import cpp \"native_namespace.hpp\"\n"
                                "\n"
                                "def main() -> i32:\n"
                                "    return matrix_space.namespaced_add(2, 3)\n");
    const dudu::Document doc{.uri = dudu::file_uri(dir / "main.dd"),
                             .path = dir / "main.dd",
                             .text = read_file(dir / "main.dd")};
    const dudu::ProjectIndex& index = dudu::project_index_for_document(doc, true);
    const dudu::ModuleAst& unit = index.visible_unit_for_path(doc.path);
    dudu::Json params = dudu::JsonParser("{\"position\":{\"line\":3,\"character\":13}}").parse();
    const dudu::AstSelection selection = dudu::ast_selection_at(unit, &params);
    const std::string query = dudu::reference_query_at(doc, &params, selection, &unit,
                                                       dudu::symbols_for_module(unit, true));
    assert(query == "matrix_space");
}

void test_lsp_static_class_member_definition_hover_and_references() {
    const std::filesystem::path dir =
        std::filesystem::temp_directory_path() / "dudu_lsp_static_member_navigation_test";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    write_file(dir / "entities.dd", "class Counter:\n"
                                    "    # Count docs.\n"
                                    "    count: static[i32] = 0\n"
                                    "\n"
                                    "    def bump() -> i32:\n"
                                    "        '''Bump docs.'''\n"
                                    "        Counter.count += 1\n"
                                    "        return Counter.count\n"
                                    "\n"
                                    "class OtherCounter:\n"
                                    "    count: static[i32] = 0\n"
                                    "\n"
                                    "    def bump() -> i32:\n"
                                    "        OtherCounter.count += 10\n"
                                    "        return OtherCounter.count\n");
    write_file(dir / "main.dd", "from entities import Counter\n"
                                "\n"
                                "def main() -> i32:\n"
                                "    return Counter.bump() + Counter.count\n");

    const dudu::Document main_doc{.uri = dudu::file_uri(dir / "main.dd"),
                                  .path = dir / "main.dd",
                                  .text = read_file(dir / "main.dd")};
    dudu::Json count_params =
        dudu::JsonParser("{\"position\":{\"line\":3,\"character\":38}}").parse();
    const std::string definition = dudu::definition_json(main_doc, &count_params);
    assert(definition.find(dudu::file_uri(dir / "entities.dd")) != std::string::npos);
    assert(definition.find("\"line\":2") != std::string::npos);

    const std::string hover = dudu::hover_json(main_doc, "", &count_params);
    assert(hover.find("count: i32") != std::string::npos);
    assert(hover.find("Count docs.") != std::string::npos);

    const std::map<std::string, dudu::Document> workspace{
        {main_doc.uri, main_doc},
        {dudu::file_uri(dir / "entities.dd"),
         {.uri = dudu::file_uri(dir / "entities.dd"),
          .path = dir / "entities.dd",
          .text = read_file(dir / "entities.dd")}},
    };
    const std::string refs = dudu::references_json(main_doc, &count_params, workspace);
    assert(refs.find(dudu::file_uri(dir / "main.dd")) != std::string::npos);
    assert(refs.find("\"line\":2") != std::string::npos);
    assert(refs.find("\"line\":6") != std::string::npos);
    assert(refs.find("\"line\":10") == std::string::npos);

    dudu::Json bump_params =
        dudu::JsonParser("{\"position\":{\"line\":3,\"character\":19}}").parse();
    const std::string method_hover = dudu::hover_json(main_doc, "", &bump_params);
    assert(method_hover.find("bump() -> i32") != std::string::npos);
    assert(method_hover.find("Bump docs.") != std::string::npos);
    const std::string method_refs = dudu::references_json(main_doc, &bump_params, workspace);
    assert(method_refs.find("\"line\":4") != std::string::npos);
    assert(method_refs.find("\"line\":12") == std::string::npos);

    dudu::Json completion_params =
        dudu::JsonParser("{\"position\":{\"line\":3,\"character\":19}}").parse();
    const std::string completions = dudu::completion_json(&main_doc, &completion_params);
    assert(completions.find("\"label\":\"count\"") != std::string::npos);
    assert(completions.find("\"label\":\"bump\"") != std::string::npos);
    assert(completions.find("Count docs.") != std::string::npos);
    assert(completions.find("Bump docs.") != std::string::npos);

    dudu::Json signature_params =
        dudu::JsonParser("{\"position\":{\"line\":3,\"character\":24}}").parse();
    const std::string signature = dudu::signature_help_json(&main_doc, &signature_params);
    assert(signature.find("bump() -> i32") != std::string::npos);
    assert(signature.find("Bump docs.") != std::string::npos);
}

} // namespace

int main() {
    try {
        test_lsp_definition_jumps_to_native_header_type();
        test_lsp_definition_jumps_to_parameter_and_inferred_local();
        test_lsp_definition_jumps_to_loop_binding();
        test_lsp_definition_jumps_to_destructured_binding();
        test_lsp_definition_uses_receiver_for_ambiguous_native_methods();
        test_lsp_definition_jumps_to_native_member_field();
        test_lsp_hover_uses_receiver_for_ambiguous_native_methods();
        test_lsp_hover_infers_local_from_native_call();
        test_lsp_references_keep_unbound_member_query_dotted();
        test_lsp_native_field_references_filter_by_receiver_type();
        test_lsp_hover_uses_loaded_module_units();
        test_lsp_hover_uses_ast_doc_comments();
        test_lsp_hover_uses_constant_and_alias_docs();
        test_lsp_hover_uses_imported_ast_doc_comments();
        test_lsp_hover_uses_imported_enum_value_identity();
        test_lsp_hover_uses_imported_member_identity_for_field_docs();
        test_lsp_unreachable_lint_uses_branch_structure();
        test_lsp_unreachable_lint_does_not_flag_partial_branch_return();
        test_lsp_unreachable_lint_does_not_flag_return_continuation();
        test_lsp_scope_lint_tracks_inferred_assignment_locals();
        test_lsp_suspicious_cast_lint_uses_type_refs();
        test_lsp_references_track_assignment_bindings();
        test_lsp_references_scope_same_named_locals_by_function();
        test_lsp_references_track_qualified_type_refs();
        test_lsp_references_use_member_identity();
        test_lsp_references_use_imported_member_identity_from_use_site();
        test_lsp_references_use_imported_method_identity_from_use_site();
        test_lsp_references_use_enum_value_identity();
        test_lsp_sum_type_variant_hover_and_references_use_identity();
        test_lsp_module_reference_filters_alias_target();
        test_lsp_module_references_include_target_declaration();
        test_lsp_selective_import_references_include_target_declaration();
        test_lsp_native_references_filter_by_identity();
        test_lsp_references_track_member_path_root_segments();
        test_lsp_native_namespace_reference_query_uses_selected_segment();
        test_lsp_static_class_member_definition_hover_and_references();
    } catch (const std::exception& error) {
        std::cerr << error.what() << "\n";
        return 1;
    }
    return 0;
}
