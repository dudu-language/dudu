#include "dudu/cpp_emit.hpp"
#include "dudu/parser.hpp"
#include "dudu/sema.hpp"

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

void test_array_literal_scalar_ast_emission() {
    const dudu::ModuleAst module = dudu::parse_source("def values() -> i32:\n"
                                                      "    xs: array[i32] = [1_000, 2]\n"
                                                      "    return xs[0]\n",
                                                      "array_literal_scalar_ast.dd");
    dudu::analyze_module(module, {.check_bodies = true});
    const std::string cpp = dudu::emit_cpp_source(module);
    assert(cpp.find("std::array<int32_t, 2> xs = {{1000, 2}};") != std::string::npos);
}

} // namespace

int main() {
    try {
        const std::filesystem::path root = DUDU_REPO_ROOT;
        test_image_filter_emission(root);
        test_pointer_member_emission(root);
        test_value_member_emission();
        test_templated_pointer_cast_emission();
        test_pointer_to_const_binding_emission();
        test_offsetof_field_emission();
        test_array_literal_scalar_ast_emission();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
