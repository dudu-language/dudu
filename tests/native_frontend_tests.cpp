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
void test_native_compiler_identity_tracks_executable_changes(const std::filesystem::path& root) {
    const std::filesystem::path compiler = root / "build/native-compiler-identity";
    {
        std::ofstream out(compiler);
        out << "compiler-v1\n";
    }
    const std::string first = dudu::native_header_compiler_identity(compiler.string());
    {
        std::ofstream out(compiler, std::ios::app);
        out << "compiler-v2\n";
    }
    const std::string second = dudu::native_header_compiler_identity(compiler.string());
    assert(!first.empty());
    assert(first != second);
}

void test_libclang_collects_stable_native_usrs(const std::filesystem::path& root) {
    const std::filesystem::path source_dir = root / "tests/fixtures";
    const std::filesystem::path probe = root / "build/native-usr-probe.cpp";
    {
        std::ofstream out(probe);
        out << "#include \"native_headers/simple_cpp.hpp\"\n";
    }
    dudu::ProjectConfig config;
    config.project_dir = root;
    const dudu::NativeCursorIdentityIndex identities =
        dudu::scan_native_cursor_identities(probe, {.config = config, .source_dir = source_dir});
    const std::filesystem::path header = source_dir / "native_headers/simple_cpp.hpp";
    const auto widget =
        identities.find(dudu::NativeCursorKind::Class, "Widget",
                        {.file = dudu::SourceFileName(header.string()), .line = 4, .column = 7});
    const auto widget_layout = identities.find_layout(
        dudu::NativeCursorKind::Class, "Widget",
        {.file = dudu::SourceFileName(header.string()), .line = 4, .column = 7});
    const auto first_overload =
        identities.find(dudu::NativeCursorKind::Function, "overloaded",
                        {.file = dudu::SourceFileName(header.string()), .line = 87, .column = 12});
    const auto second_overload =
        identities.find(dudu::NativeCursorKind::Function, "overloaded",
                        {.file = dudu::SourceFileName(header.string()), .line = 91, .column = 14});
    assert(widget.has_value() && !widget->empty());
    assert(widget_layout.has_value());
    assert(widget_layout->size == 4);
    assert(widget_layout->alignment == 4);
    assert(first_overload.has_value() && !first_overload->empty());
    assert(second_overload.has_value() && !second_overload->empty());
    assert(*first_overload != *second_overload);
    assert(identities.find_semantic(dudu::NativeCursorKind::Class, "dudu_native.Widget") == widget);

    const dudu::NativeCursorIdentityIndex restored =
        dudu::NativeCursorIdentityIndex::deserialize(identities.serialize());
    assert(restored.find(dudu::NativeCursorKind::Class, "Widget",
                         {.file = dudu::SourceFileName(header.string()), .line = 4, .column = 7}) ==
           widget);
    const auto restored_widget_layout = restored.find_layout(
        dudu::NativeCursorKind::Class, "Widget",
        {.file = dudu::SourceFileName(header.string()), .line = 4, .column = 7});
    assert(restored_widget_layout.has_value());
    assert(restored_widget_layout->size == widget_layout->size);
    assert(restored_widget_layout->alignment == widget_layout->alignment);
    assert(restored.find_semantic(dudu::NativeCursorKind::Class, "dudu_native.Widget") == widget);
    assert(
        restored.find(dudu::NativeCursorKind::Function, "overloaded",
                      {.file = dudu::SourceFileName(header.string()), .line = 91, .column = 14}) ==
        second_overload);
}

void test_native_type_declaration_emission() {
    const dudu::ModuleAst module =
        dudu::parse_source("from c.path import SDL3/SDL.h as sdl\n"
                           "\n"
                           "type SDL_Event\n"
                           "\n"
                           "def main() -> i32:\n"
                           "    event: SDL_Event\n"
                           "    while sdl.SDL_PollEvent(&event):\n"
                           "        if event.type == sdl.SDL_EVENT_QUIT:\n"
                           "            return 0\n"
                           "    return 1\n",
                           "native_type.dd");
    dudu::analyze_module(module, {.check_bodies = false});
    const std::string cpp = dudu::emit_cpp_source(module);
    assert(cpp.find("#include \"SDL3/SDL.h\"") != std::string::npos);
    assert(cpp.find("SDL_Event event{};") != std::string::npos);
    assert(cpp.find("struct SDL_Event") == std::string::npos);
}

bool identity_key_ends_with(const std::optional<std::string>& key, const std::string& suffix) {
    return key.has_value() && key->ends_with(suffix);
}

bool has_usr_identity(const std::optional<std::string>& key) {
    return key.has_value() && key->starts_with("usr:c:");
}

