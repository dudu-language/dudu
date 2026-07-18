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

void write_file(const std::filesystem::path& path, const std::string& text) {
    std::ofstream file(path);
    if (!file) {
        throw std::runtime_error("could not write " + path.string());
    }
    file << text;
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

void test_lsp_project_index_recovers_bad_indentation_without_last_good_state() {
    const std::filesystem::path dir =
        std::filesystem::temp_directory_path() / "dudu_lsp_bad_indent_recovery_test";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    const std::filesystem::path path = dir / "main.dd";
    const std::string source = "def damaged() -> i32:\n"
                               "    before = 1\n"
                               "  broken = 2\n"
                               "    return before\n"
                               "\n"
                               "def usable() -> i32:\n"
                               "    return 2\n";
    write_file(path, source);
    const dudu::Document doc{.uri = dudu::file_uri(path), .path = path, .text = source};

    dudu::clear_language_server_module_cache();
    const dudu::ProjectIndex& recovered = dudu::project_index_for_document(doc, true);
    assert(recovered.parse_diagnostics().size() == 1);
    assert(recovered.merged_module().functions.size() == 2);
    assert(recovered.merged_module().functions[1].name == "usable");
    dudu::clear_language_server_module_cache();
}

void test_lsp_project_index_isolates_missing_native_headers() {
    const std::filesystem::path dir =
        std::filesystem::temp_directory_path() / "dudu_lsp_missing_native_recovery_test";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    const std::filesystem::path path = dir / "main.dd";
    const std::string source = "from cpp.path import ./header_that_does_not_exist.hpp\n"
                               "\n"
                               "def usable(value: i32) -> i32:\n"
                               "    return value + 1\n";
    write_file(path, source);
    const dudu::Document doc{.uri = dudu::file_uri(path), .path = path, .text = source};

    dudu::clear_language_server_module_cache();
    const dudu::ProjectIndex& recovered = dudu::project_index_for_document(doc, true);
    assert(recovered.merged_module().functions.size() == 1);
    assert(recovered.merged_module().functions.front().name == "usable");

    bool scan_failed = false;
    try {
        (void)dudu::project_index_for_document(doc, true, true, false);
    } catch (const dudu::CompileError& error) {
        scan_failed = error.code() == "dudu.native_header.scan_failed";
    }
    assert(scan_failed);
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
                             .text = "from c.path import native.h as native\n"
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
        test_lsp_module_completion_uses_loaded_module_units();
        test_lsp_definition_uses_loaded_module_units();
        test_lsp_project_index_cache_invalidates_imported_file_changes();
        test_lsp_project_index_uses_open_imported_document_sources();
        test_lsp_hover_uses_open_imported_document_sources();
        test_lsp_project_index_cache_records_warm_hits();
        test_lsp_project_index_reuses_last_good_after_broken_edit();
        test_lsp_project_index_recovers_bad_indentation_without_last_good_state();
        test_lsp_project_index_isolates_missing_native_headers();
        test_lsp_project_index_cache_records_native_warm_hits();
        test_lsp_native_context_header_field_definition();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
