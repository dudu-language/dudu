#include "dudu/ast_expr.hpp"
#include "dudu/ast_type.hpp"
#include "dudu/cpp_emit.hpp"
#include "dudu/cpp_emit_modules.hpp"
#include "dudu/cpp_lower.hpp"
#include "dudu/cpp_stmt_types.hpp"
#include "dudu/format.hpp"
#include "dudu/format_path.hpp"
#include "dudu/language_server_completion.hpp"
#include "dudu/language_server_diagnostics.hpp"
#include "dudu/language_server_definition.hpp"
#include "dudu/language_server_hover.hpp"
#include "dudu/language_server_json.hpp"
#include "dudu/language_server_navigation.hpp"
#include "dudu/language_server_references.hpp"
#include "dudu/lexer.hpp"
#include "dudu/module_loader.hpp"
#include "dudu/native_headers.hpp"
#include "dudu/parser.hpp"
#include "dudu/project_config.hpp"
#include "dudu/sema.hpp"
#include "dudu/sema_alloc.hpp"
#include "dudu/sema_expr_cpp_escape_calls.hpp"
#include "dudu/sema_expr_internal.hpp"
#include "dudu/sema_function_type.hpp"
#include "dudu/sema_index.hpp"
#include "dudu/type_compat.hpp"

#include <cassert>
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

std::string
infer_emitted_local_type_text(std::string_view expr_text,
                              const std::map<std::string, std::string>& locals,
                              const std::map<std::string, dudu::TypeRef>& function_returns) {
    std::map<std::string, dudu::TypeRef> local_type_refs;
    for (const auto& [name, type] : locals) {
        local_type_refs[name] = dudu::parse_type_text(type);
    }
    const dudu::Symbols symbols;
    const dudu::TypeRef inferred = dudu::infer_emitted_local_type_ref(
        dudu::parse_expr_text(expr_text), local_type_refs, function_returns, &symbols);
    return dudu::has_type_ref(inferred) ? dudu::substitute_type_ref_text(inferred, {})
                                        : std::string{};
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
        dudu::parse_source("import cpp \"imgui.h\"\n", "direct_native.dd");
    assert(direct_native.imports.size() == 1);
    assert(direct_native.imports[0].alias.empty());
}

void write_file(const std::filesystem::path& path, const std::string& text) {
    std::ofstream file(path);
    if (!file) {
        throw std::runtime_error("could not write " + path.string());
    }
    file << text;
}

void test_module_loader_canonicalizes_physical_modules() {
    const std::filesystem::path dir =
        std::filesystem::temp_directory_path() / "dudu_module_identity_test";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    write_file(dir / "vec3.dd", "class Vec3:\n"
                                "    x: f32\n"
                                "    y: f32\n"
                                "    z: f32\n");
    write_file(dir / "camera.dd", "from vec3 import Vec3\n"
                                  "\n"
                                  "class Camera:\n"
                                  "    origin: Vec3\n"
                                  "\n"
                                  "def origin(camera: Camera) -> Vec3:\n"
                                  "    return camera.origin\n");
    write_file(dir / "main.dd", "from vec3 import Vec3\n"
                                "from camera import Camera as ViewCamera\n"
                                "from camera import origin as camera_origin\n"
                                "\n"
                                "def main() -> i32:\n"
                                "    camera = ViewCamera(origin=Vec3(1.0, 2.0, 3.0))\n"
                                "    return i32(camera_origin(camera).x)\n");

    const dudu::ModuleAst module = dudu::load_source_tree(dir / "main.dd");
    assert(module.module_units.size() == 3);
    assert(module.module_units[0].module_path == "vec3");
    assert(module.module_units[1].module_path == "camera");
    assert(module.module_units[2].module_path == "main");
    assert(module.module_units[0].dependencies.empty());
    assert(module.module_units[1].dependencies.size() == 1);
    assert(module.module_units[1].dependencies[0].import_module_path == "vec3");
    assert(module.module_units[1].dependencies[0].resolved_module_path == "vec3");
    assert(module.module_units[2].dependencies.size() == 3);
    assert(module.module_units[2].dependencies[0].resolved_module_path == "vec3");
    assert(module.module_units[2].dependencies[1].resolved_module_path == "camera");
    assert(module.module_units[2].dependencies[2].resolved_module_path == "camera");
    int vec3_count = 0;
    int camera_count = 0;
    int view_camera_alias_count = 0;
    int camera_origin_count = 0;
    for (const dudu::ClassDecl& klass : module.classes) {
        if (klass.name == "Vec3") {
            ++vec3_count;
        }
        if (klass.name == "Camera") {
            ++camera_count;
        }
    }
    for (const dudu::TypeAliasDecl& alias : module.aliases) {
        if (alias.name == "ViewCamera" && dudu::type_ref_text(alias.type_ref) == "Camera") {
            ++view_camera_alias_count;
        }
    }
    for (const dudu::FunctionDecl& fn : module.functions) {
        if (fn.name == "camera_origin") {
            ++camera_origin_count;
        }
    }
    assert(vec3_count == 1);
    assert(camera_count == 1);
    assert(view_camera_alias_count == 1);
    assert(camera_origin_count == 1);
    const std::vector<std::filesystem::path> files = dudu::source_tree_files(dir / "main.dd");
    assert(files.size() == 3);
    dudu::analyze_module(module, {.check_bodies = true});
}

void test_module_loader_rejects_duplicate_from_aliases() {
    const std::filesystem::path dir =
        std::filesystem::temp_directory_path() / "dudu_module_alias_collision_test";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    write_file(dir / "left.dd", "class Left:\n"
                                "    value: i32\n");
    write_file(dir / "right.dd", "class Right:\n"
                                 "    value: i32\n");
    write_file(dir / "main.dd", "from left import Left as Thing\n"
                                "from right import Right as Thing\n"
                                "\n"
                                "def main() -> i32:\n"
                                "    return 0\n");

    bool failed = false;
    try {
        (void)dudu::load_source_tree(dir / "main.dd");
    } catch (const dudu::CompileError& error) {
        failed =
            std::string(error.what()).find("import name 'Thing' collides") != std::string::npos;
    }
    assert(failed);
}

void test_merged_output_rejects_same_named_module_declarations() {
    const std::filesystem::path dir =
        std::filesystem::temp_directory_path() / "dudu_merged_output_collision_test";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    write_file(dir / "left.dd", "def score() -> i32:\n"
                                "    return 20\n");
    write_file(dir / "right.dd", "def score() -> i32:\n"
                                 "    return 22\n");
    write_file(dir / "main.dd", "import left as l\n"
                                "import right as r\n"
                                "\n"
                                "def main() -> i32:\n"
                                "    return l.score() + r.score()\n");

    const dudu::ModuleAst module = dudu::load_source_tree(dir / "main.dd");
    dudu::analyze_module_tree(module, {.check_bodies = true});

    bool rejected = false;
    try {
        dudu::reject_merged_output_module_conflicts(module);
    } catch (const dudu::CompileError& error) {
        const std::string message = error.what();
        rejected = message.find("merged C++ output cannot combine Dudu modules") !=
                       std::string::npos &&
                   message.find("[build] backend = \"cmake\"") != std::string::npos &&
                   message.find("duc emit-modules") != std::string::npos;
    }
    assert(rejected);
}

void test_module_loader_qualified_module_imports() {
    const std::filesystem::path dir =
        std::filesystem::temp_directory_path() / "dudu_module_qualified_import_test";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir / "renderer");
    write_file(dir / "camera.dd", "class Camera:\n"
                                  "    x: i32\n"
                                  "\n"
                                  "ORIGIN: i32 = 7\n"
                                  "\n"
                                  "def make_camera(x: i32) -> Camera:\n"
                                  "    return Camera(x=x)\n");
    write_file(dir / "renderer" / "camera.dd", "class RenderCamera:\n"
                                               "    x: i32\n"
                                               "\n"
                                               "def make_render_camera(x: i32) -> RenderCamera:\n"
                                               "    return RenderCamera(x=x)\n");
    write_file(dir / "main.dd",
               "import camera as cam\n"
               "import renderer.camera\n"
               "\n"
               "def main() -> i32:\n"
               "    first: cam.Camera = cam.make_camera(cam.ORIGIN)\n"
               "    second: renderer.camera.RenderCamera = renderer.camera.make_render_camera(35)\n"
               "    return first.x + second.x\n");

    const dudu::ModuleAst module = dudu::load_source_tree(dir / "main.dd");
    dudu::analyze_module(module, {.check_bodies = true});
    const std::string cpp = dudu::emit_cpp_source(module);
    assert(cpp.find("cam::") == std::string::npos);
    assert(cpp.find("renderer::camera") == std::string::npos);
    assert(cpp.find("make_camera(ORIGIN)") != std::string::npos);
}

