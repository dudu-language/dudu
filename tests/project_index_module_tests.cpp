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

} // namespace

int main() {
    try {
        test_project_index_records_module_graph();
        test_project_index_resolves_path_dependency_modules();
        test_project_index_source_stamps_detect_changed_modules();
        test_selected_module_analysis_falls_back_when_paths_miss();
        test_merged_output_rejects_same_named_module_declarations();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
