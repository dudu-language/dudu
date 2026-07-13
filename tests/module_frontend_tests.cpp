#include "dudu/codegen/cpp_emit.hpp"
#include "dudu/codegen/cpp_emit_modules.hpp"
#include "dudu/codegen/cpp_lower.hpp"
#include "dudu/codegen/cpp_stmt_types.hpp"
#include "dudu/core/ast_expr.hpp"
#include "dudu/core/ast_type.hpp"
#include "dudu/format/format.hpp"
#include "dudu/format/format_path.hpp"
#include "dudu/native/native_header_identity.hpp"
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

void test_module_loader_resolves_source_root_import_from_submodule() {
    const std::filesystem::path dir =
        std::filesystem::temp_directory_path() / "dudu_submodule_root_import_test";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir / "mald");
    write_file(dir / "ndad.dd", "class Tensor:\n"
                                "    value: i32\n");
    write_file(dir / "mald" / "losses.dd", "from ndad import Tensor\n"
                                           "\n"
                                           "def make_tensor() -> Tensor:\n"
                                           "    return Tensor(42)\n");
    write_file(dir / "main.dd", "from mald.losses import make_tensor\n"
                                "\n"
                                "def main() -> i32:\n"
                                "    return make_tensor().value\n");

    const dudu::ModuleAst module = dudu::load_source_tree(dir / "main.dd");
    assert(module.module_units.size() == 3);
    assert(module.module_units[0].module_path == "ndad");
    assert(module.module_units[1].module_path == "mald.losses");
    assert(module.module_units[2].module_path == "main");
    assert(module.module_units[1].dependencies.size() == 1);
    assert(module.module_units[1].dependencies[0].import_module_path == "ndad");
    assert(module.module_units[1].dependencies[0].resolved_module_path == "ndad");
    dudu::analyze_module(module, {.check_bodies = true});
}

void test_imported_classes_keep_distinct_symbol_identities() {
    const std::filesystem::path dir =
        std::filesystem::temp_directory_path() / "dudu_imported_class_identity_test";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    write_file(dir / "models.dd", "class Box[T]:\n"
                                  "    value: T\n"
                                  "\n"
                                  "class Other:\n"
                                  "    value: i32\n");
    write_file(dir / "main.dd", "from models import Box\n"
                                 "from models import Other\n"
                                 "\n"
                                 "def main() -> i32:\n"
                                 "    box = Box[i32](42)\n"
                                 "    other = Other(1)\n"
                                 "    return box.value + other.value\n");

    const dudu::ModuleAst module = dudu::load_source_tree(dir / "main.dd");
    std::map<std::string, std::string> identities;
    for (const dudu::ClassDecl& klass : module.native_classes) {
        if (klass.name == "Box" || klass.name == "models.Box" || klass.name == "Other" ||
            klass.name == "models.Other") {
            identities[klass.name] = dudu::native_symbol_identity_key(klass.identity);
        }
    }
    assert(identities.at("Box") == "path:models.Box");
    assert(identities.at("models.Box") == "path:models.Box");
    assert(identities.at("Other") == "path:models.Other");
    assert(identities.at("models.Other") == "path:models.Other");
    assert(identities.at("Box") != identities.at("Other"));
    dudu::analyze_module(module, {.check_bodies = true});
}

void test_project_index_records_module_graph() {
    const std::filesystem::path root =
        std::filesystem::path(DUDU_REPO_ROOT) / "tests" / "fixtures" / "project_import_metadata";
    const std::filesystem::path entry = root / "main.dd";
    dudu::ProjectIndexOptions options;
    options.entry_path = entry;
    const std::string source = read_file(entry);
    options.entry_source = source;
    options.config = dudu::parse_project_config(root / "dudu.toml");
    options.source_dir = root;
    options.force_module_tree = true;
    options.include_native_headers = false;
    options.check_semantics = true;
    options.semantic_options = {.check_bodies = true};

    const dudu::ProjectIndex index = dudu::ProjectIndex::load(options);
    assert(index.modules().size() == 5);
    const dudu::ProjectModuleSummary* camera = index.summary_for_module("camera");
    const dudu::ProjectModuleSummary* renderer = index.summary_for_module("renderer");
    const dudu::ProjectModuleSummary* main = index.summary_for_module("main");
    assert(camera != nullptr);
    assert(renderer != nullptr);
    assert(main != nullptr);
    assert(camera->exports.contains("Camera"));
    assert(camera->exports.contains("build_camera"));
    assert(camera->source_mtime.has_value());
    assert(renderer->dependencies.size() == 1);
    assert(renderer->dependencies[0].resolved_module_path == "camera");
    assert(std::find(camera->reverse_dependencies.begin(), camera->reverse_dependencies.end(),
                     "renderer") != camera->reverse_dependencies.end());
    assert(std::find(camera->reverse_dependencies.begin(), camera->reverse_dependencies.end(),
                     "main") != camera->reverse_dependencies.end());
    assert(index.unit_for_path(root / "camera.dd") == index.unit_for_module("camera"));
    assert(&index.visible_unit_for_path(root / "main.dd") == index.unit_for_module("main"));
    assert(index.source_stamps_current());

    const std::vector<std::string> camera_affected =
        index.affected_modules_for_sources({root / "camera.dd"});
    assert((camera_affected == std::vector<std::string>{"camera", "renderer", "main"}));
    const std::vector<std::string> renderer_affected =
        index.affected_modules_for_sources({root / "renderer.dd"});
    assert((renderer_affected == std::vector<std::string>{"renderer", "main"}));
    const std::vector<std::string> main_affected =
        index.affected_modules_for_sources({root / "main.dd"});
    assert((main_affected == std::vector<std::string>{"main"}));
}

