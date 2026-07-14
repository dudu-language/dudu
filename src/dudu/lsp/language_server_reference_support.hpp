#pragma once

#include "dudu/core/ast.hpp"
#include "dudu/lsp/language_server_navigation.hpp"
#include "dudu/lsp/language_server_symbols.hpp"
#include "dudu/lsp/language_server_types.hpp"

#include <optional>
#include <string>
#include <vector>

namespace dudu {

struct Json;
class ProjectIndex;

bool renameable_symbol_kind(int kind);
std::optional<Symbol> declaration_at_position(const Document& doc, const Json* params,
                                              const std::string& name,
                                              const std::vector<Symbol>& symbols);
bool document_declares_renameable_symbol(const Document& doc, const std::string& name,
                                         const std::vector<Symbol>& symbols);
std::optional<Symbol>
unique_document_declaration_for_references(const Document& doc, const std::string& name,
                                           const ModuleAst* module,
                                           const std::vector<Symbol>& symbols);
std::optional<std::string> native_identity_for_query(const std::vector<Symbol>& symbols,
                                                     const std::string& query);
std::optional<std::string> native_identity_for_selection(const AstSelection& selection,
                                                         const ModuleAst* module,
                                                         const std::vector<Symbol>& symbols,
                                                         const std::string& query,
                                                         const SourceLocation& cursor_location);
std::string dotted_tail(const std::string& query);
const ProjectIndex* document_project_index(const Document& doc, bool include_native);
const ModuleAst* visible_document_unit(const ProjectIndex* index, const Document& doc);
const ModuleAst* workspace_candidate_unit(const ProjectIndex* workspace_index,
                                          const Document& candidate, bool include_native);
const ProjectIndex* workspace_candidate_index(const ProjectIndex* workspace_index,
                                              const Document& candidate, bool include_native);

} // namespace dudu
