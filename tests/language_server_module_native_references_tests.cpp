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
    write_file(dir / "left.h", "static inline int shared_name(void) { return 1; }\n");
    write_file(dir / "right.h", "static inline int shared_name(void) { return 2; }\n");
    write_file(dir / "main.dd", "from c.path import ./left.h as n\n"
                                "\n"
                                "def main() -> i32:\n"
                                "    return n.shared_name()\n");
    write_file(dir / "same.dd", "from c.path import ./left.h as other_alias\n"
                                "\n"
                                "def same() -> i32:\n"
                                "    return other_alias.shared_name()\n");
    write_file(dir / "other.dd", "from c.path import ./right.h as n\n"
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

void test_lsp_native_overload_references_use_selected_identity() {
    const std::filesystem::path dir =
        std::filesystem::temp_directory_path() / "dudu_lsp_native_overload_reference_test";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    write_file(dir / "overloads.hpp", "inline int choose(int value) { return value; }\n"
                                      "inline int choose(double value) { return int(value); }\n");
    write_file(dir / "main.dd", "from cpp.path import ./overloads.hpp as n\n"
                                "\n"
                                "def main() -> i32:\n"
                                "    return n.choose(1)\n");
    write_file(dir / "same.dd", "from cpp.path import ./overloads.hpp as n\n"
                                "\n"
                                "def same() -> i32:\n"
                                "    return n.choose(2)\n");
    write_file(dir / "other.dd", "from cpp.path import ./overloads.hpp as n\n"
                                 "\n"
                                 "def other() -> i32:\n"
                                 "    return n.choose(2.0)\n");

    const dudu::Document main_doc{.uri = dudu::file_uri(dir / "main.dd"),
                                  .path = dir / "main.dd",
                                  .text = read_file(dir / "main.dd")};
    const dudu::ProjectIndex& index = dudu::project_index_for_document(main_doc, true);
    const dudu::ModuleAst& unit = index.visible_unit_for_path(main_doc.path);
    std::set<std::string> overload_identities;
    for (const dudu::NativeFunctionDecl& fn : unit.native_functions) {
        if (fn.name == "n.choose" && !fn.identity.usr.empty()) {
            overload_identities.insert(fn.identity.usr);
        }
    }
    assert(overload_identities.size() == 2);
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

void test_lsp_native_method_overload_references_use_selected_identity() {
    const std::filesystem::path dir =
        std::filesystem::temp_directory_path() / "dudu_lsp_native_method_overload_reference_test";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    write_file(dir / "methods.hpp", "class Picker {\n"
                                    "public:\n"
                                    "    int choose(int value) { return value; }\n"
                                    "    int choose(double value) { return int(value); }\n"
                                    "    static int select(int value) { return value; }\n"
                                    "    static int select(double value) { return int(value); }\n"
                                    "};\n");
    const auto write_use = [&](const std::string& file, const std::string& function,
                               const std::string& value) {
        write_file(dir / file, "from cpp.path import ./methods.hpp as n\n\n"
                               "def " +
                                   function +
                                   "() -> i32:\n"
                                   "    picker: n.Picker\n"
                                   "    return picker.choose(" +
                                   value + ")\n");
    };
    write_use("main.dd", "main", "1");
    write_use("same.dd", "same", "2");
    write_use("other.dd", "other", "2.0");
    const auto write_static_use = [&](const std::string& file, const std::string& function,
                                      const std::string& value) {
        write_file(dir / file, "from cpp.path import ./methods.hpp as n\n\n"
                               "def " +
                                   function +
                                   "() -> i32:\n"
                                   "    return n.Picker.select(" +
                                   value + ")\n");
    };
    write_static_use("static_main.dd", "static_main", "1");
    write_static_use("static_same.dd", "static_same", "2");
    write_static_use("static_other.dd", "static_other", "2.0");

    const dudu::Document main_doc{.uri = dudu::file_uri(dir / "main.dd"),
                                  .path = dir / "main.dd",
                                  .text = read_file(dir / "main.dd")};
    const dudu::ProjectIndex& index = dudu::project_index_for_document(main_doc, true);
    const dudu::ModuleAst& unit = index.visible_unit_for_path(main_doc.path);
    std::set<std::string> overload_identities;
    for (const dudu::ClassDecl& klass : unit.native_classes) {
        if (klass.name != "n.Picker") {
            continue;
        }
        for (const dudu::FunctionDecl& method : klass.methods) {
            if (method.name == "choose" && !method.native_identity.usr.empty()) {
                overload_identities.insert(method.native_identity.usr);
            }
        }
    }
    assert(overload_identities.size() == 2);
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
        {dudu::file_uri(dir / "static_main.dd"),
         {.uri = dudu::file_uri(dir / "static_main.dd"),
          .path = dir / "static_main.dd",
          .text = read_file(dir / "static_main.dd")}},
        {dudu::file_uri(dir / "static_same.dd"),
         {.uri = dudu::file_uri(dir / "static_same.dd"),
          .path = dir / "static_same.dd",
          .text = read_file(dir / "static_same.dd")}},
        {dudu::file_uri(dir / "static_other.dd"),
         {.uri = dudu::file_uri(dir / "static_other.dd"),
          .path = dir / "static_other.dd",
          .text = read_file(dir / "static_other.dd")}},
    };
    dudu::Json params = dudu::JsonParser("{\"position\":{\"line\":4,\"character\":20}}").parse();
    const std::string refs = dudu::references_json(main_doc, &params, workspace);
    assert(refs.find(dudu::file_uri(dir / "main.dd")) != std::string::npos);
    assert(refs.find(dudu::file_uri(dir / "same.dd")) != std::string::npos);
    assert(refs.find(dudu::file_uri(dir / "other.dd")) == std::string::npos);

    const dudu::Document static_doc{.uri = dudu::file_uri(dir / "static_main.dd"),
                                    .path = dir / "static_main.dd",
                                    .text = read_file(dir / "static_main.dd")};
    dudu::Json static_params =
        dudu::JsonParser("{\"position\":{\"line\":3,\"character\":22}}").parse();
    const std::string static_refs = dudu::references_json(static_doc, &static_params, workspace);
    assert(static_refs.find(dudu::file_uri(dir / "static_main.dd")) != std::string::npos);
    assert(static_refs.find(dudu::file_uri(dir / "static_same.dd")) != std::string::npos);
    assert(static_refs.find(dudu::file_uri(dir / "static_other.dd")) == std::string::npos);
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
    write_file(dir / "main.dd", "from cpp.path import native_namespace.hpp\n"
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
        test_lsp_module_reference_filters_alias_target();
        test_lsp_module_references_include_target_declaration();
        test_lsp_selective_import_references_include_target_declaration();
        test_lsp_native_references_filter_by_identity();
        test_lsp_native_overload_references_use_selected_identity();
        test_lsp_native_method_overload_references_use_selected_identity();
        test_lsp_references_track_member_path_root_segments();
        test_lsp_native_namespace_reference_query_uses_selected_segment();
        test_lsp_static_class_member_definition_hover_and_references();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
