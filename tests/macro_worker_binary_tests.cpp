#include "dudu/macro/macro_registry.hpp"
#include "dudu/macro/macro_worker_build.hpp"
#include "dudu/macro/macro_worker_process.hpp"
#include "dudu/project/module_loader.hpp"
#include "dudu/sema/sema.hpp"

#include <cassert>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

namespace {

void write_file(const std::filesystem::path& path, const std::string& text) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream output(path);
    assert(output);
    output << text;
}

void test_build_launch_and_cache() {
    const std::filesystem::path root = DUDU_REPO_ROOT;
    const std::filesystem::path build = DUDU_BUILD_DIR;
    const std::filesystem::path dir =
        std::filesystem::temp_directory_path() / "dudu_macro_worker_binary_test";
    std::filesystem::remove_all(dir);
    write_file(dir / "macros.dd", "import dudu.ast as ast\n"
                                  "\n"
                                  "@macro\n"
                                  "def Debug(item: ast.ClassDecl) -> ast.Expansion:\n"
                                  "    return ast.expansion()\n");

    const dudu::ModuleAst module = dudu::load_source_tree(dir / "macros.dd");
    dudu::analyze_module_tree(module);
    const dudu::macro::Plan plan = dudu::macro::build_plan(module);
    const dudu::macro::WorkerBuildOptions options = {.cache_dir = dir / "cache",
                                                     .sdk_cache_dir = dir / "sdk-cache",
                                                     .project_root = dir,
                                                     .package = "fixture",
                                                     .compiler = DUDU_TEST_CXX,
                                                     .cpp_standard = "c++20",
                                                     .toolchain_identity = "test-toolchain",
                                                     .dudu_toolchain_identity = "test-dudu",
                                                     .runtime_include_dirs = {root / "src"},
                                                     .runtime_library =
                                                         build / "libdudu_macro_runtime.a",
                                                     .sdk_bridge_source =
                                                         root / "src/dudu/macro/"
                                                                "macro_sdk_bridge_generated.cpp",
                                                     .include_dirs = {},
                                                     .library_dirs = {},
                                                     .cpp_sources = {},
                                                     .defines = {},
                                                     .compiler_flags = {},
                                                     .libraries = {},
                                                     .linker_flags = {},
                                                     .capabilities = {},
                                                     .non_cacheable_macros = {}};
    const dudu::macro::WorkerBinary first = dudu::macro::build_worker_binary(module, plan, options);
    assert(!first.cache_hit);
    assert(std::filesystem::is_regular_file(first.executable));
    assert(std::filesystem::is_regular_file(first.executable.parent_path() / "objects/unit_0.o"));
    assert(std::filesystem::is_regular_file(first.executable.parent_path() / "objects/unit_1.o"));
    const std::string worker_source =
        [&] {
            std::ifstream input(first.executable.parent_path() / "worker.cpp");
            assert(input);
            return std::string(std::istreambuf_iterator<char>(input), {});
        }();
    assert(worker_source.find("#include \"macros.hpp\"") != std::string::npos);
    assert(worker_source.find("#include \"macros.cpp\"") == std::string::npos);

    dudu::macro::WorkerProcess worker = dudu::macro::WorkerProcess::launch(first.executable);
    const dudu::macro::protocol::MacroCatalog catalog = worker.describe();
    assert(catalog.package == "fixture");
    assert(catalog.macros.size() == 1);
    dudu::macro::protocol::Declaration declaration;
    declaration.kind = dudu::macro::protocol::DeclarationKind::Class;
    declaration.class_decl = dudu::macro::protocol::ClassDecl{.name = "Player"};
    const dudu::macro::protocol::ExpansionResponse response =
        worker.expand({.macro_name = "macros.Debug",
                       .declaration = std::move(declaration),
                       .invocation = {},
                       .compile_values = {}});
    assert(response.expansion.members.empty());
    worker.shutdown();

    const dudu::macro::WorkerBinary second =
        dudu::macro::build_worker_binary(module, plan, options);
    assert(second.cache_hit);
    assert(second.identity == first.identity);
}

} // namespace

int main() {
    test_build_launch_and_cache();
    return 0;
}
