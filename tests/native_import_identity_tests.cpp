#include "dudu/codegen/cpp_emit.hpp"
#include "dudu/core/ast_type.hpp"
#include "dudu/lsp/language_server_symbols.hpp"
#include "dudu/native/native_header_cache_deps.hpp"
#include "dudu/native/native_header_merge.hpp"
#include "dudu/native/native_header_parse.hpp"
#include "dudu/native/native_header_scan_command.hpp"
#include "dudu/native/native_header_scope.hpp"
#include "dudu/native/native_header_types.hpp"
#include "dudu/native/native_header_usr.hpp"
#include "dudu/native/native_headers.hpp"
#include "dudu/native/native_signature_substitution.hpp"
#include "dudu/parser/parser.hpp"
#include "dudu/sema/sema.hpp"
#include "dudu/sema/sema_context.hpp"
#include "dudu/sema/sema_function_type.hpp"
#include "dudu/sema/sema_method_templates.hpp"
#include "dudu/sema/sema_ops.hpp"
#include "dudu/sema/type_compat.hpp"
#include "dudu/sema/type_compat_native.hpp"

#include <algorithm>
#include <cassert>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
void test_cxx_import_scans_c_globals_but_emits_plain_include(const std::filesystem::path& root) {
    dudu::ModuleAst module =
        dudu::parse_source("from cxx.path import native_headers/simple_c.h as native\n"
                           "\n"
                           "def main() -> i32:\n"
                           "    event: native.DuduNativeEvent\n"
                           "    return native.dudu_native_add(20, 22)\n",
                           root / "tests/fixtures/native_cxx_import.dd");
    dudu::ProjectConfig config;
    config.build_dir = root / "build" / "native-cxx-header-test-cache";
    std::filesystem::remove_all(config.build_dir);
    dudu::merge_native_headers(module, {.config = config, .source_dir = root / "tests/fixtures"});
    dudu::analyze_module(module, {.check_bodies = true});
    const std::string cpp = dudu::emit_cpp_source(module);
    assert(cpp.find("#include \"native_headers/simple_c.h\"") != std::string::npos);
    assert(cpp.find("extern \"C\" {\n#include \"native_headers/simple_c.h\"") == std::string::npos);
    assert(cpp.find("return dudu_native_add(20, 22);") != std::string::npos);
    assert(cpp.find("union DuduNativeEvent event{};") != std::string::npos);
    assert(cpp.find("native::dudu_native_add") == std::string::npos);
}

void test_native_layout_survives_parsed_scan_cache(const std::filesystem::path& root) {
    const std::filesystem::path source_dir = root / "tests/fixtures";
    dudu::ProjectConfig config;
    config.project_dir = root;
    config.build_dir = root / "build/native-layout-cache";
    std::filesystem::remove_all(config.build_dir);

    const auto scan = [&](std::string_view file_name) {
        dudu::ModuleAst module = dudu::parse_source(
            "from cpp.path import native_headers/simple_cpp.hpp\n", source_dir / file_name);
        dudu::merge_native_headers(module, {.config = config, .source_dir = source_dir});
        return module;
    };
    const auto require_widget_layout = [](const dudu::ModuleAst& module) {
        for (const dudu::ClassDecl& klass : module.native_classes) {
            if (klass.name == "dudu_native.Widget") {
                assert(klass.layout.has_value());
                assert(klass.layout->size == 4);
                assert(klass.layout->alignment == 4);
                return;
            }
        }
        assert(false && "dudu_native.Widget was not scanned");
    };
    const auto require_alias_layout = [](const dudu::ModuleAst& module) {
        for (const dudu::NativeTypeDecl& type : module.native_types) {
            if (type.name == "DuduWidgetAlias") {
                assert(type.layout.has_value());
                assert(type.layout->size == 4);
                assert(type.layout->alignment == 4);
                return;
            }
        }
        assert(false && "DuduWidgetAlias was not scanned");
    };

    const dudu::ModuleAst cold = scan("native_layout_cold.dd");
    require_widget_layout(cold);
    require_alias_layout(cold);
    const dudu::ModuleAst warm = scan("native_layout_warm.dd");
    require_widget_layout(warm);
    require_alias_layout(warm);
}