void test_module_loader_preserves_declaration_origins() {
    const std::filesystem::path dir =
        std::filesystem::temp_directory_path() / "dudu_module_origin_test";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir / "renderer");
    write_file(dir / "camera.dd", "class Camera:\n"
                                  "    x: i32\n"
                                  "\n"
                                  "def make_camera(x: i32) -> Camera:\n"
                                  "    return Camera(x=x)\n");
    write_file(dir / "renderer" / "camera.dd", "class Camera:\n"
                                               "    x: i32\n"
                                               "\n"
                                               "def make_camera(x: i32) -> Camera:\n"
                                               "    return Camera(x=x)\n");
    write_file(dir / "main.dd", "import camera as cam\n"
                                "import renderer.camera as render_camera\n"
                                "\n"
                                "def main() -> i32:\n"
                                "    first: cam.Camera = cam.make_camera(1)\n"
                                "    second: render_camera.Camera = render_camera.make_camera(2)\n"
                                "    return first.x + second.x\n");

    const dudu::ModuleAst module = dudu::load_source_tree(dir / "main.dd");
    assert(module.module_units.size() == 3);
    assert(module.module_units[2].dependencies.size() == 2);
    assert(module.module_units[2].dependencies[0].import_module_path == "camera");
    assert(module.module_units[2].dependencies[0].resolved_module_path == "camera");
    assert(module.module_units[2].dependencies[1].import_module_path == "renderer.camera");
    assert(module.module_units[2].dependencies[1].resolved_module_path == "renderer.camera");
    int root_camera_count = 0;
    int renderer_camera_count = 0;
    int root_make_count = 0;
    int renderer_make_count = 0;
    std::string root_camera_cpp_name;
    std::string renderer_camera_cpp_name;
    std::string root_make_cpp_name;
    std::string renderer_make_cpp_name;
    for (const dudu::ClassDecl& klass : module.classes) {
        if (klass.name == "Camera" && klass.origin_module == "camera") {
            ++root_camera_count;
            root_camera_cpp_name = klass.cpp_name;
        }
        if (klass.name == "Camera" && klass.origin_module == "renderer.camera") {
            ++renderer_camera_count;
            renderer_camera_cpp_name = klass.cpp_name;
        }
    }
    for (const dudu::FunctionDecl& fn : module.functions) {
        if (fn.name == "make_camera" && fn.origin_module == "camera") {
            ++root_make_count;
            root_make_cpp_name = fn.cpp_name;
        }
        if (fn.name == "make_camera" && fn.origin_module == "renderer.camera") {
            ++renderer_make_count;
            renderer_make_cpp_name = fn.cpp_name;
        }
    }
    assert(root_camera_count == 1);
    assert(renderer_camera_count == 1);
    assert(root_make_count == 1);
    assert(renderer_make_count == 1);
    assert(root_camera_cpp_name == "DuduCameraCamera");
    assert(renderer_camera_cpp_name == "DuduRendererCameraCamera");
    assert(root_make_cpp_name == "dudu_camera_make_camera");
    assert(renderer_make_cpp_name == "dudu_renderer_camera_make_camera");
}

void test_cpp_module_artifacts_preserve_module_boundaries() {
    const std::filesystem::path dir =
        std::filesystem::temp_directory_path() / "dudu_module_artifact_test";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir / "renderer");
    write_file(dir / "camera.dd", "class Camera:\n"
                                  "    x: i32\n"
                                  "\n"
                                  "def make_camera(x: i32) -> Camera:\n"
                                  "    return Camera(x=x)\n");
    write_file(dir / "renderer" / "camera.dd", "class Camera:\n"
                                               "    x: i32\n"
                                               "\n"
                                               "def make_camera(x: i32) -> Camera:\n"
                                               "    return Camera(x=x)\n");
    write_file(dir / "main.dd", "import camera as cam\n"
                                "import renderer.camera as render_camera\n"
                                "\n"
                                "def main() -> i32:\n"
                                "    first: cam.Camera = cam.make_camera(1)\n"
                                "    second: render_camera.Camera = render_camera.make_camera(2)\n"
                                "    return first.x + second.x\n");

    const dudu::ModuleAst module = dudu::load_source_tree(dir / "main.dd");
    const std::vector<dudu::CppModuleArtifact> artifacts = dudu::emit_cpp_module_artifacts(module);
    std::map<std::filesystem::path, std::string> by_path;
    for (const dudu::CppModuleArtifact& artifact : artifacts) {
        by_path[artifact.path] = artifact.content;
    }
    assert(by_path.contains("camera.hpp"));
    assert(by_path.contains("camera.cpp"));
    assert(by_path.contains(std::filesystem::path("renderer") / "camera.hpp"));
    assert(by_path.contains(std::filesystem::path("renderer") / "camera.cpp"));
    assert(by_path.contains("dudu_runtime.hpp"));
    assert(by_path.contains("main.hpp"));
    assert(by_path.contains("main.cpp"));
    assert(by_path.at("dudu_runtime.hpp").find("template <typename T, typename E> struct Result") !=
           std::string::npos);
    assert(by_path.at("camera.hpp").find("#include \"dudu_runtime.hpp\"") != std::string::npos);
    assert(by_path.at("main.cpp").find("#include \"dudu_runtime.hpp\"") != std::string::npos);
    assert(by_path.at("camera.hpp").find("template <typename T, typename E> struct Result") ==
           std::string::npos);
    assert(by_path.at("camera.cpp").find("// dudu module: camera") != std::string::npos);
    assert(by_path.at(std::filesystem::path("renderer") / "camera.cpp")
               .find("// dudu module: renderer.camera") != std::string::npos);
    assert(by_path.at("main.hpp").find("#include \"camera.hpp\"") != std::string::npos);
    assert(by_path.at("main.hpp").find("#include \"renderer/camera.hpp\"") != std::string::npos);
    assert(by_path.at("main.cpp").find("#include \"camera.hpp\"") != std::string::npos);
    assert(by_path.at("main.cpp").find("#include \"renderer/camera.hpp\"") != std::string::npos);
    assert(by_path.at("camera.cpp").find("struct DuduCameraCamera") != std::string::npos);
    assert(by_path.at(std::filesystem::path("renderer") / "camera.cpp")
               .find("struct DuduRendererCameraCamera") != std::string::npos);
    assert(by_path.at("camera.hpp").find("struct DuduCameraCamera") != std::string::npos);
    assert(by_path.at(std::filesystem::path("renderer") / "camera.hpp")
               .find("struct DuduRendererCameraCamera") != std::string::npos);
    assert(by_path.at("camera.cpp").find("DuduCameraCamera dudu_camera_make_camera") !=
           std::string::npos);
    assert(by_path.at("camera.cpp").find("return DuduCameraCamera{.x = x};") != std::string::npos);
    assert(by_path.at("camera.cpp").find("return Camera{.x = x};") == std::string::npos);
    assert(by_path.at(std::filesystem::path("renderer") / "camera.cpp")
               .find("DuduRendererCameraCamera dudu_renderer_camera_make_camera") !=
           std::string::npos);
    assert(by_path.at(std::filesystem::path("renderer") / "camera.cpp")
               .find("return DuduRendererCameraCamera{.x = x};") != std::string::npos);
    assert(by_path.at("main.cpp").find("DuduCameraCamera first = dudu_camera_make_camera(1);") !=
           std::string::npos);
    assert(by_path.at("main.cpp")
               .find("DuduRendererCameraCamera second = dudu_renderer_camera_make_camera(2);") !=
           std::string::npos);
    assert(by_path.at("main.cpp").find("cam.make_camera") == std::string::npos);
    assert(by_path.at("main.cpp").find("render_camera.make_camera") == std::string::npos);
    assert(by_path.at("main.cpp").find("int main()") != std::string::npos);
    assert(by_path.at("main.cpp").find("return dudu_main_main();") != std::string::npos);
}

void test_cpp_module_artifacts_use_resolved_dependency_paths() {
    const std::filesystem::path dir =
        std::filesystem::temp_directory_path() / "dudu_module_resolved_dependency_test";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir / "renderer");
    write_file(dir / "renderer" / "vec3.dd", "class Vec3:\n"
                                             "    x: f32\n");
    write_file(dir / "renderer" / "camera.dd", "from vec3 import Vec3\n"
                                               "\n"
                                               "class Camera:\n"
                                               "    origin: Vec3\n");
    write_file(dir / "main.dd", "import renderer.camera\n"
                                "\n"
                                "def main() -> i32:\n"
                                "    return 0\n");

    const dudu::ModuleAst module = dudu::load_source_tree(dir / "main.dd");
    assert(module.module_units.size() == 3);
    assert(module.module_units[1].module_path == "renderer.camera");
    assert(module.module_units[1].dependencies.size() == 1);
    assert(module.module_units[1].dependencies[0].import_module_path == "vec3");
    assert(module.module_units[1].dependencies[0].resolved_module_path == "renderer.vec3");

    const std::vector<dudu::CppModuleArtifact> artifacts = dudu::emit_cpp_module_artifacts(module);
    std::map<std::filesystem::path, std::string> by_path;
    for (const dudu::CppModuleArtifact& artifact : artifacts) {
        by_path[artifact.path] = artifact.content;
    }
    const std::filesystem::path camera_header = std::filesystem::path("renderer") / "camera.hpp";
    assert(by_path.contains(camera_header));
    assert(by_path.at(camera_header).find("#include \"renderer/vec3.hpp\"") != std::string::npos);
    assert(by_path.at(camera_header).find("#include \"vec3.hpp\"") == std::string::npos);
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

void test_header_emission() {
    const dudu::ModuleAst module = dudu::parse_source("import cpp \"raylib.h\" as rl\n"
                                                      "\n"
                                                      "class Vec3:\n"
                                                      "    x: f32\n"
                                                      "    y: f32\n"
                                                      "\n"
                                                      "def _helper(a: Vec3) -> f32:\n"
                                                      "    return a.x\n"
                                                      "\n"
                                                      "def dot(a: Vec3, b: Vec3) -> f32:\n"
                                                      "    return a.x * b.x + a.y * b.y\n",
                                                      "header.dd");
    const std::string header = dudu::emit_cpp_header(module);
    assert(header.find("#include \"raylib.h\"") != std::string::npos);
    assert(header.find("struct Vec3") != std::string::npos);
    assert(header.find("struct Vec3") < header.find("float dot"));
    assert(header.find("float x{};") != std::string::npos);
    assert(header.find("float dot(Vec3 a, Vec3 b);") != std::string::npos);
    assert(header.find("_helper") == std::string::npos);

    const dudu::ModuleAst alias_module =
        dudu::parse_source("type PlayerId = u64\n"
                           "\n"
                           "def next_id(id: PlayerId) -> PlayerId:\n"
                           "    return id + 1\n",
                           "alias_header.dd");
    const std::string alias_header = dudu::emit_cpp_header(alias_module);
    assert(alias_header.find("using PlayerId = uint64_t;") != std::string::npos);
    assert(alias_header.find("PlayerId next_id(PlayerId id);") != std::string::npos);
}