void test_native_header_type_scan(const std::filesystem::path& root) {
    dudu::ModuleAst module =
        dudu::parse_source("from c.path import native_headers/simple_c.h\n"
                           "from cpp.path import native_headers/simple_cpp.hpp\n"
                           "\n"
                           "def main() -> i32:\n"
                           "    event: DuduNativeEvent\n"
                           "    window: *DuduNativeWindow = None\n"
                           "    widget: DuduWidgetAlias\n"
                           "    other: Widget = Widget(5)\n"
                           "    namespaced: dudu_native.Widget = "
                           "dudu_native.Widget(6)\n"
                           "    derived: dudu_native.DerivedWidget = "
                           "dudu_native.DerivedWidget(9)\n"
                           "    nested: dudu_native.Outer.Inner = "
                           "dudu_native.Outer.Inner(21)\n"
                           "    amount: f32 = 2.0\n"
                           "    if derived.base_scaled(2) != 18:\n"
                           "        return 1\n"
                           "    if dudu_native.use_base_widget(&derived) != 9:\n"
                           "        return 2\n"
                           "    if dudu_native.read_const_ref(namespaced) != 6:\n"
                           "        return 5\n"
                           "    if nested.doubled() != 42:\n"
                           "        return 4\n"
                           "    proc = dudu_native_proc()\n"
                           "    if proc == None:\n"
                           "        return 3\n"
                           "    if DUDU_NATIVE_CHECK():\n"
                           "        return dudu_native.add(20, 22) + "
                           "DUDU_NATIVE_MAGIC\n"
                           "    if dudu_native_ready(&event):\n"
                           "        return DUDU_NATIVE_SCALE(other.scaled(3)) + "
                           "i32(dudu_native.overloaded(amount))\n"
                           "    dudu_native_format(\"%d %d\", event.type, "
                           "dudu_native_kind_ok)\n"
                           "    return event.type + widget.value + "
                           "other.value + i32(dudu_native_kind_ok)\n",
                           root / "tests/fixtures/native_scan.dd");
    dudu::ProjectConfig config;
    config.build_dir = root / "build" / "native-header-test-cache";
    std::filesystem::remove_all(config.build_dir);
    dudu::merge_native_headers(module, {.config = config, .source_dir = root / "tests/fixtures"});
    dudu::analyze_module(module, {.check_bodies = true});
    const std::string cpp = dudu::emit_cpp_source(module);
    assert(cpp.find("DuduNativeEvent event{};") != std::string::npos);
    assert(cpp.find("DuduNativeWindow* window = nullptr;") != std::string::npos);
    assert(cpp.find("DuduWidgetAlias widget{};") != std::string::npos);
    assert(cpp.find("Widget other = Widget(5);") != std::string::npos);
    assert(cpp.find("dudu_native::Widget namespaced = dudu_native::Widget(6);") !=
           std::string::npos);
    assert(cpp.find("dudu_native::DerivedWidget derived = dudu_native::DerivedWidget(9);") !=
           std::string::npos);
    assert(cpp.find("dudu_native::Outer::Inner nested = dudu_native::Outer::Inner(21);") !=
           std::string::npos);
    assert(cpp.find("derived.base_scaled(2)") != std::string::npos);
    assert(cpp.find("nested.doubled()") != std::string::npos);
    assert(cpp.find("dudu_native::use_base_widget((&derived))") != std::string::npos);
    assert(cpp.find("dudu_native::read_const_ref(namespaced)") != std::string::npos);
    bool saw_pack_holder = false;
    bool saw_namespace_identity = false;
    bool saw_type_identity = false;
    bool saw_function_identity = false;
    bool saw_function_doc = false;
    bool saw_value_identity = false;
    bool saw_macro_identity = false;
    for (const dudu::NativeNamespaceDecl& ns : module.native_namespaces) {
        if (ns.name == "dudu_native") {
            assert(ns.identity.canonical_path == "dudu_native");
            saw_namespace_identity = true;
        }
    }
    for (const dudu::NativeTypeDecl& type : module.native_types) {
        if (type.name == "DuduWidgetAlias") {
            assert(type.identity.canonical_path == "DuduWidgetAlias");
            saw_type_identity = true;
        }
    }
    for (const dudu::NativeFunctionDecl& fn : module.native_functions) {
        if (fn.name == "dudu_native.add") {
            assert(fn.identity.canonical_path == "dudu_native.add");
            saw_function_identity = true;
        } else if (fn.name == "dudu_native_add") {
            assert(fn.doc_comment.find("Adds two native integers.") != std::string::npos);
            saw_function_doc = true;
        }
    }
    for (const dudu::NativeValueDecl& value : module.native_values) {
        if (value.name == "DUDU_NATIVE_MAGIC") {
            assert(value.identity.canonical_path == "DUDU_NATIVE_MAGIC");
            saw_value_identity = true;
        }
    }
    for (const dudu::NativeMacroDecl& macro : module.native_macros) {
        if (macro.name == "DUDU_NATIVE_CHECK") {
            assert(macro.identity.canonical_path == "DUDU_NATIVE_CHECK");
            saw_macro_identity = true;
        }
    }
    for (const dudu::ClassDecl& klass : module.native_classes) {
        if (klass.name != "dudu_native.PackHolder") {
            continue;
        }
        for (const dudu::FunctionDecl& method : klass.methods) {
            if (method.name != "accept" || method.params.empty()) {
                continue;
            }
            const dudu::TypeRef& param = method.params.front().type_ref;
            assert(param.kind == dudu::TypeKind::Template);
            assert(param.name == "dudu_native.PackValue");
            assert(param.children.size() == 1);
            assert(param.children.front().kind == dudu::TypeKind::PackExpansion);
            saw_pack_holder = true;
        }
    }
    assert(saw_pack_holder);
    assert(saw_namespace_identity);
    assert(saw_type_identity);
    assert(saw_function_identity);
    assert(saw_function_doc);
    assert(saw_value_identity);
    assert(saw_macro_identity);
    bool saw_lsp_type_identity = false;
    bool saw_lsp_function_identity = false;
    bool saw_lsp_method_identity = false;
    bool saw_lsp_value_identity = false;
    bool saw_lsp_macro_identity = false;
    for (const dudu::Symbol& symbol : dudu::symbols_for_module(module)) {
        if (symbol.name == "DuduWidgetAlias") {
            assert(has_usr_identity(symbol.native_identity_key));
            saw_lsp_type_identity = true;
        } else if (symbol.name == "dudu_native.add") {
            assert(has_usr_identity(symbol.native_identity_key));
            saw_lsp_function_identity = true;
        } else if (symbol.name == "dudu_native.Widget.scaled") {
            assert(has_usr_identity(symbol.native_identity_key));
            saw_lsp_method_identity = true;
        } else if (symbol.name == "DUDU_NATIVE_MAGIC") {
            assert(symbol.native_identity_key == "path:DUDU_NATIVE_MAGIC" ||
                   identity_key_ends_with(symbol.native_identity_key,
                                          "native_headers/simple_c.h::DUDU_NATIVE_MAGIC"));
            saw_lsp_value_identity = true;
        } else if (symbol.name == "DUDU_NATIVE_CHECK") {
            assert(symbol.native_identity_key == "path:DUDU_NATIVE_CHECK");
            saw_lsp_macro_identity = true;
        }
    }
    assert(saw_lsp_type_identity);
    assert(saw_lsp_function_identity);
    assert(saw_lsp_method_identity);
    assert(saw_lsp_value_identity);
    assert(saw_lsp_macro_identity);
    assert(std::filesystem::exists(config.build_dir / "dudu-header-cache"));
}

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

void test_native_scan_dedupe_allows_opaque_redeclarations() {
    dudu::NativeHeaderScan scan;
    dudu::NativeTypeDecl left;
    left.name = "Thing";
    left.identity.canonical_path = "left.Thing";
    left.location = {.file = dudu::SourceFileName("left.hpp"), .line = 1, .column = 8};
    scan.types.push_back(std::move(left));
    dudu::NativeTypeDecl right;
    right.name = "Thing";
    right.identity.canonical_path = "right.Thing";
    right.location = {.file = dudu::SourceFileName("right.hpp"), .line = 1, .column = 8};
    scan.types.push_back(std::move(right));

    scan = dudu::dedupe_scan(std::move(scan));
    size_t count = 0;
    for (const dudu::NativeTypeDecl& type : scan.types) {
        if (type.name == "Thing") {
            ++count;
        }
    }
    assert(count == 1);
}

void test_native_scan_dedupes_function_redeclarations_by_usr() {
    dudu::NativeHeaderScan scan;
    for (const auto& [template_param, source] :
         {std::pair{"_RAIter", "algorithmfwd.h"},
          std::pair{"_RandomAccessIterator", "stl_algo.h"}}) {
        dudu::NativeFunctionDecl function;
        function.name = "std.sort";
        function.template_params = {template_param};
        function.template_param_is_value = {false};
        function.param_type_refs = {
            dudu::parse_type_text(template_param, {}),
            dudu::parse_type_text(template_param, {}),
        };
        function.return_type_ref = dudu::parse_type_text("void", {});
        function.identity.usr = "c:@N@std@FT@>1#Tsort#t0.0#S0_#v#";
        function.identity.canonical_path = "std.sort";
        function.location = {.file = dudu::SourceFileName(source), .line = 1, .column = 1};
        scan.functions.push_back(std::move(function));
    }

    scan = dudu::dedupe_scan(std::move(scan));
    assert(std::ranges::count_if(scan.functions, [](const dudu::NativeFunctionDecl& function) {
               return function.name == "std.sort";
           }) == 1);
}

void test_native_scan_dedupe_rejects_alias_identity_collision() {
    dudu::NativeHeaderScan scan;
    dudu::NativeTypeDecl left;
    left.name = "Thing";
    left.native_spelling = "i32";
    left.type_ref = dudu::parse_type_text(
        "i32", {.file = dudu::SourceFileName("left.hpp"), .line = 1, .column = 8});
    left.identity.canonical_path = "left.Thing";
    left.location = {.file = dudu::SourceFileName("left.hpp"), .line = 1, .column = 8};
    scan.types.push_back(std::move(left));
    dudu::NativeTypeDecl right;
    right.name = "Thing";
    right.native_spelling = "f32";
    right.type_ref = dudu::parse_type_text(
        "f32", {.file = dudu::SourceFileName("right.hpp"), .line = 1, .column = 8});
    right.identity.canonical_path = "right.Thing";
    right.location = {.file = dudu::SourceFileName("right.hpp"), .line = 1, .column = 8};
    scan.types.push_back(std::move(right));

    bool failed = false;
    try {
        (void)dudu::dedupe_scan(std::move(scan));
    } catch (const dudu::CompileError& error) {
        failed = std::string(error.what()).find("native type name collision: Thing") !=
                 std::string::npos;
    }
    assert(failed);
}

void test_native_scan_dedupe_allows_equivalent_alias_redeclarations() {
    dudu::NativeHeaderScan scan;
    for (const std::string source : {"first.hpp", "second.hpp"}) {
        dudu::NativeTypeDecl alias;
        alias.name = "NativeSize";
        alias.native_spelling = "u64";
        alias.type_ref = dudu::parse_type_text(
            "u64", {.file = dudu::SourceFileName(source), .line = 1, .column = 8});
        alias.identity.canonical_path = source + ".NativeSize";
        alias.location = {.file = dudu::SourceFileName(source), .line = 1, .column = 8};
        scan.types.push_back(std::move(alias));
    }

    scan = dudu::dedupe_scan(std::move(scan));
    assert(std::ranges::count_if(scan.types, [](const dudu::NativeTypeDecl& type) {
               return type.name == "NativeSize";
           }) == 1);
}

