#include "dudu/cpp_emit.hpp"
#include "dudu/cpp_expr_call_emit.hpp"
#include "dudu/parser.hpp"
#include "dudu/sema.hpp"
#include "dudu/source.hpp"

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
    dudu::analyze_module(module, {.check_bodies = true});
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
    const dudu::ModuleAst module = dudu::parse_source("import cpp \"cmath\" as std\n"
                                                      "\n"
                                                      "class Player:\n"
                                                      "    health: i32\n"
                                                      "\n"
                                                      "def read(player: Player) -> i32:\n"
                                                      "    wave = std.sin(1.0)\n"
                                                      "    return player.health\n",
                                                      "value_member_emission.dd");
    dudu::analyze_module(module, {.check_bodies = true});
    const std::string cpp = dudu::emit_cpp_source(module);
    assert(cpp.find("auto wave = std::sin(1.0);") != std::string::npos);
    assert(cpp.find("return player.health;") != std::string::npos);
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
    malformed.text = "\"flags\"";
    malformed.location =
        dudu::SourceLocation{.file = "synthetic_offsetof.dd", .line = 1, .column = 1};

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
    assert(cpp.find("std::array<int32_t, 2> xs = {{1000, 2}};") != std::string::npos);
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

} // namespace

int main() {
    try {
        const std::filesystem::path root = DUDU_REPO_ROOT;
        test_image_filter_emission(root);
        test_pointer_member_emission(root);
        test_value_member_emission();
        test_templated_pointer_cast_emission();
        test_template_pointer_cast_type_detection_uses_type_ast();
        test_pointer_to_const_binding_emission();
        test_offsetof_field_emission();
        test_offsetof_string_field_requires_parsed_value();
        test_array_literal_scalar_ast_emission();
        test_typed_literal_initializers_use_type_ast();
        test_inferred_auto_assignment_is_not_redeclared();
        test_expected_generic_method_emission_uses_type_ast_receiver();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
