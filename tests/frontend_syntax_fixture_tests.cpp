#include "dudu/codegen/cpp_emit.hpp"
#include "dudu/codegen/cpp_emit_modules.hpp"
#include "dudu/codegen/cpp_lower.hpp"
#include "dudu/codegen/cpp_stmt_types.hpp"
#include "dudu/core/ast_expr.hpp"
#include "dudu/core/ast_type.hpp"
#include "dudu/format/format.hpp"
#include "dudu/format/format_path.hpp"
#include "dudu/lsp/language_server_completion.hpp"
#include "dudu/lsp/language_server_definition.hpp"
#include "dudu/lsp/language_server_diagnostics.hpp"
#include "dudu/lsp/language_server_hover.hpp"
#include "dudu/lsp/language_server_json.hpp"
#include "dudu/lsp/language_server_navigation.hpp"
#include "dudu/lsp/language_server_reference_collect.hpp"
#include "dudu/lsp/language_server_references.hpp"
#include "dudu/lsp/language_server_support.hpp"
#include "dudu/native/native_headers.hpp"
#include "dudu/parser/lexer.hpp"
#include "dudu/parser/parser.hpp"
#include "dudu/project/module_loader.hpp"
#include "dudu/project/project_config.hpp"
#include "dudu/project/project_index.hpp"
#include "dudu/sema/sema.hpp"
#include "dudu/sema/sema_alloc.hpp"
#include "dudu/sema/sema_expr_cpp_escape_calls.hpp"
#include "dudu/sema/sema_expr_internal.hpp"
#include "dudu/sema/sema_function_type.hpp"
#include "dudu/sema/sema_index.hpp"
#include "dudu/sema/sema_ops.hpp"
#include "dudu/sema/type_compat.hpp"

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

void test_lexer_indentation() {
    const std::string source = "def main() -> i32:\n"
                               "    x: i32 = 1\n"
                               "    if x > 0:\n"
                               "        return x\n"
                               "    return 0\n";
    const std::vector<dudu::Token> tokens = dudu::lex_source(source, "inline.dd");

    int indents = 0;
    int dedents = 0;
    for (const dudu::Token& token : tokens) {
        indents += token.kind == dudu::TokenKind::Indent ? 1 : 0;
        dedents += token.kind == dudu::TokenKind::Dedent ? 1 : 0;
    }
    assert(indents == 2);
    assert(dedents == 2);
}

void test_import_bindings() {
    const dudu::ModuleAst module = dudu::parse_source("import renderer.camera\n"
                                                      "import renderer.light\n"
                                                      "import renderer.camera as camera\n"
                                                      "from ui.button import Button as UiButton\n",
                                                      "imports.dd");
    assert(module.imports.size() == 4);
    assert(dudu::bound_import_name(module.imports[0]) == "renderer");
    assert(dudu::bound_import_name(module.imports[2]) == "camera");
    assert(dudu::bound_import_name(module.imports[3]) == "UiButton");
    assert(module.imports[0].range.start.line == 1);
    assert(module.imports[0].range.end.line == 1);

    bool collided = false;
    try {
        (void)dudu::parse_source("from ui.button import Button\n"
                                 "from game.input import Button\n",
                                 "collision.dd");
    } catch (const dudu::CompileError&) {
        collided = true;
    }
    assert(collided);

    const dudu::ModuleAst direct_native =
        dudu::parse_source("from c import SDL3/SDL.h\n"
                           "from c import sqlite3.h as sqlite\n"
                           "from c.path import vendor/foo.h\n"
                           "from c.path import vendor/foo.h as foo\n"
                           "from cxx import libxml/parser.h\n"
                           "from cxx import libxml/parser.h as xml\n"
                           "from cxx.path import vendor/c_api.h\n"
                           "from cxx.path import vendor/c_api.h as c_api\n"
                           "from cpp import thread\n"
                           "from cpp import thread as threading\n"
                           "from cpp.path import vendor/math.hpp\n"
                           "from cpp.path import vendor/math.hpp as math\n",
                           "direct_native.dd");
    assert(direct_native.imports.size() == 12);
    for (size_t index = 0; index < direct_native.imports.size(); ++index) {
        const dudu::ImportDecl& import = direct_native.imports[index];
        assert(import.native_include_style == (index % 4 >= 2 ? dudu::NativeIncludeStyle::Path
                                                              : dudu::NativeIncludeStyle::System));
        assert(import.alias.empty() == (index % 2 == 0));
        assert(import.native_language_range.start.column > 0);
        if (index % 4 >= 2) {
            assert(import.native_path_mode_range.start.column > 0);
        }
    }
    assert(direct_native.imports[0].kind == dudu::ImportKind::ForeignC);
    assert(direct_native.imports[4].kind == dudu::ImportKind::ForeignCxx);
    assert(direct_native.imports[8].kind == dudu::ImportKind::ForeignCpp);
    assert(dudu::render_import_decl(direct_native.imports[0]) == "from c import SDL3/SDL.h");
    assert(dudu::render_import_decl(direct_native.imports[5]) ==
           "from cxx import libxml/parser.h as xml");
    assert(dudu::render_import_decl(direct_native.imports[11]) ==
           "from cpp.path import vendor/math.hpp as math");

    const std::string native_cpp = dudu::emit_cpp_source(direct_native);
    assert(native_cpp.find("extern \"C\" {\n#include <SDL3/SDL.h>\n}") != std::string::npos);
    assert(native_cpp.find("extern \"C\" {\n#include \"vendor/foo.h\"\n}") != std::string::npos);
    assert(native_cpp.find("#include <libxml/parser.h>") != std::string::npos);
    assert(native_cpp.find("#include \"vendor/c_api.h\"") != std::string::npos);
    assert(native_cpp.find("#include <thread>") != std::string::npos);
    assert(native_cpp.find("#include \"vendor/math.hpp\"") != std::string::npos);
}