void test_native_static_class_fields_keep_owner_and_cache(const std::filesystem::path& root) {
    const std::filesystem::path source_dir = root / "examples";
    dudu::ProjectConfig config;
    config.project_dir = root;
    config.build_dir = root / "build/native-static-field-cache";
    std::filesystem::remove_all(config.build_dir);

    const std::string source = "from cpp.path import cuda_runtime.h as cuda\n"
                               "\n"
                               "def index_sum() -> i32:\n"
                               "    return cuda.blockIdx.x + cuda.blockDim.x + cuda.threadIdx.x\n";
    const auto scan = [&](std::string_view file_name) {
        dudu::ModuleAst module = dudu::parse_source(source, source_dir / file_name);
        dudu::merge_native_headers(module, {.config = config, .source_dir = source_dir});
        return module;
    };
    const auto require_static_x = [](const dudu::ModuleAst& module, std::string_view class_name) {
        for (const dudu::ClassDecl& klass : module.native_classes) {
            if (klass.name != class_name) {
                continue;
            }
            assert(std::ranges::any_of(klass.static_fields, [](const dudu::ConstDecl& field) {
                return field.name == "x" && dudu::type_ref_text(field.type_ref) == "i32";
            }));
            return;
        }
        assert(false && "CUDA index class was not scanned");
    };

    dudu::ModuleAst cold = scan("native_static_fields_cold.dd");
    assert(std::ranges::none_of(cold.native_values, [](const dudu::NativeValueDecl& value) {
        return value.name == "x" || value.name.ends_with(".x");
    }));
    assert(std::ranges::any_of(cold.native_values, [](const dudu::NativeValueDecl& value) {
        return value.name == "cuda.cudaSuccess" &&
               dudu::type_ref_text(value.type_ref) == "const[cuda.cudaError_t]";
    }));
    for (std::string_view class_name : {"cuda.blockIdx", "cuda.blockDim", "cuda.threadIdx"}) {
        require_static_x(cold, class_name);
    }
    dudu::analyze_module(cold, {.check_bodies = true});
    const std::string cpp = dudu::emit_cpp_source(cold);
    assert(cpp.find("blockIdx::x") != std::string::npos);
    assert(cpp.find("blockDim::x") != std::string::npos);
    assert(cpp.find("threadIdx::x") != std::string::npos);

    const dudu::ModuleAst warm = scan("native_static_fields_warm.dd");
    for (std::string_view class_name : {"cuda.blockIdx", "cuda.blockDim", "cuda.threadIdx"}) {
        require_static_x(warm, class_name);
    }
}

void test_native_header_alias_preserves_identity(const std::filesystem::path& root) {
    dudu::ModuleAst module =
        dudu::parse_source("from cpp.path import native_headers/simple_cpp.hpp as native\n"
                           "\n"
                           "def main() -> i32:\n"
                           "    return native.dudu_native.add(20, 22)\n",
                           root / "tests/fixtures/native_alias_identity.dd");
    dudu::ProjectConfig config;
    config.build_dir = root / "build" / "native-header-alias-identity-cache";
    std::filesystem::remove_all(config.build_dir);
    dudu::merge_native_headers(module, {.config = config, .source_dir = root / "tests/fixtures"});
    dudu::analyze_module(module, {.check_bodies = true});

    bool saw_prefixed_type = false;
    bool saw_prefixed_function = false;
    for (const dudu::NativeTypeDecl& type : module.native_types) {
        if (type.name == "native.DuduWidgetAlias") {
            assert(type.identity.canonical_path == "DuduWidgetAlias");
            saw_prefixed_type = true;
        }
    }
    for (const dudu::NativeFunctionDecl& fn : module.native_functions) {
        if (fn.name == "native.dudu_native.add") {
            assert(fn.identity.canonical_path == "dudu_native.add");
            saw_prefixed_function = true;
        }
    }
    assert(saw_prefixed_type);
    assert(saw_prefixed_function);
}