void test_native_scan_dedupe_resolves_alias_chains() {
    dudu::NativeHeaderScan scan;
    dudu::NativeTypeDecl bridge;
    bridge.name = "PlatformValue";
    bridge.native_spelling = "BuiltinValue";
    bridge.type_ref = dudu::parse_type_text("BuiltinValue", {});
    scan.types.push_back(std::move(bridge));

    for (const auto& [source, spelling] :
         {std::pair{"first.hpp", "BuiltinValue"}, std::pair{"second.hpp", "PlatformValue"}}) {
        dudu::NativeTypeDecl alias;
        alias.name = "PublicValue";
        alias.native_spelling = spelling;
        alias.type_ref = dudu::parse_type_text(spelling, {});
        alias.identity.canonical_path = std::string(source) + ".PublicValue";
        alias.location = {.file = dudu::SourceFileName(source), .line = 1, .column = 8};
        scan.types.push_back(std::move(alias));
    }

    scan = dudu::dedupe_scan(std::move(scan));
    assert(std::ranges::count_if(scan.types, [](const dudu::NativeTypeDecl& type) {
               return type.name == "PublicValue";
           }) == 1);
}

void test_native_single_underscore_function_macros(const std::filesystem::path& root) {
    const std::filesystem::path source_dir = root / "build" / "native-macro-scan";
    const std::filesystem::path header_dir = root / "build" / "native-macro-include";
    const std::filesystem::path header = header_dir / "single_underscore_macro.hpp";
    std::filesystem::remove_all(source_dir);
    std::filesystem::remove_all(header_dir);
    std::filesystem::create_directories(source_dir);
    std::filesystem::create_directories(header_dir);

    {
        std::ofstream out(header);
        out << "#pragma once\n"
               "#define _dudu_macro_call(a, b) ((a) + (b))\n"
               "#define __dudu_private_call(a) (a)\n"
               "#define _DUDU_PRIVATE_VALUE 3\n";
    }
    dudu::ModuleAst module = dudu::parse_source("from cpp.path import single_underscore_macro.hpp\n"
                                                "\n"
                                                "def main() -> i32:\n"
                                                "    return _dudu_macro_call(20, 22)\n",
                                                source_dir / "main.dd");
    dudu::ProjectConfig config;
    config.project_dir = source_dir;
    config.build_dir = source_dir / "build";
    config.include_dirs = {"../native-macro-include"};
    dudu::merge_native_headers(module, {.config = config, .source_dir = source_dir});
    dudu::analyze_module(module, {.check_bodies = true});
    const std::string cpp = dudu::emit_cpp_source(module);
    assert(cpp.find("_dudu_macro_call(20, 22)") != std::string::npos);
}

void test_native_call_arity(const std::filesystem::path& root) {
    dudu::ModuleAst module = dudu::parse_source("from c.path import native_headers/simple_c.h\n"
                                                "\n"
                                                "def main() -> i32:\n"
                                                "    return dudu_native_add()\n",
                                                root / "tests/fixtures/native_bad_arity.dd");
    dudu::merge_native_headers(
        module, {.config = dudu::ProjectConfig{}, .source_dir = root / "tests/fixtures"});
    bool rejected = false;
    try {
        dudu::analyze_module(module, {.check_bodies = true});
    } catch (const dudu::CompileError&) {
        rejected = true;
    }
    assert(rejected);
}

void test_native_header_collision(const std::filesystem::path& root) {
    bool failed = false;
    try {
        dudu::ModuleAst module = dudu::parse_source("from c.path import native_headers/simple_c.h\n"
                                                    "DUDU_NATIVE_MAGIC: i32 = 1\n",
                                                    root / "tests/fixtures/native_collision.dd");
        dudu::merge_native_headers(
            module, {.config = dudu::ProjectConfig{}, .source_dir = root / "tests/fixtures"});
        dudu::analyze_module(module, {.check_bodies = true});
    } catch (const dudu::CompileError&) {
        failed = true;
    }
    assert(failed);
}

void test_native_header_cache_ignores_generated_scanner_source(const std::filesystem::path& root) {
    const std::filesystem::path source_dir = root / "build" / "native-cache-generated-source";
    const std::filesystem::path generated = source_dir / "dudu_native_headers_123.cpp";
    const std::filesystem::path header = source_dir / "real.hpp";
    std::filesystem::remove_all(source_dir);
    std::filesystem::create_directories(source_dir);
    {
        std::ofstream out(generated);
        out << "#include \"real.hpp\"\n";
    }
    {
        std::ofstream out(header);
        out << "#pragma once\ninline int real_answer(void) { return 42; }\n";
    }

    const std::string make_deps =
        "dudu_native_scan: " + generated.string() + " " + header.string() + "\n";
    const std::string stamps =
        dudu::native_header_dependency_stamps_from_makefile(make_deps, generated);
    assert(stamps.find(generated.string()) == std::string::npos);
    assert(stamps.find(header.string()) != std::string::npos);
    assert(dudu::native_header_dependency_stamps_current(stamps));
}

void test_native_header_cache_invalidates_local_header(const std::filesystem::path& root) {
    const std::filesystem::path source_dir = root / "build" / "native-cache-invalidation";
    const std::filesystem::path header = source_dir / "cache_probe.hpp";
    dudu::ProjectConfig config;
    config.build_dir = source_dir / "build";
    std::filesystem::remove_all(source_dir);
    std::filesystem::create_directories(source_dir);

    {
        std::ofstream out(header);
        out << "#pragma once\ninline bool cache_probe(bool value) { return value; }\n";
    }
    dudu::ModuleAst first = dudu::parse_source("from cpp.path import ./cache_probe.hpp\n"
                                               "\n"
                                               "def main() -> i32:\n"
                                               "    if cache_probe(True):\n"
                                               "        return 42\n"
                                               "    return 0\n",
                                               source_dir / "main.dd");
    dudu::merge_native_headers(first, {.config = config, .source_dir = source_dir});
    dudu::analyze_module(first, {.check_bodies = true});

    {
        std::ofstream out(header);
        out << "#pragma once\n"
               "inline int cache_probe(int value, int salt) { return value + salt; }\n";
    }
    bool failed = false;
    try {
        dudu::ModuleAst second = dudu::parse_source("from cpp.path import ./cache_probe.hpp\n"
                                                    "\n"
                                                    "def main() -> i32:\n"
                                                    "    if cache_probe(True):\n"
                                                    "        return 42\n"
                                                    "    return 0\n",
                                                    source_dir / "main.dd");
        dudu::merge_native_headers(second, {.config = config, .source_dir = source_dir});
        dudu::analyze_module(second, {.check_bodies = true});
    } catch (const dudu::CompileError& error) {
        failed = std::string(error.what()).find("cache_probe") != std::string::npos;
    }
    assert(failed);
}

void test_native_header_cache_invalidates_included_header(const std::filesystem::path& root) {
    const std::filesystem::path source_dir = root / "build" / "native-cache-transitive";
    const std::filesystem::path wrapper = source_dir / "wrapper.hpp";
    const std::filesystem::path detail = source_dir / "detail.hpp";
    dudu::ProjectConfig config;
    config.build_dir = source_dir / "build";
    std::filesystem::remove_all(source_dir);
    std::filesystem::create_directories(source_dir);

    {
        std::ofstream out(wrapper);
        out << "#pragma once\n#include \"detail.hpp\"\n";
    }
    {
        std::ofstream out(detail);
        out << "#pragma once\ninline int included_answer(void) { return 42; }\n";
    }
    dudu::ModuleAst first = dudu::parse_source("from cpp.path import ./wrapper.hpp as wrap\n"
                                               "\n"
                                               "def main() -> i32:\n"
                                               "    return wrap.included_answer()\n",
                                               source_dir / "main.dd");
    dudu::merge_native_headers(first, {.config = config, .source_dir = source_dir});
    dudu::analyze_module(first, {.check_bodies = true});

    {
        std::ofstream out(detail);
        out << "#pragma once\ninline int replacement_answer(void) { return 42; }\n";
    }
    bool failed = false;
    try {
        dudu::ModuleAst second = dudu::parse_source("from cpp.path import ./wrapper.hpp as wrap\n"
                                                    "\n"
                                                    "def main() -> i32:\n"
                                                    "    return wrap.included_answer()\n",
                                                    source_dir / "main.dd");
        dudu::merge_native_headers(second, {.config = config, .source_dir = source_dir});
        dudu::analyze_module(second, {.check_bodies = true});
    } catch (const dudu::CompileError& error) {
        failed = std::string(error.what()).find("included_answer") != std::string::npos;
    }
    assert(failed);
}

