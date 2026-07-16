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

} // namespace

int main() {
    try {
        const std::filesystem::path root = DUDU_REPO_ROOT;
        test_native_compiler_identity_tracks_executable_changes(root);
        test_libclang_collects_stable_native_usrs(root);
        test_native_type_declaration_emission();
        test_native_header_type_scan(root);
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
