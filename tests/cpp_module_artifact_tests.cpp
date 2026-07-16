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

std::string emitted_runtime_for_source(const std::string& case_name, const std::string& source) {
    const std::filesystem::path dir =
        std::filesystem::temp_directory_path() / ("dudu_runtime_features_" + case_name);
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    write_file(dir / "main.dd", source);
    const dudu::ModuleAst module = dudu::load_source_tree(dir / "main.dd");
    for (const dudu::CppModuleArtifact& artifact : dudu::emit_cpp_module_artifacts(module)) {
        if (artifact.path == "dudu_runtime.hpp") {
            return artifact.content;
        }
    }
    throw std::runtime_error("Dudu runtime artifact was not emitted");
}

void test_cpp_module_artifacts_preserve_module_boundaries() {
    const std::filesystem::path dir =
        std::filesystem::temp_directory_path() / "dudu_module_artifact_test";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir / "renderer");
    write_file(dir / "camera.dd", "class Camera:\n"
                                  "    x: i32\n"
                                  "\n"
                                  "    def value(self) -> i32:\n"
                                  "        return self.x\n"
                                  "\n"
                                  "def _hidden_value(x: i32) -> i32:\n"
                                  "    return x + 1\n"
                                  "\n"
                                  "def make_camera(x: i32) -> Camera:\n"
                                  "    return Camera(x=_hidden_value(x) - 1)\n");
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
    std::string hidden_function_cpp_name;
    for (const dudu::ModuleAst& unit : module.module_units) {
        if (unit.module_path != "camera") {
            continue;
        }
        for (const dudu::FunctionDecl& fn : unit.functions) {
            if (fn.name == "_hidden_value") {
                hidden_function_cpp_name = fn.cpp_name.empty() ? fn.name : fn.cpp_name;
            }
        }
    }
    assert(!hidden_function_cpp_name.empty());
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
    assert(by_path.at("dudu_runtime.hpp").find("template <typename T, typename E> struct Result") ==
           std::string::npos);
    assert(by_path.at("camera.hpp").find("#include \"dudu_runtime.hpp\"") != std::string::npos);
    assert(by_path.at("main.cpp").find("#include \"main.hpp\"") != std::string::npos);
    assert(by_path.at("camera.hpp").find("template <typename T, typename E> struct Result") ==
           std::string::npos);
    assert(by_path.at("camera.cpp").find("// dudu module: camera") != std::string::npos);
    assert(by_path.at(std::filesystem::path("renderer") / "camera.cpp")
               .find("// dudu module: renderer.camera") != std::string::npos);
    assert(by_path.at("main.hpp").find("#include \"camera.hpp\"") != std::string::npos);
    assert(by_path.at("main.hpp").find("#include \"renderer/camera.hpp\"") == std::string::npos);
    assert(by_path.at("main.cpp").find("#include \"camera.hpp\"") == std::string::npos);
    assert(by_path.at("main.cpp").find("#include \"renderer/camera.hpp\"") != std::string::npos);
    assert(by_path.at("camera.cpp").find("#include \"camera.hpp\"") != std::string::npos);
    assert(by_path.at(std::filesystem::path("renderer") / "camera.cpp")
               .find("#include \"renderer/camera.hpp\"") != std::string::npos);
    assert(by_path.at("camera.cpp").find("struct DuduCameraCamera") == std::string::npos);
    assert(by_path.at(std::filesystem::path("renderer") / "camera.cpp")
               .find("struct DuduRendererCameraCamera") == std::string::npos);
    assert(by_path.at("camera.hpp").find("struct DuduCameraCamera") != std::string::npos);
    assert(by_path.at("camera.hpp").find(hidden_function_cpp_name) == std::string::npos);
    assert(by_path.at("camera.cpp").find(hidden_function_cpp_name) != std::string::npos);
    assert(by_path.at("camera.cpp").find("DuduCameraCamera::value()") != std::string::npos);
    assert(by_path.at(std::filesystem::path("renderer") / "camera.hpp")
               .find("struct DuduRendererCameraCamera") != std::string::npos);
    assert(by_path.at("camera.cpp").find("DuduCameraCamera dudu_camera_make_camera") !=
           std::string::npos);
    assert(by_path.at("camera.cpp").find("return DuduCameraCamera{.x = ") != std::string::npos);
    assert(by_path.at("camera.cpp").find("return Camera{.x = x};") == std::string::npos);
    assert(by_path.at(std::filesystem::path("renderer") / "camera.cpp")
               .find("DuduRendererCameraCamera dudu_renderer_camera_make_camera") !=
           std::string::npos);
    assert(by_path.at(std::filesystem::path("renderer") / "camera.cpp")
               .find("return DuduRendererCameraCamera{.x = x};") != std::string::npos);
    assert(by_path.at("main.cpp").find("dudu_camera_make_camera(1)") != std::string::npos);
    assert(
        by_path.at("main.hpp").find("DuduCameraCamera dudu_main_relay(DuduCameraCamera value);") !=
        std::string::npos);
    assert(
        by_path.at("main.cpp").find("DuduCameraCamera dudu_main_relay(DuduCameraCamera value)") !=
        std::string::npos);
    assert(by_path.at("main.cpp").find("dudu_renderer_camera_make_camera(2)") != std::string::npos);
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