void test_semantic_diagnostics() {
    bool duplicate = false;
    try {
        const dudu::ModuleAst module = dudu::parse_source("class Vec:\n"
                                                          "    x: i32\n"
                                                          "\n"
                                                          "class Vec:\n"
                                                          "    y: i32\n",
                                                          "duplicate.dd");
        dudu::analyze_module(module);
    } catch (const dudu::CompileError&) {
        duplicate = true;
    }
    assert(duplicate);

    bool bad_return = false;
    try {
        const dudu::ModuleAst module = dudu::parse_source("def bad() -> i32:\n"
                                                          "    return True\n",
                                                          "bad_return.dd");
        dudu::analyze_module(module, {.check_bodies = true});
    } catch (const dudu::CompileError& error) {
        assert(error.location().line == 2);
        assert(error.location().column > 5);
        bad_return = true;
    }
    assert(bad_return);

    bool bad_local_value = false;
    try {
        const dudu::ModuleAst module = dudu::parse_source("def bad_value():\n"
                                                          "    value: i32 = \"nope\"\n",
                                                          "bad_value.dd");
        dudu::analyze_module(module, {.check_bodies = true});
    } catch (const dudu::CompileError& error) {
        assert(error.location().line == 2);
        assert(error.location().column > 5);
        bad_local_value = true;
    }
    assert(bad_local_value);

    bool bad_local_type = false;
    try {
        const dudu::ModuleAst module = dudu::parse_source("def bad_type_name():\n"
                                                          "    value: MissingType = 1\n",
                                                          "bad_type_name.dd");
        dudu::analyze_module(module, {.check_bodies = true});
    } catch (const dudu::CompileError& error) {
        assert(error.location().line == 2);
        assert(error.location().column > 5);
        bad_local_type = true;
    }
    assert(bad_local_type);

    bool bad_generic_value = false;
    try {
        const dudu::ModuleAst module = dudu::parse_source("def bad_generic_value[T]() -> i32:\n"
                                                          "    return T\n",
                                                          "bad_generic_value.dd");
        dudu::analyze_module(module, {.check_bodies = true});
    } catch (const dudu::CompileError& error) {
        assert(error.location().line == 2);
        assert(error.location().column > 5);
        assert(std::string(error.what()).find("type parameter used as a value: T") !=
               std::string::npos);
        bad_generic_value = true;
    }
    assert(bad_generic_value);

    bool bad_value_as_type = false;
    try {
        const dudu::ModuleAst module = dudu::parse_source("def bad_value_as_type(value: i32):\n"
                                                          "    other: value = 1\n",
                                                          "bad_value_as_type.dd");
        dudu::analyze_module(module, {.check_bodies = true});
    } catch (const dudu::CompileError& error) {
        assert(error.location().line == 2);
        assert(error.location().column > 5);
        assert(std::string(error.what()).find("value used as a type: value") !=
               std::string::npos);
        bad_value_as_type = true;
    }
    assert(bad_value_as_type);

    bool bad_prior_local_as_type = false;
    try {
        const dudu::ModuleAst module = dudu::parse_source("def bad_prior_local_as_type():\n"
                                                          "    value: i32 = 1\n"
                                                          "    other: value = 2\n",
                                                          "bad_prior_local_as_type.dd");
        dudu::analyze_module(module, {.check_bodies = true});
    } catch (const dudu::CompileError& error) {
        assert(error.location().line == 3);
        assert(error.location().column > 5);
        assert(std::string(error.what()).find("value used as a type: value") !=
               std::string::npos);
        bad_prior_local_as_type = true;
    }
    assert(bad_prior_local_as_type);

    for (const std::string type : {"int", "float", "double"}) {
        bool rejected = false;
        try {
            const dudu::ModuleAst module = dudu::parse_source("def bad_type():\n"
                                                              "    value: " +
                                                                  type + " = 0\n",
                                                              "bad_type.dd");
            dudu::analyze_module(module, {.check_bodies = true});
        } catch (const dudu::CompileError&) {
            rejected = true;
        }
        assert(rejected);
    }
}

void test_lsp_diagnostic_sources_are_structured() {
    const dudu::Document parser_doc{.uri = "",
                                    .path = "parser_diag.dd",
                                    .text = "def main() -> i32\n"
                                            "    return 0\n"};
    const std::vector<dudu::Diagnostic> parser_diags = dudu::diagnostics_for_document(parser_doc);
    assert(parser_diags.size() == 1);
    assert(parser_diags.front().source == "dudu/parser");
    assert(parser_diags.front().code.starts_with("dudu.parser."));

    const dudu::Document sema_doc{.uri = "",
                                  .path = "sema_diag.dd",
                                  .text = "def main() -> i32:\n"
                                          "    return True\n"};
    const std::vector<dudu::Diagnostic> sema_diags = dudu::diagnostics_for_document(sema_doc);
    assert(sema_diags.size() == 1);
    assert(sema_diags.front().source == "dudu/sema");
    assert(sema_diags.front().code.starts_with("dudu.sema."));
}

void test_lsp_diagnostics_use_open_buffer_for_module_entry() {
    const std::filesystem::path dir =
        std::filesystem::temp_directory_path() / "dudu_lsp_open_buffer_module_test";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    write_file(dir / "helper.dd", "def helper() -> i32:\n"
                                  "    return 7\n");
    write_file(dir / "main.dd", "def main() -> i32:\n"
                                "    return helper()\n");

    const dudu::Document doc{.uri = dudu::file_uri(dir / "main.dd"),
                             .path = dir / "main.dd",
                             .text = "from helper import helper\n"
                                     "\n"
                                     "def main() -> i32:\n"
                                     "    return helper()\n"};
    const std::vector<dudu::Diagnostic> diags = dudu::diagnostics_for_document(doc);
    for (const dudu::Diagnostic& diag : diags) {
        assert(diag.source != "dudu/sema");
        assert(diag.message.find("helper") == std::string::npos);
    }
}

void test_lsp_lints_do_not_leak_from_dependency_modules() {
    const std::filesystem::path dir =
        std::filesystem::temp_directory_path() / "dudu_lsp_dependency_lint_scope_test";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    write_file(dir / "helper.dd", "def helper() -> i32:\n"
                                  "    unused = 9\n"
                                  "    return 7\n");
    write_file(dir / "main.dd", "from helper import helper\n"
                                "\n"
                                "def main() -> i32:\n"
                                "    return helper()\n");

    const dudu::Document doc{.uri = dudu::file_uri(dir / "main.dd"),
                             .path = dir / "main.dd",
                             .text = read_file(dir / "main.dd")};
    const std::vector<dudu::Diagnostic> diags = dudu::diagnostics_for_document(doc);
    for (const dudu::Diagnostic& diag : diags) {
        assert(diag.code != "dudu.lint.unused");
    }
}

void test_lsp_member_completion_uses_imported_module_shapes() {
    const std::filesystem::path dir =
        std::filesystem::temp_directory_path() / "dudu_lsp_imported_member_completion_test";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    write_file(dir / "vec3.dd", "class Vec3:\n"
                                "    x: f32\n"
                                "    y: f32\n");
    write_file(dir / "main.dd", "def main() -> i32:\n"
                                "    return 0\n");

    const dudu::Document doc{.uri = dudu::file_uri(dir / "main.dd"),
                             .path = dir / "main.dd",
                             .text = "from vec3 import Vec3\n"
                                     "\n"
                                     "def main() -> i32:\n"
                                     "    v: Vec3 = Vec3(x=1.0, y=2.0)\n"
                                     "    v.x\n"
                                     "    return 0\n"};
    dudu::Json params =
        dudu::JsonParser("{\"position\":{\"line\":4,\"character\":6}}").parse();
    const std::string completions = dudu::completion_json(&doc, &params);
    assert(completions.find("\"label\":\"x\"") != std::string::npos);
    assert(completions.find("\"label\":\"y\"") != std::string::npos);
}

void test_lsp_completion_uses_visible_imported_functions() {
    const std::filesystem::path dir =
        std::filesystem::temp_directory_path() / "dudu_lsp_imported_function_completion_test";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    write_file(dir / "helper.dd", "def helper(value: i32) -> i32:\n"
                                  "    return value + 1\n");
    write_file(dir / "main.dd", "def main() -> i32:\n"
                                "    return 0\n");

    const dudu::Document doc{.uri = dudu::file_uri(dir / "main.dd"),
                             .path = dir / "main.dd",
                             .text = "from helper import helper\n"
                                     "\n"
                                     "def main() -> i32:\n"
                                     "    return hel\n"};
    dudu::Json params = dudu::JsonParser("{\"position\":{\"line\":3,\"character\":14}}").parse();
    const std::string completions = dudu::completion_json(&doc, &params);
    assert(completions.find("\"label\":\"helper\"") != std::string::npos);
    assert(completions.find("helper(i32) -> i32") != std::string::npos);
}

