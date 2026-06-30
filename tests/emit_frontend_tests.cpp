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

void test_image_filter_emission(const std::filesystem::path& root) {
    const std::filesystem::path path = root / "examples" / "image_filter.dd";
    const dudu::ModuleAst module = dudu::parse_source(read_file(path), path);
    dudu::analyze_module(module, {.check_bodies = false});
    const std::string cpp = dudu::emit_cpp_source(module);
    assert(cpp.find("dst.at<cv::Vec3b>(y, x)") != std::string::npos);
    assert(cpp.find("edges.at<uint8_t>(y, x)") != std::string::npos);
    assert(cpp.find("pixel[0]") != std::string::npos);
}

void test_pointer_member_emission(const std::filesystem::path& root) {
    {
        const std::filesystem::path path = root / "tests" / "fixtures" / "pointer_member.dd";
        const dudu::ModuleAst module = dudu::parse_source(read_file(path), path);
        dudu::analyze_module(module, {.check_bodies = true});
        const std::string cpp = dudu::emit_cpp_source(module);
        assert(cpp.find("item->value += 2") != std::string::npos);
        assert(cpp.find("int32_t result = item->value;") != std::string::npos);
    }
    {
        const std::filesystem::path path =
            root / "tests" / "fixtures" / "value_pointer_containers.dd";
        const dudu::ModuleAst module = dudu::parse_source(read_file(path), path);
        dudu::analyze_module(module, {.check_bodies = true});
        const std::string cpp = dudu::emit_cpp_source(module);
        assert(cpp.find("ptrs[0]->value += 1") != std::string::npos);
    }
    {
        const dudu::ModuleAst module =
            dudu::parse_source("class Item:\n"
                               "    value: i32\n"
                               "\n"
                               "def read(ptrs: list[*const[Item]]) -> i32:\n"
                               "    return ptrs[0].value\n",
                               "pointer_const_list_member.dd");
        dudu::analyze_module(module, {.check_bodies = true});
        const std::string cpp = dudu::emit_cpp_source(module);
        assert(cpp.find("return ptrs[0]->value;") != std::string::npos);
    }
    {
        const dudu::ModuleAst module =
            dudu::parse_source("class Item:\n"
                               "    value: i32\n"
                               "\n"
                               "def copy(item: *Item, ptrs: list[*Item]):\n"
                               "    cpp(\"item.value = ptrs[0].value;\")\n",
                               "cpp_escape_pointer_members.dd");
        dudu::analyze_module(module, {.check_bodies = true});
        const std::string cpp = dudu::emit_cpp_source(module);
        assert(cpp.find("item->value = ptrs[0]->value;") != std::string::npos);
    }
}

void test_value_member_emission() {
    dudu::ModuleAst module = dudu::parse_source("from cpp import cmath\n"
                                                "\n"
                                                "class Player:\n"
                                                "    health: i32\n"
                                                "\n"
                                                "def read(player: Player) -> i32:\n"
                                                "    wave = std.sin(1.0)\n"
                                                "    return player.health\n",
                                                "value_member_emission.dd");
    dudu::merge_native_header_types(module, {});
    dudu::analyze_module(module, {.check_bodies = false});
    const std::string cpp = dudu::emit_cpp_source(module);
    assert(cpp.find("auto wave = std::sin(1.0);") != std::string::npos);
    assert(cpp.find("return player.health;") != std::string::npos);
}

void test_c_imports_emit_c_linkage() {
    const dudu::ModuleAst module = dudu::parse_source("from c import math.h\n"
                                                      "from cxx import libxml/parser.h\n"
                                                      "from cpp import vector\n",
                                                      "c_import_linkage.dd");
    const std::string cpp = dudu::emit_cpp_source(module);
    assert(cpp.find("extern \"C\" {\n#include <math.h>\n}\n") != std::string::npos);
    assert(cpp.find("#include <libxml/parser.h>") != std::string::npos);
    assert(cpp.find("#include <vector>") != std::string::npos);
    assert(cpp.find("extern \"C\" {\n#include \"libxml/parser.h\"") == std::string::npos);
    assert(cpp.find("extern \"C\" {\n#include \"vector\"") == std::string::npos);
}

void test_path_imports_emit_quoted_includes() {
    const dudu::ModuleAst module = dudu::parse_source("from c.path import vendor/foo.h as foo\n"
                                                      "from cpp.path import local/bar.hpp\n",
                                                      "path_import_linkage.dd");
    const std::string cpp = dudu::emit_cpp_source(module);
    assert(cpp.find("extern \"C\" {\n#include \"vendor/foo.h\"\n}\n") != std::string::npos);
    assert(cpp.find("#include \"local/bar.hpp\"") != std::string::npos);
    assert(cpp.find("#include <vendor/foo.h>") == std::string::npos);
    assert(cpp.find("#include <local/bar.hpp>") == std::string::npos);
}