void test_cpp_runtime_support_is_feature_gated() {
    const std::string minimal = emitted_runtime_for_source("minimal", "def main() -> i32:\n"
                                                                      "    return 0\n");
    assert(minimal.find("#include <iostream>") == std::string::npos);
    assert(minimal.find("#include <vector>") == std::string::npos);
    assert(minimal.find("#include <optional>") == std::string::npos);
    assert(minimal.find("#include <variant>") == std::string::npos);
    assert(minimal.find("struct Result") == std::string::npos);
    assert(minimal.find("struct Slice") == std::string::npos);
    assert(minimal.find("namespace shader") == std::string::npos);

    const std::string result =
        emitted_runtime_for_source("result", "def choose() -> Result[i32, i32]:\n"
                                             "    return Ok(1)\n");
    assert(result.find("template <typename T, typename E> struct Result") != std::string::npos);
    assert(result.find("struct Slice") == std::string::npos);
    assert(result.find("#include <vector>") == std::string::npos);

    const std::string collections =
        emitted_runtime_for_source("collections", "def values() -> list[i32]:\n"
                                                  "    return [1, 2, 3]\n");
    assert(collections.find("#include <vector>") != std::string::npos);
    assert(collections.find("struct Result") == std::string::npos);

    const std::string inferred_array =
        emitted_runtime_for_source("inferred_array", "VALUES: array[i32] = [1, 2, 3]\n");
    assert(inferred_array.find("#include <array>") != std::string::npos);

    const std::string indexing = emitted_runtime_for_source(
        "indexing", "def column(values: &array[i32][2, 2]) -> array_view[i32]:\n"
                    "    return values[:, 0]\n");
    assert(indexing.find("struct Slice") != std::string::npos);
    assert(indexing.find("struct ArrayView") != std::string::npos);
    assert(indexing.find("#include <algorithm>") != std::string::npos);

    const std::string print = emitted_runtime_for_source("print", "def main() -> i32:\n"
                                                                  "    print(1)\n"
                                                                  "    return 0\n");
    assert(print.find("#include <iostream>") != std::string::npos);
    assert(print.find("void print") != std::string::npos);

    const std::string assertions = emitted_runtime_for_source("assert", "def verify() -> void:\n"
                                                                        "    assert 1 == 1\n");
    assert(assertions.find("#include <stdexcept>") != std::string::npos);

    const std::string shader =
        emitted_runtime_for_source("shader", "@shader.compute\n"
                                             "def fill() -> i32:\n"
                                             "    return shader.global_id.x\n");
    assert(shader.find("#define DUDU_SHADER_COMPUTE") != std::string::npos);
    assert(shader.find("namespace shader") != std::string::npos);
}