void test_lsp_signature_help_uses_visible_imported_functions() {
    const std::filesystem::path dir =
        std::filesystem::temp_directory_path() / "dudu_lsp_imported_function_signature_test";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    write_file(dir / "helper.dd", "def helper(value: i32, amount: i32) -> i32:\n"
                                  "    return value + amount\n");
    write_file(dir / "main.dd", "def main() -> i32:\n"
                                "    return 0\n");

    const dudu::Document doc{.uri = dudu::file_uri(dir / "main.dd"),
                             .path = dir / "main.dd",
                             .text = "from helper import helper\n"
                                     "\n"
                                     "def main() -> i32:\n"
                                     "    return helper(1, 2)\n"};
    dudu::Json params = dudu::JsonParser("{\"position\":{\"line\":3,\"character\":22}}").parse();
    const std::string help = dudu::signature_help_json(&doc, &params);
    assert(help.find("helper(i32, i32) -> i32") != std::string::npos);
    assert(help.find("\"activeParameter\":1") != std::string::npos);
}

void test_lsp_module_completion_uses_loaded_module_units() {
    const std::filesystem::path dir =
        std::filesystem::temp_directory_path() / "dudu_lsp_module_completion_unit_test";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    write_file(dir / "maths.dd", "def inc(value: i32) -> i32:\n"
                                 "    return value + 1\n");
    write_file(dir / "main.dd", "def main() -> i32:\n"
                                "    return 0\n");

    const dudu::Document doc{.uri = dudu::file_uri(dir / "main.dd"),
                             .path = dir / "main.dd",
                             .text = "import maths\n"
                                     "\n"
                                     "def main() -> i32:\n"
                                     "    maths.\n"
                                     "    return 0\n"};
    dudu::Json params =
        dudu::JsonParser("{\"position\":{\"line\":3,\"character\":10}}").parse();
    const std::string completions = dudu::completion_json(&doc, &params);
    assert(completions.find("\"label\":\"inc\"") != std::string::npos);
    assert(completions.find("inc(value: i32) -> i32") != std::string::npos);
}

void test_lsp_definition_uses_loaded_module_units() {
    const std::filesystem::path dir =
        std::filesystem::temp_directory_path() / "dudu_lsp_module_definition_unit_test";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    write_file(dir / "maths.dd", "def inc(value: i32) -> i32:\n"
                                 "    return value + 1\n");
    write_file(dir / "main.dd", "def main() -> i32:\n"
                                "    return 0\n");

    const dudu::Document doc{.uri = dudu::file_uri(dir / "main.dd"),
                             .path = dir / "main.dd",
                             .text = "import maths\n"
                                     "\n"
                                     "def main() -> i32:\n"
                                     "    return maths.inc(1)\n"};
    dudu::Json params =
        dudu::JsonParser("{\"position\":{\"line\":3,\"character\":18}}").parse();
    const std::string definition = dudu::definition_json(doc, &params);
    assert(definition.find(dudu::file_uri(dir / "maths.dd")) != std::string::npos);
    assert(definition.find("\"line\":0") != std::string::npos);
}

void test_lsp_definition_jumps_to_native_header_type() {
    const std::filesystem::path dir =
        std::filesystem::temp_directory_path() / "dudu_lsp_native_definition_unit_test";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    write_file(dir / "native_point.h", "typedef struct NativePoint {\n"
                                       "    int x;\n"
                                       "    int y;\n"
                                       "} NativePoint;\n");

    const dudu::Document doc{.uri = dudu::file_uri(dir / "main.dd"),
                             .path = dir / "main.dd",
                             .text = "import c \"native_point.h\"\n"
                                     "\n"
                                     "def main() -> i32:\n"
                                     "    point: NativePoint\n"
                                     "    return point.x\n"};
    dudu::Json params =
        dudu::JsonParser("{\"position\":{\"line\":3,\"character\":16}}").parse();
    const std::string definition = dudu::definition_json(doc, &params);
    assert(definition.find(dudu::file_uri(dir / "native_point.h")) != std::string::npos);
    assert(definition.find("\"line\":0") != std::string::npos);
}

void test_lsp_definition_uses_receiver_for_ambiguous_native_methods() {
    const std::filesystem::path dir =
        std::filesystem::temp_directory_path() / "dudu_lsp_native_method_definition_test";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    write_file(dir / "native_methods.hpp",
               "namespace left {\n"
               "struct Thing {\n"
               "    int shared() const {\n"
               "        return 1;\n"
               "    }\n"
               "};\n"
               "}\n"
               "\n"
               "namespace right {\n"
               "struct Thing {\n"
               "    int shared() const {\n"
               "        return 2;\n"
               "    }\n"
               "};\n"
               "}\n");

    const dudu::Document doc{.uri = dudu::file_uri(dir / "main.dd"),
                             .path = dir / "main.dd",
                             .text = "import cpp \"./native_methods.hpp\"\n"
                                     "\n"
                                     "def main() -> i32:\n"
                                     "    first: left.Thing\n"
                                     "    second: right.Thing\n"
                                     "    return second.shared()\n"};
    dudu::Json params =
        dudu::JsonParser("{\"position\":{\"line\":5,\"character\":20}}").parse();
    const std::string definition = dudu::definition_json(doc, &params);
    assert(definition.find(dudu::file_uri(dir / "native_methods.hpp")) != std::string::npos);
    assert(definition.find("\"line\":10") != std::string::npos);
}

void test_lsp_hover_uses_receiver_for_ambiguous_native_methods() {
    const std::filesystem::path dir =
        std::filesystem::temp_directory_path() / "dudu_lsp_native_method_hover_test";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    write_file(dir / "native_methods.hpp",
               "namespace left {\n"
               "struct Thing {\n"
               "    int shared() const {\n"
               "        return 1;\n"
               "    }\n"
               "};\n"
               "}\n"
               "\n"
               "namespace right {\n"
               "struct Thing {\n"
               "    float shared() const {\n"
               "        return 2.0f;\n"
               "    }\n"
               "};\n"
               "}\n");

    const dudu::Document doc{.uri = dudu::file_uri(dir / "main.dd"),
                             .path = dir / "main.dd",
                             .text = "import cpp \"./native_methods.hpp\"\n"
                                     "\n"
                                     "def main() -> i32:\n"
                                     "    first: left.Thing\n"
                                     "    second: right.Thing\n"
                                     "    return i32(second.shared())\n"};
    dudu::Json params =
        dudu::JsonParser("{\"position\":{\"line\":5,\"character\":24}}").parse();
    const std::string hover = dudu::hover_json(doc, "second.shared", "", &params);
    assert(hover.find("shared() -> f32") != std::string::npos);
    assert(hover.find("shared() -> i32") == std::string::npos);
}

void test_lsp_references_keep_unbound_member_query_dotted() {
    const std::filesystem::path dir =
        std::filesystem::temp_directory_path() / "dudu_lsp_member_reference_query_test";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    write_file(dir / "native_methods.hpp",
               "namespace left {\n"
               "struct Thing {\n"
               "    int shared() const {\n"
               "        return 1;\n"
               "    }\n"
               "};\n"
               "}\n"
               "\n"
               "namespace right {\n"
               "struct Thing {\n"
               "    int shared() const {\n"
               "        return 2;\n"
               "    }\n"
               "};\n"
               "}\n");

    const dudu::Document doc{.uri = dudu::file_uri(dir / "main.dd"),
                             .path = dir / "main.dd",
                             .text = "import cpp \"./native_methods.hpp\"\n"
                                     "\n"
                                     "def main() -> i32:\n"
                                     "    first: left.Thing\n"
                                     "    second: right.Thing\n"
                                     "    return second.shared() + first.shared()\n"};
    const std::map<std::string, dudu::Document> workspace{{doc.uri, doc}};
    dudu::Json params =
        dudu::JsonParser("{\"position\":{\"line\":5,\"character\":20}}").parse();
    const std::string refs = dudu::references_json(doc, &params, workspace);
    assert(refs.find("\"start\":{\"line\":5,\"character\":18}") != std::string::npos);
    assert(refs.find("\"start\":{\"line\":5,\"character\":35}") == std::string::npos);
}

void test_lsp_hover_uses_loaded_module_units() {
    const std::filesystem::path dir =
        std::filesystem::temp_directory_path() / "dudu_lsp_module_hover_unit_test";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    write_file(dir / "maths.dd", "def inc(value: i32) -> i32:\n"
                                 "    return value + 1\n");
    write_file(dir / "main.dd", "def main() -> i32:\n"
                                "    return 0\n");

    const dudu::Document doc{.uri = dudu::file_uri(dir / "main.dd"),
                             .path = dir / "main.dd",
                             .text = "import maths\n"
                                     "\n"
                                     "def main() -> i32:\n"
                                     "    return maths.inc(1)\n"};
    const std::string hover = dudu::hover_json(doc, "maths.inc", "");
    assert(hover.find("def inc(value: i32) -> i32") != std::string::npos);
}

void test_lsp_unreachable_lint_uses_branch_structure() {
    const dudu::Document doc{.uri = "",
                             .path = "lint_unreachable.dd",
                             .text = "def choose(x: i32) -> i32:\n"
                                     "    if x < 0:\n"
                                     "        return -1\n"
                                     "    elif x == 0:\n"
                                     "        return 0\n"
                                     "    else:\n"
                                     "        return 1\n"
                                     "    value: i32 = 4\n"
                                     "    return value\n"};
    const std::vector<dudu::Diagnostic> diags = dudu::diagnostics_for_document(doc);
    int unreachable_count = 0;
    for (const dudu::Diagnostic& diag : diags) {
        if (diag.code == "dudu.lint.unreachable") {
            ++unreachable_count;
            assert(diag.location.line == 8);
        }
    }
    assert(unreachable_count == 1);
}

