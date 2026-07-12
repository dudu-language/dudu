#include "dudu/lsp/language_server_completion.hpp"
#include "dudu/lsp/language_server_definition.hpp"
#include "dudu/lsp/language_server_diagnostics.hpp"
#include "dudu/lsp/language_server_hover.hpp"
#include "dudu/lsp/language_server_inlay_hints.hpp"
#include "dudu/lsp/language_server_json.hpp"
#include "dudu/lsp/language_server_navigation.hpp"
#include "dudu/lsp/language_server_reference_collect.hpp"
#include "dudu/lsp/language_server_references.hpp"
#include "dudu/lsp/language_server_semantic_tokens.hpp"
#include "dudu/lsp/language_server_support.hpp"
#include "dudu/lsp/language_server_symbol_results.hpp"
#include "dudu/native/native_headers.hpp"
#include "dudu/parser/parser.hpp"
#include "dudu/project/module_loader.hpp"
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

void test_lsp_diagnostic_sources_are_structured() {
    const dudu::Document parser_doc{.uri = "",
                                    .path = "parser_diag.dd",
                                    .text = "def main() -> i32\n"
                                            "    return 0\n"};
    const std::vector<dudu::Diagnostic> parser_diags = dudu::diagnostics_for_document(parser_doc);
    assert(parser_diags.size() == 1);
    assert(parser_diags.front().source == "dudu/parser");
    assert(parser_diags.front().code.starts_with("dudu.parser."));

    const dudu::Document sema_doc{.uri = "",
                                  .path = "sema_diag.dd",
                                  .text = "def main() -> i32:\n"
                                          "    return True\n"};
    const std::vector<dudu::Diagnostic> sema_diags = dudu::diagnostics_for_document(sema_doc);
    assert(sema_diags.size() == 1);
    assert(sema_diags.front().source == "dudu/sema");
    assert(sema_diags.front().code.starts_with("dudu.sema."));
}

void test_lsp_parser_recovery_reports_multiple_diagnostics() {
    const dudu::Document doc{.uri = "",
                             .path = "recover_multiple.dd",
                             .text = "not a declaration\n"
                                     "\n"
                                     "def broken() -> i32\n"
                                     "    return 1\n"
                                     "\n"
                                     "def usable() -> i32:\n"
                                     "    return 2\n"};
    const std::vector<dudu::Diagnostic> diagnostics = dudu::diagnostics_for_document(doc);
    assert(diagnostics.size() == 2);
    assert(diagnostics[0].source == "dudu/parser");
    assert(diagnostics[1].source == "dudu/parser");
    assert(diagnostics[0].location.line == 1);
    assert(diagnostics[1].location.line == 3);
}

void test_lsp_semantic_recovery_reports_independent_body_errors() {
    const dudu::Document doc{.uri = "",
                             .path = "recover_semantic_bodies.dd",
                             .text = "def wrong_number() -> i32:\n"
                                     "    return \"bad\"\n"
                                     "\n"
                                     "def wrong_bool() -> bool:\n"
                                     "    return 1\n"
                                     "\n"
                                     "def usable() -> i32:\n"
                                     "    return 3\n"};
    const std::vector<dudu::Diagnostic> diagnostics = dudu::diagnostics_for_document(doc);
    assert(diagnostics.size() == 2);
    assert(diagnostics[0].source == "dudu/sema");
    assert(diagnostics[1].source == "dudu/sema");
    assert(diagnostics[0].location.line == 2);
    assert(diagnostics[1].location.line == 5);

    const dudu::ProjectIndex& project = dudu::project_index_for_document(doc, false);
    assert(project.merged_module().functions.size() == 3);
    assert(project.merged_module().functions.back().name == "usable");
}

void test_lsp_parser_error_only_suppresses_its_damaged_body() {
    const dudu::Document doc{.uri = "",
                             .path = "recover_parser_and_sema.dd",
                             .text = "def damaged() -> i32:\n"
                                     "    broken = call(\n"
                                     "    return missing_after_broken_statement\n"
                                     "\n"
                                     "def independently_wrong() -> bool:\n"
                                     "    return 1\n"};
    const std::vector<dudu::Diagnostic> diagnostics = dudu::diagnostics_for_document(doc);
    assert(diagnostics.size() == 2);
    assert(diagnostics[0].source == "dudu/parser");
    assert(diagnostics[0].location.line == 2);
    assert(diagnostics[1].source == "dudu/sema");
    assert(diagnostics[1].location.line == 6);
    assert(diagnostics[1].message.find("missing_after_broken_statement") == std::string::npos);
}

void test_lsp_editor_requests_use_recovered_current_ast() {
    const std::filesystem::path dir =
        std::filesystem::temp_directory_path() / "dudu_lsp_recovered_editor_requests";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    const std::filesystem::path path = dir / "main.dd";
    write_file(path, "def main() -> i32:\n"
                     "    return 0\n");

    const dudu::Document doc{.uri = dudu::file_uri(path),
                             .path = path,
                             .text = "not a declaration\n"
                                     "\n"
                                     "def usable(value: i32) -> i32:\n"
                                     "    return value + 1\n"
                                     "\n"
                                     "def main() -> i32:\n"
                                     "    result = usable(1)\n"
                                     "    return result\n"};
    dudu::clear_language_server_module_cache();

    dudu::Json call_params =
        dudu::JsonParser("{\"position\":{\"line\":6,\"character\":15}}").parse();
    const std::string definition = dudu::definition_json(doc, &call_params);
    assert(definition.find(doc.uri) != std::string::npos);
    assert(definition.find("\"line\":2") != std::string::npos);

    const std::string hover = dudu::hover_json(doc, "usable", &call_params);
    assert(hover.find("def usable(value: i32) -> i32") != std::string::npos);

    dudu::Json declaration_params =
        dudu::JsonParser("{\"position\":{\"line\":2,\"character\":5}}").parse();
    const std::string references =
        dudu::references_json(doc, &declaration_params, {{doc.uri, doc}});
    assert(references.find("\"line\":6") != std::string::npos);

    const std::string hints = dudu::inlay_hints_json(doc, nullptr);
    assert(hints.find("\"value\":\"i32\"") != std::string::npos);

    const dudu::ProjectIndex& project = dudu::project_index_for_document(doc, false);
    const std::string tokens = dudu::semantic_tokens_json(project, path, project);
    assert(tokens != "{\"data\":[]}");

    const dudu::Document completion_doc{.uri = doc.uri,
                                        .path = doc.path,
                                        .text = "not a declaration\n"
                                                "\n"
                                                "def usable(value: i32) -> i32:\n"
                                                "    return value + 1\n"
                                                "\n"
                                                "def main() -> i32:\n"
                                                "    return usa\n"};
    dudu::Json completion_params =
        dudu::JsonParser("{\"position\":{\"line\":6,\"character\":14}}").parse();
    const std::string completions = dudu::completion_json(&completion_doc, &completion_params);
    assert(completions.find("\"label\":\"usable\"") != std::string::npos);
    dudu::clear_language_server_module_cache();
}

