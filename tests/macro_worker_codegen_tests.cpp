#include "dudu/macro/macro_registry.hpp"
#include "dudu/macro/macro_worker_codegen.hpp"
#include "dudu/project/module_loader.hpp"

#include <cassert>
#include <filesystem>
#include <fstream>
#include <string>

namespace {

void write_file(const std::filesystem::path& path, const std::string& text) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path);
    assert(out);
    out << text;
}

void test_worker_source_uses_stable_entry_points() {
    const std::filesystem::path dir =
        std::filesystem::temp_directory_path() / "dudu_macro_worker_codegen_test";
    std::filesystem::remove_all(dir);
    write_file(dir / "macros.dd", "import dudu.ast as ast\n"
                                  "\n"
                                  "@macro\n"
                                  "def Debug(item: ast.ClassDecl) -> ast.Expansion:\n"
                                  "    return ast.expansion()\n");
    const dudu::ModuleAst module = dudu::load_source_tree(dir / "macros.dd");
    const dudu::macro::Plan plan = dudu::macro::build_plan(module);
    const std::string source =
        dudu::macro::generate_worker_source(plan, {.package = "demo",
                                                   .binary_identity = "identity-1",
                                                   .project_root = dir.string(),
                                                   .module_sources = {},
                                                   .capabilities = {"fs.read=schemas/**", "clock"},
                                                   .non_cacheable_macros = {}});
    assert(source.find("#include \"macros.cpp\"") != std::string::npos);
    assert(source.find(".entry_point = \"macros.Debug\"") != std::string::npos);
    assert(source.find("request.declaration.class_decl") != std::string::npos);
    assert(source.find("dudu_macros_Debug(std::move(input))") != std::string::npos);
    assert(source.find(".cacheable = true") != std::string::npos);
    assert(source.find("CapabilityKind::FsRead") != std::string::npos);
    assert(source.find("\"schemas/**\"") != std::string::npos);
    assert(source.find("CapabilityKind::Clock") != std::string::npos);
}

void test_worker_catalog_contains_helper_schema_and_definition() {
    const std::filesystem::path dir =
        std::filesystem::temp_directory_path() / "dudu_macro_worker_catalog_test";
    std::filesystem::remove_all(dir);
    write_file(dir / "macros.dd", "import dudu.ast as ast\n"
                                  "\n"
                                  "class JsonOptions:\n"
                                  "    skip: bool = False\n"
                                  "\n"
                                  "@macro(attributes=JsonOptions)\n"
                                  "def Json(item: ast.ClassDecl) -> ast.Expansion:\n"
                                  "    return ast.expansion()\n");
    const dudu::ModuleAst module = dudu::load_source_tree(dir / "macros.dd");
    const dudu::macro::Plan plan = dudu::macro::build_plan(module);
    const std::string source =
        dudu::macro::generate_worker_source(plan, {.package = "demo",
                                                   .binary_identity = "identity-2",
                                                   .project_root = dir.string(),
                                                   .module_sources = {},
                                                   .capabilities = {},
                                                   .non_cacheable_macros = {}});
    assert(source.find("descriptor.attribute_schema = decode_class_decl") != std::string::npos);
    assert(source.find("descriptor.definition.file") != std::string::npos);
}

} // namespace

int main() {
    test_worker_source_uses_stable_entry_points();
    test_worker_catalog_contains_helper_schema_and_definition();
    return 0;
}
