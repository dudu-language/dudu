#pragma once

#include "dudu/core/ast.hpp"
#include "dudu/core/ast_expr.hpp"
#include "dudu/lsp/language_server_types.hpp"

#include <optional>
#include <string>

namespace dudu {

struct Json;
struct LspPosition;

struct DecoratorSelection {
    std::string name;
    std::string detail;
    SourceLocation name_location;
    SourceRange range;
    std::optional<ExprPath> path;
};

std::optional<DecoratorSelection> decorator_selection_at(const ModuleAst& module,
                                                         const Json* params);
std::optional<Symbol> builtin_decorator_symbol(const DecoratorSelection& selection);

} // namespace dudu