void test_lsp_index_diagnostic_preserves_candidate_reasons() {
    const dudu::Document doc{.uri = "",
                             .path = "bad_index_candidate_diag.dd",
                             .text = "class Choice:\n"
                                     "    @operator(\"[]\")\n"
                                     "    def by_value(self, index: i32) -> i32:\n"
                                     "        return index\n"
                                     "\n"
                                     "    @operator(\"[]\")\n"
                                     "    def by_pair(self, row: i32, col: i32) -> i32:\n"
                                     "        return row + col\n"
                                     "\n"
                                     "\n"
                                     "def main() -> i32:\n"
                                     "    value = Choice()\n"
                                     "    return value[1, \"bad\"]\n"};
    const std::vector<dudu::Diagnostic> diags = dudu::diagnostics_for_document(doc);
    assert(diags.size() == 1);
    assert(diags.front().source == "dudu/sema");
    assert(diags.front().message.find("no matching @operator(\"[]\")") != std::string::npos);
    assert(diags.front().message.find(
               "candidate Choice.by_value(i32) -> i32 rejected: expects 1 arguments, got 2") !=
           std::string::npos);
    assert(diags.front().message.find("candidate Choice.by_pair(i32, i32) -> i32 rejected: "
                                      "argument 2 expects i32, got str") != std::string::npos);
}

void test_lsp_shape_mismatch_diagnostic_explains_axis() {
    const dudu::Document doc{.uri = "",
                             .path = "bad_shape_annotation.dd",
                             .text = "def main() -> i32:\n"
                                     "    matrix: array[i32] = [\n"
                                     "        [1, 2, 3, 4],\n"
                                     "        [10, 20, 30, 40],\n"
                                     "        [100, 200, 300, 400],\n"
                                     "    ]\n"
                                     "    col: array_view[i32][4] = matrix[:, 1]\n"
                                     "    return col[0]\n"};
    const std::vector<dudu::Diagnostic> diags = dudu::diagnostics_for_document(doc);
    assert(diags.size() == 1);
    assert(diags.front().source == "dudu/sema");
    assert(diags.front().message.find("cannot assign array_view[i32][3] to "
                                      "array_view[i32][4]") != std::string::npos);
    assert(diags.front().message.find("shape mismatch: expected [4], got [3]") !=
           std::string::npos);
    assert(diags.front().message.find("axis 0 expected 4, got 3") != std::string::npos);
}

void test_lsp_block_header_diagnostics_use_extra_token_location() {
    const dudu::Document doc{.uri = "",
                             .path = "bad_if_header.dd",
                             .text = "def main() -> i32:\n"
                                     "    if fps_elapsed_ms: i32 >= 250:\n"
                                     "        return 1\n"
                                     "    return 0\n"};
    const std::vector<dudu::Diagnostic> diags = dudu::diagnostics_for_document(doc);
    assert(diags.size() == 1);
    assert(diags.front().source == "dudu/parser");
    assert(diags.front().message.find("unexpected tokens after if header") != std::string::npos);
    assert(diags.front().location.line == 2);
    assert(diags.front().location.column == 24);
}

void test_lsp_diagnostics_use_open_buffer_for_module_entry() {
    const std::filesystem::path dir =
        std::filesystem::temp_directory_path() / "dudu_lsp_open_buffer_module_test";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    write_file(dir / "helper.dd", "def helper() -> i32:\n"
                                  "    return 7\n");
    write_file(dir / "main.dd", "def main() -> i32:\n"
                                "    return helper()\n");

    const dudu::Document doc{.uri = dudu::file_uri(dir / "main.dd"),
                             .path = dir / "main.dd",
                             .text = "from helper import helper\n"
                                     "\n"
                                     "def main() -> i32:\n"
                                     "    return helper()\n"};
    const std::vector<dudu::Diagnostic> diags = dudu::diagnostics_for_document(doc);
    for (const dudu::Diagnostic& diag : diags) {
        assert(diag.source != "dudu/sema");
        assert(diag.message.find("helper") == std::string::npos);
    }
}

void test_lsp_lints_do_not_leak_from_dependency_modules() {
    const std::filesystem::path dir =
        std::filesystem::temp_directory_path() / "dudu_lsp_dependency_lint_scope_test";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    write_file(dir / "helper.dd", "def helper() -> i32:\n"
                                  "    unused = 9\n"
                                  "    return 7\n");
    write_file(dir / "main.dd", "from helper import helper\n"
                                "\n"
                                "def main() -> i32:\n"
                                "    return helper()\n");

    const dudu::Document doc{.uri = dudu::file_uri(dir / "main.dd"),
                             .path = dir / "main.dd",
                             .text = read_file(dir / "main.dd")};
    const std::vector<dudu::Diagnostic> diags = dudu::diagnostics_for_document(doc);
    for (const dudu::Diagnostic& diag : diags) {
        assert(diag.code != "dudu.lint.unused");
    }
}