void test_canonical_examples_parse(const std::filesystem::path& root) {
    const std::vector<std::string> examples = {
        "allocators.dd",           "audio_synth.dd",     "compile_time.dd",
        "cpp_library.dd",          "cuda_kernel.dd",     "cuda_shared_memory_tile.dd",
        "ffmpeg_probe_decode.dd",  "fibonacci.dd",       "function_pointers.dd",
        "glfw_opengl_triangle.dd", "image_filter.dd",    "layout_hardware.dd",
        "modules_visibility.dd",   "native_escape.dd",   "numerics_kmeans.dd",
        "opencl_kernel_host.dd",   "raylib_game.dd",     "sdl3_imgui.dd",
        "sdl3_window.dd",          "shader_compute.dd",  "systems_mmap.dd",
        "threading_atomics.dd",    "vulkan_triangle.dd", "web_server.dd",
    };

    for (const std::string& example : examples) {
        const std::filesystem::path path = root / "examples" / example;
        const dudu::ModuleAst module = dudu::parse_source(read_file(path), path);
        assert(!module.classes.empty() || !module.functions.empty() || !module.imports.empty() ||
               !module.constants.empty() || !module.static_asserts.empty() ||
               !module.enums.empty() || !module.aliases.empty());
    }
}

void test_rejected_oop_surface_fixtures(const std::filesystem::path& root) {
    struct Case {
        std::string path;
        std::string expected;
    };
    const std::vector<Case> cases = {
        {"bad_dunder_init.dd", "reserved Python-style dunder"},
        {"bad_dunder_del.dd", "reserved Python-style dunder"},
        {"bad_dunder_operator.dd", "reserved Python-style dunder"},
        {"bad_dunder_free.dd", "reserved Python-style dunder"},
        {"bad_staticmethod_self.dd", "@staticmethod is not supported"},
        {"bad_staticmethod_free.dd", "@staticmethod is not supported"},
        {"bad_classmethod.dd", "@classmethod is not supported"},
        {"bad_property.dd", "@property is not supported"},
        {"bad_class_member_visibility.dd", "explicit visibility keywords are not supported"},
    };

    for (const Case& item : cases) {
        bool rejected = false;
        try {
            const std::filesystem::path path = root / "tests" / "fixtures" / item.path;
            const dudu::ModuleAst module = dudu::parse_source(read_file(path), path);
            dudu::analyze_module(module, {.check_bodies = true});
        } catch (const dudu::CompileError& error) {
            rejected = std::string(error.what()).find(item.expected) != std::string::npos;
        }
        assert(rejected);
    }
}

void test_rejected_fancy_indexing_fixtures(const std::filesystem::path& root) {
    struct Case {
        std::string path;
        std::string expected;
    };
    const std::vector<Case> cases = {
        {"bad_pairwise_indexer_missing_hook.dd", "no matching @operator(\"[]\")"},
        {"bad_cartesian_indexer_missing_hook.dd", "no matching @operator(\"[]\")"},
        {"bad_tensor_missing_index_hook.dd",
         "no matching @operator(\"[]\") for indexed access to tensor"},
        {"bad_tensor_missing_index_set_hook.dd",
         "no matching @operator(\"[]=\") for indexed assignment to tensor"},
    };

    for (const Case& item : cases) {
        bool rejected = false;
        try {
            const std::filesystem::path path = root / "tests" / "fixtures" / item.path;
            const dudu::ModuleAst module = dudu::parse_source(read_file(path), path);
            dudu::analyze_module(module, {.check_bodies = true});
        } catch (const dudu::CompileError& error) {
            rejected = std::string(error.what()).find(item.expected) != std::string::npos;
        }
        assert(rejected);
    }
}

} // namespace

int main() {
    try {
        const std::filesystem::path root = DUDU_REPO_ROOT;
        test_lexer_indentation();
        test_import_bindings();
        test_canonical_examples_parse(root);
        test_rejected_oop_surface_fixtures(root);
        test_rejected_fancy_indexing_fixtures(root);
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
