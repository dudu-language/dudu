#include "dudu/codegen/cpp_emit_modules.hpp"
#include "dudu/project/project_config.hpp"
#include "dudu/project/project_index.hpp"

#include <algorithm>
#include <cassert>
#include <filesystem>
#include <string>
#include <string_view>

#ifndef DUDU_REPO_ROOT
#error "DUDU_REPO_ROOT must be defined"
#endif

namespace {

const dudu::ClassDecl& find_class(const dudu::ModuleAst& module, std::string_view name) {
    for (const dudu::ModuleAst& unit : module.module_units) {
        for (const dudu::ClassDecl& klass : unit.classes) {
            if (klass.name == name)
                return klass;
        }
    }
    assert(false && "class is missing");
    return module.classes.front();
}

const dudu::EnumDecl& find_enum(const dudu::ModuleAst& module, std::string_view name) {
    for (const dudu::ModuleAst& unit : module.module_units) {
        for (const dudu::EnumDecl& en : unit.enums) {
            if (en.name == name)
                return en;
        }
    }
    assert(false && "enum is missing");
    return module.enums.front();
}

bool has_method(const dudu::ClassDecl& klass, const std::string& name) {
    return std::any_of(klass.methods.begin(), klass.methods.end(),
                       [&](const dudu::FunctionDecl& method) { return method.name == name; });
}

const dudu::ModuleAst& find_unit(const dudu::ModuleAst& module, std::string_view name) {
    for (const dudu::ModuleAst& unit : module.module_units) {
        if (unit.module_path == name)
            return unit;
    }
    assert(false && "module unit is missing");
    return module.module_units.front();
}

void test_public_macro_packages_expand_through_codegen() {
    const std::filesystem::path root =
        std::filesystem::path(DUDU_REPO_ROOT) / "tests/fixtures/macro_packages";
    const dudu::ProjectConfig config = dudu::parse_project_config(root / "dudu.toml");
    dudu::ProjectIndexOptions options;
    options.entry_path = root / "showcase.dd";
    options.config = config;
    options.source_dir = root;
    options.force_module_tree = true;

    const dudu::ProjectIndex index = dudu::ProjectIndex::load(options);
    assert(index.macro_report().definitions.size() == 9);
    assert(index.macro_report().invocations == 9);
    const auto warning =
        std::find_if(index.macro_report().expansions.begin(), index.macro_report().expansions.end(),
                     [](const auto& expansion) { return expansion.macro_name == "Warn"; });
    assert(warning != index.macro_report().expansions.end());
    assert(warning->expansion.diagnostics.size() == 1);
    assert(warning->expansion.diagnostics.front().severity ==
           dudu::macro::protocol::DiagnosticSeverity::Warning);
    assert(warning->expansion.diagnostics.front().message ==
           "showcase warning from public macro API");

    const dudu::ModuleAst& merged = index.merged_module();
    const dudu::ClassDecl& player = find_class(merged, "Player");
    assert(has_method(player, "debug_field_count"));
    assert(has_method(player, "json_field_count"));
    assert(has_method(player, "reflected_field_count"));
    assert(has_method(player, "runtime_answer"));
    assert(has_method(find_class(merged, "Options"), "argument_count"));
    assert(has_method(find_class(merged, "Packet"), "schema_bits"));

    const dudu::EnumDecl& direction = find_enum(merged, "Direction");
    assert(std::any_of(direction.methods.begin(), direction.methods.end(),
                       [](const auto& method) { return method.name == "variant_count"; }));

    const std::vector<dudu::CppModuleArtifact> artifacts = dudu::emit_cpp_module_artifacts(merged);
    const auto emitted = [&](const std::string& text) {
        return std::any_of(artifacts.begin(), artifacts.end(), [&](const auto& item) {
            return item.content.find(text) != std::string::npos;
        });
    };
    assert(emitted("debug_field_count"));
    assert(emitted("json_field_count"));
    assert(emitted("reflected_field_count"));
    assert(emitted("runtime_answer"));
    assert(emitted("generated_runtime_answer"));
    assert(emitted("argument_count"));
    assert(emitted("schema_bits"));
    assert(emitted("variant_count"));
    assert(emitted("demo_export_count"));
}

void test_imported_class_shapes_include_generated_members() {
    const std::filesystem::path root =
        std::filesystem::path(DUDU_REPO_ROOT) / "tests/fixtures/macro_packages";
    const dudu::ProjectConfig config = dudu::parse_project_config(root / "dudu.toml");
    dudu::ProjectIndexOptions options;
    options.entry_path = root / "imported_generated_members.dd";
    options.config = config;
    options.source_dir = root;
    options.force_module_tree = true;

    const dudu::ProjectIndex index = dudu::ProjectIndex::load(options);
    const dudu::ModuleAst& consumer =
        find_unit(index.merged_module(), "imported_generated_members");
    const auto imported =
        std::find_if(consumer.native_classes.begin(), consumer.native_classes.end(),
                     [](const dudu::ClassDecl& klass) { return klass.name == "Player"; });
    assert(imported != consumer.native_classes.end());
    assert(has_method(*imported, "debug_field_count"));
    assert(has_method(*imported, "json_field_count"));
}

} // namespace

int main() {
    test_public_macro_packages_expand_through_codegen();
    test_imported_class_shapes_include_generated_members();
    return 0;
}