void test_lsp_document_symbols_include_ast_doc_summary() {
    const dudu::Document doc{.uri = "file:///doc_symbols.dd",
                             .path = "doc_symbols.dd",
                             .text = "# Player state docs.\n"
                                     "class Player:\n"
                                     "    hp: i32\n"};
    const std::string symbols = dudu::document_symbols_json(doc);
    assert(symbols.find("\"selectionRange\"") != std::string::npos);
    assert(symbols.find("\"location\"") == std::string::npos);
    assert(symbols.find("class Player - Player state docs.") != std::string::npos);
}

void test_lsp_member_completion_uses_imported_module_shapes() {
    const std::filesystem::path dir =
        std::filesystem::temp_directory_path() / "dudu_lsp_imported_member_completion_test";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    write_file(dir / "vec3.dd", "class Vec3:\n"
                                "    x: f32\n"
                                "    y: f32\n");
    write_file(dir / "main.dd", "def main() -> i32:\n"
                                "    return 0\n");

    const dudu::Document doc{.uri = dudu::file_uri(dir / "main.dd"),
                             .path = dir / "main.dd",
                             .text = "from vec3 import Vec3\n"
                                     "\n"
                                     "def main() -> i32:\n"
                                     "    v: Vec3 = Vec3(x=1.0, y=2.0)\n"
                                     "    v.x\n"
                                     "    return 0\n"};
    dudu::Json params = dudu::JsonParser("{\"position\":{\"line\":4,\"character\":6}}").parse();
    const std::string completions = dudu::completion_json(&doc, &params);
    assert(completions.find("\"label\":\"x\"") != std::string::npos);
    assert(completions.find("\"label\":\"y\"") != std::string::npos);
}

void test_lsp_completion_uses_visible_imported_functions() {
    const std::filesystem::path dir =
        std::filesystem::temp_directory_path() / "dudu_lsp_imported_function_completion_test";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    write_file(dir / "helper.dd", "def helper(value: i32) -> i32:\n"
                                  "    return value + 1\n");
    write_file(dir / "main.dd", "def main() -> i32:\n"
                                "    return 0\n");

    const dudu::Document doc{.uri = dudu::file_uri(dir / "main.dd"),
                             .path = dir / "main.dd",
                             .text = "from helper import helper\n"
                                     "\n"
                                     "def main() -> i32:\n"
                                     "    return hel\n"};
    dudu::Json params = dudu::JsonParser("{\"position\":{\"line\":3,\"character\":14}}").parse();
    const std::string completions = dudu::completion_json(&doc, &params);
    assert(completions.find("\"label\":\"helper\"") != std::string::npos);
    assert(completions.find("helper(i32) -> i32") != std::string::npos);
}

void test_lsp_completion_includes_imported_ast_docs() {
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "dudu_lsp_comp_doc";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    write_file(dir / "helper.dd", "# Adds one for completion docs.\n"
                                  "def helper(value: i32) -> i32:\n"
                                  "    return value + 1\n");
    write_file(dir / "main.dd", "def main() -> i32:\n"
                                "    return 0\n");

    const dudu::Document doc{.uri = dudu::file_uri(dir / "main.dd"),
                             .path = dir / "main.dd",
                             .text = "from helper import helper\n"
                                     "\n"
                                     "def main() -> i32:\n"
                                     "    return hel\n"};
    dudu::Json params = dudu::JsonParser("{\"position\":{\"line\":3,\"character\":14}}").parse();
    const std::string completions = dudu::completion_json(&doc, &params);
    assert(completions.find("\"label\":\"helper\"") != std::string::npos);
    assert(completions.find("Adds one for completion docs.") != std::string::npos);
}

void test_lsp_member_completion_includes_ast_docs() {
    const std::filesystem::path dir =
        std::filesystem::temp_directory_path() / "dudu_lsp_member_comp_doc";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    write_file(dir / "vec3.dd", "class Vec3:\n"
                                "    # Horizontal component.\n"
                                "    x: f32\n"
                                "\n"
                                "    # Normalize this vector.\n"
                                "    def normalized(self) -> Vec3:\n"
                                "        return self\n");
    write_file(dir / "main.dd", "def main() -> i32:\n"
                                "    return 0\n");

    const dudu::Document doc{.uri = dudu::file_uri(dir / "main.dd"),
                             .path = dir / "main.dd",
                             .text = "from vec3 import Vec3\n"
                                     "\n"
                                     "def main() -> i32:\n"
                                     "    v: Vec3 = Vec3(x=1.0)\n"
                                     "    v.\n"
                                     "    return 0\n"};
    dudu::Json params = dudu::JsonParser("{\"position\":{\"line\":4,\"character\":6}}").parse();
    const std::string completions = dudu::completion_json(&doc, &params);
    assert(completions.find("\"label\":\"x\"") != std::string::npos);
    assert(completions.find("Horizontal component.") != std::string::npos);
    assert(completions.find("\"label\":\"normalized\"") != std::string::npos);
    assert(completions.find("Normalize this vector.") != std::string::npos);
}

void test_lsp_completion_resolve_preserves_ast_docs() {
    const dudu::Json params =
        dudu::JsonParser("{\"label\":\"helper\",\"kind\":3,\"detail\":\"def helper()\","
                         "\"documentation\":{\"kind\":\"markdown\","
                         "\"value\":\"Existing AST docs.\"}}")
            .parse();
    const std::string resolved = dudu::completion_resolve_json(&params);
    assert(resolved.find("Existing AST docs.") != std::string::npos);
    assert(resolved.find("def helper()") != std::string::npos);
}

