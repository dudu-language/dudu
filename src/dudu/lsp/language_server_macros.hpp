#pragma once

#include "dudu/lsp/language_server_types.hpp"

#include <optional>
#include <string>
#include <vector>

namespace dudu {

struct Json;
struct ModuleAst;
class ProjectIndex;

enum class MacroEditorSelectionKind {
    Macro,
    HelperOption,
};

struct MacroEditorSelection {
    MacroEditorSelectionKind kind = MacroEditorSelectionKind::Macro;
    std::string reference;
    std::string option;
    SourceLocation location;
};

struct MacroEditorOption {
    std::string name;
    std::string type;
    std::string documentation;
    bool required = true;
};

struct MacroEditorCall {
    std::string name;
    std::string signature;
    std::string documentation;
    std::vector<MacroEditorOption> options;
};

struct MacroReferenceTarget {
    std::string identity;
    std::string name;
};

std::optional<MacroEditorSelection> macro_selection_at(const ModuleAst& module, const Json* params);
std::optional<Symbol> macro_symbol_for_reference(const ProjectIndex& index,
                                                 const ModuleAst& current,
                                                 const MacroEditorSelection& selection);
bool macro_reference_resolves(const ProjectIndex& index, const ModuleAst& current,
                              std::string_view reference);
std::optional<MacroEditorCall> macro_call_for_reference(const ProjectIndex& index,
                                                        const ModuleAst& current,
                                                        std::string_view reference);
std::optional<MacroReferenceTarget>
macro_reference_target_at(const ProjectIndex& index, const ModuleAst& current, const Json* params);
std::vector<ReferenceLocation> macro_reference_locations(const ProjectIndex& index,
                                                         const ModuleAst& current,
                                                         const Document& document,
                                                         std::string_view identity);
Symbol with_macro_generated_origin(const ModuleAst& module, std::string_view reference,
                                   Symbol symbol);
std::optional<Symbol> macro_generated_symbol_for_reference(const ModuleAst& module,
                                                           std::string_view reference);
std::optional<SourceLocation> macro_generated_definition_location(const ModuleAst& module,
                                                                  std::string_view reference);

} // namespace dudu