void test_direct_cpp_import_preserves_namespace_type_aliases(const std::filesystem::path& root) {
    const std::filesystem::path source_dir = root / "build" / "native-direct-namespace-alias";
    const std::filesystem::path header = source_dir / "namespaced_alias.hpp";
    std::filesystem::remove_all(source_dir);
    std::filesystem::create_directories(source_dir);
    {
        std::ofstream out(header);
        out << "#pragma once\n"
               "namespace dudu_ns {\n"
               "struct Box { int value; int get() const { return value; } };\n"
               "using BoxAlias = Box;\n"
               "using Count = int;\n"
               "inline Count add(Count lhs, Count rhs) { return lhs + rhs; }\n"
               "inline BoxAlias make_box(int value) { return BoxAlias{value}; }\n"
               "}\n";
    }

    dudu::ModuleAst module = dudu::parse_source("from cpp.path import ./namespaced_alias.hpp\n"
                                                "\n"
                                                "def main() -> i32:\n"
                                                "    value: dudu_ns.Count = 20\n"
                                                "    box: dudu_ns.BoxAlias = dudu_ns.make_box(5)\n"
                                                "    return dudu_ns.add(value, 22) + box.get()\n",
                                                source_dir / "main.dd");
    dudu::ProjectConfig config;
    config.project_dir = source_dir;
    config.build_dir = source_dir / "build";
    dudu::merge_native_headers(module, {.config = config, .source_dir = source_dir});

    bool saw_alias = false;
    for (const dudu::NativeTypeDecl& type : module.native_types) {
        if (type.name == "dudu_ns.Count") {
            assert(type.identity.canonical_path == "dudu_ns.Count");
            saw_alias = true;
        }
    }
    assert(saw_alias);

    dudu::analyze_module(module, {.check_bodies = true});
    const std::string cpp = dudu::emit_cpp_source(module);
    assert(cpp.find("dudu_ns::Count value = 20;") != std::string::npos);
    assert(cpp.find("dudu_ns::BoxAlias box = dudu_ns::make_box(5);") != std::string::npos);
    assert(cpp.find("dudu_ns::add(value, 22)") != std::string::npos);
    assert(cpp.find("box.get()") != std::string::npos);
}

void test_native_operator_does_not_hijack_dudu_class_operator(const std::filesystem::path& root) {
    const std::filesystem::path source_dir = root / "build" / "native-operator-hijack";
    const std::filesystem::path header = source_dir / "native_operator.hpp";
    std::filesystem::remove_all(source_dir);
    std::filesystem::create_directories(source_dir);
    {
        std::ofstream out(header);
        out << "#pragma once\n"
               "template <typename T> struct NativeIter { T value; };\n"
               "template <typename T> NativeIter<T> operator*(T value, float scale) { "
               "return NativeIter<T>{value}; }\n";
    }

    dudu::ModuleAst module = dudu::parse_source("from cpp.path import ./native_operator.hpp\n"
                                                "\n"
                                                "class Vec:\n"
                                                "    x: f32\n"
                                                "\n"
                                                "    @operator(\"*\")\n"
                                                "    def mul(self, scale: f32) -> Vec:\n"
                                                "        return Vec(self.x * scale)\n"
                                                "\n"
                                                "def main() -> i32:\n"
                                                "    left = Vec(2.0)\n"
                                                "    value: Vec = left * 3.0\n"
                                                "    return i32(value.x)\n",
                                                source_dir / "main.dd");
    dudu::ProjectConfig config;
    config.project_dir = source_dir;
    config.build_dir = source_dir / "build";
    dudu::merge_native_headers(module, {.config = config, .source_dir = source_dir});
    dudu::analyze_module(module, {.check_bodies = true});
    const std::string cpp = dudu::emit_cpp_source(module);
    assert(cpp.find("left * 3.0") != std::string::npos);
    assert(cpp.find("NativeIter") == std::string::npos);
}

