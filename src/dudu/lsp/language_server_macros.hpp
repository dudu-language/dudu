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

std::optional<MacroEditorSelection> macro_selection_at(const ModuleAst& module, const Json* params);
std::optional<Symbol> macro_symbol_for_reference(const ProjectIndex& index,
                                                 const ModuleAst& current,
                                                 const MacroEditorSelection& selection);
bool macro_reference_resolves(const ProjectIndex& index, const ModuleAst& current,
                              std::string_view reference);
std::optional<MacroEditorCall> macro_call_for_reference(const ProjectIndex& index,
                                                        const ModuleAst& current,
                                                        std::string_view reference);

} // namespace dudu
