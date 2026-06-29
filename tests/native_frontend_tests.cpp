#include "dudu/core/ast_type.hpp"
#include "dudu/codegen/cpp_emit.hpp"
#include "dudu/lsp/language_server_symbols.hpp"
#include "dudu/native/native_header_cache_deps.hpp"
#include "dudu/native/native_header_parse.hpp"
#include "dudu/native/native_header_types.hpp"
#include "dudu/native/native_headers.hpp"
#include "dudu/parser/parser.hpp"
#include "dudu/sema/sema.hpp"
#include "dudu/sema/type_compat.hpp"

#include <cassert>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace {
void test_native_type_declaration_emission() {
    const dudu::ModuleAst module =
        dudu::parse_source("import c \"SDL3/SDL.h\" as sdl\n"
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

void test_native_header_type_scan(const std::filesystem::path& root) {
    dudu::ModuleAst module =
        dudu::parse_source("import c \"native_headers/simple_c.h\"\n"
                           "import cpp \"native_headers/simple_cpp.hpp\"\n"
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
    dudu::merge_native_header_types(module,
                                    {.config = config, .source_dir = root / "tests/fixtures"});
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
    assert(saw_value_identity);
    assert(saw_macro_identity);
    bool saw_lsp_type_identity = false;
    bool saw_lsp_function_identity = false;
    bool saw_lsp_method_identity = false;
    bool saw_lsp_value_identity = false;
    bool saw_lsp_macro_identity = false;
    for (const dudu::Symbol& symbol : dudu::symbols_for_module(module)) {
        if (symbol.name == "DuduWidgetAlias") {
            assert(identity_key_ends_with(symbol.native_identity_key,
                                          "native_headers/simple_cpp.hpp::DuduWidgetAlias"));
            saw_lsp_type_identity = true;
        } else if (symbol.name == "dudu_native.add") {
            assert(identity_key_ends_with(symbol.native_identity_key,
                                          "native_headers/simple_cpp.hpp::dudu_native.add"));
            saw_lsp_function_identity = true;
        } else if (symbol.name == "dudu_native.Widget.scaled") {
            assert(
                identity_key_ends_with(symbol.native_identity_key,
                                       "native_headers/simple_cpp.hpp::dudu_native.Widget.scaled"));
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
        dudu::parse_source("import cxx \"native_headers/simple_c.h\" as native\n"
                           "\n"
                           "def main() -> i32:\n"
                           "    return native.dudu_native_add(20, 22)\n",
                           root / "tests/fixtures/native_cxx_import.dd");
    dudu::ProjectConfig config;
    config.build_dir = root / "build" / "native-cxx-header-test-cache";
    std::filesystem::remove_all(config.build_dir);
    dudu::merge_native_header_types(module,
                                    {.config = config, .source_dir = root / "tests/fixtures"});
    dudu::analyze_module(module, {.check_bodies = true});
    const std::string cpp = dudu::emit_cpp_source(module);
    assert(cpp.find("#include \"native_headers/simple_c.h\"") != std::string::npos);
    assert(cpp.find("extern \"C\" {\n#include \"native_headers/simple_c.h\"") == std::string::npos);
    assert(cpp.find("return dudu_native_add(20, 22);") != std::string::npos);
    assert(cpp.find("native::dudu_native_add") == std::string::npos);
}

void test_native_header_alias_preserves_identity(const std::filesystem::path& root) {
    dudu::ModuleAst module =
        dudu::parse_source("import cpp \"native_headers/simple_cpp.hpp\" as native\n"
                           "\n"
                           "def main() -> i32:\n"
                           "    return native.dudu_native.add(20, 22)\n",
                           root / "tests/fixtures/native_alias_identity.dd");
    dudu::ProjectConfig config;
    config.build_dir = root / "build" / "native-header-alias-identity-cache";
    std::filesystem::remove_all(config.build_dir);
    dudu::merge_native_header_types(module,
                                    {.config = config, .source_dir = root / "tests/fixtures"});
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

    dudu::ModuleAst module =
        dudu::parse_source("import cpp \"./native_identity_cases.hpp\"\n", source_dir / "main.dd");
    dudu::ProjectConfig config;
    config.project_dir = source_dir;
    config.build_dir = source_dir / "build";
    dudu::merge_native_header_types(module, {.config = config, .source_dir = source_dir});

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

void test_native_identity_name_collision_is_rejected(const std::filesystem::path& root) {
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

    bool failed = false;
    try {
        dudu::ModuleAst module = dudu::parse_source("import cpp \"./left.hpp\"\n"
                                                    "import cpp \"./right.hpp\"\n",
                                                    source_dir / "main.dd");
        dudu::ProjectConfig config;
        config.project_dir = source_dir;
        config.build_dir = source_dir / "build";
        dudu::merge_native_header_types(module, {.config = config, .source_dir = source_dir});
    } catch (const dudu::CompileError& error) {
        failed = std::string(error.what()).find("native class name collision: Thing") !=
                 std::string::npos;
    }
    assert(failed);
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
    dudu::ModuleAst module = dudu::parse_source("import cpp \"single_underscore_macro.hpp\"\n"
                                                "\n"
                                                "def main() -> i32:\n"
                                                "    return _dudu_macro_call(20, 22)\n",
                                                source_dir / "main.dd");
    dudu::ProjectConfig config;
    config.project_dir = source_dir;
    config.build_dir = source_dir / "build";
    config.include_dirs = {"../native-macro-include"};
    dudu::merge_native_header_types(module, {.config = config, .source_dir = source_dir});
    dudu::analyze_module(module, {.check_bodies = true});
    const std::string cpp = dudu::emit_cpp_source(module);
    assert(cpp.find("_dudu_macro_call(20, 22)") != std::string::npos);
}

void test_native_call_arity(const std::filesystem::path& root) {
    dudu::ModuleAst module = dudu::parse_source("import c \"native_headers/simple_c.h\"\n"
                                                "\n"
                                                "def main() -> i32:\n"
                                                "    return dudu_native_add()\n",
                                                root / "tests/fixtures/native_bad_arity.dd");
    dudu::merge_native_header_types(
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
        dudu::ModuleAst module = dudu::parse_source("import c \"native_headers/simple_c.h\"\n"
                                                    "DUDU_NATIVE_MAGIC: i32 = 1\n",
                                                    root / "tests/fixtures/native_collision.dd");
        dudu::merge_native_header_types(
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
    dudu::ModuleAst first = dudu::parse_source("import cpp \"./cache_probe.hpp\"\n"
                                               "\n"
                                               "def main() -> i32:\n"
                                               "    if cache_probe(True):\n"
                                               "        return 42\n"
                                               "    return 0\n",
                                               source_dir / "main.dd");
    dudu::merge_native_header_types(first, {.config = config, .source_dir = source_dir});
    dudu::analyze_module(first, {.check_bodies = true});

    {
        std::ofstream out(header);
        out << "#pragma once\n"
               "inline int cache_probe(int value, int salt) { return value + salt; }\n";
    }
    bool failed = false;
    try {
        dudu::ModuleAst second = dudu::parse_source("import cpp \"./cache_probe.hpp\"\n"
                                                    "\n"
                                                    "def main() -> i32:\n"
                                                    "    if cache_probe(True):\n"
                                                    "        return 42\n"
                                                    "    return 0\n",
                                                    source_dir / "main.dd");
        dudu::merge_native_header_types(second, {.config = config, .source_dir = source_dir});
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
    dudu::ModuleAst first = dudu::parse_source("import cpp \"./wrapper.hpp\" as wrap\n"
                                               "\n"
                                               "def main() -> i32:\n"
                                               "    return wrap.included_answer()\n",
                                               source_dir / "main.dd");
    dudu::merge_native_header_types(first, {.config = config, .source_dir = source_dir});
    dudu::analyze_module(first, {.check_bodies = true});

    {
        std::ofstream out(detail);
        out << "#pragma once\ninline int replacement_answer(void) { return 42; }\n";
    }
    bool failed = false;
    try {
        dudu::ModuleAst second = dudu::parse_source("import cpp \"./wrapper.hpp\" as wrap\n"
                                                    "\n"
                                                    "def main() -> i32:\n"
                                                    "    return wrap.included_answer()\n",
                                                    source_dir / "main.dd");
        dudu::merge_native_header_types(second, {.config = config, .source_dir = source_dir});
        dudu::analyze_module(second, {.check_bodies = true});
    } catch (const dudu::CompileError& error) {
        failed = std::string(error.what()).find("included_answer") != std::string::npos;
    }
    assert(failed);
}

void test_native_header_pointer_diagnostics(const std::filesystem::path& root) {
    bool failed = false;
    try {
        dudu::ModuleAst module = dudu::parse_source("import c \"native_headers/simple_c.h\"\n"
                                                    "\n"
                                                    "def main() -> i32:\n"
                                                    "    event: DuduNativeEvent\n"
                                                    "    if dudu_native_ready(event):\n"
                                                    "        return 1\n"
                                                    "    return 0\n",
                                                    root / "tests/fixtures/native_pointer.dd");
        dudu::merge_native_header_types(
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

    dudu::ModuleAst module = dudu::parse_source("import cpp \"./method_template_overload.hpp\"\n"
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
    dudu::merge_native_header_types(module, {.config = config, .source_dir = source_dir});

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

void test_native_fixed_array_typedef_alias(const std::filesystem::path& root) {
    assert(dudu::dudu_type("unsigned char[16]") == "array[u8][16]");
    assert(dudu::dudu_type("int[2][3]") == "array[i32][2, 3]");

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

    dudu::ModuleAst module = dudu::parse_source("import cpp \"./fixed_array_alias.hpp\" as native\n"
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
    dudu::merge_native_header_types(module, {.config = config, .source_dir = source_dir});

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

    dudu::ModuleAst module = dudu::parse_source("import c \"./needs_c_context.h\" as native\n"
                                                "\n"
                                                "def main() -> i32:\n"
                                                "    value: struct DuduNeedsContext\n"
                                                "    value.count = 7\n"
                                                "    value.state = 35\n"
                                                "    return i32(value.count) + value.state\n",
                                                source_dir / "main.dd");
    dudu::ProjectConfig config;
    config.project_dir = source_dir;
    config.build_dir = source_dir / "build";
    dudu::merge_native_header_types(module, {.config = config, .source_dir = source_dir});
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
    dudu::ModuleAst positive = dudu::parse_source("import c \"./wrap_stdio.h\" as wrap\n"
                                                  "\n"
                                                  "def main() -> i32:\n"
                                                  "    return wrap.dudu_wrap_answer()\n",
                                                  source_dir / "positive.dd");
    dudu::merge_native_header_types(positive, {.config = config, .source_dir = source_dir});
    dudu::analyze_module(positive, {.check_bodies = true});
    const std::string cpp = dudu::emit_cpp_source(positive);
    assert(cpp.find("dudu_wrap_answer()") != std::string::npos);

    dudu::ModuleAst transitive = dudu::parse_source("import c \"./wrap_stdio.h\" as wrap\n"
                                                    "\n"
                                                    "def main() -> i32:\n"
                                                    "    return wrap.remove(\"x\")\n",
                                                    source_dir / "transitive.dd");
    dudu::merge_native_header_types(transitive, {.config = config, .source_dir = source_dir});
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

} // namespace

int main() {
    try {
        const std::filesystem::path root = DUDU_REPO_ROOT;
        test_native_type_declaration_emission();
        test_native_header_type_scan(root);
        test_cxx_import_scans_c_globals_but_emits_plain_include(root);
        test_native_header_alias_preserves_identity(root);
        test_native_identity_edge_cases(root);
        test_native_identity_name_collision_is_rejected(root);
        test_native_scan_dedupe_allows_opaque_redeclarations();
        test_native_scan_dedupe_rejects_alias_identity_collision();
        test_native_single_underscore_function_macros(root);
        test_native_call_arity(root);
        test_native_header_collision(root);
        test_native_header_cache_ignores_generated_scanner_source(root);
        test_native_header_cache_invalidates_local_header(root);
        test_native_header_cache_invalidates_included_header(root);
        test_native_header_pointer_diagnostics(root);
        test_native_method_templates_do_not_mask_concrete_overloads(root);
        test_native_fixed_array_typedef_alias(root);
        test_native_scan_retries_with_c_prelude_for_context_headers(root);
        test_aliased_c_import_prefixes_visible_transitive_functions(root);
        test_native_scan_ignores_anonymous_record_definitions();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
