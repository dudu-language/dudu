#include "dudu/parser_statement_ops.hpp"

namespace dudu {

bool is_assignment_operator(const Token& token) {
    if (token.kind == TokenKind::Assign) {
        return true;
    }
    return token.kind == TokenKind::Operator && token.text.size() >= 2 &&
           token.text.back() == '=' && token.text != "==" && token.text != "!=" &&
           token.text != "<=" && token.text != ">=";
}

bool is_compound_assignment_operator(const Token& token) {
    return token.kind == TokenKind::Operator && is_assignment_operator(token);
}

std::optional<CompoundAssignOp> compound_assignment_op(std::string_view token_text) {
    if (token_text == "+=") {
        return CompoundAssignOp::Add;
    }
    if (token_text == "-=") {
        return CompoundAssignOp::Sub;
    }
    if (token_text == "*=") {
        return CompoundAssignOp::Mul;
    }
    if (token_text == "/=") {
        return CompoundAssignOp::Div;
    }
    if (token_text == "%=") {
        return CompoundAssignOp::Mod;
    }
    if (token_text == "&=") {
        return CompoundAssignOp::BitAnd;
    }
    if (token_text == "|=") {
        return CompoundAssignOp::BitOr;
    }
    if (token_text == "^=") {
        return CompoundAssignOp::BitXor;
    }
    if (token_text == "<<=") {
        return CompoundAssignOp::ShiftLeft;
    }
    if (token_text == ">>=") {
        return CompoundAssignOp::ShiftRight;
    }
    return std::nullopt;
}

} // namespace dudu
