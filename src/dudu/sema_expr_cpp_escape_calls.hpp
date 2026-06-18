#pragma once

#include "dudu/ast.hpp"
#include "dudu/source.hpp"

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace dudu {

struct EscapeCall {
    std::string callee;
    Expr callee_expr;
    std::vector<Expr> args;
};

struct EscapeMemberCall {
    std::string receiver;
    std::string method;
    Expr receiver_expr;
};

std::optional<EscapeCall> parsed_escape_call(const Expr& parsed);
std::optional<EscapeCall> escape_call_from_text(const std::string& expr, size_t open,
                                                SourceLocation location);
std::optional<EscapeMemberCall> parsed_member_call(const EscapeCall& call);

} // namespace dudu
