#include "dudu/lsp/language_server_presentation_symbols.hpp"

#include "dudu/project/project_index.hpp"

namespace dudu {
namespace {

void add_imported_class(LspPresentationSymbols& context, const std::string& name,
                        const ClassDecl& klass, const ModuleAst& imported) {
    if (name.empty()) {
        return;
    }
    auto copy = std::make_unique<ClassDecl>(klass);
    copy->name = name;
    if (!imported.source_path.empty()) {
        copy->location.file = SourceFileName(imported.source_path.string());
    }
    const ClassDecl* stored = copy.get();
    context.imported_classes.push_back(std::move(copy));
    context.symbols.types.insert(name);
    context.symbols.classes[name] = stored;
}

void add_imported_classes(LspPresentationSymbols& context, const ProjectIndex& index,
                          const ModuleAst& module) {
    for (const ImportDecl& import : module.imports) {
        if (import.kind != ImportKind::Module && import.kind != ImportKind::From) {
            continue;
        }
        const ModuleAst* imported = index.imported_unit(module, import);
        if (imported == nullptr) {
            continue;
        }
        if (import.kind == ImportKind::Module) {
            const std::string prefix = bound_import_name(import);
            for (const ClassDecl& klass : imported->classes) {
                add_imported_class(context, prefix + "." + klass.name, klass, *imported);
            }
            continue;
        }
        for (const ClassDecl& klass : imported->classes) {
            if (klass.name != import.imported_name) {
                continue;
            }
            const std::string bound = bound_import_name(import);
            add_imported_class(context, bound, klass, *imported);
            add_imported_class(context, import.module_path + "." + klass.name, klass, *imported);
            break;
        }
    }
}

} // namespace

LspPresentationSymbols presentation_symbols(const ProjectIndex& index, const ModuleAst& module) {
    LspPresentationSymbols context{.symbols = collect_symbols(module), .imported_classes = {}};
    add_imported_classes(context, index, module);
    return context;
}

} // namespace dudu