void test_lsp_inlay_hints_show_inferred_types_and_receiver() {
    const dudu::Document doc{.uri = "file:///inlay_hints.dd",
                             .path = "inlay_hints.dd",
                             .text = "class Counter:\n"
                                     "    value: i32\n"
                                     "\n"
                                     "    def bump(self, amount: i32) -> &Self:\n"
                                     "        next = self.value + amount\n"
                                     "        self.value = next\n"
                                     "        return self\n"
                                     "\n"
                                     "def total(values: list[i32]) -> i32:\n"
                                     "    sum = 0\n"
                                     "    for value in values:\n"
                                     "        sum += value\n"
                                     "    return sum\n"
                                     "\n"
                                     "def add(left: i32, right: i32) -> i32:\n"
                                     "    return left + right\n"
                                     "\n"
                                     "def main() -> i32:\n"
                                     "    counter = Counter(7)\n"
                                     "    return add(counter.value, 2)\n"};
    const std::string hints = dudu::inlay_hints_json(doc, nullptr);
    assert(hints.find("\"label\":\": &Self\"") != std::string::npos);
    assert(hints.find("\"line\":3") != std::string::npos);
    assert(hints.find("\"value\":\"i32\"") != std::string::npos);
    assert(hints.find("size = 4 bytes, align = 4 bytes") != std::string::npos);
    assert(hints.find("\"value\":\"Counter\"") != std::string::npos);
    assert(hints.find("class Counter") != std::string::npos);
    assert(hints.find("\"location\"") != std::string::npos);
    assert(hints.find("\"line\":4") != std::string::npos);
    assert(hints.find("\"line\":9") != std::string::npos);
    assert(hints.find("\"line\":10") != std::string::npos);
    assert(hints.find("\"label\":\"value:\"") != std::string::npos);
    assert(hints.find("\"label\":\"left:\"") != std::string::npos);
    assert(hints.find("\"label\":\"right:\"") != std::string::npos);
    assert(hints.find("\"label\":\"left:\",\"kind\":2,\"paddingLeft\":true,\"tooltip\"") !=
           std::string::npos);
    assert(hints.find("\"label\":\"right:\",\"kind\":2,\"paddingLeft\":true,\"tooltip\"") !=
           std::string::npos);

    dudu::InlayHintOptions quiet;
    quiet.parameter_names = false;
    const std::string quiet_hints = dudu::inlay_hints_json(doc, nullptr, quiet);
    assert(quiet_hints.find("\"label\":\"left:\"") == std::string::npos);
}

void test_lsp_inlay_hints_show_inferred_tensor_view_shapes() {
    const std::filesystem::path dir =
        std::filesystem::temp_directory_path() / "dudu_lsp_tensor_inlay_shape_test";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    write_file(dir / "tensor.dd", "class Mask:\n"
                                  "    data: list[bool]\n"
                                  "\n"
                                  "    @operator(\"[]\")\n"
                                  "    def at(self, index: i32) -> bool:\n"
                                  "        return self.data[index]\n"
                                  "\n"
                                  "class Tensor[T]:\n"
                                  "    rows: i32\n"
                                  "    cols: i32\n"
                                  "    data: list[T]\n"
                                  "\n"
                                  "    @operator(\"[]\")\n"
                                  "    def masked_rows(self, mask: Mask, columns: slice) -> "
                                  "Tensor[T][dyn, 2]:\n"
                                  "        return Tensor[T](0, 2, [])\n"
                                  "\n"
                                  "def zeros[T](rows: i32, cols: i32) -> Tensor[T]:\n"
                                  "    return Tensor[T](rows, cols, [])\n");
    write_file(dir / "main.dd", "from tensor import Mask\n"
                                "from tensor import Tensor\n"
                                "from tensor import zeros\n"
                                "\n"
                                "def main() -> i32:\n"
                                "    values = zeros[f32](4, 2)\n"
                                "    mask = Mask([True, False, True, False])\n"
                                "    selected = values[mask, :]\n"
                                "    return selected.rows\n");

    const dudu::Document doc{.uri = dudu::file_uri(dir / "main.dd"),
                             .path = dir / "main.dd",
                             .text = read_file(dir / "main.dd")};
    const std::string hints = dudu::inlay_hints_json(doc, nullptr);
    assert(hints.find("\"line\":7") != std::string::npos);
    assert(hints.find("Tensor[f32][dyn, 2]") != std::string::npos);
    assert(hints.find("\"value\":\"Tensor\"") != std::string::npos);
    assert(hints.find("class Tensor") != std::string::npos);
    assert(hints.find("\"location\"") != std::string::npos);
    assert(hints.find("tensor.dd") != std::string::npos);
    assert(hints.find("\"value\":\"dyn\"") != std::string::npos);
}

void test_lsp_inlay_hints_use_inferred_array_literal_shapes() {
    const dudu::Document doc{.uri = "",
                             .path = "array_literal_shape_inlay.dd",
                             .text = "def main() -> i32:\n"
                                     "    matrix: array[i32] = [\n"
                                     "        [1, 2, 3, 4],\n"
                                     "        [10, 20, 30, 40],\n"
                                     "        [100, 200, 300, 400],\n"
                                     "    ]\n"
                                     "    col = matrix[:, 1]\n"
                                     "    row = matrix[1, :]\n"
                                     "    patch = matrix[1:3, 2:4]\n"
                                     "    expanded = matrix[:, None, 1]\n"
                                     "    same = matrix[...]\n"
                                     "    return col[1] + row[2] + patch[1, 0] + expanded[2, 0] + "
                                     "same[2, 3]\n"};

    const std::string hints = dudu::inlay_hints_json(doc, nullptr);
    assert(hints.find("\"line\":6") != std::string::npos);
    assert(hints.find("\"line\":7") != std::string::npos);
    assert(hints.find("\"line\":8") != std::string::npos);
    assert(hints.find("\"line\":9") != std::string::npos);
    assert(hints.find("\"line\":10") != std::string::npos);
    assert(hints.find("array_view[i32][3]") != std::string::npos);
    assert(hints.find("array_view[i32][4]") != std::string::npos);
    assert(hints.find("array_view[i32][2, 2]") != std::string::npos);
    assert(hints.find("array_view[i32][3, 1]") != std::string::npos);
    assert(hints.find("array_view[i32][3, 4]") != std::string::npos);
    assert(hints.find("\": i32\"") == std::string::npos);
}