void test_native_header_pointer_diagnostics(const std::filesystem::path& root) {
    bool failed = false;
    try {
        dudu::ModuleAst module = dudu::parse_source("from c.path import native_headers/simple_c.h\n"
                                                    "\n"
                                                    "def main() -> i32:\n"
                                                    "    event: DuduNativeEvent\n"
                                                    "    if dudu_native_ready(event):\n"
                                                    "        return 1\n"
                                                    "    return 0\n",
                                                    root / "tests/fixtures/native_pointer.dd");
        dudu::merge_native_headers(
            module, {.config = dudu::ProjectConfig{}, .source_dir = root / "tests/fixtures"});
        dudu::analyze_module(module, {.check_bodies = true});
    } catch (const dudu::CompileError&) {
        failed = true;
    }
    assert(failed);
}

void test_native_method_templates_do_not_mask_concrete_overloads(
    const std::filesystem::path& root) {
    const std::filesystem::path source_dir = root / "build" / "native-method-template-overload";
    const std::filesystem::path header = source_dir / "method_template_overload.hpp";
    std::filesystem::remove_all(source_dir);
    std::filesystem::create_directories(source_dir);
    {
        std::ofstream out(header);
        out << "#pragma once\n"
               "#include <string>\n"
               "struct Holder {\n"
               "    template <typename String>\n"
               "    String text() const { return String{}; }\n"
               "    const std::string& text() const { static std::string value = \"ok\"; return "
               "value; }\n"
               "};\n";
    }

    dudu::ModuleAst module =
        dudu::parse_source("from cpp.path import ./method_template_overload.hpp\n"
                           "\n"
                           "def main() -> i32:\n"
                           "    holder = Holder()\n"
                           "    text: str = holder.text()\n"
                           "    if len(text) == 2:\n"
                           "        return 42\n"
                           "    return 1\n",
                           source_dir / "main.dd");
    dudu::ProjectConfig config;
    config.project_dir = source_dir;
    config.build_dir = source_dir / "build";
    dudu::merge_native_headers(module, {.config = config, .source_dir = source_dir});

    bool saw_template = false;
    bool saw_concrete = false;
    for (const dudu::ClassDecl& klass : module.native_classes) {
        if (klass.name != "Holder") {
            continue;
        }
        for (const dudu::FunctionDecl& method : klass.methods) {
            if (method.name != "text") {
                continue;
            }
            if (!method.generic_params.empty()) {
                saw_template = method.generic_params.front() == "String";
            }
            if (method.generic_params.empty() &&
                dudu::type_assignment_allowed(dudu::parse_type_text("str"),
                                              dudu::function_return_type_ref(method))) {
                saw_concrete = true;
            }
        }
    }
    assert(saw_template);
    assert(saw_concrete);

    dudu::analyze_module(module, {.check_bodies = true});
    const std::string cpp = dudu::emit_cpp_source(module);
    assert(cpp.find("std::string text = holder.text();") != std::string::npos);
}

void test_native_class_templates_preserve_declared_metadata(const std::filesystem::path& root) {
    const std::filesystem::path source_dir = root / "tests/fixtures";
    const auto require_class = [](const dudu::ModuleAst& scanned,
                                  std::string_view name) -> const dudu::ClassDecl& {
        for (const dudu::ClassDecl& klass : scanned.native_classes) {
            if (klass.name == name) {
                return klass;
            }
        }
        throw std::runtime_error(std::string(name) + " was not scanned");
    };
    const auto require_type = [](const dudu::ModuleAst& scanned,
                                 std::string_view name) -> const dudu::NativeTypeDecl& {
        for (const dudu::NativeTypeDecl& type : scanned.native_types) {
            if (type.name == name) {
                return type;
            }
        }
        throw std::runtime_error(std::string(name) + " was not scanned");
    };
    dudu::ModuleAst module =
        dudu::parse_source("from cpp.path import native_dependent_alias_metadata.hpp\n",
                           source_dir / "native_dependent_alias_metadata_scan.dd");
    dudu::ProjectConfig config;
    config.project_dir = root;
    config.build_dir = root / "build";
    dudu::merge_native_headers(module, {.config = config, .source_dir = source_dir});

    const dudu::ClassDecl& envelope = require_class(module, "depmeta.Envelope");
    assert((envelope.generic_params ==
            std::vector<std::string>{"LeftPayload", "RightPayload", "Extent"}));

    bool saw_selected = false;
    bool saw_wrapped = false;
    for (const dudu::TypeAliasDecl& alias : envelope.type_aliases) {
        if (alias.name == "selected_result") {
            saw_selected = dudu::type_ref_text(alias.type_ref) == "RightPayload";
        }
        if (alias.name == "wrapped_result") {
            saw_wrapped = dudu::type_ref_text(alias.type_ref) == "depmeta.Wrapper[RightPayload]";
        }
    }
    assert(saw_selected);
    assert(saw_wrapped);

    const dudu::TypeRef selected = dudu::substitute_receiver_template_type(
        dudu::parse_type_text("selected_result"), envelope,
        {dudu::parse_type_text("i32"), dudu::parse_type_text("f32"), dudu::parse_type_text("4")});
    assert(dudu::type_ref_text(selected) == "f32");
    const dudu::ClassDecl& defaulted = require_class(module, "depmeta.DefaultedEnvelope");
    assert(defaulted.generic_params.size() == 2);
    assert(defaulted.generic_min_args == 1);
    assert(defaulted.generic_default_args.size() == 2);
    assert(!dudu::has_type_ref(defaulted.generic_default_args[0]));
    assert(dudu::type_ref_text(defaulted.generic_default_args[1]) == "4");
    const dudu::NativeTypeDecl& carrier = require_type(module, "depmeta.AliasCarrier");
    assert((carrier.generic_params == std::vector<std::string>{"Selected", "Left"}));
    assert(carrier.generic_min_args == 2);
    const dudu::NativeTypeDecl& defaulted_alias = require_type(module, "depmeta.DefaultedAlias");
    assert((defaulted_alias.generic_params == std::vector<std::string>{"Payload", "Holder"}));
    assert(defaulted_alias.generic_min_args == 1);
    assert(defaulted_alias.generic_default_args.size() == 2);
    assert(dudu::type_ref_text(defaulted_alias.generic_default_args[1]) ==
           "depmeta.Wrapper[Payload]");

    dudu::ModuleAst cached =
        dudu::parse_source("from cpp.path import native_dependent_alias_metadata.hpp\n",
                           source_dir / "native_dependent_alias_metadata_cached.dd");
    dudu::merge_native_headers(cached, {.config = config, .source_dir = source_dir});
    const dudu::ClassDecl& cached_envelope = require_class(cached, "depmeta.Envelope");
    assert(cached_envelope.generic_params == envelope.generic_params);
    assert(cached_envelope.type_aliases.size() == envelope.type_aliases.size());
    const dudu::ClassDecl& cached_defaulted = require_class(cached, "depmeta.DefaultedEnvelope");
    assert(cached_defaulted.generic_min_args == defaulted.generic_min_args);
    assert(cached_defaulted.generic_default_args.size() == defaulted.generic_default_args.size());
    assert(dudu::type_ref_text(cached_defaulted.generic_default_args[1]) == "4");
    const dudu::NativeTypeDecl& cached_carrier = require_type(cached, "depmeta.AliasCarrier");
    assert(cached_carrier.generic_params == carrier.generic_params);
    const dudu::NativeTypeDecl& cached_defaulted_alias =
        require_type(cached, "depmeta.DefaultedAlias");
    assert(cached_defaulted_alias.generic_default_args.size() ==
           defaulted_alias.generic_default_args.size());
    assert(dudu::type_ref_text(cached_defaulted_alias.generic_default_args[1]) ==
           "depmeta.Wrapper[Payload]");
}

