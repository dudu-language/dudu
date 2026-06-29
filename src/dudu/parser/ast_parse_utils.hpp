#pragma once

#include "dudu/core/ast.hpp"

#include <string_view>
#include <vector>

namespace dudu {

struct CommaPart {
    std::string_view text;
    size_t offset = 0;
};

std::string_view trim_view(std::string_view text);
bool is_identifier_continue(char c);
bool is_identifier_start(char c);
bool starts_keyword(std::string_view text, std::string_view keyword);
bool starts_keyword_exact(std::string_view text, std::string_view keyword);
bool starts_statement_keyword(std::string_view text, std::string_view keyword);
std::string trim_string(std::string_view text);
SourceLocation advance_columns(SourceLocation location, size_t columns);
std::string_view trim_view_with_location(std::string_view text, SourceLocation& location);
SourceRange range_for_text(SourceLocation location, std::string_view text);
SourceLocation location_for_piece(SourceLocation base, std::string_view full,
                                  std::string_view piece);
bool is_identifier(std::string_view text);
bool is_integer_literal(std::string_view text);
bool is_float_literal(std::string_view text);
std::string strip_trailing_colon(std::string_view text);
CommaPart trim_comma_part(std::string_view text, size_t offset);
std::vector<CommaPart> split_top_level_comma_parts(std::string_view text);
Expr make_expr(ExprKind kind, std::string_view text, SourceLocation location);
TypeRef make_type(TypeKind kind, std::string_view text, SourceLocation location);
TypeKind wrapper_type_kind(std::string_view name);
std::vector<TypeRef> parse_type_list(std::string_view text, SourceLocation location);

} // namespace dudu