void test_lsp_inlay_hints_type_value_generic_extents_as_usize() {
    const dudu::Document doc{.uri = "",
                             .path = "value_generic_extent_inlay.dd",
                             .text = "def apply_conv2d[H, W, K](\n"
                                     "    image: &array[f32][H, W],\n"
                                     "    kernel: &array[f32][K, K],\n"
                                     ") -> i32:\n"
                                     "    out_h = H - K + 1\n"
                                     "    out_w = W - K + 1\n"
                                     "    total = 0\n"
                                     "    for y in range(out_h):\n"
                                     "        total += i32(y)\n"
                                     "    return total + i32(out_w)\n"};

    const std::string hints = dudu::inlay_hints_json(doc, nullptr);
    assert(hints.find("\"line\":4") != std::string::npos);
    assert(hints.find("\"line\":5") != std::string::npos);
    assert(hints.find("\"line\":7") != std::string::npos);
    assert(hints.find("\"value\":\"usize\"") != std::string::npos);
}

void test_lsp_hover_describes_tensor_indexing_builtin_types() {
    const dudu::Document doc{.uri = "",
                             .path = "tensor_index_builtin_hover.dd",
                             .text = "def consume(values: array_view[i32], item: basic_index, "
                                     "axis: new_axis) -> i32:\n"
                                     "    return 0\n"};

    dudu::Json array_params =
        dudu::JsonParser("{\"position\":{\"line\":0,\"character\":22}}").parse();
    const std::string array_hover = dudu::hover_json(doc, "", &array_params);
    assert(array_hover.find("type array_view") != std::string::npos);
    assert(array_hover.find("Rank-independent non-owning view") != std::string::npos);

    dudu::Json basic_params =
        dudu::JsonParser("{\"position\":{\"line\":0,\"character\":45}}").parse();
    const std::string basic_hover = dudu::hover_json(doc, "", &basic_params);
    assert(basic_hover.find("type basic_index") != std::string::npos);
    assert(basic_hover.find("scalar indices, slices, ellipsis, and new-axis") != std::string::npos);

    dudu::Json new_axis_params =
        dudu::JsonParser("{\"position\":{\"line\":0,\"character\":64}}").parse();
    const std::string new_axis_hover = dudu::hover_json(doc, "", &new_axis_params);
    assert(new_axis_hover.find("type new_axis") != std::string::npos);
    assert(new_axis_hover.find("produced by `None` inside `[]`") != std::string::npos);
}

void test_lsp_signature_help_uses_visible_imported_functions() {
    const std::filesystem::path dir =
        std::filesystem::temp_directory_path() / "dudu_lsp_imported_function_signature_test";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    write_file(dir / "helper.dd", "# Combines two values for signature docs.\n"
                                  "def helper(value: i32, amount: i32) -> i32:\n"
                                  "    return value + amount\n");
    write_file(dir / "main.dd", "def main() -> i32:\n"
                                "    return 0\n");

    const dudu::Document doc{.uri = dudu::file_uri(dir / "main.dd"),
                             .path = dir / "main.dd",
                             .text = "from helper import helper\n"
                                     "\n"
                                     "def main() -> i32:\n"
                                     "    return helper(1, 2)\n"};
    dudu::Json params = dudu::JsonParser("{\"position\":{\"line\":3,\"character\":22}}").parse();
    const std::string help = dudu::signature_help_json(&doc, &params);
    assert(help.find("helper(i32, i32) -> i32") != std::string::npos);
    assert(help.find("Combines two values for signature docs.") != std::string::npos);
    assert(help.find("\"activeParameter\":1") != std::string::npos);
}

void test_lsp_native_member_docs_reach_completion_and_signature_help() {
    const std::filesystem::path dir =
        std::filesystem::temp_directory_path() / "dudu_lsp_native_member_docs_test";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    write_file(dir / "native_widget.hpp", "#pragma once\n"
                                          "\n"
                                          "/** Native widget class docs. */\n"
                                          "class NativeWidget {\n"
                                          "  public:\n"
                                          "    /** Scale docs for native member. */\n"
                                          "    int scaled(int factor) const {\n"
                                          "        return value * factor;\n"
                                          "    }\n"
                                          "\n"
                                          "    /** Value docs for native field. */\n"
                                          "    int value = 0;\n"
                                          "};\n"
                                          "\n"
                                          "using NativeWidgetAlias = NativeWidget;\n");

    const dudu::Document doc{.uri = dudu::file_uri(dir / "main.dd"),
                             .path = dir / "main.dd",
                             .text = "import cpp \"native_widget.hpp\"\n"
                                     "\n"
                                     "def main() -> i32:\n"
                                     "    widget: NativeWidget\n"
                                     "    widget.value = 5\n"
                                     "    alias_widget: NativeWidgetAlias\n"
                                     "    return widget.scaled(2)\n"};
    dudu::Json completion_params =
        dudu::JsonParser("{\"position\":{\"line\":4,\"character\":11}}").parse();
    const std::string completions = dudu::completion_json(&doc, &completion_params);
    assert(completions.find("\"label\":\"scaled\"") != std::string::npos);
    assert(completions.find("Scale docs for native member.") != std::string::npos);
    assert(completions.find("\"label\":\"value\"") != std::string::npos);
    assert(completions.find("Value docs for native field.") != std::string::npos);

    dudu::Json signature_params =
        dudu::JsonParser("{\"position\":{\"line\":6,\"character\":26}}").parse();
    const std::string help = dudu::signature_help_json(&doc, &signature_params);
    assert(help.find("scaled(factor: i32) -> i32") != std::string::npos);
    assert(help.find("Scale docs for native member.") != std::string::npos);

    dudu::Json hover_params =
        dudu::JsonParser("{\"position\":{\"line\":3,\"character\":14}}").parse();
    const std::string hover = dudu::hover_json(doc, "", &hover_params);
    assert(hover.find("native class NativeWidget") != std::string::npos);
    assert(hover.find("size = 4 bytes, align = 4 bytes") != std::string::npos);
    assert(hover.find("Native widget class docs.") != std::string::npos);

    dudu::Json alias_hover_params =
        dudu::JsonParser("{\"position\":{\"line\":5,\"character\":18}}").parse();
    const std::string alias_hover = dudu::hover_json(doc, "", &alias_hover_params);
    assert(alias_hover.find("native type = NativeWidget") != std::string::npos);
    assert(alias_hover.find("size = 4 bytes, align = 4 bytes") != std::string::npos);
    assert(alias_hover.find("Native widget class docs.") != std::string::npos);
}

