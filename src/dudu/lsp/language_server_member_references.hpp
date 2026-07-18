#pragma once

#include "dudu/core/source.hpp"
#include "dudu/lsp/language_server_types.hpp"

#include <optional>
#include <string>

namespace dudu {

struct ExprPath;
struct AstSelection;
struct Json;
struct ModuleAst;

struct MemberReferenceTarget {
    std::string name;
    SourceLocation declaration;
};

std::optional<MemberReferenceTarget> member_reference_target_at(const Document& doc,
                                                                const Json* params,
                                                                const AstSelection& selection,
                                                                const ModuleAst* module);
std::vector<ReferenceLocation> member_reference_locations(const ModuleAst& module,
                                                          const Document& doc,
                                                          const MemberReferenceTarget& target);

std::optional<std::string> member_declaration_reference_query_at(const Document& doc,
                                                                 const Json* params,
                                                                 const ModuleAst* module);
std::optional<std::string> enum_value_declaration_reference_query_at(const Document& doc,
                                                                     const Json* params,
                                                                     const ModuleAst* module);
std::optional<std::string> member_use_reference_query_at(const ModuleAst& module,
                                                         const Document& doc, const ExprPath& path,
                                                         const Json* params);

} // namespace dudu
