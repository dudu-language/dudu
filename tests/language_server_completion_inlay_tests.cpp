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
    assert(completions.find("helper(value: i32) -> i32") != std::string::npos);
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
                                     "    return add(counter.value, 2)\n"
                                     "\n"
                                     "class ExplicitReceiver:\n"
                                     "    value: i32\n"
                                     "\n"
                                     "    def read(self: &const[Self]) -> i32:\n"
                                     "        return self.value\n"};
    const std::string hints = dudu::inlay_hints_json(doc, nullptr);
    assert(hints.find("\"label\":\": &Self\"") != std::string::npos);
    assert(hints.find("\": &const[Self]\"") == std::string::npos);
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

void test_lsp_inlay_hints_show_inferred_collection_types() {
    const dudu::Document doc{.uri = "file:///collection_inlay_hints.dd",
                             .path = "collection_inlay_hints.dd",
                             .text = "def main() -> i32:\n"
                                     "    numbers = [1, 2, 3]\n"
                                     "    scores = {\"ada\": 20, \"bob\": 22}\n"
                                     "    names = {\"ada\", \"bob\"}\n"
                                     "    nested = [[1, 2], [3, 4]]\n"
                                     "    return numbers[0] + scores[\"bob\"] + nested[1][0]\n"};
    const std::string hints = dudu::inlay_hints_json(doc, nullptr);
    assert(hints.find("list[i32]") != std::string::npos);
    assert(hints.find("dict[str, i32]") != std::string::npos);
    assert(hints.find("set[str]") != std::string::npos);
    assert(hints.find("list[list[i32]]") != std::string::npos);
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
                                     "    volume: array[f32] = [[[[1.0, 2.0]]]]\n"
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
    assert(hints.find("array[f32][1, 1, 1, 2]") != std::string::npos);
    assert(hints.find("\": i32\"") == std::string::npos);

    dudu::Json hover_params =
        dudu::JsonParser("{\"position\":{\"line\":6,\"character\":6}}").parse();
    const std::string hover = dudu::hover_json(doc, "", &hover_params);
    assert(hover.find("col: array_view[i32][3]") != std::string::npos);
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

void test_lsp_inlay_hints_fold_inferred_value_generic_results() {
    const dudu::Document doc{.uri = "",
                             .path = "value_generic_result_inlay.dd",
                             .text = "def grouped_count[N](values: &array[i32][N]) "
                                     "-> array[i32][N / 4 + N % 4]:\n"
                                     "    out: array[i32][N / 4 + N % 4]\n"
                                     "    return out\n"
                                     "\n"
                                     "def main() -> i32:\n"
                                     "    values: array[i32][10]\n"
                                     "    groups = grouped_count(values)\n"
                                     "    return i32(len(groups))\n"};

    const std::string hints = dudu::inlay_hints_json(doc, nullptr);
    assert(hints.find("array[i32][4]") != std::string::npos);

    dudu::Json hover_params =
        dudu::JsonParser("{\"position\":{\"line\":6,\"character\":6}}").parse();
    const std::string hover = dudu::hover_json(doc, "", &hover_params);
    assert(hover.find("groups: array[i32][4]") != std::string::npos);
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

void test_lsp_move_builtin_hover_inlay_and_shadowing() {
    const dudu::Document builtin_doc{.uri = "file:///move_builtin_hover.dd",
                                     .path = "move_builtin_hover.dd",
                                     .text = "def main() -> i32:\n"
                                             "    value = 42\n"
                                             "    moved = move(value)\n"
                                             "    return moved\n"};
    dudu::Json builtin_params =
        dudu::JsonParser("{\"position\":{\"line\":2,\"character\":14}}").parse();
    const std::string builtin_hover = dudu::hover_json(builtin_doc, "", &builtin_params);
    assert(builtin_hover.find("move[T](value: T) -&gt; T") != std::string::npos ||
           builtin_hover.find("move[T](value: T) -> T") != std::string::npos);
    assert(builtin_hover.find("moved-from state") != std::string::npos);
    const std::string hints = dudu::inlay_hints_json(builtin_doc, nullptr);
    assert(hints.find("\"label\":\"value:\"") != std::string::npos);

    const dudu::Document shadow_doc{.uri = "file:///move_shadow_hover.dd",
                                    .path = "move_shadow_hover.dd",
                                    .text = "# User move docs.\n"
                                            "def move(value: i32) -> i32:\n"
                                            "    return value + 1\n"
                                            "\n"
                                            "\n"
                                            "def main() -> i32:\n"
                                            "    return move(41)\n"};
    dudu::Json shadow_params =
        dudu::JsonParser("{\"position\":{\"line\":6,\"character\":13}}").parse();
    const std::string shadow_hover = dudu::hover_json(shadow_doc, "", &shadow_params);
    assert(shadow_hover.find("User move docs.") != std::string::npos);
    assert(shadow_hover.find("moved-from state") == std::string::npos);
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
    assert(help.find("helper(value: i32, amount: i32) -> i32") != std::string::npos);
    assert(help.find("Combines two values for signature docs.") != std::string::npos);
    assert(help.find("\"activeParameter\":1") != std::string::npos);
}

void test_lsp_signature_help_does_not_guess_a_bare_member_call() {
    const dudu::Document doc{
        .uri = "file:///bare_member_signature.dd",
        .path = "bare_member_signature.dd",
        .text = "class Worker:\n"
                "    # Receiver-only run docs.\n"
                "    def run(self, value: i32) -> i32:\n"
                "        return value\n"
                "\n"
                "def main() -> i32:\n"
                "    return run(1)\n"};
    dudu::Json params =
        dudu::JsonParser("{\"position\":{\"line\":6,\"character\":16}}").parse();
    const std::string help = dudu::signature_help_json(&doc, &params);
    assert(help.find("Receiver-only run docs.") == std::string::npos);
    assert(help.find("\"signatures\":[]") != std::string::npos);
}

} // namespace

int main() {
    try {
        test_lsp_document_symbols_include_ast_doc_summary();
        test_lsp_member_completion_uses_imported_module_shapes();
        test_lsp_completion_uses_visible_imported_functions();
        test_lsp_completion_includes_imported_ast_docs();
        test_lsp_member_completion_includes_ast_docs();
        test_lsp_completion_resolve_preserves_ast_docs();
        test_lsp_inlay_hints_show_inferred_types_and_receiver();
        test_lsp_inlay_hints_show_inferred_collection_types();
        test_lsp_inlay_hints_show_inferred_tensor_view_shapes();
        test_lsp_inlay_hints_use_inferred_array_literal_shapes();
        test_lsp_inlay_hints_type_value_generic_extents_as_usize();
        test_lsp_inlay_hints_fold_inferred_value_generic_results();
        test_lsp_hover_describes_tensor_indexing_builtin_types();
        test_lsp_move_builtin_hover_inlay_and_shadowing();
        test_lsp_signature_help_uses_visible_imported_functions();
        test_lsp_signature_help_does_not_guess_a_bare_member_call();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