void test_lsp_signature_help_resolves_native_callable_values() {
    const std::filesystem::path dir =
        std::filesystem::temp_directory_path() / "dudu_lsp_native_callable_value_test";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    write_file(dir / "native_callback.hpp",
               "#pragma once\n"
               "using NativeTransform = int (*)(int);\n"
               "inline int increment(int value) { return value + 1; }\n"
               "/** Applies the installed native transform. */\n"
               "inline NativeTransform transform = increment;\n");

    const dudu::Document doc{.uri = dudu::file_uri(dir / "main.dd"),
                             .path = dir / "main.dd",
                             .text = "import cpp \"native_callback.hpp\" as native\n"
                                     "\n"
                                     "def main() -> i32:\n"
                                     "    return native.transform(41)\n"};
    dudu::Json params = dudu::JsonParser("{\"position\":{\"line\":3,\"character\":29}}").parse();
    const std::string signature = dudu::signature_help_json(&doc, &params);
    assert(signature.find("native.transform(i32) -> i32") != std::string::npos);
    assert(signature.find("Applies the installed native transform.") != std::string::npos);

    dudu::Json hover_params =
        dudu::JsonParser("{\"position\":{\"line\":3,\"character\":20}}").parse();
    const std::string hover = dudu::hover_json(doc, "", &hover_params);
    assert(hover.find("Applies the installed native transform.") != std::string::npos);
}

void test_lsp_inlay_hints_include_native_parameter_names() {
    const std::filesystem::path dir =
        std::filesystem::temp_directory_path() /
        ("dudu_lsp_native_inlay_" +
         std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    write_file(dir / "native_widget.hpp", "#pragma once\n"
                                          "class NativeWidget {\n"
                                          "  public:\n"
                                          "    void set_position(int x, int y) {}\n"
                                          "};\n"
                                          "inline int native_add(int left, int right) {\n"
                                          "    return left + right;\n"
                                          "}\n"
                                          "typedef struct NativeOpaque NativeOpaque;\n"
                                          "inline NativeOpaque* native_make() {\n"
                                          "    return nullptr;\n"
                                          "}\n");

    const dudu::Document doc{.uri = dudu::file_uri(dir / "main.dd"),
                             .path = dir / "main.dd",
                             .text = "import cpp \"native_widget.hpp\"\n"
                                     "\n"
                                     "def main() -> i32:\n"
                                     "    widget: NativeWidget\n"
                                     "    widget.set_position(3, 4)\n"
                                     "    inferred_widget = NativeWidget()\n"
                                     "    made = native_make()\n"
                                     "    return native_add(1, 2)\n"};
    const std::string hints = dudu::inlay_hints_json(doc, nullptr);
    assert(hints.find("\"label\":\"x:\"") != std::string::npos);
    assert(hints.find("\"label\":\"y:\"") != std::string::npos);
    assert(hints.find("\"label\":\"left:\"") != std::string::npos);
    assert(hints.find("\"label\":\"right:\"") != std::string::npos);
    assert(hints.find("\"label\":\"x:\",\"kind\":2,\"paddingLeft\":true,\"tooltip\"") !=
           std::string::npos);
    assert(hints.find("\"label\":\"left:\",\"kind\":2,\"paddingLeft\":true,\"tooltip\"") !=
           std::string::npos);
    assert(hints.find("\"value\":\"NativeOpaque\"") != std::string::npos);
    assert(hints.find("native type NativeOpaque") != std::string::npos);
    assert(hints.find("native class NativeWidget") != std::string::npos);
    assert(hints.find("size = 1 bytes, align = 1 bytes") != std::string::npos);
    assert(hints.find("\"label\":\"arg0:\"") == std::string::npos);
}

void test_lsp_inlay_hints_include_builtin_method_parameter_names() {
    const dudu::Document doc{.uri = "file:///inlay_builtin_method.dd",
                             .path = "inlay_builtin_method.dd",
                             .text = "def main() -> i32:\n"
                                     "    values: list[i32] = []\n"
                                     "    values.append(7)\n"
                                     "    return 0\n"};
    const std::string hints = dudu::inlay_hints_json(doc, nullptr);
    assert(hints.find("\"label\":\"value:\"") != std::string::npos);
}

void test_lsp_module_completion_uses_loaded_module_units() {
    const std::filesystem::path dir =
        std::filesystem::temp_directory_path() / "dudu_lsp_module_completion_unit_test";
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
                                     "    maths.\n"
                                     "    return 0\n"};
    dudu::Json params = dudu::JsonParser("{\"position\":{\"line\":3,\"character\":10}}").parse();
    const std::string completions = dudu::completion_json(&doc, &params);
    assert(completions.find("\"label\":\"inc\"") != std::string::npos);
    assert(completions.find("inc(value: i32) -> i32") != std::string::npos);
}

void test_lsp_definition_uses_loaded_module_units() {
    const std::filesystem::path dir =
        std::filesystem::temp_directory_path() / "dudu_lsp_module_definition_unit_test";
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
    dudu::Json params = dudu::JsonParser("{\"position\":{\"line\":3,\"character\":18}}").parse();
    const std::string definition = dudu::definition_json(doc, &params);
    assert(definition.find(dudu::file_uri(dir / "maths.dd")) != std::string::npos);
    assert(definition.find("\"line\":0") != std::string::npos);
}