void test_lsp_unreachable_lint_does_not_flag_partial_branch_return() {
    const dudu::Document doc{.uri = "",
                             .path = "lint_partial_branch_return.dd",
                             .text = "def choose(x: i32) -> i32:\n"
                                     "    if x < 0:\n"
                                     "        return -1\n"
                                     "    value: i32 = x + 1\n"
                                     "    return value\n"};
    const std::vector<dudu::Diagnostic> diags = dudu::diagnostics_for_document(doc);
    for (const dudu::Diagnostic& diag : diags) {
        assert(diag.code != "dudu.lint.unreachable");
    }
}

void test_lsp_scope_lint_tracks_inferred_assignment_locals() {
    const dudu::Document doc{.uri = "",
                             .path = "lint_inferred_locals.dd",
                             .text = "def main() -> i32:\n"
                                     "    used = 1\n"
                                     "    unused = 2\n"
                                     "    return used\n"};
    const std::vector<dudu::Diagnostic> diags = dudu::diagnostics_for_document(doc);
    int unused_count = 0;
    for (const dudu::Diagnostic& diag : diags) {
        if (diag.code == "dudu.lint.unused") {
            ++unused_count;
            assert(diag.location.line == 3);
        }
    }
    assert(unused_count == 1);
}

void test_lsp_suspicious_cast_lint_uses_type_refs() {
    const dudu::Document doc{.uri = "",
                             .path = "lint_suspicious_cast.dd",
                             .text = "def main() -> i32:\n"
                                     "    wide: f64 = 1.0\n"
                                     "    narrow = f32(wide)\n"
                                     "    return i32(narrow)\n"};
    const std::vector<dudu::Diagnostic> diags = dudu::diagnostics_for_document(doc);
    int cast_count = 0;
    for (const dudu::Diagnostic& diag : diags) {
        if (diag.code == "dudu.lint.suspicious_cast") {
            ++cast_count;
            assert(diag.location.line == 3);
        }
    }
    assert(cast_count == 1);
}

void test_lsp_references_track_assignment_bindings() {
    const dudu::Document doc{.uri = "file:///refs.dd",
                             .path = "refs.dd",
                             .text = "def pair() -> tuple[i32, i32]:\n"
                                     "    return 1, 2\n"
                                     "\n"
                                     "def main() -> i32:\n"
                                     "    used = 1\n"
                                     "    left, right = pair()\n"
                                     "    return used + left + right\n"};

    const std::vector<dudu::ReferenceLocation> used_refs = dudu::references_in(doc, "used");
    assert(used_refs.size() == 2);
    const std::vector<dudu::ReferenceLocation> left_refs = dudu::references_in(doc, "left");
    assert(left_refs.size() == 2);
    const std::vector<dudu::ReferenceLocation> right_refs = dudu::references_in(doc, "right");
    assert(right_refs.size() == 2);
}

void test_lsp_references_track_qualified_type_refs() {
    const dudu::Document doc{.uri = "file:///qualified_type_refs.dd",
                             .path = "qualified_type_refs.dd",
                             .text = "import cpp \"raylib.h\" as rl\n"
                                     "\n"
                                     "def length(value: rl.Vector2) -> f32:\n"
                                     "    other: rl.Vector2\n"
                                     "    return value.x + other.x\n"};

    const std::vector<dudu::ReferenceLocation> refs = dudu::references_in(doc, "rl.Vector2");
    assert(refs.size() == 2);

    const std::map<std::string, dudu::Document> workspace{{doc.uri, doc}};
    dudu::Json params =
        dudu::JsonParser("{\"position\":{\"line\":2,\"character\":23}}").parse();
    const std::string refs_json = dudu::references_json(doc, &params, workspace);
    assert(refs_json.find("\"start\":{\"line\":2,\"character\":18}") != std::string::npos);
    assert(refs_json.find("\"start\":{\"line\":3,\"character\":11}") != std::string::npos);
}

void test_lsp_module_reference_filters_alias_target() {
    const std::filesystem::path dir =
        std::filesystem::temp_directory_path() / "dudu_lsp_module_reference_target_test";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    write_file(dir / "left.dd", "def answer() -> i32:\n"
                                "    return 1\n");
    write_file(dir / "right.dd", "def answer() -> i32:\n"
                                 "    return 2\n");
    write_file(dir / "main.dd", "import left as m\n"
                                "\n"
                                "def main() -> i32:\n"
                                "    return m.answer()\n");
    write_file(dir / "same.dd", "import left as m\n"
                                "\n"
                                "def same() -> i32:\n"
                                "    return m.answer()\n");
    write_file(dir / "other.dd", "import right as m\n"
                                 "\n"
                                 "def other() -> i32:\n"
                                 "    return m.answer()\n");

    const dudu::Document main_doc{.uri = dudu::file_uri(dir / "main.dd"),
                                  .path = dir / "main.dd",
                                  .text = read_file(dir / "main.dd")};
    const std::map<std::string, dudu::Document> workspace{
        {main_doc.uri, main_doc},
        {dudu::file_uri(dir / "same.dd"),
         {.uri = dudu::file_uri(dir / "same.dd"),
          .path = dir / "same.dd",
          .text = read_file(dir / "same.dd")}},
        {dudu::file_uri(dir / "other.dd"),
         {.uri = dudu::file_uri(dir / "other.dd"),
          .path = dir / "other.dd",
          .text = read_file(dir / "other.dd")}},
    };
    dudu::Json params =
        dudu::JsonParser("{\"position\":{\"line\":3,\"character\":15}}").parse();
    const std::string refs = dudu::references_json(main_doc, &params, workspace);
    assert(refs.find(dudu::file_uri(dir / "main.dd")) != std::string::npos);
    assert(refs.find(dudu::file_uri(dir / "same.dd")) != std::string::npos);
    assert(refs.find(dudu::file_uri(dir / "other.dd")) == std::string::npos);
}

void test_lsp_module_references_include_target_declaration() {
    const std::filesystem::path dir =
        std::filesystem::temp_directory_path() / "dudu_lsp_module_reference_declaration_test";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    write_file(dir / "helper.dd", "def answer() -> i32:\n"
                                  "    return 42\n");
    write_file(dir / "main.dd", "import helper as h\n"
                                "\n"
                                "def main() -> i32:\n"
                                "    return h.answer()\n");

    const dudu::Document main_doc{.uri = dudu::file_uri(dir / "main.dd"),
                                  .path = dir / "main.dd",
                                  .text = read_file(dir / "main.dd")};
    const dudu::Document helper_doc{.uri = dudu::file_uri(dir / "helper.dd"),
                                    .path = dir / "helper.dd",
                                    .text = read_file(dir / "helper.dd")};
    const std::map<std::string, dudu::Document> workspace{{main_doc.uri, main_doc},
                                                          {helper_doc.uri, helper_doc}};
    dudu::Json params =
        dudu::JsonParser("{\"position\":{\"line\":3,\"character\":15}}").parse();
    const std::string refs = dudu::references_json(main_doc, &params, workspace);
    assert(refs.find(dudu::file_uri(dir / "main.dd")) != std::string::npos);
    assert(refs.find(dudu::file_uri(dir / "helper.dd")) != std::string::npos);
    assert(refs.find("\"start\":{\"line\":0,\"character\":4}") != std::string::npos);
}

void test_lsp_selective_import_references_include_target_declaration() {
    const std::filesystem::path dir =
        std::filesystem::temp_directory_path() / "dudu_lsp_selective_import_reference_test";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    write_file(dir / "helper.dd", "def answer() -> i32:\n"
                                  "    return 42\n");
    write_file(dir / "other_helper.dd", "def answer() -> i32:\n"
                                        "    return 7\n");
    write_file(dir / "main.dd", "from helper import answer as local_answer\n"
                                "\n"
                                "def main() -> i32:\n"
                                "    return local_answer()\n");
    write_file(dir / "same.dd", "from helper import answer as local_answer\n"
                                "\n"
                                "def same() -> i32:\n"
                                "    return local_answer()\n");
    write_file(dir / "other.dd", "from other_helper import answer as local_answer\n"
                                 "\n"
                                 "def other() -> i32:\n"
                                 "    return local_answer()\n");

    const dudu::Document main_doc{.uri = dudu::file_uri(dir / "main.dd"),
                                  .path = dir / "main.dd",
                                  .text = read_file(dir / "main.dd")};
    const dudu::Document helper_doc{.uri = dudu::file_uri(dir / "helper.dd"),
                                    .path = dir / "helper.dd",
                                    .text = read_file(dir / "helper.dd")};
    const std::map<std::string, dudu::Document> workspace{
        {main_doc.uri, main_doc},
        {helper_doc.uri, helper_doc},
        {dudu::file_uri(dir / "same.dd"),
         {.uri = dudu::file_uri(dir / "same.dd"),
          .path = dir / "same.dd",
          .text = read_file(dir / "same.dd")}},
        {dudu::file_uri(dir / "other.dd"),
         {.uri = dudu::file_uri(dir / "other.dd"),
          .path = dir / "other.dd",
          .text = read_file(dir / "other.dd")}},
    };
    dudu::Json params =
        dudu::JsonParser("{\"position\":{\"line\":3,\"character\":15}}").parse();
    const std::string refs = dudu::references_json(main_doc, &params, workspace);
    assert(refs.find(dudu::file_uri(dir / "main.dd")) != std::string::npos);
    assert(refs.find(dudu::file_uri(dir / "same.dd")) != std::string::npos);
    assert(refs.find(dudu::file_uri(dir / "helper.dd")) != std::string::npos);
    assert(refs.find(dudu::file_uri(dir / "other.dd")) == std::string::npos);
    assert(refs.find("\"start\":{\"line\":0,\"character\":4}") != std::string::npos);
}

