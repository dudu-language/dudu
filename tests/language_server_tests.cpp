#include "dudu/lsp/language_server_completion.hpp"
#include "dudu/lsp/language_server_definition.hpp"
#include "dudu/lsp/language_server_diagnostics.hpp"
#include "dudu/lsp/language_server_hover.hpp"
#include "dudu/lsp/language_server_json.hpp"
#include "dudu/lsp/language_server_navigation.hpp"
#include "dudu/lsp/language_server_reference_collect.hpp"
#include "dudu/lsp/language_server_references.hpp"
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
    assert(help.find("scaled(arg0: i32) -> i32") != std::string::npos);
    assert(help.find("Scale docs for native member.") != std::string::npos);

    dudu::Json hover_params =
        dudu::JsonParser("{\"position\":{\"line\":3,\"character\":14}}").parse();
    const std::string hover = dudu::hover_json(doc, "", &hover_params);
    assert(hover.find("native class NativeWidget") != std::string::npos);
    assert(hover.find("Native widget class docs.") != std::string::npos);

    dudu::Json alias_hover_params =
        dudu::JsonParser("{\"position\":{\"line\":5,\"character\":18}}").parse();
    const std::string alias_hover = dudu::hover_json(doc, "", &alias_hover_params);
    assert(alias_hover.find("native type = NativeWidget") != std::string::npos);
    assert(alias_hover.find("Native widget class docs.") != std::string::npos);
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

} // namespace

int main() {
    try {
        test_lsp_diagnostic_sources_are_structured();
        test_lsp_diagnostics_use_open_buffer_for_module_entry();
        test_lsp_lints_do_not_leak_from_dependency_modules();
        test_lsp_document_symbols_include_ast_doc_summary();
        test_lsp_member_completion_uses_imported_module_shapes();
        test_lsp_completion_uses_visible_imported_functions();
        test_lsp_completion_includes_imported_ast_docs();
        test_lsp_member_completion_includes_ast_docs();
        test_lsp_completion_resolve_preserves_ast_docs();
        test_lsp_signature_help_uses_visible_imported_functions();
        test_lsp_native_member_docs_reach_completion_and_signature_help();
        test_lsp_module_completion_uses_loaded_module_units();
        test_lsp_definition_uses_loaded_module_units();
        test_lsp_project_index_cache_invalidates_imported_file_changes();
        test_lsp_project_index_uses_open_imported_document_sources();
        test_lsp_hover_uses_open_imported_document_sources();
        test_lsp_project_index_cache_records_warm_hits();
        test_lsp_project_index_cache_records_native_warm_hits();
    } catch (const std::exception& error) {
        std::cerr << error.what() << "\n";
        return 1;
    }
    return 0;
}