void test_internal_native_template_aliases_resolve_public_returns(
    const std::filesystem::path& root) {
    const std::filesystem::path source_dir = root / "tests/fixtures";
    dudu::ProjectConfig config;
    config.project_dir = root;
    config.build_dir = root / "build";
    const dudu::ModuleAst import_only =
        dudu::parse_source("from cpp.path import native_internal_alias_factory.hpp\n",
                           source_dir / "native_internal_alias_factory_import.dd");
    const dudu::NativeHeaderScan scan =
        dudu::scan_native_headers(import_only, {.config = config, .source_dir = source_dir});
    const auto select_pack = std::ranges::find_if(scan.classes, [](const dudu::ClassDecl& klass) {
        return klass.name == "internal_alias.SelectPack" &&
               !klass.native_specialization_args.empty();
    });
    assert(select_pack != scan.classes.end());
    assert(select_pack->native_specialization_args.size() == 3);
    assert(dudu::type_ref_text(select_pack->native_specialization_args[0]) == "0");
    assert(dudu::type_ref_text(select_pack->native_specialization_args[1]) == "T0");
    assert(dudu::type_ref_text(select_pack->native_specialization_args[2]) == "Rest...");
    assert(select_pack->type_aliases.size() == 1);
    assert(dudu::type_ref_text(select_pack->type_aliases[0].type_ref) == "T0");

    dudu::ModuleAst module =
        dudu::parse_source("from cpp.path import native_internal_alias_factory.hpp\n"
                           "\n"
                           "class Node:\n"
                           "    value: i32\n"
                           "\n"
                           "def probe():\n"
                           "    holder: internal_alias.Holder[Node] = "
                           "internal_alias.make_holder[Node]()\n"
                           "    pack_result: i32 = "
                           "internal_alias.make_pack_element[1, i32, f32]()\n"
                           "    selected_pack: i32 = "
                           "internal_alias.select_pack[0, i32, f32]()\n"
                           "    pointer_result: i32 = internal_alias.select_result[*i32]()\n"
                           "    value_result: f32 = internal_alias.select_result[i32]()\n",
                           source_dir / "native_internal_alias_factory_scan.dd");
    dudu::merge_native_headers(module, {.config = config, .source_dir = source_dir});

    const auto internal =
        std::ranges::find_if(module.native_classes, [](const dudu::ClassDecl& klass) {
            return klass.name == "internal_alias.__factory_result";
        });
    assert(internal != module.native_classes.end());
    assert(std::ranges::any_of(internal->type_aliases, [](const dudu::TypeAliasDecl& alias) {
        return alias.name == "type" &&
               dudu::type_ref_text(alias.type_ref) == "internal_alias.Holder[T]";
    }));
    assert(std::ranges::none_of(module.native_types, [](const dudu::NativeTypeDecl& type) {
        return type.name == "internal_alias.__factory_result";
    }));
    const auto defaulted =
        std::ranges::find_if(module.native_classes, [](const dudu::ClassDecl& klass) {
            return klass.name == "internal_alias.__defaulted_result";
        });
    assert(defaulted != module.native_classes.end());
    assert(defaulted->generic_params.size() == 3);
    assert(defaulted->generic_min_args == 2);
    assert(defaulted->generic_default_args.size() == 3);
    assert(dudu::type_ref_text(defaulted->generic_default_args[2]) == "void");
    const auto scoped =
        std::ranges::find_if(module.native_classes, [](const dudu::ClassDecl& klass) {
            return klass.name == "internal_alias.__scope.__rebind";
        });
    assert(scoped != module.native_classes.end());
    assert(std::ranges::none_of(module.native_classes, [](const dudu::ClassDecl& klass) {
        return klass.name == "internal_alias.__rebind";
    }));
    const auto detected_specialization =
        std::ranges::find_if(module.native_classes, [](const dudu::ClassDecl& klass) {
            return klass.name == "internal_alias.DetectedValue" &&
                   !klass.native_specialization_args.empty();
        });
    assert(detected_specialization != module.native_classes.end());
    assert(detected_specialization->native_specialization_requirements.size() == 1);
    assert(
        dudu::type_ref_text(detected_specialization->native_specialization_requirements.front()) ==
        "internal_alias.Void[T.value_type]");

    const dudu::Symbols symbols = dudu::collect_symbols(module);
    const dudu::TypeRef resolved = dudu::resolve_associated_type_ref(
        symbols, dudu::parse_type_text("internal_alias.__factory_result[Node].type"));
    if (dudu::type_ref_text(resolved) != "internal_alias.Holder[Node]") {
        throw std::runtime_error("internal associated alias resolved as " +
                                 dudu::type_ref_text(resolved));
    }
    const dudu::TypeRef scoped_holder = dudu::resolve_associated_type_ref(
        symbols, dudu::parse_type_text("internal_alias.ScopedHolder[i32, Node]"));
    if (dudu::type_ref_text(scoped_holder) != "internal_alias.Holder[Node]") {
        throw std::runtime_error("nested internal alias resolved as " +
                                 dudu::type_ref_text(scoped_holder));
    }
    const dudu::TypeRef detected = dudu::resolve_associated_type_ref(
        symbols,
        dudu::parse_type_text("internal_alias.DetectedValue[internal_alias.WithValue].type"));
    assert(dudu::type_ref_text(detected) == "i32");
    const dudu::TypeRef fallback = dudu::resolve_associated_type_ref(
        symbols,
        dudu::parse_type_text("internal_alias.DetectedValue[internal_alias.WithoutValue].type"));
    assert(dudu::type_ref_text(fallback) == "f32");
    const auto signatures = symbols.native_function_signatures.find("internal_alias.make_holder");
    assert(signatures != symbols.native_function_signatures.end());
    assert(signatures->second.size() == 1);
    const dudu::FunctionSignature signature = dudu::substitute_explicit_template_signature(
        symbols, signatures->second.front(), {dudu::parse_type_text("Node")});
    const std::string return_type = dudu::type_ref_text(dudu::signature_return_type_ref(signature));
    if (return_type != "internal_alias.Holder[Node]") {
        throw std::runtime_error("internal factory return resolved as " + return_type);
    }
    const auto constant =
        std::ranges::find_if(module.native_classes, [](const dudu::ClassDecl& klass) {
            return klass.name == "internal_alias.Constant";
        });
    assert(constant != module.native_classes.end());
    const auto value =
        std::ranges::find_if(constant->static_fields,
                             [](const dudu::ConstDecl& field) { return field.name == "value"; });
    assert(value != constant->static_fields.end());
    assert(value->value_expr.kind == dudu::ExprKind::Name);
    assert(value->value_expr.name == "Value");
    const auto select =
        std::ranges::find_if(module.native_classes, [](const dudu::ClassDecl& klass) {
            return klass.name == "internal_alias.Select" &&
                   klass.native_specialization_args.empty();
        });
    assert(select != module.native_classes.end());
    const auto select_type =
        std::ranges::find_if(select->type_aliases,
                             [](const dudu::TypeAliasDecl& alias) { return alias.name == "type"; });
    assert(select_type != select->type_aliases.end());
    assert(select_type->generic_params.size() == 2);
    const std::string pointer_value = dudu::type_ref_text(dudu::resolve_associated_type_ref(
        symbols, dudu::parse_type_text("internal_alias.IsPointer[*i32].value")));
    const std::string plain_value = dudu::type_ref_text(dudu::resolve_associated_type_ref(
        symbols, dudu::parse_type_text("internal_alias.IsPointer[i32].value")));
    if (pointer_value != "true" || plain_value != "false") {
        throw std::runtime_error("native constexpr values resolved as " + pointer_value + " and " +
                                 plain_value);
    }
    const auto selected_signatures =
        symbols.native_function_signatures.find("internal_alias.select_result");
    assert(selected_signatures != symbols.native_function_signatures.end());
    const dudu::FunctionSignature pointer_signature = dudu::substitute_explicit_template_signature(
        symbols, selected_signatures->second.front(), {dudu::parse_type_text("*i32")});
    const dudu::FunctionSignature value_signature = dudu::substitute_explicit_template_signature(
        symbols, selected_signatures->second.front(), {dudu::parse_type_text("i32")});
    assert(dudu::type_ref_text(dudu::signature_return_type_ref(pointer_signature)) == "i32");
    if (const std::string selected =
            dudu::type_ref_text(dudu::signature_return_type_ref(value_signature));
        selected != "f32") {
        throw std::runtime_error("false native constexpr selection resolved as " + selected);
    }

    dudu::analyze_module(module, {.check_bodies = true});
}

void test_equivalent_dependent_native_spellings_compare() {
    const dudu::TypeRef parsed =
        dudu::parse_type_text("std.allocator_traits._Size[_Alloc, i64].type");
    dudu::TypeRef alternate = parsed;
    alternate.kind = dudu::TypeKind::Qualified;
    alternate.name = dudu::type_ref_text(parsed);
    alternate.children.clear();
    assert(!dudu::type_ref_equivalent(parsed, alternate));
    assert(dudu::comparison_rhs_allowed({}, "<", parsed, {}, alternate));
}

