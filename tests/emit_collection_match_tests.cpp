#include "dudu/codegen/cpp_emit.hpp"
#include "dudu/codegen/cpp_expr_call_emit.hpp"
#include "dudu/core/source.hpp"
#include "dudu/native/native_headers.hpp"
#include "dudu/parser/parser.hpp"
#include "dudu/sema/sema.hpp"

#include <cassert>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace {

std::string read_file(const std::filesystem::path& path) {
    std::ifstream file(path);
    if (!file) {
        throw std::runtime_error("could not open " + path.string());
    }
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

void test_inferred_collection_literal_emission() {
    const dudu::ModuleAst module =
        dudu::parse_source("def main() -> i32:\n"
                           "    numbers = [1, 2, 3]\n"
                           "    scores = {\"ada\": 20, \"bob\": 22}\n"
                           "    names = {\"ada\", \"bob\"}\n"
                           "    nested = [[1, 2], [3, 4]]\n"
                           "    return numbers[0] + scores[\"bob\"] + nested[1][0]\n",
                           "inferred_collection_emission.dd");
    dudu::analyze_module(module, {.check_bodies = true});
    const std::string cpp = dudu::emit_cpp_source(module);
    assert(cpp.find("std::vector<int32_t> numbers = std::vector<int32_t>{1, 2, 3};") !=
           std::string::npos);
    assert(cpp.find("std::unordered_map<std::string, int32_t> scores") != std::string::npos);
    assert(cpp.find("std::unordered_set<std::string> names") != std::string::npos);
    assert(cpp.find("std::vector<std::vector<int32_t>> nested") != std::string::npos);
}

void test_array_literal_scalar_ast_emission() {
    const dudu::ModuleAst module = dudu::parse_source("def values() -> i32:\n"
                                                      "    xs: array[i32] = [1_000, 2]\n"
                                                      "    return xs[0]\n",
                                                      "array_literal_scalar_ast.dd");
    dudu::analyze_module(module, {.check_bodies = true});
    const std::string cpp = dudu::emit_cpp_source(module);
    assert(cpp.find("std::array<int32_t, 2> xs = std::array<int32_t, 2>{1000, 2};") !=
           std::string::npos);
}

void test_top_level_array_constant_ast_emission() {
    const dudu::ModuleAst module = dudu::parse_source("DATA: array[u32] = [1, 2, 3]\n"
                                                      "ROWS: array[i32] = [[1, 2], [3, 4]]\n"
                                                      "\n"
                                                      "def values() -> i32:\n"
                                                      "    return ROWS[1][0]\n",
                                                      "top_level_array_constant_ast.dd");
    dudu::analyze_module(module, {.check_bodies = true});
    const std::string cpp = dudu::emit_cpp_source(module);
    assert(cpp.find("inline constexpr std::array<uint32_t, 3> DATA = "
                    "std::array<uint32_t, 3>{1, 2, 3};") != std::string::npos);
    assert(cpp.find("inline constexpr std::array<std::array<int32_t, 2>, 2> ROWS = "
                    "std::array<std::array<int32_t, 2>, 2>{std::array<int32_t, 2>{1, "
                    "2}, std::array<int32_t, 2>{3, 4}};") != std::string::npos);
}

void test_compile_time_programming_emission(const std::filesystem::path& root) {
    const std::filesystem::path path = root / "tests" / "fixtures" / "compile_time_programming.dd";
    const dudu::ModuleAst module = dudu::parse_source(read_file(path), path);
    dudu::analyze_module(module, {.check_bodies = true});
    const std::string cpp = dudu::emit_cpp_source(module);
    assert(cpp.find("constexpr size_t align_up(size_t value, size_t alignment)") !=
           std::string::npos);
    assert(cpp.find("inline constexpr size_t PACKET_BYTES = ") != std::string::npos);
    assert(cpp.find("static_assert(PACKET_ALIGNMENT == 16);") != std::string::npos);
    assert(cpp.find("if constexpr ((build::RENDER_BACKEND == \"vulkan\"))") != std::string::npos);
    assert(cpp.find("std::array<uint32_t, QUEUE_CAPACITY> queue") != std::string::npos);
}

void test_three_dimensional_array_literal_emission() {
    const dudu::ModuleAst module = dudu::parse_source("def values() -> i32:\n"
                                                      "    xs: array[i32] = [\n"
                                                      "        [[1, 2], [3, 4]],\n"
                                                      "        [[5, 6], [7, 8]],\n"
                                                      "    ]\n"
                                                      "    return xs[1, 1, 0]\n",
                                                      "array_literal_3d_ast.dd");
    dudu::analyze_module(module, {.check_bodies = true});
    const std::string cpp = dudu::emit_cpp_source(module);
    assert(cpp.find("std::array<std::array<std::array<int32_t, 2>, 2>, 2> xs = "
                    "std::array<std::array<std::array<int32_t, 2>, 2>, "
                    "2>{std::array<std::array<int32_t, 2>, 2>{std::array<int32_t, "
                    "2>{1, 2}, std::array<int32_t, 2>{3, 4}}, "
                    "std::array<std::array<int32_t, 2>, 2>{std::array<int32_t, "
                    "2>{5, 6}, std::array<int32_t, 2>{7, 8}}};") != std::string::npos);
}

void test_value_match_ast_emission() {
    const dudu::ModuleAst module = dudu::parse_source("def classify(value: i32) -> i32:\n"
                                                      "    match value:\n"
                                                      "        case 0:\n"
                                                      "            return 10\n"
                                                      "        case 2 if value > 1:\n"
                                                      "            return 20\n"
                                                      "        case _:\n"
                                                      "            return 30\n"
                                                      "\n"
                                                      "def main() -> i32:\n"
                                                      "    return classify(2) - 20\n",
                                                      "value_match_ast.dd");
    dudu::analyze_module(module, {.check_bodies = true});
    const std::string cpp = dudu::emit_cpp_source(module);
    assert(cpp.find("auto&& __dudu_match_2_5 = value;") != std::string::npos);
    assert(cpp.find("__dudu_match_2_5_matched") == std::string::npos);
    assert(cpp.find("if (__dudu_match_2_5 == 0)") != std::string::npos);
    assert(cpp.find("if ((__dudu_match_2_5 == 2) && ((value > 1)))") != std::string::npos);
}

void test_non_returning_value_match_emits_else_chain() {
    const dudu::ModuleAst module = dudu::parse_source("def classify(value: i32) -> i32:\n"
                                                      "    score = 0\n"
                                                      "    match value:\n"
                                                      "        case 0:\n"
                                                      "            score = 10\n"
                                                      "        case 2 if value > 1:\n"
                                                      "            score = 20\n"
                                                      "        case _:\n"
                                                      "            score = 30\n"
                                                      "    return score\n"
                                                      "\n"
                                                      "def main() -> i32:\n"
                                                      "    return classify(2) - 20\n",
                                                      "non_returning_value_match_ast.dd");
    dudu::analyze_module(module, {.check_bodies = true});
    const std::string cpp = dudu::emit_cpp_source(module);
    assert(cpp.find("auto&& __dudu_match_3_5 = value;") != std::string::npos);
    assert(cpp.find("__dudu_match_3_5_matched") == std::string::npos);
    assert(cpp.find("if (__dudu_match_3_5 == 0)") != std::string::npos);
    assert(cpp.find("else if ((__dudu_match_3_5 == 2) && ((value > 1)))") != std::string::npos);
    assert(cpp.find("else {\n") != std::string::npos);
    assert(cpp.find("score = 30;") != std::string::npos);
}

void test_guarded_enum_match_emits_ordered_chain() {
    const dudu::ModuleAst module =
        dudu::parse_source("enum Direction:\n"
                           "    North\n"
                           "    South\n"
                           "\n"
                           "def classify(direction: Direction, enabled: bool) -> i32:\n"
                           "    match direction:\n"
                           "        case Direction.North if enabled:\n"
                           "            return 10\n"
                           "        case Direction.North:\n"
                           "            return 20\n"
                           "        case Direction.South:\n"
                           "            return 30\n"
                           "\n"
                           "def main() -> i32:\n"
                           "    return classify(Direction.North, False) - 20\n",
                           "guarded_enum_match_ast.dd");
    dudu::analyze_module(module, {.check_bodies = true});
    const std::string cpp = dudu::emit_cpp_source(module);
    assert(cpp.find("auto&& __dudu_match_6_5 = direction;") != std::string::npos);
    assert(cpp.find("__dudu_match_6_5_matched") == std::string::npos);
    assert(cpp.find("if ((__dudu_match_6_5 == Direction::North) && (enabled))") !=
           std::string::npos);
    assert(cpp.find("else if (__dudu_match_6_5 == Direction::North)") != std::string::npos);
    assert(cpp.find("else if (__dudu_match_6_5 == Direction::South)") != std::string::npos);
}

void test_typed_literal_initializers_use_type_ast() {
    const dudu::ModuleAst module = dudu::parse_source("def values() -> i32:\n"
                                                      "    maybe: Option[i32] = None\n"
                                                      "    xs: list[i32] = [1, 2]\n"
                                                      "    empty: list[str] = []\n"
                                                      "    counts: dict[str, i32] = {\"a\": 1}\n"
                                                      "    seen: set[i32] = {1, 2}\n"
                                                      "    maybe = None\n"
                                                      "    return xs[0]\n",
                                                      "typed_literal_initializers.dd");
    dudu::analyze_module(module, {.check_bodies = true});
    const std::string cpp = dudu::emit_cpp_source(module);
    assert(cpp.find("std::optional<int32_t> maybe = std::nullopt;") != std::string::npos);
    assert(cpp.find("std::vector<int32_t> xs = {1, 2};") != std::string::npos);
    assert(cpp.find("std::vector<std::string> empty = {};") != std::string::npos);
    assert(cpp.find("std::unordered_map<std::string, int32_t> counts = {{\"a\", 1}};") !=
           std::string::npos);
    assert(cpp.find("std::unordered_set<int32_t> seen = {1, 2};") != std::string::npos);
    assert(cpp.find("maybe = std::nullopt;") != std::string::npos);
}

} // namespace

int main() {
    try {
        const std::filesystem::path root = DUDU_REPO_ROOT;
        test_inferred_collection_literal_emission();
        test_array_literal_scalar_ast_emission();
        test_top_level_array_constant_ast_emission();
        test_compile_time_programming_emission(root);
        test_three_dimensional_array_literal_emission();
        test_value_match_ast_emission();
        test_non_returning_value_match_emits_else_chain();
        test_guarded_enum_match_emits_ordered_chain();
        test_typed_literal_initializers_use_type_ast();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