void test_project_index_resolves_path_dependency_modules() {
    const std::filesystem::path dir =
        std::filesystem::temp_directory_path() / "dudu_path_dependency_module_test";
    std::filesystem::remove_all(dir);
    const std::filesystem::path app = dir / "app";
    const std::filesystem::path dependency = dir / "local_math";
    std::filesystem::create_directories(app / "src");
    std::filesystem::create_directories(dependency / "src");
    write_file(app / "dudu.toml", "name = \"app\"\n"
                                  "entry = \"src/main.dd\"\n"
                                  "\n"
                                  "[deps]\n"
                                  "local_math = { path = \"../local_math\" }\n");
    write_file(app / "src" / "main.dd", "from local_math import value\n"
                                        "\n"
                                        "def main() -> i32:\n"
                                        "    return value()\n");
    write_file(dependency / "src" / "local_math.dd", "def value() -> i32:\n"
                                                     "    return 42\n");

    dudu::ProjectIndexOptions options;
    options.entry_path = app / "src" / "main.dd";
    options.config = dudu::parse_project_config(app / "dudu.toml");
    options.source_dir = app / "src";
    options.force_module_tree = true;
    options.include_native_headers = false;
    options.check_semantics = true;
    options.semantic_options = {.check_bodies = true};

    const dudu::ProjectIndex index = dudu::ProjectIndex::load(options);
    assert(index.modules().size() == 2);
    const dudu::ProjectModuleSummary* local_math = index.summary_for_module("local_math");
    const dudu::ProjectModuleSummary* main = index.summary_for_module("main");
    assert(local_math != nullptr);
    assert(main != nullptr);
    assert(local_math->exports.contains("value"));
    assert(main->dependencies.size() == 1);
    assert(main->dependencies[0].resolved_module_path == "local_math");
    assert(std::filesystem::weakly_canonical(main->dependencies[0].source_path) ==
           std::filesystem::weakly_canonical(dependency / "src" / "local_math.dd"));
}

void test_project_index_source_stamps_detect_changed_modules() {
    const std::filesystem::path dir =
        std::filesystem::temp_directory_path() / "dudu_project_index_stamps_test";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    const std::filesystem::path dependency = dir / "dep.dd";
    const std::filesystem::path entry = dir / "main.dd";
    write_file(dependency, "def value() -> i32:\n"
                           "    return 1\n");
    write_file(entry, "import dep\n"
                      "\n"
                      "def main() -> i32:\n"
                      "    return dep.value()\n");

    dudu::ProjectIndexOptions options;
    options.entry_path = entry;
    const std::string source = read_file(entry);
    options.entry_source = source;
    options.source_dir = dir;
    options.force_module_tree = true;
    options.include_native_headers = false;
    options.check_semantics = true;
    options.semantic_options = {.check_bodies = true};
    const dudu::ProjectIndex index = dudu::ProjectIndex::load(options);
    assert(index.source_stamps_current());
    const std::filesystem::path stamp_file = dir / ".dudu_sources.stamp";
    index.write_source_stamp_file(stamp_file);
    assert(index.changed_sources_since_stamp_file(stamp_file).empty());

    write_file(dependency, "def value() -> i32:\n"
                           "    return 2\n");
    std::error_code error;
    std::filesystem::last_write_time(
        dependency, std::filesystem::file_time_type::clock::now() + std::chrono::seconds(5), error);
    assert(!error);
    assert(!index.source_stamps_current());

    const dudu::ProjectIndex changed_index = dudu::ProjectIndex::load(options);
    const std::vector<std::filesystem::path> changed_sources =
        changed_index.changed_sources_since_stamp_file(stamp_file);
    assert(changed_sources.size() == 1);
    assert(std::filesystem::weakly_canonical(changed_sources[0]) ==
           std::filesystem::weakly_canonical(dependency));
    const std::vector<std::string> affected =
        changed_index.affected_modules_for_sources(changed_sources);
    assert((affected == std::vector<std::string>{"dep", "main"}));
}