void test_standard_string_size_type_resolves_numeric(const std::filesystem::path& root) {
    const std::filesystem::path source_dir = root / "tests/fixtures";
    dudu::ModuleAst module =
        dudu::parse_source("from cpp import string\n"
                           "\n"
                           "def prefix(value: std.string, count: usize) -> std.string:\n"
                           "    return std.string(value.substr(0, count))\n",
                           source_dir / "native_string_size_type_scan.dd");
    dudu::ProjectConfig config;
    config.project_dir = root;
    config.build_dir = root / "build";
    dudu::merge_native_headers(module, {.config = config, .source_dir = source_dir});
    const dudu::Symbols symbols = dudu::collect_symbols(module);
    if (!symbols.classes.contains("std.allocator_traits._Size")) {
        return;
    }
    const dudu::TypeRef resolved = dudu::resolve_associated_type_ref(
        symbols, dudu::parse_type_text("std.allocator_traits._Size[std.allocator[i32], i64].type"));
    if (!dudu::native_numeric_operator_operand(resolved)) {
        throw std::runtime_error("std string size type resolved as " +
                                 dudu::type_ref_text(resolved));
    }
    dudu::analyze_module(module, {.check_bodies = true});
}

void test_native_class_partial_specialization_metadata(const std::filesystem::path& root) {
    const std::filesystem::path source_dir = root / "tests/fixtures";
    dudu::ProjectConfig config;
    config.project_dir = root;
    config.build_dir = root / "build/native-partial-specialization-cache";
    std::filesystem::remove_all(config.build_dir);

    const auto scan_and_check = [&]() {
        dudu::ModuleAst module =
            dudu::parse_source("from cpp.path import native_headers/simple_cpp.hpp\n",
                               source_dir / "native_partial_specialization.dd");
        dudu::merge_native_headers(module, {.config = config, .source_dir = source_dir});

        const std::string class_name = "dudu_native.AssociatedResult";
        size_t primary_count = 0;
        size_t specialization_count = 0;
        for (const dudu::ClassDecl& klass : module.native_classes) {
            if (klass.name != class_name) {
                continue;
            }
            if (klass.native_specialization_args.empty()) {
                ++primary_count;
                assert((klass.generic_params == std::vector<std::string>{"T", "Enabled"}));
                continue;
            }
            ++specialization_count;
            assert(klass.native_partial_specialization);
            assert((klass.generic_params == std::vector<std::string>{"T"}));
            assert(klass.native_specialization_args.size() == 2);
            assert(dudu::type_ref_text(klass.native_specialization_args[0]) == "T");
            const std::string enabled = dudu::type_ref_text(klass.native_specialization_args[1]);
            if (enabled != "1" && enabled != "true") {
                throw std::runtime_error("unexpected boolean specialization value: " + enabled);
            }
            assert(klass.type_aliases.size() == 1);
            assert(klass.type_aliases.front().name == "type");
            assert(dudu::type_ref_text(klass.type_aliases.front().type_ref) == "T");
        }
        assert(primary_count == 1);
        assert(specialization_count == 1);

        const dudu::Symbols symbols = dudu::collect_symbols(module);
        const dudu::TypeRef resolved = dudu::resolve_associated_type_ref(
            symbols, dudu::parse_type_text("dudu_native.AssociatedResult[i32, true].type"));
        assert(dudu::type_ref_text(resolved) == "i32");

        const dudu::TypeRef pointer_pattern = dudu::resolve_associated_type_ref(
            symbols, dudu::parse_type_text("dudu_native.PatternResult[*i32].type"));
        assert(dudu::type_ref_text(pointer_pattern) == "i32");

        const dudu::TypeRef exact_specialization = dudu::resolve_associated_type_ref(
            symbols, dudu::parse_type_text("dudu_native.PatternResult[i32].type"));
        assert(dudu::type_ref_text(exact_specialization) == "f32");

        const dudu::TypeRef ambiguous = dudu::resolve_associated_type_ref(
            symbols, dudu::parse_type_text("dudu_native.AmbiguousResult[Condition, i32].type"));
        assert(dudu::type_ref_text(ambiguous) ==
               "dudu_native.AmbiguousResult[Condition, i32].type");
    };

    scan_and_check();
    scan_and_check();
}

void test_native_class_redeclarations_merge_specialization_metadata() {
    dudu::ClassDecl declaration;
    declaration.name = "native.Result";
    declaration.identity.usr = "c:@N@native@Result";
    declaration.generic_params = {"T"};
    declaration.native_specialization_args = {dudu::parse_type_text("*T")};
    declaration.native_partial_specialization = true;

    dudu::ClassDecl definition = declaration;
    dudu::TypeAliasDecl alias;
    alias.name = "type";
    alias.type_ref = dudu::parse_type_text("T");
    definition.type_aliases.push_back(std::move(alias));
    dudu::FieldDecl field;
    field.name = "value";
    field.type_ref = dudu::parse_type_text("T");
    definition.fields.push_back(std::move(field));

    std::vector<dudu::ClassDecl> merged = {declaration};
    dudu::append_unique_native_classes(merged, {definition});
    assert(merged.size() == 1);
    assert(merged.front().native_partial_specialization);
    assert(merged.front().type_aliases.size() == 1);
    assert(merged.front().fields.size() == 1);
}

void test_native_fixed_array_typedef_alias(const std::filesystem::path& root) {
    assert(dudu::dudu_type("unsigned char[16]") == "array[u8][16]");
    assert(dudu::dudu_type("int[2][3]") == "array[i32][2, 3]");
    assert(dudu::dudu_type("enum NativeMode") == "NativeMode");
    assert(dudu::dudu_type("const enum NativeMode") == "const[NativeMode]");

    const std::filesystem::path source_dir = root / "build" / "native-fixed-array-typedef";
    const std::filesystem::path header = source_dir / "fixed_array_alias.hpp";
    std::filesystem::remove_all(source_dir);
    std::filesystem::create_directories(source_dir);
    {
        std::ofstream out(header);
        out << "#pragma once\n"
               "typedef unsigned char DuduFixedBytes[16];\n"
               "inline int read_fixed(const DuduFixedBytes value) { return value[0]; }\n"
               "inline void copy_fixed(DuduFixedBytes dst, const DuduFixedBytes src) { "
               "dst[0] = src[0]; }\n";
    }

    dudu::ModuleAst module =
        dudu::parse_source("from cpp.path import ./fixed_array_alias.hpp as native\n"
                           "\n"
                           "def main() -> i32:\n"
                           "    value: native.DuduFixedBytes\n"
                           "    copy: native.DuduFixedBytes\n"
                           "    native.copy_fixed(copy, value)\n"
                           "    if native.read_fixed(value) == 0:\n"
                           "        return 42\n"
                           "    return 1\n",
                           source_dir / "main.dd");
    dudu::ProjectConfig config;
    config.project_dir = source_dir;
    config.build_dir = source_dir / "build";
    dudu::merge_native_headers(module, {.config = config, .source_dir = source_dir});

    bool saw_alias = false;
    for (const dudu::NativeTypeDecl& type : module.native_types) {
        if (type.name == "native.DuduFixedBytes") {
            assert(dudu::type_ref_text(type.type_ref) == "array[u8][16]");
            saw_alias = true;
        }
    }
    assert(saw_alias);

    dudu::analyze_module(module, {.check_bodies = true});
    const std::string cpp = dudu::emit_cpp_source(module);
    assert(cpp.find("DuduFixedBytes value{};") != std::string::npos);
    assert(cpp.find("copy_fixed(copy, value);") != std::string::npos);
    assert(cpp.find("read_fixed(value)") != std::string::npos);
}

void test_native_scan_retries_with_c_prelude_for_context_headers(
    const std::filesystem::path& root) {
    const std::filesystem::path source_dir = root / "build" / "native-context-header-scan";
    const std::filesystem::path header = source_dir / "needs_c_context.h";
    std::filesystem::remove_all(source_dir);
    std::filesystem::create_directories(source_dir);
    {
        std::ofstream out(header);
        out << "#pragma once\n"
               "struct DuduNeedsContext {\n"
               "    size_t count;\n"
               "    int state;\n"
               "};\n";
    }

    dudu::ModuleAst module = dudu::parse_source("from c.path import ./needs_c_context.h as native\n"
                                                "\n"
                                                "def main() -> i32:\n"
                                                "    value: native.DuduNeedsContext\n"
                                                "    value.count = 7\n"
                                                "    value.state = 35\n"
                                                "    return i32(value.count) + value.state\n",
                                                source_dir / "main.dd");
    dudu::ProjectConfig config;
    config.project_dir = source_dir;
    config.build_dir = source_dir / "build";
    dudu::merge_native_headers(module, {.config = config, .source_dir = source_dir});
    dudu::analyze_module(module, {.check_bodies = true});
    const std::string cpp = dudu::emit_cpp_source(module);
    assert(cpp.find("value.count = 7;") != std::string::npos);
    assert(cpp.find("value.state = 35;") != std::string::npos);
}

