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
                             .text = "from cpp.path import native_widget.hpp\n"
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

void test_lsp_native_macro_identity_completion_signature_and_definition() {
    const std::filesystem::path dir =
        std::filesystem::temp_directory_path() / "dudu_lsp_native_macro_test";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    const std::filesystem::path header = dir / "native_macros.hpp";
    write_file(header, "#pragma once\n"
                       "#define NATIVE_MAGIC 21\n"
                       "#define NATIVE_SCALE(value) ((value) * 2)\n"
                       "#define NATIVE_FIRST(first, ...) (first)\n");

    const std::string source = "from cpp.path import native_macros.hpp as macros\n"
                               "\n"
                               "def main() -> i32:\n"
                               "    value = macros.NATIVE_SCALE(macros.NATIVE_MAGIC)\n"
                               "    return macros.NATIVE_FIRST(value, 0)\n";
    const std::filesystem::path path = dir / "main.dd";
    write_file(path, source);
    const dudu::Document doc{.uri = dudu::file_uri(path), .path = path, .text = source};

    dudu::clear_language_server_module_cache();
    dudu::Json hover_params =
        dudu::JsonParser("{\"position\":{\"line\":3,\"character\":26}}").parse();
    const std::string hover = dudu::hover_json(doc, "", &hover_params);
    assert(hover.find("macro macros.NATIVE_SCALE") != std::string::npos);

    dudu::Json completion_params =
        dudu::JsonParser("{\"position\":{\"line\":3,\"character\":19}}").parse();
    const std::string completion = dudu::completion_json(&doc, &completion_params);
    assert(completion.find("NATIVE_SCALE") != std::string::npos);
    assert(completion.find("NATIVE_MAGIC") != std::string::npos);

    dudu::Json signature_params =
        dudu::JsonParser("{\"position\":{\"line\":4,\"character\":38}}").parse();
    const std::string signature = dudu::signature_help_json(&doc, &signature_params);
    assert(signature.find("NATIVE_FIRST") != std::string::npos);

    const std::string definition = dudu::definition_json(doc, &hover_params);
    assert(definition.find(dudu::file_uri(header)) != std::string::npos);
    dudu::clear_language_server_module_cache();
}

void test_lsp_native_template_identity_completion_signature_and_definition() {
    const std::filesystem::path dir =
        std::filesystem::temp_directory_path() /
        ("dudu_lsp_native_template_test_" +
         std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    const std::filesystem::path header = dir / "native_templates.hpp";
    write_file(header, "#pragma once\n"
                       "template <typename T> struct NativeBox { T value; };\n"
                       "template <typename T> T native_identity(T value) { return value; }\n");

    const std::string source = "from cpp.path import native_templates.hpp\n"
                               "\n"
                               "def main() -> i32:\n"
                               "    box: NativeBox[i32]\n"
                               "    box.value = native_identity[i32](41)\n"
                               "    return box.value\n";
    const std::filesystem::path path = dir / "main.dd";
    write_file(path, source);
    const dudu::Document doc{.uri = dudu::file_uri(path), .path = path, .text = source};

    dudu::clear_language_server_module_cache();
    dudu::Json box_params =
        dudu::JsonParser("{\"position\":{\"line\":3,\"character\":12}}").parse();
    const std::string box_hover = dudu::hover_json(doc, "", &box_params);
    assert(box_hover.find("NativeBox") != std::string::npos);
    const std::string box_definition = dudu::definition_json(doc, &box_params);
    assert(box_definition.find(dudu::file_uri(header)) != std::string::npos);

    dudu::Json function_params =
        dudu::JsonParser("{\"position\":{\"line\":4,\"character\":27}}").parse();
    const std::string function_hover = dudu::hover_json(doc, "", &function_params);
    assert(function_hover.find("native_identity") != std::string::npos);
    assert(dudu::definition_json(doc, &function_params).find(dudu::file_uri(header)) !=
           std::string::npos);

    dudu::Json signature_params =
        dudu::JsonParser("{\"position\":{\"line\":4,\"character\":38}}").parse();
    const std::string signature = dudu::signature_help_json(&doc, &signature_params);
    assert(signature.find("native_identity") != std::string::npos);

    dudu::Json completion_params =
        dudu::JsonParser("{\"position\":{\"line\":5,\"character\":4}}").parse();
    const std::string completion = dudu::completion_json(&doc, &completion_params);
    assert(completion.find("NativeBox") != std::string::npos);
    assert(completion.find("native_identity") != std::string::npos);
    dudu::clear_language_server_module_cache();
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
                             .text = "from cpp.path import native_callback.hpp as native\n"
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
                             .text = "from cpp.path import native_widget.hpp\n"
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

} // namespace

int main() {
    try {
        test_lsp_native_member_docs_reach_completion_and_signature_help();
        test_lsp_native_macro_identity_completion_signature_and_definition();
        test_lsp_native_template_identity_completion_signature_and_definition();
        test_lsp_signature_help_resolves_native_callable_values();
        test_lsp_inlay_hints_include_native_parameter_names();
        test_lsp_inlay_hints_include_builtin_method_parameter_names();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