void test_templated_pointer_cast_emission() {
    const dudu::ModuleAst module =
        dudu::parse_source("def read_i32(left: *const[void]) -> const[i32]:\n"
                           "    value: *const[i32] = *const[i32](left)\n"
                           "    return *value\n",
                           "templated_pointer_cast.dd");
    dudu::analyze_module(module, {.check_bodies = true});
    const std::string cpp = dudu::emit_cpp_source(module);
    assert(cpp.find("const int32_t* value = reinterpret_cast<const int32_t*>(left);") !=
           std::string::npos);
}

void test_template_pointer_cast_type_detection_uses_type_ast() {
    const dudu::ModuleAst module =
        dudu::parse_source("def cast_list(raw: *const[void]) -> *list[i32]:\n"
                           "    return *list[i32](raw)\n",
                           "template_pointer_cast_type_ast.dd");
    dudu::analyze_module(module, {.check_bodies = true});
    const std::string cpp = dudu::emit_cpp_source(module);
    assert(cpp.find("reinterpret_cast<std::vector<int32_t>*>(raw)") != std::string::npos);
}

void test_pointer_to_const_binding_emission() {
    const dudu::ModuleAst module =
        dudu::parse_source("def read_value(value: * const[f32]) -> const[f32]:\n"
                           "    return *value\n"
                           "\n"
                           "def main() -> i32:\n"
                           "    values: list[f32] = [1.0]\n"
                           "    out: const[f32] = read_value(&values[0])\n"
                           "    return i32(f32(out))\n",
                           "pointer_to_const_binding.dd");
    dudu::analyze_module(module, {.check_bodies = true});
    const std::string cpp = dudu::emit_cpp_source(module);
    assert(cpp.find("float const* value") != std::string::npos ||
           cpp.find("const float* value") != std::string::npos);
    assert(cpp.find("read_value((&values[0]))") != std::string::npos);
}

void test_offsetof_field_emission() {
    const dudu::ModuleAst module =
        dudu::parse_source("class Packet:\n"
                           "    flags: u8\n"
                           "\n"
                           "FLAGS_OFFSET: usize = offsetof[Packet](flags)\n"
                           "FLAGS_OFFSET_STR: usize = offsetof[Packet](\"flags\")\n",
                           "offsetof_field.dd");
    dudu::analyze_module(module, {.check_bodies = true});
    const std::string cpp = dudu::emit_cpp_source(module);
    assert(cpp.find("FLAGS_OFFSET = offsetof(Packet, flags);") != std::string::npos);
    assert(cpp.find("FLAGS_OFFSET_STR = offsetof(Packet, flags);") != std::string::npos);
}