void test_aliased_c_import_prefixes_visible_transitive_functions(
    const std::filesystem::path& root) {
    const std::filesystem::path source_dir = root / "build" / "native-transitive-c-filter";
    const std::filesystem::path header = source_dir / "wrap_stdio.h";
    std::filesystem::remove_all(source_dir);
    std::filesystem::create_directories(source_dir);
    {
        std::ofstream out(header);
        out << "#pragma once\n"
               "#include <stdio.h>\n"
               "static inline int dudu_wrap_answer(void) { return 42; }\n";
    }

    dudu::ProjectConfig config;
    config.project_dir = source_dir;
    config.build_dir = source_dir / "build";
    dudu::ModuleAst positive = dudu::parse_source("from c.path import ./wrap_stdio.h as wrap\n"
                                                  "\n"
                                                  "def main() -> i32:\n"
                                                  "    return wrap.dudu_wrap_answer()\n",
                                                  source_dir / "positive.dd");
    dudu::merge_native_headers(positive, {.config = config, .source_dir = source_dir});
    dudu::analyze_module(positive, {.check_bodies = true});
    const std::string cpp = dudu::emit_cpp_source(positive);
    assert(cpp.find("dudu_wrap_answer()") != std::string::npos);

    dudu::ModuleAst transitive = dudu::parse_source("from c.path import ./wrap_stdio.h as wrap\n"
                                                    "\n"
                                                    "def main() -> i32:\n"
                                                    "    return wrap.remove(\"x\")\n",
                                                    source_dir / "transitive.dd");
    dudu::merge_native_headers(transitive, {.config = config, .source_dir = source_dir});
    dudu::analyze_module(transitive, {.check_bodies = true});
    const std::string transitive_cpp = dudu::emit_cpp_source(transitive);
    assert(transitive_cpp.find("remove(\"x\")") != std::string::npos);
}

void test_native_scan_ignores_anonymous_record_definitions() {
    dudu::NativeHeaderScan scan;
    dudu::parse_ast_dump(
        scan,
        "|-CXXRecordDecl 0x1 <test.hpp:10:5, line:13:5> line:10:5 union "
        "definition\n"
        "|-CXXRecordDecl 0x2 <test.hpp:20:5, line:23:5> line:20:5 struct "
        "definition\n"
        "|-CXXRecordDecl 0x3 <test.hpp:30:1, line:32:1> line:30:8 struct "
        "NamedThing definition\n",
        {.file = dudu::SourceFileName("native_anonymous.dd"), .line = 1, .column = 1});
    scan = dudu::dedupe_scan(std::move(scan));
    bool saw_named = false;
    for (const dudu::NativeTypeDecl& type : scan.types) {
        assert(type.name != "definition");
        saw_named = saw_named || type.name == "NamedThing";
    }
    assert(saw_named);
}

void test_native_scan_preserves_scoped_enum_owners() {
    dudu::NativeHeaderScan scan;
    dudu::parse_ast_dump(scan,
                         "`-NamespaceDecl 0x1 <test.hpp:1:1, line:8:1> line:1:11 ns\n"
                         "  |-EnumDecl 0x2 <line:2:1, col:27> col:12 class First 'int'\n"
                         "  | |-EnumConstantDecl 0x3 <col:20> col:20 Same 'ns::First'\n"
                         "  | `-EnumConstantDecl 0x4 <col:26> col:26 FirstOnly 'ns::First'\n"
                         "  |-EnumDecl 0x5 <line:3:1, col:28> col:12 class Second 'int'\n"
                         "  | |-EnumConstantDecl 0x6 <col:21> col:21 Same 'ns::Second'\n"
                         "  | `-EnumConstantDecl 0x7 <col:27> col:27 SecondOnly 'ns::Second'\n"
                         "  `-EnumDecl 0x8 <line:4:1, col:24> col:8 Plain\n"
                         "    `-EnumConstantDecl 0x9 <col:16> col:16 PlainValue 'ns::Plain'\n",
                         {.file = dudu::SourceFileName("test.hpp"), .line = 1, .column = 1});
    scan = dudu::dedupe_scan(std::move(scan));

    const auto has_type = [&](const std::string& name) {
        return std::ranges::any_of(
            scan.types, [&](const dudu::NativeTypeDecl& type) { return type.name == name; });
    };
    const auto value_type = [&](const std::string& name) -> std::string {
        for (const dudu::NativeValueDecl& value : scan.values) {
            if (value.name == name) {
                return dudu::type_ref_text(value.type_ref);
            }
        }
        return {};
    };

    assert(has_type("ns.First"));
    assert(has_type("ns.Second"));
    assert(has_type("ns.Plain"));
    assert(value_type("ns.First.Same") == "ns.First");
    assert(value_type("ns.Second.Same") == "ns.Second");
    assert(value_type("ns.First.FirstOnly") == "ns.First");
    assert(value_type("ns.Second.SecondOnly") == "ns.Second");
    assert(value_type("ns.PlainValue") == "ns.Plain");
}

void test_native_scan_preserves_namespace_value_owners() {
    dudu::NativeHeaderScan scan;
    dudu::parse_ast_dump(scan,
                         "|-NamespaceDecl 0x1 <test.hpp:1:1, line:3:1> line:1:11 first\n"
                         "| `-VarDecl 0x2 <line:2:1, col:18> col:12 value 'const int'\n"
                         "`-NamespaceDecl 0x3 <line:4:1, line:6:1> line:4:11 second\n"
                         "  `-VarDecl 0x4 <line:5:1, col:18> col:12 value 'const int'\n",
                         {.file = dudu::SourceFileName("test.hpp"), .line = 1, .column = 1});
    scan = dudu::dedupe_scan(std::move(scan));

    const auto has_value = [&](const std::string& name) {
        return std::ranges::any_of(
            scan.values, [&](const dudu::NativeValueDecl& value) { return value.name == name; });
    };
    assert(has_value("first.value"));
    assert(has_value("second.value"));
}

void test_native_scan_imports_using_shadow_function_overloads() {
    dudu::NativeHeaderScan scan;
    dudu::parse_ast_dump(
        scan,
        "`-NamespaceDecl 0x1 <cmath:1:1, line:5:1> line:1:11 std\n"
        "  |-UsingDecl 0x2 <line:2:1, col:9> col:9 ::sqrt\n"
        "  |-UsingShadowDecl 0x3 <col:9> col:9 implicit Function 0x10 'sqrt' 'double "
        "(double) noexcept(true)'\n"
        "  |-UsingShadowDecl 0x4 <col:9> col:9 implicit Function 0x11 'sqrt' 'float "
        "(float) noexcept'\n"
        "  `-UsingShadowDecl 0x5 <col:9> col:9 implicit FunctionTemplate 0x12 'sqrt'\n",
        {.file = dudu::SourceFileName("cmath"), .line = 1, .column = 1});
    scan = dudu::dedupe_scan(std::move(scan));

    const auto overload_count =
        std::ranges::count_if(scan.functions, [](const dudu::NativeFunctionDecl& function) {
            return function.name == "std.sqrt";
        });
    assert(overload_count == 2);
    assert(std::ranges::any_of(scan.functions, [](const dudu::NativeFunctionDecl& function) {
        return function.name == "std.sqrt" &&
               dudu::type_ref_text(function.return_type_ref) == "f64" &&
               function.param_type_refs.size() == 1 &&
               dudu::type_ref_text(function.param_type_refs.front()) == "f64";
    }));
    assert(std::ranges::any_of(scan.functions, [](const dudu::NativeFunctionDecl& function) {
        return function.name == "std.sqrt" &&
               dudu::type_ref_text(function.return_type_ref) == "f32" &&
               function.param_type_refs.size() == 1 &&
               dudu::type_ref_text(function.param_type_refs.front()) == "f32";
    }));
}

