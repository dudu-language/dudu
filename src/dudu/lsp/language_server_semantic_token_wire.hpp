#pragma once

#include "dudu/core/source.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace dudu {

struct SemanticToken {
    int line = 0;
    int column = 0;
    int length = 0;
    int type = 0;
    int modifiers = 0;
};

namespace semantic_token_type {

inline constexpr int namespace_ = 0;
inline constexpr int type = 1;
inline constexpr int class_ = 2;
inline constexpr int enum_ = 3;
inline constexpr int function = 4;
inline constexpr int method = 5;
inline constexpr int variable = 6;
inline constexpr int parameter = 7;
inline constexpr int property = 8;
inline constexpr int enum_member = 9;
inline constexpr int macro = 10;
inline constexpr int keyword = 11;
inline constexpr int number = 12;
inline constexpr int string = 13;
inline constexpr int operator_ = 14;

} // namespace semantic_token_type

namespace semantic_token_modifier {

inline constexpr int declaration = 1;
inline constexpr int readonly = 4;
inline constexpr int static_ = 8;
inline constexpr int native = 16;
inline constexpr int unresolved = 32;

} // namespace semantic_token_modifier

void add_semantic_token(std::vector<SemanticToken>& tokens, const SourceLocation& location,
                        std::string_view text, int type, int modifiers = 0);
void add_native_semantic_token(std::vector<SemanticToken>& tokens,
                               const SourceLocation& location, std::string_view text, int type,
                               int modifiers = 0);
void add_semantic_token_range(std::vector<SemanticToken>& tokens, const SourceRange& range,
                              int type, int modifiers = 0);
std::string_view semantic_token_legend_json();
std::string encode_semantic_tokens(std::vector<SemanticToken> tokens);

} // namespace dudu