void test_allocation_type_ref_diagnostics() {
    dudu::Symbols symbols;
    const dudu::FunctionScope scope(symbols);
    const dudu::SourceLocation location{.file = "cpp_escape_alloc.dd", .line = 7, .column = 12};

    const std::vector<dudu::TypeRef> type_args = {dudu::parse_type_text("list[i32]", location)};
    const std::optional<dudu::TypeRef> allocation =
        dudu::infer_allocation_call_type_ref(symbols, &location, "new", type_args, 0);
    assert(allocation.has_value());
    assert(allocation->kind == dudu::TypeKind::Pointer);
    assert(allocation->children.size() == 1);
    assert(allocation->children.front().kind == dudu::TypeKind::Template);
    assert(dudu::substitute_type_ref_text(*allocation, {}) == "*list[i32]");
    assert(dudu::substitute_type_ref_text(
               dudu::infer_cpp_escape_expr_ref(scope, "new[list[i32]]()", &location), {}) ==
           "*list[i32]");
    assert(dudu::substitute_type_ref_text(
               dudu::infer_cpp_escape_expr_ref(scope, "*struct State(None)", &location), {}) ==
           "*struct State");
    const std::optional<dudu::EscapeCall> parsed_template_call =
        dudu::parsed_escape_call(dudu::parse_expr_text("Box[i32](7)", location));
    assert(parsed_template_call.has_value());
    assert(parsed_template_call->callee == "Box");
    assert(parsed_template_call->callee_type_ref.kind == dudu::TypeKind::Template);
    assert(dudu::substitute_type_ref_text(parsed_template_call->callee_type_ref, {}) == "Box[i32]");

    bool rejected = false;
    try {
        (void)dudu::infer_cpp_escape_expr_ref(scope, "new[list[MissingType]]()", &location);
    } catch (const dudu::CompileError& error) {
        assert(error.location().line == 7);
        assert(error.location().column > location.column);
        assert(std::string(error.what()).find("unknown allocation type: MissingType") !=
               std::string::npos);
        rejected = true;
    }
    assert(rejected);
}

void test_emitted_local_index_type_inference() {
    const std::map<std::string, std::string> locals = {
        {"flag", "bool"},
        {"count", "i32"},
        {"scale", "f32"},
        {"values", "list[i32]"},
        {"names", "dict[str, Player]"},
        {"matrix", "array[f32][3, 4]"},
    };
    const std::map<std::string, dudu::TypeRef> functions;

    assert(infer_emitted_local_type_text("values[0]", locals, functions) == "i32");
    assert(infer_emitted_local_type_text("names[key]", locals, functions) == "Player");
    assert(infer_emitted_local_type_text("matrix[1]", locals, functions) == "array[f32][4]");
    assert(infer_emitted_local_type_text("matrix[1, 2]", locals, functions) == "f32");
}

void test_index_type_inference_uses_type_ast() {
    dudu::Symbols symbols;
    const dudu::SourceLocation location{.file = "index_types.dd", .line = 1, .column = 1};
    symbols.alias_type_refs["Ints"] = dudu::parse_type_text("list[i32]", location);
    symbols.alias_type_refs["ItemAlias"] = dudu::parse_type_text("Item", location);
    symbols.alias_type_refs["AliasItems"] = dudu::parse_type_text("list[ItemAlias]", location);
    const dudu::TypeRef pointer_item = dudu::indexed_type_ref_from_type(
        symbols, location, dudu::parse_type_text("*const[Item]", location),
        dudu::parse_expr_text("0", location), "items");
    assert(dudu::substitute_type_ref_text(pointer_item, {}) == "Item");
    const dudu::TypeRef indexed_bag_item = dudu::indexed_type_ref_from_type(
        symbols, location, dudu::parse_type_text("Bag[Item]", location),
        dudu::parse_expr_text("0", location), "bag");
    assert(dudu::substitute_type_ref_text(indexed_bag_item, {}) == "Item");
    const dudu::TypeRef dict_item = dudu::indexed_type_ref_from_type(
        symbols, location, dudu::parse_type_text("dict[str, Item]", location),
        dudu::parse_expr_text("key", location), "items");
    assert(dudu::substitute_type_ref_text(dict_item, {}) == "Item");
    const dudu::TypeRef aliased_list_item =
        dudu::indexed_type_ref_from_type(symbols, location, dudu::parse_type_text("Ints", location),
                                         dudu::parse_expr_text("0", location), "ints");
    assert(dudu::substitute_type_ref_text(aliased_list_item, {}) == "i32");
    const dudu::TypeRef nested_alias_item = dudu::indexed_type_ref_from_type(
        symbols, location, dudu::parse_type_text("AliasItems", location),
        dudu::parse_expr_text("0", location), "items");
    assert(dudu::substitute_type_ref_text(nested_alias_item, {}) == "Item");

    const dudu::TypeRef matrix_type = dudu::parse_type_text("array[list[i32]][3, 4]");
    const dudu::TypeRef row_type = dudu::indexed_type_ref_from_type(
        symbols, location, matrix_type, dudu::parse_expr_text("1", location), "matrix");
    assert(row_type.kind == dudu::TypeKind::FixedArray);
    assert(dudu::substitute_type_ref_text(row_type, {}) == "array[list[i32]][4]");

    const dudu::TypeRef cell_type = dudu::indexed_type_ref_from_type(
        symbols, location, matrix_type, dudu::parse_expr_text("1, 2", location), "matrix");
    assert(cell_type.kind == dudu::TypeKind::Template);
    assert(cell_type.name == "list");
    assert(dudu::substitute_type_ref_text(cell_type, {}) == "list[i32]");

    const dudu::TypeRef dense_matrix_type = dudu::parse_type_text("array[i32][3, 4]");
    const dudu::TypeRef column_type = dudu::indexed_type_ref_from_type(
        symbols, location, dense_matrix_type, dudu::parse_expr_text(":, 1", location), "matrix");
    assert(column_type.kind == dudu::TypeKind::Template);
    assert(column_type.name == "strided_span");
    assert(dudu::substitute_type_ref_text(column_type, {}) == "strided_span[i32]");

    const dudu::TypeRef row_span_type = dudu::indexed_type_ref_from_type(
        symbols, location, dense_matrix_type, dudu::parse_expr_text("1, :", location), "matrix");
    assert(row_span_type.kind == dudu::TypeKind::Template);
    assert(row_span_type.name == "span");
    assert(dudu::substitute_type_ref_text(row_span_type, {}) == "span[i32]");

    const std::map<std::string, dudu::TypeRef> local_type_refs = {
        {"bag", dudu::parse_type_text("Bag[Item]", location)},
    };
    const std::optional<dudu::TypeRef> bag_item =
        dudu::iterable_value_type_ref(local_type_refs, "bag");
    assert(bag_item);
    assert(dudu::substitute_type_ref_text(*bag_item, {}) == "Item");

    dudu::ClassDecl tensor;
    tensor.name = "Tensor";
    dudu::FunctionDecl index_method;
    index_method.name = "get";
    index_method.decorators.push_back(
        {.expr = dudu::parse_expr_text("operator(\"[]\")", location), .location = location});
    index_method.params.push_back({.name = "self",
                                   .type_ref = dudu::parse_type_text("Tensor", location),
                                   .location = location});
    index_method.params.push_back({.name = "index",
                                   .type_ref = dudu::parse_type_text("i32", location),
                                   .location = location});
    index_method.return_type_ref = dudu::parse_type_text("f32", location);
    tensor.methods.push_back(index_method);
    symbols.classes["Tensor"] = &tensor;

    const dudu::TypeRef tensor_index_type = dudu::indexed_type_ref_from_type(
        symbols, location, dudu::parse_type_text("Tensor", location),
        dudu::parse_expr_text("0", location), "tensor");
    assert(dudu::substitute_type_ref_text(tensor_index_type, {}) == "f32");
}

void test_direct_call_return_type_inference_uses_type_ast() {
    const dudu::ModuleAst module =
        dudu::parse_source("def make_matrix() -> array[i32][2, 2]:\n"
                           "    out: array[i32][2, 2] = [[1, 2], [3, 4]]\n"
                           "    return out\n"
                           "\n"
                           "def main() -> i32:\n"
                           "    matrix = make_matrix()\n"
                           "    value: i32 = matrix[1, 1]\n"
                           "    return value\n",
                           "call_return_type.dd");
    dudu::analyze_module(module, {.check_bodies = true});
}