void test_lsp_project_index_cache_invalidates_imported_file_changes() {
    const std::filesystem::path dir =
        std::filesystem::temp_directory_path() / "dudu_lsp_project_index_stale_dep_test";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    const std::filesystem::path dependency = dir / "maths.dd";
    write_file(dependency, "def inc(value: i32) -> i32:\n"
                           "    return value + 1\n");
    write_file(dir / "main.dd", "def main() -> i32:\n"
                                "    return 0\n");

    const dudu::Document doc{.uri = dudu::file_uri(dir / "main.dd"),
                             .path = dir / "main.dd",
                             .text = "import maths\n"
                                     "\n"
                                     "def main() -> i32:\n"
                                     "    return maths.inc(1)\n"};
    dudu::Json params = dudu::JsonParser("{\"position\":{\"line\":3,\"character\":18}}").parse();
    dudu::clear_language_server_module_cache();
    const std::string first_definition = dudu::definition_json(doc, &params);
    assert(first_definition.find(dudu::file_uri(dependency)) != std::string::npos);
    assert(first_definition.find("\"line\":0") != std::string::npos);

    write_file(dependency, "# shifted\n"
                           "def inc(value: i32) -> i32:\n"
                           "    return value + 2\n");
    std::error_code error;
    std::filesystem::last_write_time(
        dependency, std::filesystem::file_time_type::clock::now() + std::chrono::seconds(5), error);
    assert(!error);

    const std::string second_definition = dudu::definition_json(doc, &params);
    assert(second_definition.find(dudu::file_uri(dependency)) != std::string::npos);
    assert(second_definition.find("\"line\":1") != std::string::npos);
    dudu::clear_language_server_module_cache();
}

void test_lsp_project_index_uses_open_imported_document_sources() {
    const std::filesystem::path dir =
        std::filesystem::temp_directory_path() / "dudu_lsp_project_index_open_dep_test";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    const std::filesystem::path dependency = dir / "maths.dd";
    write_file(dependency, "def inc(value: i32) -> i32:\n"
                           "    return value + 1\n");
    write_file(dir / "main.dd", "def main() -> i32:\n"
                                "    return 0\n");

    const dudu::Document main_doc{.uri = dudu::file_uri(dir / "main.dd"),
                                  .path = dir / "main.dd",
                                  .text = "import maths\n"
                                          "\n"
                                          "def main() -> i32:\n"
                                          "    return maths.inc(1)\n"};
    const dudu::Document dependency_doc{.uri = dudu::file_uri(dependency),
                                        .path = dependency,
                                        .text = "# unsaved edit\n"
                                                "def inc(value: i32) -> i32:\n"
                                                "    return value + 2\n"};
    dudu::clear_language_server_module_cache();
    dudu::set_language_server_open_documents(
        {{main_doc.uri, main_doc}, {dependency_doc.uri, dependency_doc}});

    dudu::Json params = dudu::JsonParser("{\"position\":{\"line\":3,\"character\":18}}").parse();
    const std::string definition = dudu::definition_json(main_doc, &params);
    assert(definition.find(dudu::file_uri(dependency)) != std::string::npos);
    assert(definition.find("\"line\":1") != std::string::npos);
    dudu::clear_language_server_module_cache();
}

void test_lsp_hover_uses_open_imported_document_sources() {
    const std::filesystem::path dir =
        std::filesystem::temp_directory_path() / "dudu_lsp_hover_open_dep_test";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    const std::filesystem::path dependency = dir / "maths.dd";
    write_file(dir / "main.dd", "def main() -> i32:\n"
                                "    return 0\n");

    const dudu::Document main_doc{.uri = dudu::file_uri(dir / "main.dd"),
                                  .path = dir / "main.dd",
                                  .text = "import maths\n"
                                          "\n"
                                          "def main() -> i32:\n"
                                          "    return maths.inc(1)\n"};
    const dudu::Document dependency_doc{.uri = dudu::file_uri(dependency),
                                        .path = dependency,
                                        .text = "def inc(value: i32) -> i32:\n"
                                                "    return value + 2\n"};
    dudu::clear_language_server_module_cache();
    dudu::set_language_server_open_documents(
        {{main_doc.uri, main_doc}, {dependency_doc.uri, dependency_doc}});

    dudu::Json params = dudu::JsonParser("{\"position\":{\"line\":3,\"character\":18}}").parse();
    const std::string hover = dudu::hover_json(main_doc, "maths.inc", &params);
    assert(hover.find("def inc(value: i32) -> i32") != std::string::npos);
    dudu::clear_language_server_module_cache();
}

void test_lsp_project_index_cache_records_warm_hits() {
    const std::filesystem::path dir =
        std::filesystem::temp_directory_path() / "dudu_lsp_project_index_stats_test";
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
    dudu::clear_language_server_module_cache();
    dudu::ProjectIndexCacheStats stats = dudu::language_server_project_index_cache_stats();
    assert(stats.entries == 0);
    assert(stats.hits == 0);
    assert(stats.misses == 0);

    (void)dudu::project_index_for_document(doc, false);
    stats = dudu::language_server_project_index_cache_stats();
    assert(stats.entries == 1);
    assert(stats.hits == 0);
    assert(stats.misses == 1);
    assert(stats.loads == 1);

    (void)dudu::project_index_for_document(doc, false);
    stats = dudu::language_server_project_index_cache_stats();
    assert(stats.entries == 1);
    assert(stats.hits == 1);
    assert(stats.misses == 1);
    assert(stats.loads == 1);
    dudu::clear_language_server_module_cache();
}

void test_lsp_project_index_reuses_last_good_after_broken_edit() {
    const std::filesystem::path dir =
        std::filesystem::temp_directory_path() / "dudu_lsp_last_good_index_test";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    const std::filesystem::path path = dir / "main.dd";
    write_file(path, "def answer() -> i32:\n"
                     "    return 42\n");

    const dudu::Document good{.uri = dudu::file_uri(path),
                              .path = path,
                              .text = "def answer() -> i32:\n"
                                      "    return 42\n"};
    dudu::clear_language_server_module_cache();
    const dudu::ProjectIndex& current = dudu::project_index_for_document(good, false);
    assert(current.merged_module().functions.size() == 1);
    assert(current.merged_module().functions.front().name == "answer");

    const dudu::Document broken{.uri = good.uri,
                                .path = path,
                                .text = "def answer() -> i32\n"
                                        "    return 42\n"};
    const dudu::ProjectIndex& recovered = dudu::project_index_for_document(broken, false);
    assert(recovered.merged_module().functions.empty());
    assert(recovered.parse_diagnostics().size() == 1);

    const dudu::Document missing_import{.uri = good.uri,
                                        .path = path,
                                        .text = "import module_that_does_not_exist\n"
                                                "\n"
                                                "def answer() -> i32:\n"
                                                "    return 42\n"};
    const dudu::ProjectIndex& last_good = dudu::project_index_for_document(missing_import, false);
    assert(last_good.merged_module().functions.size() == 1);
    assert(last_good.merged_module().functions.front().name == "answer");
    dudu::clear_language_server_module_cache();
}

