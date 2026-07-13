#include "dudu/sema/sema.hpp"

#include "dudu/core/naming.hpp"
#include "dudu/core/source.hpp"
#include "dudu/project/build_flags.hpp"
#include "dudu/sema/sema_body.hpp"
#include "dudu/sema/sema_constexpr.hpp"
#include "dudu/sema/sema_context.hpp"
#include "dudu/sema/sema_scan.hpp"
#include "dudu/sema/unsupported.hpp"

#include <iterator>
#include <map>
#include <set>

namespace dudu {
namespace {

void analyze_module_in_tree(const ModuleAst& module, const ModuleAst* module_tree,
                            SemanticOptions options) {
    Symbols symbols = collect_symbols(module);
    symbols.module_tree = module_tree;
    check_build_flags(module);
    check_naming(module);
    check_unsupported_python(module);
    check_declarations(module, symbols);
    check_constexpr_uses(module);
    if (options.check_bodies) {
        check_bodies(module, symbols);
    }
}

std::vector<CompileError> analyze_module_in_tree_collecting(const ModuleAst& module,
                                                            const ModuleAst* module_tree,
                                                            SemanticOptions options) {
    Symbols symbols = collect_symbols(module);
    symbols.module_tree = module_tree;
    try {
        check_build_flags(module);
        check_naming(module);
        check_unsupported_python(module);
        check_declarations(module, symbols);
        check_constexpr_uses(module);
    } catch (const CompileError& error) {
        return {error};
    }
    return options.check_bodies ? check_bodies_collecting(module, symbols)
                                : std::vector<CompileError>{};
}

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
    analyze_module_in_tree(module, nullptr, options);
}

std::vector<CompileError> analyze_module_collecting(const ModuleAst& module,
                                                    SemanticOptions options) {
    return analyze_module_in_tree_collecting(module, nullptr, options);
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
        if (unit.compilation_domain == CompilationDomain::MacroHost &&
            !options.include_macro_host_modules) {
            continue;
        }
        analyze_module_in_tree(unit, &module, options);
    }
}

std::vector<CompileError> analyze_module_tree_collecting(const ModuleAst& module,
                                                         SemanticOptions options) {
    if (module.module_units.empty()) {
        return analyze_module_collecting(module, options);
    }
    std::vector<CompileError> diagnostics;
    for (const ModuleAst& unit : module.module_units) {
        if (unit.compilation_domain == CompilationDomain::MacroHost &&
            !options.include_macro_host_modules) {
            continue;
        }
        std::vector<CompileError> unit_diagnostics =
            analyze_module_in_tree_collecting(unit, &module, options);
        diagnostics.insert(diagnostics.end(), std::make_move_iterator(unit_diagnostics.begin()),
                           std::make_move_iterator(unit_diagnostics.end()));
    }
    return diagnostics;
}

void analyze_module_tree(const ModuleAst& module, const std::vector<std::string>& module_paths,
                         SemanticOptions options) {
    if (module_paths.empty()) {
        return;
    }
    if (module.module_units.empty()) {
        analyze_module(module, options);
        return;
    }
    const std::set<std::string> selected(module_paths.begin(), module_paths.end());
    bool matched_any = false;
    for (const ModuleAst& unit : module.module_units) {
        if (selected.contains(unit.module_path) &&
            (options.include_macro_host_modules ||
             unit.compilation_domain != CompilationDomain::MacroHost)) {
            matched_any = true;
            analyze_module_in_tree(unit, &module, options);
        }
    }
    if (!matched_any) {
        analyze_module_tree(module, options);
    }
}

} // namespace dudu
