#pragma once

#include "dudu/core/ast.hpp"
#include "dudu/core/ast_expr.hpp"
#include "dudu/core/source.hpp"
#include "dudu/lsp/language_server_types.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace dudu {

struct Json;
struct Expr;

struct LspPosition {
    int line = 0;
    int character = 0;
};

struct AstSelection {
    std::optional<std::string> symbol;
    std::optional<std::string> symbol_path;
    std::optional<ExprPath> expr_path;
    std::optional<Expr> operator_expr;
    std::optional<Expr> call_expr;
    bool call_callee = false;
};

std::string range_json(const SourceLocation& location);
std::string range_json(const SourceRange& range);
std::string range_json(int line, int start_character, int end_character);
std::string range_json(int start_line, int start_character, int end_line, int end_character);

LspPosition lsp_position(const Json* params);
std::string location_json(const std::string& uri, const std::string& range);
std::string uri_for_location(const SourceLocation& location, const Document& doc);
std::string file_uri(const std::filesystem::path& path);
SourceLocation expr_name_location(const Expr& expr);

AstSelection ast_selection_at(const ModuleAst& module, const Json* params);
AstSelection ast_selection_at(const ModuleAst& module, LspPosition position);
bool symbol_matches(const std::string& symbol, const std::string& query);
bool identifier_char(char c);
bool valid_identifier(const std::string& value);

bool same_path(const std::filesystem::path& lhs, const std::filesystem::path& rhs);
bool skip_workspace_dir(const std::string& name);

std::string lower_copy(std::string value);

} // namespace dudu