void test_offsetof_string_field_requires_parsed_value() {
    dudu::Expr malformed;
    malformed.kind = dudu::ExprKind::StringLiteral;
    malformed.location = dudu::SourceLocation{
        .file = dudu::SourceFileName("synthetic_offsetof.dd"), .line = 1, .column = 1};

    bool threw = false;
    try {
        (void)dudu::lower_offsetof_field(malformed, {}, {});
    } catch (const dudu::CompileError& error) {
        threw = std::string(error.what()).find("missing parsed value") != std::string::npos;
    }
    assert(threw);
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

void test_inferred_auto_assignment_is_not_redeclared() {
    const dudu::ModuleAst module = dudu::parse_source("def main() -> i32:\n"
                                                      "    value = cpp(\"MAKE_VALUE()\")\n"
                                                      "    value = cpp(\"MAKE_VALUE()\")\n"
                                                      "    return i32(0)\n",
                                                      "inferred_auto_assignment.dd");
    dudu::analyze_module(module, {.check_bodies = true});
    const std::string cpp = dudu::emit_cpp_source(module);
    assert(cpp.find("auto value = MAKE_VALUE();") != std::string::npos);
    assert(cpp.find("value = MAKE_VALUE();") != std::string::npos);
    assert(cpp.find("auto value = MAKE_VALUE();\n    auto value = MAKE_VALUE();") ==
           std::string::npos);
}

void test_inferred_native_pointer_member_emission_uses_type_ast(const std::filesystem::path& root) {
    const dudu::ModuleAst module =
        dudu::parse_source("import c \"native_headers/pointer_return.h\" as ptr\n"
                           "\n"
                           "def read() -> i32:\n"
                           "    info = ptr.dudu_pointer_info_get()\n"
                           "    return info.value\n",
                           root / "tests" / "fixtures" / "inferred_native_pointer_member.dd");
    dudu::ModuleAst checked = module;
    dudu::merge_native_header_types(
        checked,
        dudu::NativeHeaderOptions{.config = {}, .source_dir = root / "tests" / "fixtures"});
    dudu::analyze_module(checked, {.check_bodies = true});
    const std::string cpp = dudu::emit_cpp_source(checked);
    assert(cpp.find("auto info = dudu_pointer_info_get();") != std::string::npos);
    assert(cpp.find("info->value") != std::string::npos);
}

void test_expected_generic_method_emission_uses_type_ast_receiver() {
    const dudu::ModuleAst module = dudu::parse_source("class Box[T]:\n"
                                                      "    def make[U](self) -> U:\n"
                                                      "        return U()\n"
                                                      "\n"
                                                      "class Wrapper(Box[f32]):\n"
                                                      "    tag: i32\n"
                                                      "\n"
                                                      "\n"
                                                      "def use(wrapper: Wrapper) -> str:\n"
                                                      "    text: str = wrapper.make()\n"
                                                      "    return text\n",
                                                      "expected_generic_method_type_ast_emit.dd");
    dudu::analyze_module(module, {.check_bodies = false});
    const std::string cpp = dudu::emit_cpp_source(module);
    assert(cpp.find("std::string text = wrapper.make<std::string>();") != std::string::npos);
}

void test_receiver_reference_emission() {
    const dudu::ModuleAst module =
        dudu::parse_source("class Vec3:\n"
                           "    x: f32\n"
                           "\n"
                           "    def length(self: &const[Self]) -> f32:\n"
                           "        return self.x\n"
                           "\n"
                           "    def copy(self) -> Self:\n"
                           "        return self\n"
                           "\n"
                           "    def const_copy(self: &const[Self]) -> Self:\n"
                           "        return self\n"
                           "\n"
                           "    def const_ref(self: &const[Self]) -> &const[Self]:\n"
                           "        return self\n"
                           "\n"
                           "    def normalize(self) -> &Self:\n"
                           "        return self\n"
                           "\n"
                           "    @operator(\"+=\")\n"
                           "    def add_assign(self, other: &const[Self]) -> &Self:\n"
                           "        self.x += other.x\n"
                           "        return self\n",
                           "receiver_reference_emit.dd");
    dudu::analyze_module(module, {.check_bodies = true});
    const std::string cpp = dudu::emit_cpp_source(module);
    assert(cpp.find("float length() const") != std::string::npos);
    assert(cpp.find("Vec3 copy()") != std::string::npos);
    assert(cpp.find("Vec3 const_copy() const") != std::string::npos);
    assert(cpp.find("const Vec3& const_ref() const") != std::string::npos);
    assert(cpp.find("Vec3& normalize()") != std::string::npos);
    assert(cpp.find("Vec3& operator+=") != std::string::npos);
    assert(cpp.find("auto& self = *this;") != std::string::npos);
}

void test_class_emit_order_uses_type_ast_fields() {
    const dudu::ModuleAst module = dudu::parse_source("class Holder:\n"
                                                      "    items: list[Node]\n"
                                                      "\n"
                                                      "class Node:\n"
                                                      "    value: i32\n",
                                                      "class_emit_order_type_ast.dd");
    dudu::analyze_module(module, {.check_bodies = true});
    const std::string cpp = dudu::emit_cpp_source(module);
    assert(cpp.find("\nstruct Node {") != std::string::npos);
    assert(cpp.find("\nstruct Holder {") != std::string::npos);
    assert(cpp.find("\nstruct Node {") < cpp.find("\nstruct Holder {"));
}

void test_operator_continuation_is_part_of_return_expression() {
    const dudu::ModuleAst module = dudu::parse_source("def value(x: i32) -> i32:\n"
                                                      "    return x\n"
                                                      "        + 1\n"
                                                      "        + 2\n",
                                                      "operator_continuation_return.dd");
    dudu::analyze_module(module, {.check_bodies = true});
    const std::string cpp = dudu::emit_cpp_source(module);
    assert(cpp.find("return ((x + 1) + 2);") != std::string::npos);
}

} // namespace

int main() {
    try {
        const std::filesystem::path root = DUDU_REPO_ROOT;
        test_image_filter_emission(root);
        test_pointer_member_emission(root);
        test_value_member_emission();
        test_c_imports_emit_c_linkage();
        test_path_imports_emit_quoted_includes();
        test_templated_pointer_cast_emission();
        test_template_pointer_cast_type_detection_uses_type_ast();
        test_pointer_to_const_binding_emission();
        test_offsetof_field_emission();
        test_offsetof_string_field_requires_parsed_value();
        test_array_literal_scalar_ast_emission();
        test_top_level_array_constant_ast_emission();
        test_three_dimensional_array_literal_emission();
        test_value_match_ast_emission();
        test_non_returning_value_match_emits_else_chain();
        test_guarded_enum_match_emits_ordered_chain();
        test_typed_literal_initializers_use_type_ast();
        test_inferred_auto_assignment_is_not_redeclared();
        test_inferred_native_pointer_member_emission_uses_type_ast(root);
        test_expected_generic_method_emission_uses_type_ast_receiver();
        test_receiver_reference_emission();
        test_class_emit_order_uses_type_ast_fields();
        test_operator_continuation_is_part_of_return_expression();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