void test_native_identity_edge_cases(const std::filesystem::path& root) {
    const std::filesystem::path source_dir = root / "build" / "native-identity-edge-cases";
    const std::filesystem::path header = source_dir / "native_identity_cases.hpp";
    std::filesystem::remove_all(source_dir);
    std::filesystem::create_directories(source_dir);
    {
        std::ofstream out(header);
        out << "#pragma once\n"
               "namespace left { struct Thing {}; inline int value(Thing*) { return 1; } }\n"
               "namespace right { struct Thing {}; inline int value(Thing*) { return 2; } }\n"
               "namespace outer { inline namespace v1 { struct InlineThing {}; } }\n"
               "using LeftThing = left::Thing;\n";
    }

    dudu::ModuleAst module = dudu::parse_source(
        "from cpp.path import ./native_identity_cases.hpp\n", source_dir / "main.dd");
    dudu::ProjectConfig config;
    config.project_dir = source_dir;
    config.build_dir = source_dir / "build";
    dudu::merge_native_headers(module, {.config = config, .source_dir = source_dir});

    bool saw_left = false;
    bool saw_right = false;
    bool saw_alias = false;
    bool saw_inline = false;
    for (const dudu::NativeTypeDecl& type : module.native_types) {
        if (type.name == "left.Thing") {
            assert(type.identity.canonical_path == "left.Thing");
            saw_left = true;
        } else if (type.name == "right.Thing") {
            assert(type.identity.canonical_path == "right.Thing");
            saw_right = true;
        } else if (type.name == "LeftThing") {
            assert(type.identity.canonical_path == "LeftThing");
            assert(dudu::type_ref_head_name(type.type_ref) == "left.Thing");
            saw_alias = true;
        } else if (type.name == "outer.InlineThing") {
            assert(type.identity.canonical_path == "outer.InlineThing");
            saw_inline = true;
        }
    }
    assert(saw_left);
    assert(saw_right);
    assert(saw_alias);
    assert(saw_inline);
}

void test_native_identity_uses_clang_redeclaration_identity(const std::filesystem::path& root) {
    const std::filesystem::path source_dir = root / "build" / "native-identity-name-collision";
    const std::filesystem::path left = source_dir / "left.hpp";
    const std::filesystem::path right = source_dir / "right.hpp";
    std::filesystem::remove_all(source_dir);
    std::filesystem::create_directories(source_dir);
    {
        std::ofstream out(left);
        out << "#pragma once\nstruct Thing { int x; };\n";
    }
    {
        std::ofstream out(right);
        out << "#pragma once\nstruct Thing { float y; };\n";
    }

    dudu::ModuleAst module = dudu::parse_source("from cpp.path import ./left.hpp\n"
                                                "from cpp.path import ./right.hpp\n",
                                                source_dir / "main.dd");
    dudu::ProjectConfig config;
    config.project_dir = source_dir;
    config.build_dir = source_dir / "build";
    dudu::merge_native_headers(module, {.config = config, .source_dir = source_dir});
    size_t things = 0;
    for (const dudu::ClassDecl& klass : module.native_classes) {
        if (klass.name == "Thing") {
            ++things;
            assert(klass.identity.usr.starts_with("c:"));
        }
    }
    assert(things == 1);
}

} // namespace

int main() {
    try {
        const std::filesystem::path root = DUDU_REPO_ROOT;
        test_cxx_import_scans_c_globals_but_emits_plain_include(root);
        test_native_layout_survives_parsed_scan_cache(root);
        test_native_static_class_fields_keep_owner_and_cache(root);
        test_native_header_alias_preserves_identity(root);
        test_direct_cpp_import_preserves_namespace_type_aliases(root);
        test_native_operator_does_not_hijack_dudu_class_operator(root);
        test_native_identity_edge_cases(root);
        test_native_identity_uses_clang_redeclaration_identity(root);
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