void test_selected_module_analysis_falls_back_when_paths_miss() {
    const std::filesystem::path dir =
        std::filesystem::temp_directory_path() / "dudu_selected_module_analysis_fallback_test";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    write_file(dir / "main.dd", "class Vec3:\n"
                                "    x: f32\n"
                                "\n"
                                "    def bad(self: &const[Self]) -> &Self:\n"
                                "        return self\n");

    const dudu::ModuleAst module = dudu::load_source_tree(dir / "main.dd");
    bool rejected = false;
    try {
        dudu::analyze_module_tree(module, {"missing.module"}, {.check_bodies = true});
    } catch (const dudu::CompileError& error) {
        rejected = std::string(error.what()).find("return type mismatch") != std::string::npos;
    }
    assert(rejected);
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
        rejected =
            message.find("merged C++ output cannot combine Dudu modules") != std::string::npos &&
            message.find("use `dudu build`") != std::string::npos &&
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
                                "def relay(value: cam.Camera) -> cam.Camera:\n"
                                "    return value\n"
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
    assert(by_path.at("main.cpp").find("dudu_camera_make_camera(1)") != std::string::npos);
    assert(by_path.at("main.hpp").find(
               "DuduCameraCamera dudu_main_relay(DuduCameraCamera value);") !=
           std::string::npos);
    assert(by_path.at("main.cpp").find(
               "DuduCameraCamera dudu_main_relay(DuduCameraCamera value)") !=
           std::string::npos);
    assert(by_path.at("main.cpp")
               .find("dudu_renderer_camera_make_camera(2)") !=
           std::string::npos);
    assert(by_path.at("main.cpp").find("cam.make_camera") == std::string::npos);
    assert(by_path.at("main.cpp").find("render_camera.make_camera") == std::string::npos);
    assert(by_path.at("main.cpp").find("int main()") != std::string::npos);
    assert(by_path.at("main.cpp").find("return dudu_main_main();") != std::string::npos);

    std::vector<std::filesystem::path> source_artifact_paths;
    for (const dudu::CppModuleArtifact& artifact : artifacts) {
        if (artifact.kind == dudu::CppModuleArtifactKind::Source) {
            source_artifact_paths.push_back(artifact.path);
        }
    }
    assert(dudu::cpp_module_source_paths(module) == source_artifact_paths);
    source_artifact_paths.push_back("test_harness.cpp");
    assert(dudu::cpp_test_module_source_paths(module) == source_artifact_paths);

    by_path.clear();
    const std::vector<dudu::CppModuleArtifact> selected_artifacts =
        dudu::emit_cpp_module_artifacts(module, {"camera"});
    for (const dudu::CppModuleArtifact& artifact : selected_artifacts) {
        by_path[artifact.path] = artifact.content;
    }
    assert(by_path.contains("dudu_runtime.hpp"));
    assert(by_path.contains("camera.hpp"));
    assert(by_path.contains("camera.cpp"));
    assert(!by_path.contains(std::filesystem::path("renderer") / "camera.hpp"));
    assert(!by_path.contains(std::filesystem::path("renderer") / "camera.cpp"));
    assert(!by_path.contains("main.hpp"));
    assert(!by_path.contains("main.cpp"));
    assert(dudu::emit_cpp_module_artifacts(module, {}).empty());
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

} // namespace

int main() {
    try {
        test_module_loader_canonicalizes_physical_modules();
        test_module_loader_rejects_duplicate_from_aliases();
        test_module_loader_resolves_source_root_import_from_submodule();
        test_imported_classes_keep_distinct_symbol_identities();
        test_project_index_records_module_graph();
        test_project_index_resolves_path_dependency_modules();
        test_project_index_source_stamps_detect_changed_modules();
        test_selected_module_analysis_falls_back_when_paths_miss();
        test_merged_output_rejects_same_named_module_declarations();
        test_module_loader_qualified_module_imports();
        test_module_loader_preserves_declaration_origins();
        test_cpp_module_artifacts_preserve_module_boundaries();
        test_cpp_module_artifacts_use_resolved_dependency_paths();
    } catch (const std::exception& error) {
        std::cerr << error.what() << "\n";
        return 1;
    }
    return 0;
}
