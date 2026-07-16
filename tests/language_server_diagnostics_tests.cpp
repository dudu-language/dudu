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
#include "dudu/lsp/language_server_signature_help.hpp"
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
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
