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

void test_transitive_qualified_imports_keep_canonical_type_identity() {
    const std::filesystem::path dir =
        std::filesystem::temp_directory_path() / "dudu_transitive_qualified_identity_test";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    write_file(dir / "model.dd", "class Item:\n"
                                 "    value: i32\n");
    write_file(dir / "helper.dd", "import model as api\n"
                                  "\n"
                                  "def make() -> api.Item:\n"
                                  "    return api.Item(42)\n"
                                  "\n"
                                  "def read(item: &const[api.Item]) -> i32:\n"
                                  "    return item.value\n");
    write_file(dir / "main.dd", "import model as api\n"
                                "from model import Item\n"
                                "from helper import make\n"
                                "from helper import read\n"
                                "\n"
                                "def main() -> i32:\n"
                                "    item: Item = make()\n"
                                "    return read(item)\n");

    const dudu::ModuleAst module = dudu::load_source_tree(dir / "main.dd");
    dudu::analyze_module_tree(module, {.check_bodies = true});
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

void test_module_loader_recovers_missing_selective_import_without_stale_alias() {
    const std::filesystem::path dir =
        std::filesystem::temp_directory_path() / "dudu_missing_selective_import_recovery_test";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    write_file(dir / "helper.dd", "def replacement(value: i32) -> i32:\n"
                                  "    return value + 1\n");
    write_file(dir / "main.dd", "from helper import removed as local_removed\n"
                                "\n"
                                "def main() -> i32:\n"
                                "    return local_removed(1)\n");

    bool strict_failed = false;
    try {
        (void)dudu::load_source_tree(dir / "main.dd");
    } catch (const dudu::CompileError& error) {
        strict_failed =
            error.code() == "dudu.sema.missing_import" && error.data_name() == "removed";
    }
    assert(strict_failed);

    const dudu::LoadSourceTreeResult recovered = dudu::load_source_tree_recovering(
        {.entry = dir / "main.dd", .source_overrides = {}, .module_roots = {}});
    assert(recovered.module.module_units.size() == 2);
    assert(recovered.diagnostics.size() == 1);
    assert(recovered.diagnostics.front().code == "dudu.sema.missing_import");
    assert(recovered.diagnostics.front().data_name == "removed");
    assert(std::none_of(recovered.module.native_functions.begin(),
                        recovered.module.native_functions.end(),
                        [](const dudu::NativeFunctionDecl& fn) {
                            return fn.name == "removed" || fn.name == "local_removed";
                        }));
}

void test_module_loader_recovers_missing_module_without_stale_declarations() {
    const std::filesystem::path dir =
        std::filesystem::temp_directory_path() / "dudu_missing_module_recovery_test";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    write_file(dir / "main.dd", "import removed_module\n"
                                "\n"
                                "def current_answer() -> i32:\n"
                                "    return 42\n");

    bool strict_failed = false;
    try {
        (void)dudu::load_source_tree(dir / "main.dd");
    } catch (const dudu::CompileError& error) {
        strict_failed = error.code() == "dudu.sema.missing_module" &&
                        error.data_name() == "removed_module" && error.location().line == 1;
    }
    assert(strict_failed);

    const dudu::LoadSourceTreeResult recovered = dudu::load_source_tree_recovering(
        {.entry = dir / "main.dd", .source_overrides = {}, .module_roots = {}});
    assert(recovered.module.module_units.size() == 1);
    assert(recovered.diagnostics.size() == 1);
    assert(recovered.diagnostics.front().code == "dudu.sema.missing_module");
    assert(recovered.diagnostics.front().data_name == "removed_module");
    assert(recovered.module.functions.size() == 1);
    assert(recovered.module.functions.front().name == "current_answer");
}

} // namespace

int main() {
    try {
        test_module_loader_canonicalizes_physical_modules();
        test_transitive_qualified_imports_keep_canonical_type_identity();
        test_module_loader_rejects_duplicate_from_aliases();
        test_module_loader_resolves_source_root_import_from_submodule();
        test_imported_classes_keep_distinct_symbol_identities();
        test_module_loader_qualified_module_imports();
        test_module_loader_preserves_declaration_origins();
        test_module_loader_recovers_missing_selective_import_without_stale_alias();
        test_module_loader_recovers_missing_module_without_stale_declarations();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