void test_cpp_module_native_imports_stay_local() {
    const std::filesystem::path dir =
        std::filesystem::temp_directory_path() / "dudu_module_native_import_boundary_test";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir / "vendor");
    write_file(dir / "vendor" / "public.hpp", "struct PublicPixel { int value; };\n");
    write_file(dir / "vendor" / "graphics.hpp", "inline int draw_native() { return 1; }\n");
    write_file(dir / "vendor" / "audio.h", "static inline int play_native(void) { return 2; }\n");
    write_file(dir / "vendor" / "inline.hpp",
               "namespace native_api { inline int value() { return 3; } }\n");
    write_file(dir / "graphics.dd", "from cpp.path import vendor/graphics.hpp\n"
                                    "\n"
                                    "def draw() -> i32:\n"
                                    "    return 1\n");
    write_file(dir / "audio.dd", "from c.path import vendor/audio.h\n"
                                 "\n"
                                 "def play() -> i32:\n"
                                 "    return 2\n");
    write_file(dir / "public_native.dd", "from cpp.path import vendor/public.hpp\n"
                                         "\n"
                                         "def inspect(pixel: &const[PublicPixel]) -> i32:\n"
                                         "    return 0\n");
    write_file(dir / "inline_native.dd", "from cpp.path import vendor/inline.hpp\n"
                                         "\n"
                                         "class Box[T]:\n"
                                         "    def value(self) -> i32:\n"
                                         "        return cpp(\"native_api::value()\")\n");
    write_file(dir / "main.dd", "from graphics import draw\n"
                                "from audio import play\n"
                                "from public_native import inspect\n"
                                "from inline_native import Box\n"
                                "\n"
                                "def main() -> i32:\n"
                                "    return draw() + play() - 3\n");

    dudu::ModuleAst module = dudu::load_source_tree(dir / "main.dd");
    for (dudu::ModuleAst& unit : module.module_units) {
        dudu::merge_native_headers(unit, {.config = {}, .source_dir = dir});
    }
    const std::vector<dudu::CppModuleArtifact> artifacts = dudu::emit_cpp_module_artifacts(module);
    std::map<std::filesystem::path, std::string> by_path;
    for (const dudu::CppModuleArtifact& artifact : artifacts) {
        by_path[artifact.path] = artifact.content;
    }

    const std::string& runtime = by_path.at("dudu_runtime.hpp");
    assert(runtime.find("vendor/graphics.hpp") == std::string::npos);
    assert(runtime.find("vendor/audio.h") == std::string::npos);
    assert(by_path.at("graphics.hpp").find("#include \"vendor/graphics.hpp\"") ==
           std::string::npos);
    assert(by_path.at("graphics.cpp").find("#include \"vendor/graphics.hpp\"") !=
           std::string::npos);
    assert(by_path.at("graphics.hpp").find("vendor/audio.h") == std::string::npos);
    assert(by_path.at("audio.hpp").find("#include \"vendor/audio.h\"") == std::string::npos);
    assert(by_path.at("audio.cpp").find("#include \"vendor/audio.h\"") != std::string::npos);
    assert(by_path.at("audio.hpp").find("vendor/graphics.hpp") == std::string::npos);
    assert(by_path.at("main.hpp").find("vendor/graphics.hpp") == std::string::npos);
    assert(by_path.at("main.hpp").find("vendor/audio.h") == std::string::npos);
    assert(by_path.at("public_native.hpp").find("#include \"vendor/public.hpp\"") !=
           std::string::npos);
    assert(by_path.at("public_native.cpp").find("#include \"vendor/public.hpp\"") ==
           std::string::npos);
    assert(by_path.at("inline_native.hpp").find("#include \"vendor/inline.hpp\"") !=
           std::string::npos);
    assert(by_path.at("inline_native.cpp").find("#include \"vendor/inline.hpp\"") ==
           std::string::npos);
}

void test_imported_dudu_api_carries_native_type_support() {
    const std::filesystem::path dir =
        std::filesystem::temp_directory_path() / "dudu_imported_native_type_support_test";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir / "vendor");
    write_file(dir / "vendor" / "worker.hpp", "namespace worker_api {\n"
                                              "class Worker {\n"
                                              "public:\n"
                                              "    void join() {}\n"
                                              "};\n"
                                              "inline int unrelated() { return 1; }\n"
                                              "} // namespace worker_api\n");
    write_file(dir / "workers.dd", "from cpp.path import vendor/worker.hpp\n"
                                   "\n"
                                   "def make_workers() -> list[worker_api.Worker]:\n"
                                   "    return []\n");
    write_file(dir / "main.dd", "from workers import make_workers\n"
                                "\n"
                                "def main() -> i32:\n"
                                "    workers = make_workers()\n"
                                "    workers[0].join()\n"
                                "    return 0\n");

    dudu::ProjectIndexOptions options;
    options.entry_path = dir / "main.dd";
    options.source_dir = dir;
    options.force_module_tree = true;
    options.include_native_headers = true;
    options.check_semantics = true;
    options.semantic_options = {.check_bodies = true};
    const dudu::ProjectIndex index = dudu::ProjectIndex::load(options);
    assert(index.modules().size() == 2);
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
        test_cpp_module_artifacts_preserve_module_boundaries();
        test_cpp_runtime_support_is_feature_gated();
        test_cpp_module_native_imports_stay_local();
        test_imported_dudu_api_carries_native_type_support();
        test_cpp_module_artifacts_use_resolved_dependency_paths();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