void test_emitted_local_expression_type_inference() {
    const std::map<std::string, std::string> locals = {
        {"flag", "bool"},
        {"count", "i32"},
        {"player", "*const[Player]"},
        {"item_ptr", "*Item"},
        {"queue", "storage[Queue[i32]]"},
        {"scale", "f32"},
    };
    const std::map<std::string, dudu::TypeRef> functions = {
        {"make_matrix", dudu::parse_type_text("array[i32][2, 2]")},
        {"make_values", dudu::parse_type_text("list[i32]")},
        {"make_count", dudu::parse_type_text("i32")},
        {"Player.hp", dudu::parse_type_text("i32")},
        {"Queue.pop", dudu::parse_type_text("i32")},
    };

    assert(infer_emitted_local_type_text("True", locals, functions) == "bool");
    assert(infer_emitted_local_type_text("1_024", locals, functions) == "i32");
    assert(infer_emitted_local_type_text("1.5", locals, functions) == "f64");
    assert(infer_emitted_local_type_text("\"hi\"", locals, functions) == "str");
    assert(infer_emitted_local_type_text("&count", locals, functions) == "*i32");
    assert(infer_emitted_local_type_text("*&count", locals, functions) == "i32");
    assert(infer_emitted_local_type_text("*item_ptr", locals, functions) == "Item");
    assert(infer_emitted_local_type_text("not flag", locals, functions) == "bool");
    assert(infer_emitted_local_type_text("count + 2", locals, functions) == "i32");
    assert(infer_emitted_local_type_text("scale + 2", locals, functions) == "f32");
    assert(infer_emitted_local_type_text("count < 4", locals, functions) == "bool");
    assert(infer_emitted_local_type_text("make_count()", locals, functions) == "i32");
    assert(infer_emitted_local_type_text("player.hp()", locals, functions) == "i32");
    assert(infer_emitted_local_type_text("queue.pop()", locals, functions) == "i32");
    assert(infer_emitted_local_type_text("make_values()[0]", locals, functions) == "i32");
    assert(infer_emitted_local_type_text("make_matrix()[1][0]", locals, functions) == "i32");

    const dudu::ModuleAst module = dudu::parse_source("class lowercase:\n"
                                                      "    value: i32\n",
                                                      "lowercase_constructor_type.dd");
    const dudu::Symbols symbols = dudu::collect_symbols(module);
    const dudu::TypeRef constructed =
        dudu::infer_emitted_local_type_ref(dudu::parse_expr_text("lowercase(1)"), {}, {}, &symbols);
    assert(dudu::type_ref_text(constructed) == "lowercase");
}

void test_tuple_expression_inference_uses_type_ast() {
    const dudu::ModuleAst module = dudu::parse_source("def main() -> i32:\n"
                                                      "    count: i32 = 7\n"
                                                      "    name: str = \"dudu\"\n"
                                                      "    pair: tuple[i32, str] = (count, name)\n"
                                                      "    return count\n",
                                                      "tuple_inference.dd");
    dudu::analyze_module(module, {.check_bodies = true});
}

void test_result_constructor_inference_uses_type_ast() {
    const dudu::ModuleAst module = dudu::parse_source("def ok_value() -> Result[i32, str]:\n"
                                                      "    return Ok(7)\n"
                                                      "\n"
                                                      "def err_value() -> Result[i32, str]:\n"
                                                      "    return Err(\"bad\")\n",
                                                      "result_constructor_inference.dd");
    dudu::analyze_module(module, {.check_bodies = true});
}

void test_formatter() {
    const std::string formatted = dudu::format_source("def main() -> i32:   \n"
                                                      "    return 0\t\n"
                                                      "\n"
                                                      "\n"
                                                      "\n"
                                                      "def other():\n");
    assert(formatted == "def main() -> i32:\n"
                        "    return 0\n"
                        "\n"
                        "\n"
                        "def other():\n");

    const std::string sorted = dudu::format_source("import zeta\n"
                                                   "from beta import Thing\n"
                                                   "import alpha\n"
                                                   "\n"
                                                   "def main() -> i32:\n"
                                                   "    return 0\n");
    assert(sorted == "from beta import Thing\n"
                     "import alpha\n"
                     "import zeta\n"
                     "\n"
                     "def main() -> i32:\n"
                     "    return 0\n");

    const std::string tabs = dudu::format_source("def main() -> i32:\n"
                                                 "\tvalue: str = \"\\tkept\"\n"
                                                 "\treturn 0\n");
    assert(tabs == "def main() -> i32:\n"
                   "    value: str = \"\\tkept\"\n"
                   "    return 0\n");

    const std::filesystem::path dir =
        std::filesystem::temp_directory_path() / "dudu_format_path_test";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir / "src");
    std::filesystem::create_directories(dir / "build");
    write_file(dir / "src" / "main.dd", "def main() -> i32:   \n\treturn 0\n");
    write_file(dir / "build" / "generated.dd", "def generated() -> i32:   \n\treturn 1\n");

    dudu::format_path_in_place(dir / "src" / "main.dd");
    assert(read_file(dir / "src" / "main.dd") == "def main() -> i32:\n"
                                                 "    return 0\n");

    dudu::format_path_in_place(dir, {.excluded_dirs = {dir / "build"}});
    assert(read_file(dir / "build" / "generated.dd") == "def generated() -> i32:   \n"
                                                        "\treturn 1\n");
}

void test_list_iterator_methods() {
    const dudu::ModuleAst module = dudu::parse_source("def main() -> i32:\n"
                                                      "    values: list[i32] = [1, 2]\n"
                                                      "    values.begin()\n"
                                                      "    values.end()\n"
                                                      "    return 0\n",
                                                      "list_iter.dd");
    dudu::analyze_module(module, {.check_bodies = true});
}

void test_reference_list_indexing() {
    const dudu::ModuleAst module = dudu::parse_source("def write(values: &list[i32]):\n"
                                                      "    values[0] = 5\n"
                                                      "\n"
                                                      "def main() -> i32:\n"
                                                      "    values: list[i32] = [1]\n"
                                                      "    write(values)\n"
                                                      "    return values[0]\n",
                                                      "reference_list_indexing.dd");
    dudu::analyze_module(module, {.check_bodies = true});
}

void test_negative_numeric_literals_contextualize_as_f32_args() {
    const dudu::ModuleAst module =
        dudu::parse_source("def take_f32(x: f32, y: f32):\n"
                           "    pass\n"
                           "\n"
                           "class Player:\n"
                           "    x: f32\n"
                           "    y: f32\n"
                           "\n"
                           "    def move(self, dx: f32, dy: f32):\n"
                           "        self.x += dx\n"
                           "        self.y += dy\n"
                           "\n"
                           "def main() -> i32:\n"
                           "    take_f32(-2.0, -1.0)\n"
                           "    player = Player(x=1.0, y=2.0)\n"
                           "    player.move(2.0, -1.0)\n"
                           "    return 0\n",
                           "negative_numeric_literal_args.dd");
    dudu::analyze_module(module, {.check_bodies = true});
}

void test_pointer_dereference_uses_type_ast() {
    const dudu::ModuleAst module = dudu::parse_source("def read(ptr: *const[i32]) -> const[i32]:\n"
                                                      "    return *ptr\n"
                                                      "\n"
                                                      "def write(ptr: *i32):\n"
                                                      "    *ptr = 5\n",
                                                      "pointer_dereference_type_ast.dd");
    dudu::analyze_module(module, {.check_bodies = true});
}

void test_extern_c_signature_uses_type_ast() {
    const dudu::ModuleAst ok =
        dudu::parse_source("@extern_c\n"
                           "def take_struct(value: *struct NativeThing) -> void:\n"
                           "    return\n",
                           "extern_c_struct_pointer.dd");
    dudu::analyze_module(ok, {.check_bodies = true});

    bool rejected = false;
    try {
        const dudu::ModuleAst bad = dudu::parse_source("@extern_c\n"
                                                       "def bad_ref(value: &i32) -> void:\n"
                                                       "    return\n",
                                                       "extern_c_ref.dd");
        dudu::analyze_module(bad, {.check_bodies = true});
    } catch (const dudu::CompileError& error) {
        rejected =
            std::string(error.what()).find("@extern_c parameter type is not C ABI safe: &") !=
            std::string::npos;
    }
    assert(rejected);

    rejected = false;
    try {
        const dudu::ModuleAst bad = dudu::parse_source("class Player:\n"
                                                       "    hp: i32\n"
                                                       "\n"
                                                       "@extern_c\n"
                                                       "def bad_class(value: Player) -> void:\n"
                                                       "    return\n",
                                                       "extern_c_class.dd");
        dudu::analyze_module(bad, {.check_bodies = true});
    } catch (const dudu::CompileError& error) {
        rejected =
            std::string(error.what()).find("@extern_c parameter type is not C ABI safe: Player") !=
            std::string::npos;
    }
    assert(rejected);
}

void test_pointer_arithmetic_uses_type_ast() {
    const dudu::ModuleAst module = dudu::parse_source("def next_ptr(ptr: *i32) -> *i32:\n"
                                                      "    return ptr + 1\n",
                                                      "pointer_arithmetic_type_ast.dd");
    dudu::analyze_module(module, {.check_bodies = true});
}

void test_base_pointer_assignment_uses_type_ast() {
    const dudu::ModuleAst module = dudu::parse_source("class Base:\n"
                                                      "    @virtual\n"
                                                      "    def id(self) -> i32:\n"
                                                      "        return 1\n"
                                                      "\n"
                                                      "class Derived(Base):\n"
                                                      "    @override\n"
                                                      "    def id(self) -> i32:\n"
                                                      "        return 2\n"
                                                      "\n"
                                                      "def assign(value: *Derived) -> *Base:\n"
                                                      "    base: *Base = value\n"
                                                      "    return base\n",
                                                      "base_pointer_assignment_type_ast.dd");
    dudu::analyze_module(module, {.check_bodies = true});
}

void test_duplicate_base_check_resolves_type_aliases() {
    bool rejected = false;
    try {
        const dudu::ModuleAst module = dudu::parse_source("class Base:\n"
                                                          "    id: i32\n"
                                                          "\n"
                                                          "type BaseAlias = Base\n"
                                                          "\n"
                                                          "class Derived(Base, BaseAlias):\n"
                                                          "    value: i32\n",
                                                          "duplicate_base_alias.dd");
        dudu::analyze_module(module, {.check_bodies = true});
    } catch (const dudu::CompileError& error) {
        rejected =
            std::string(error.what()).find("duplicate base class: BaseAlias") != std::string::npos;
    }
    assert(rejected);
}

