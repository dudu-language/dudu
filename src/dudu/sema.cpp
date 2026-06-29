#include "dudu/sema.hpp"

#include "dudu/build_flags.hpp"
#include "dudu/naming.hpp"
#include "dudu/sema_body.hpp"
#include "dudu/sema_constexpr.hpp"
#include "dudu/sema_context.hpp"
#include "dudu/sema_scan.hpp"
#include "dudu/source.hpp"
#include "dudu/unsupported.hpp"

#include <map>

namespace dudu {
namespace {

void add_direct_name(std::map<std::string, std::pair<std::string, SourceLocation>>& names,
                     const std::string& module_path, const std::string& name,
                     const SourceLocation& location) {
    if (const auto found = names.find(name); found != names.end()) {
        if (found->second.first == module_path) {
            return;
        }
        throw CompileError(
            location, "merged C++ output cannot combine Dudu modules that both declare '" + name +
                          "'; use `dudu build` "
                          "or emit per-module artifacts with `duc emit-modules`");
    }
    names.emplace(name, std::pair{module_path, location});
}

} // namespace

void analyze_module(const ModuleAst& module, SemanticOptions options) {
    const Symbols symbols = collect_symbols(module);
    check_build_flags(module);
    check_naming(module);
    check_unsupported_python(module);
    check_declarations(module, symbols);
    check_constexpr_uses(module);
    if (options.check_bodies) {
        check_bodies(module, symbols);
    }
}

void reject_merged_output_module_conflicts(const ModuleAst& module) {
    if (module.module_units.empty()) {
        return;
    }
    std::map<std::string, std::pair<std::string, SourceLocation>> names;
    for (const ModuleAst& unit : module.module_units) {
        for (const TypeAliasDecl& alias : unit.aliases) {
            add_direct_name(names, unit.module_path, alias.name, alias.location);
        }
        for (const EnumDecl& en : unit.enums) {
            add_direct_name(names, unit.module_path, en.name, en.location);
        }
        for (const ClassDecl& klass : unit.classes) {
            add_direct_name(names, unit.module_path, klass.name, klass.location);
        }
        for (const ConstDecl& constant : unit.constants) {
            add_direct_name(names, unit.module_path, constant.name, constant.location);
        }
        for (const FunctionDecl& fn : unit.functions) {
            add_direct_name(names, unit.module_path, fn.name, fn.location);
        }
    }
}

void analyze_module_tree(const ModuleAst& module, SemanticOptions options) {
    if (module.module_units.empty()) {
        analyze_module(module, options);
        return;
    }
    for (const ModuleAst& unit : module.module_units) {
        analyze_module(unit, options);
    }
}

} // namespace dudu
