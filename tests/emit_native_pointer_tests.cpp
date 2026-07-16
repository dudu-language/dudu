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
    assert(cpp.find("cvtColor") != std::string::npos);
    assert(cpp.find("Canny") != std::string::npos);
    assert(cpp.find("bitwise_not") != std::string::npos);
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
    dudu::merge_native_headers(module, {});
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

void test_inferred_native_pointer_member_emission_uses_type_ast(const std::filesystem::path& root) {
    const dudu::ModuleAst module =
        dudu::parse_source("from c.path import native_headers/pointer_return.h as ptr\n"
                           "\n"
                           "def read() -> i32:\n"
                           "    info = ptr.dudu_pointer_info_get()\n"
                           "    return info.value\n",
                           root / "tests" / "fixtures" / "inferred_native_pointer_member.dd");
    dudu::ModuleAst checked = module;
    dudu::merge_native_headers(
        checked,
        dudu::NativeHeaderOptions{.config = {}, .source_dir = root / "tests" / "fixtures"});
    dudu::analyze_module(checked, {.check_bodies = true});
    const std::string cpp = dudu::emit_cpp_source(checked);
    assert(cpp.find("auto info = dudu_pointer_info_get();") != std::string::npos);
    assert(cpp.find("info->value") != std::string::npos);
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
        test_inferred_native_pointer_member_emission_uses_type_ast(root);
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