void test_native_scan_preserves_callable_and_static_methods() {
    dudu::NativeHeaderScan scan;
    dudu::parse_ast_dump(
        scan,
        "`-CXXRecordDecl 0x1 <test.hpp:1:1, line:5:1> line:1:8 struct Callable definition\n"
        "  |-CXXMethodDecl 0x2 <line:2:5, col:40> col:9 operator() 'int (int) const'\n"
        "  | `-ParmVarDecl 0x3 <col:20, col:24> col:24 value 'int'\n"
        "  |-CXXMethodDecl 0x4 <line:3:5, col:29> col:16 now 'int ()' static\n"
        "  `-CXXMethodDecl 0x5 <line:4:5, col:31> col:14 clone 'Callable () const'\n",
        {.file = dudu::SourceFileName("test.hpp"), .line = 1, .column = 1});
    scan = dudu::dedupe_scan(std::move(scan));

    const auto klass = std::ranges::find_if(
        scan.classes, [](const dudu::ClassDecl& item) { return item.name == "Callable"; });
    assert(klass != scan.classes.end());
    const auto call = std::ranges::find_if(klass->methods, [](const dudu::FunctionDecl& method) {
        return method.name == "operator()";
    });
    assert(call != klass->methods.end());
    assert(dudu::type_ref_text(call->receiver_type_ref) == "const[Self]");
    assert(call->params.size() == 1);
    assert(dudu::type_ref_text(call->params.front().type_ref) == "i32");

    const auto now = std::ranges::find_if(
        klass->methods, [](const dudu::FunctionDecl& method) { return method.name == "now"; });
    assert(now != klass->methods.end());
    assert(!dudu::has_type_ref(now->receiver_type_ref));

    const auto clone = std::ranges::find_if(
        klass->methods, [](const dudu::FunctionDecl& method) { return method.name == "clone"; });
    assert(clone != klass->methods.end());
    assert(dudu::type_ref_text(clone->return_type_ref) == "Callable");
}

void test_native_scan_merges_reopened_namespaces() {
    dudu::NativeHeaderScan scan;
    for (const std::string source : {"first.hpp", "second.hpp"}) {
        dudu::NativeNamespaceDecl space;
        space.name = "shared";
        space.identity.usr = source + "::shared";
        space.location = {.file = dudu::SourceFileName(source), .line = 1, .column = 11};
        scan.namespaces.push_back(std::move(space));
    }
    scan = dudu::dedupe_scan(std::move(scan));
    assert(std::ranges::count_if(scan.namespaces, [](const dudu::NativeNamespaceDecl& space) {
               return space.name == "shared";
           }) == 1);
}

void test_native_scan_keeps_internal_generic_class_aliases_scoped() {
    dudu::NativeHeaderScan scan;
    dudu::parse_ast_dump(
        scan,
        "`-NamespaceDecl 0x1 <test.hpp:1:1, line:9:1> line:1:11 internal\n"
        "  `-ClassTemplateDecl 0x2 <line:2:1, line:8:3> line:3:10 __traits\n"
        "    |-TemplateTypeParmDecl 0x3 <line:2:10, col:19> col:19 referenced typename depth 0 "
        "index 0 T\n"
        "    |-TemplateTypeParmDecl 0x4 <col:27, col:48> col:36 typename depth 0 index 1\n"
        "    | `-TemplateArgument type 'typename T::value_type'\n"
        "    `-CXXRecordDecl 0x5 <line:3:3, line:8:3> line:3:10 struct __traits definition\n"
        "      |-TypedefDecl 0x6 <line:4:5, col:40> col:40 value_type 'typename T::value_type'\n"
        "      |-TypedefDecl 0x7 <line:5:5, col:26> col:26 reference 'value_type &'\n"
        "      `-TypedefDecl 0x8 <line:6:5, col:38> col:38 const_reference 'const value_type &'\n",
        {.file = dudu::SourceFileName("test.hpp"), .line = 1, .column = 1});
    scan = dudu::dedupe_scan(std::move(scan));

    const auto klass = std::ranges::find_if(scan.classes, [](const dudu::ClassDecl& item) {
        return item.name == "internal.__traits" && item.native_specialization_args.empty();
    });
    assert(klass != scan.classes.end());
    assert(klass->generic_params.size() == 2);
    assert(std::ranges::any_of(klass->type_aliases, [](const dudu::TypeAliasDecl& alias) {
        return alias.name == "reference" && alias.type_ref.kind == dudu::TypeKind::Reference &&
               alias.type_ref.children.size() == 1 &&
               dudu::type_ref_text(alias.type_ref.children.front()) ==
                   "internal.__traits.value_type";
    }));
    assert(std::ranges::none_of(scan.types, [](const dudu::NativeTypeDecl& type) {
        return type.name == "internal.reference";
    }));
}

void test_completed_native_scan_qualifies_forward_visible_types() {
    const dudu::SourceLocation location{
        .file = dudu::SourceFileName("test.hpp"), .line = 1, .column = 1};
    dudu::NativeHeaderScan scan;
    scan.types.push_back({
        .name = "std.LateAlias",
        .native_spelling = "i32",
        .type_ref = dudu::named_type_ref("i32", location),
        .location = location,
    });
    scan.functions.push_back({
        .name = "std.make_late",
        .template_params = {"T"},
        .template_param_is_value = {false},
        .param_names = {"value"},
        .param_native_spellings = {"T"},
        .param_type_refs = {dudu::named_type_ref("T", location)},
        .return_native_spelling = "LateAlias",
        .return_type_ref = dudu::named_type_ref("LateAlias", location),
        .location = location,
    });

    dudu::qualify_completed_native_scan(scan);

    assert(dudu::type_ref_text(scan.functions.front().return_type_ref) == "std.LateAlias");
    assert(dudu::type_ref_text(scan.functions.front().param_type_refs.front()) == "T");
}

} // namespace

int main() {
    try {
        const std::filesystem::path root = DUDU_REPO_ROOT;
        test_native_compiler_identity_tracks_executable_changes(root);
        test_libclang_collects_stable_native_usrs(root);
        test_native_type_declaration_emission();
        test_native_header_type_scan(root);
        test_cxx_import_scans_c_globals_but_emits_plain_include(root);
        test_native_layout_survives_parsed_scan_cache(root);
        test_native_static_class_fields_keep_owner_and_cache(root);
        test_native_header_alias_preserves_identity(root);
        test_direct_cpp_import_preserves_namespace_type_aliases(root);
        test_native_operator_does_not_hijack_dudu_class_operator(root);
        test_native_identity_edge_cases(root);
        test_native_identity_uses_clang_redeclaration_identity(root);
        test_native_scan_dedupe_allows_opaque_redeclarations();
        test_native_scan_dedupes_function_redeclarations_by_usr();
        test_native_scan_dedupe_rejects_alias_identity_collision();
        test_native_scan_dedupe_allows_equivalent_alias_redeclarations();
        test_native_scan_dedupe_resolves_alias_chains();
        test_native_single_underscore_function_macros(root);
        test_native_call_arity(root);
        test_native_header_collision(root);
        test_native_header_cache_ignores_generated_scanner_source(root);
        test_native_header_cache_invalidates_local_header(root);
        test_native_header_cache_invalidates_included_header(root);
        test_native_header_pointer_diagnostics(root);
        test_native_method_templates_do_not_mask_concrete_overloads(root);
        test_native_class_templates_preserve_declared_metadata(root);
        test_internal_native_template_aliases_resolve_public_returns(root);
        test_equivalent_dependent_native_spellings_compare();
        test_standard_string_size_type_resolves_numeric(root);
        test_native_class_partial_specialization_metadata(root);
        test_native_class_redeclarations_merge_specialization_metadata();
        test_native_fixed_array_typedef_alias(root);
        test_native_scan_retries_with_c_prelude_for_context_headers(root);
        test_aliased_c_import_prefixes_visible_transitive_functions(root);
        test_native_scan_ignores_anonymous_record_definitions();
        test_native_scan_preserves_scoped_enum_owners();
        test_native_scan_preserves_namespace_value_owners();
        test_native_scan_imports_using_shadow_function_overloads();
        test_native_scan_preserves_callable_and_static_methods();
        test_native_scan_merges_reopened_namespaces();
        test_native_scan_keeps_internal_generic_class_aliases_scoped();
        test_completed_native_scan_qualifies_forward_visible_types();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