void test_lsp_project_index_cache_records_native_warm_hits() {
    const std::filesystem::path dir =
        std::filesystem::temp_directory_path() / "dudu_lsp_native_project_index_stats_test";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    write_file(dir / "native.h", "typedef struct NativeThing { int value; } NativeThing;\n"
                                 "int native_add(int left, int right);\n");
    write_file(dir / "main.dd", "def main() -> i32:\n"
                                "    return 0\n");

    const dudu::Document doc{.uri = dudu::file_uri(dir / "main.dd"),
                             .path = dir / "main.dd",
                             .text = "import c \"native.h\" as native\n"
                                     "\n"
                                     "def main() -> i32:\n"
                                     "    thing: native.NativeThing\n"
                                     "    return native.native_add(thing.value, 1)\n"};
    dudu::clear_language_server_module_cache();
    (void)dudu::project_index_for_document(doc, true);
    dudu::ProjectIndexCacheStats stats = dudu::language_server_project_index_cache_stats();
    assert(stats.entries == 1);
    assert(stats.misses == 1);
    assert(stats.loads == 1);

    (void)dudu::project_index_for_document(doc, true);
    stats = dudu::language_server_project_index_cache_stats();
    assert(stats.entries == 1);
    assert(stats.hits == 1);
    assert(stats.misses == 1);
    assert(stats.loads == 1);
    dudu::clear_language_server_module_cache();
}

void test_lsp_native_context_header_field_definition() {
    const std::filesystem::path dir =
        std::filesystem::temp_directory_path() / "dudu_lsp_native_context_field_test";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    const std::filesystem::path header = dir / "needs_c_context.h";
    write_file(header, "#pragma once\n"
                       "struct DuduNeedsContext {\n"
                       "    size_t count;\n"
                       "    int state;\n"
                       "};\n");
    const std::string source = "from c.path import ./needs_c_context.h as native\n"
                               "\n"
                               "def main() -> i32:\n"
                               "    value: native.DuduNeedsContext\n"
                               "    value.count = 7\n"
                               "    value.state = 35\n"
                               "    return i32(value.count) + value.state\n";
    const std::filesystem::path path = dir / "main.dd";
    write_file(path, source);
    const dudu::Document doc{.uri = dudu::file_uri(path), .path = path, .text = source};

    dudu::clear_language_server_module_cache();
    const dudu::ProjectIndex& index = dudu::project_index_for_document(doc, true);
    const dudu::ModuleAst& unit = index.visible_unit_for_path(path);
    bool saw_class = false;
    bool saw_field = false;
    for (const dudu::ClassDecl& klass : unit.native_classes) {
        if (klass.name != "native.DuduNeedsContext") {
            continue;
        }
        saw_class = true;
        for (const dudu::FieldDecl& field : klass.fields) {
            saw_field = saw_field || field.name == "count";
        }
    }
    assert(saw_class);
    assert(saw_field);

    dudu::Json params = dudu::JsonParser("{\"position\":{\"line\":4,\"character\":11}}").parse();
    const std::string definition = dudu::definition_json(doc, &params);
    assert(definition.find(dudu::file_uri(header)) != std::string::npos);
    dudu::clear_language_server_module_cache();
}

} // namespace

int main() {
    try {
        test_lsp_diagnostic_sources_are_structured();
        test_lsp_parser_recovery_reports_multiple_diagnostics();
        test_lsp_semantic_recovery_reports_independent_body_errors();
        test_lsp_parser_error_only_suppresses_its_damaged_body();
        test_lsp_editor_requests_use_recovered_current_ast();
        test_lsp_index_diagnostic_preserves_candidate_reasons();
        test_lsp_shape_mismatch_diagnostic_explains_axis();
        test_lsp_block_header_diagnostics_use_extra_token_location();
        test_lsp_diagnostics_use_open_buffer_for_module_entry();
        test_lsp_lints_do_not_leak_from_dependency_modules();
        test_lsp_document_symbols_include_ast_doc_summary();
        test_lsp_member_completion_uses_imported_module_shapes();
        test_lsp_completion_uses_visible_imported_functions();
        test_lsp_completion_includes_imported_ast_docs();
        test_lsp_member_completion_includes_ast_docs();
        test_lsp_completion_resolve_preserves_ast_docs();
        test_lsp_inlay_hints_show_inferred_types_and_receiver();
        test_lsp_inlay_hints_show_inferred_tensor_view_shapes();
        test_lsp_inlay_hints_use_inferred_array_literal_shapes();
        test_lsp_inlay_hints_type_value_generic_extents_as_usize();
        test_lsp_hover_describes_tensor_indexing_builtin_types();
        test_lsp_signature_help_uses_visible_imported_functions();
        test_lsp_native_member_docs_reach_completion_and_signature_help();
        test_lsp_signature_help_resolves_native_callable_values();
        test_lsp_inlay_hints_include_native_parameter_names();
        test_lsp_inlay_hints_include_builtin_method_parameter_names();
        test_lsp_module_completion_uses_loaded_module_units();
        test_lsp_definition_uses_loaded_module_units();
        test_lsp_project_index_cache_invalidates_imported_file_changes();
        test_lsp_project_index_uses_open_imported_document_sources();
        test_lsp_hover_uses_open_imported_document_sources();
        test_lsp_project_index_cache_records_warm_hits();
        test_lsp_project_index_reuses_last_good_after_broken_edit();
        test_lsp_project_index_cache_records_native_warm_hits();
        test_lsp_native_context_header_field_definition();
    } catch (const std::exception& error) {
        std::cerr << error.what() << "\n";
        return 1;
    }
    return 0;
}