void test_inherited_method_identity_resolves_type_aliases() {
    bool rejected = false;
    try {
        const dudu::ModuleAst module =
            dudu::parse_source("class Node:\n"
                               "    id: i32\n"
                               "\n"
                               "class Payload:\n"
                               "    value: i32\n"
                               "\n"
                               "type PayloadAlias = Payload\n"
                               "\n"
                               "class Named:\n"
                               "    @abstract\n"
                               "    def name(self) -> str:\n"
                               "\n"
                               "    def label(self, payload: Payload) -> str:\n"
                               "        return self.name()\n"
                               "\n"
                               "class Titled:\n"
                               "    @abstract\n"
                               "    def title(self) -> str:\n"
                               "\n"
                               "    def label(self, payload: PayloadAlias) -> str:\n"
                               "        return self.title()\n"
                               "\n"
                               "class Sprite(Node, Named, Titled):\n"
                               "    @override\n"
                               "    def name(self) -> str:\n"
                               "        return \"sprite\"\n"
                               "\n"
                               "    @override\n"
                               "    def title(self) -> str:\n"
                               "        return \"sprite\"\n",
                               "ambiguous_inherited_alias_signature.dd");
        dudu::analyze_module(module, {.check_bodies = true});
    } catch (const dudu::CompileError& error) {
        rejected =
            std::string(error.what()).find("ambiguous inherited concrete method: label(Payload)") !=
            std::string::npos;
    }
    assert(rejected);
}

void test_bare_void_return() {
    const dudu::ModuleAst module = dudu::parse_source("def done():\n"
                                                      "    return\n"
                                                      "\n"
                                                      "def main() -> i32:\n"
                                                      "    done()\n"
                                                      "    return 0\n",
                                                      "bare_void_return.dd");
    dudu::analyze_module(module, {.check_bodies = true});
}

void test_typed_for_emission() {
    const dudu::ModuleAst module = dudu::parse_source("class Item:\n"
                                                      "    value: i32\n"
                                                      "\n"
                                                      "type ItemAlias = Item\n"
                                                      "\n"
                                                      "def sum_items(items: list[Item]) -> i32:\n"
                                                      "    total: i32 = 0\n"
                                                      "    for index in range(3):\n"
                                                      "        total += index\n"
                                                      "    for item: &Item in items:\n"
                                                      "        total += item.value\n"
                                                      "    for alias_item: ItemAlias in items:\n"
                                                      "        total += alias_item.value\n"
                                                      "    for copy in items:\n"
                                                      "        total += copy.value\n"
                                                      "    return total\n",
                                                      "typed_for.dd");
    dudu::analyze_module(module, {.check_bodies = true});
    const std::string cpp = dudu::emit_cpp_source(module);
    assert(cpp.find("for (auto index = 0; index < 3; index += 1)") != std::string::npos);
    assert(cpp.find("for (Item& item : items)") != std::string::npos);
    assert(cpp.find("for (ItemAlias alias_item : items)") != std::string::npos);
    assert(cpp.find("for (auto&& copy : items)") != std::string::npos);
}

void test_class_field_defaults_and_static_fields() {
    const dudu::ModuleAst module = dudu::parse_source("class Counter:\n"
                                                      "    value: i32 = 7\n"
                                                      "    count: static[i32] = 0\n"
                                                      "\n"
                                                      "    def bump() -> i32:\n"
                                                      "        Counter.count += 1\n"
                                                      "        return Counter.count\n",
                                                      "class_defaults.dd");
    assert(module.classes.size() == 1);
    const dudu::ClassDecl& counter = module.classes.front();
    assert(counter.fields.size() == 1);
    assert(counter.fields[0].name == "value");
    assert(counter.fields[0].value_expr.kind == dudu::ExprKind::IntLiteral);
    assert(counter.fields[0].value_expr.value == "7");
    assert(counter.static_fields.size() == 1);
    assert(counter.static_fields[0].name == "count");
    assert(dudu::type_ref_text(counter.static_fields[0].type_ref) == "i32");
    assert(counter.methods.size() == 1);

    const std::string cpp = dudu::emit_cpp_source(module);
    assert(cpp.find("int32_t value = 7;") != std::string::npos);
    assert(cpp.find("inline static int32_t count = 0;") != std::string::npos);
    assert(cpp.find("static int32_t bump()") != std::string::npos);
}

void test_project_driver_config(const std::filesystem::path& root) {
    const std::filesystem::path dir = root / "build" / "project-driver-config-test";
    std::filesystem::create_directories(dir);
    const std::filesystem::path config_path = dir / "dudu.toml";
    {
        std::ofstream out(config_path);
        out << "name = \"tool\"\n"
               "entry = \"src/main.dd\"\n"
               "\n"
               "[cxx]\n"
               "standard = \"c++23\"\n"
               "compiler = \"clang++\"\n"
               "\n"
               "[include]\n"
               "paths = [\n"
               "    \"src\",\n"
               "    \"include\",\n"
               "]\n"
               "\n"
               "[sources]\n"
               "cpp = [\"src/native.cpp\"]\n"
               "c = [\"src/native.c\"]\n"
               "\n"
               "[pkg]\n"
               "libs = [\"raylib\"]\n"
               "\n"
               "[link]\n"
               "paths = [\"lib\"]\n"
               "libs = [\"m\"]\n"
               "flags = [\"-pthread\"]\n"
               "\n"
               "[build]\n"
               "dir = \"out\"\n"
               "\n"
               "[targets.tool]\n"
               "entry = \"tools/tool.dd\"\n"
               "kind = \"executable\"\n"
               "\n"
               "[targets.tool.pkg]\n"
               "libs = [\"sqlite3\"]\n"
               "\n"
               "[targets.tests]\n"
               "entry = \"tests/main.dd\"\n"
               "kind = \"executable\"\n"
               "mode = \"hosted\"\n";
    }
    const dudu::ProjectConfig config = dudu::parse_project_config(config_path);
    assert(config.name == "tool");
    assert(config.main == "src/main.dd");
    assert(config.cpp_std == "c++23");
    assert(config.compiler == "clang++");
    assert(config.include_dirs.size() == 2);
    assert(config.cpp_sources.size() == 1);
    assert(config.c_sources.size() == 1);
    assert(config.pkg_config_packages.size() == 1);
    assert(config.lib_dirs.size() == 1);
    assert(config.libs.size() == 1);
    assert(config.link_flags.size() == 1);
    assert(config.build_dir == "out");
    assert(config.targets.size() == 2);
    assert(config.targets.at("tool").main == "tools/tool.dd");
    assert(config.targets.at("tool").pkg_config_packages.size() == 1);
    assert(config.targets.at("tests").target_mode == "hosted");
    const dudu::ProjectConfig tests_config = dudu::apply_project_target(config, "tests");
    assert(tests_config.name == "tests");
    assert(tests_config.main == "tests/main.dd");
    assert(tests_config.target_kind == "executable");
    assert(tests_config.target_mode == "hosted");
}

} // namespace

int main() {
    try {
        const std::filesystem::path root = DUDU_REPO_ROOT;
        test_lexer_indentation();
        test_import_bindings();
        test_module_loader_canonicalizes_physical_modules();
        test_module_loader_rejects_duplicate_from_aliases();
        test_merged_output_rejects_same_named_module_declarations();
        test_module_loader_qualified_module_imports();
        test_module_loader_preserves_declaration_origins();
        test_cpp_module_artifacts_preserve_module_boundaries();
        test_cpp_module_artifacts_use_resolved_dependency_paths();
        test_canonical_examples_parse(root);
        test_header_emission();
        test_semantic_diagnostics();
        test_lsp_diagnostic_sources_are_structured();
        test_lsp_diagnostics_use_open_buffer_for_module_entry();
        test_lsp_lints_do_not_leak_from_dependency_modules();
        test_lsp_member_completion_uses_imported_module_shapes();
        test_lsp_completion_uses_visible_imported_functions();
        test_lsp_signature_help_uses_visible_imported_functions();
        test_lsp_module_completion_uses_loaded_module_units();
        test_lsp_definition_uses_loaded_module_units();
        test_lsp_definition_jumps_to_native_header_type();
        test_lsp_definition_uses_receiver_for_ambiguous_native_methods();
        test_lsp_hover_uses_receiver_for_ambiguous_native_methods();
        test_lsp_references_keep_unbound_member_query_dotted();
        test_lsp_hover_uses_loaded_module_units();
        test_lsp_unreachable_lint_uses_branch_structure();
        test_lsp_unreachable_lint_does_not_flag_partial_branch_return();
        test_lsp_scope_lint_tracks_inferred_assignment_locals();
        test_lsp_suspicious_cast_lint_uses_type_refs();
        test_lsp_references_track_assignment_bindings();
        test_lsp_references_track_qualified_type_refs();
        test_lsp_module_reference_filters_alias_target();
        test_lsp_module_references_include_target_declaration();
        test_lsp_selective_import_references_include_target_declaration();
        test_allocation_type_ref_diagnostics();
        test_emitted_local_index_type_inference();
        test_index_type_inference_uses_type_ast();
        test_direct_call_return_type_inference_uses_type_ast();
        test_emitted_local_expression_type_inference();
        test_tuple_expression_inference_uses_type_ast();
        test_result_constructor_inference_uses_type_ast();
        test_formatter();
        test_list_iterator_methods();
        test_reference_list_indexing();
        test_negative_numeric_literals_contextualize_as_f32_args();
        test_pointer_dereference_uses_type_ast();
        test_extern_c_signature_uses_type_ast();
        test_pointer_arithmetic_uses_type_ast();
        test_base_pointer_assignment_uses_type_ast();
        test_duplicate_base_check_resolves_type_aliases();
        test_inherited_method_identity_resolves_type_aliases();
        test_bare_void_return();
        test_typed_for_emission();
        test_class_field_defaults_and_static_fields();
        test_project_driver_config(root);
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
