#include "dudu/cpp_expr_slices.hpp"

#include "dudu/array_shape.hpp"
#include "dudu/ast_expr.hpp"
#include "dudu/cpp_expr_emit.hpp"

namespace dudu {
namespace {

bool is_full_slice_expr(const Expr& expr) {
    return expr.kind == ExprKind::Slice && expr.children.size() == 2 &&
           expr_missing(expr.children[0]) && expr_missing(expr.children[1]);
}

} // namespace

std::optional<std::string>
lower_trailing_full_slice_expr(const Expr& base, const Expr& index,
                               const std::vector<std::string>& aliases,
                               const std::map<std::string, std::string>& locals,
                               const Symbols* symbols, const CppEmitOptions& options) {
    if (index.kind != ExprKind::TupleLiteral || index.children.empty() ||
        !is_full_slice_expr(index.children.back())) {
        return std::nullopt;
    }
    std::string row = lower_expr(base, aliases, locals, symbols, options);
    for (size_t i = 0; i + 1 < index.children.size(); ++i) {
        if (index.children[i].kind == ExprKind::Slice) {
            return std::nullopt;
        }
        row += "[" + lower_expr(index.children[i], aliases, locals, symbols, options) + "]";
    }
    return "std::span(&(" + row + ")[0], (" + row + ").size())";
}

std::optional<std::string> lower_trailing_full_slice_expr(
    const Expr& base, const Expr& index, const std::vector<std::string>& aliases,
    const std::map<std::string, std::string>& locals, const Symbols* symbols) {
    return lower_trailing_full_slice_expr(base, index, aliases, locals, symbols, {});
}

std::optional<std::string> lower_column_slice_expr(const Expr& base, const Expr& index,
                                                   const std::vector<std::string>& aliases,
                                                   const std::map<std::string, std::string>& locals,
                                                   const Symbols* symbols,
                                                   const CppEmitOptions& options) {
    (void)symbols;
    if (base.kind != ExprKind::Name || index.kind != ExprKind::TupleLiteral ||
        index.children.size() != 2 || !is_full_slice_expr(index.children[0]) ||
        index.children[1].kind == ExprKind::Slice) {
        return std::nullopt;
    }
    const auto local = locals.find(base.name);
    if (local == locals.end()) {
        return std::nullopt;
    }
    const std::vector<size_t> shape = explicit_array_shape(local->second);
    if (shape.size() != 2) {
        return std::nullopt;
    }
    const std::string lowered_base = lower_expr(base, aliases, locals, symbols, options);
    const std::string column = lower_expr(index.children[1], aliases, locals, symbols, options);
    return "dudu::StridedSpan{" + lowered_base + ".data()->data() + (" + column + "), " +
           std::to_string(shape[0]) + ", " + std::to_string(shape[1]) + "}";
}

std::optional<std::string> lower_column_slice_expr(const Expr& base, const Expr& index,
                                                   const std::vector<std::string>& aliases,
                                                   const std::map<std::string, std::string>& locals,
                                                   const Symbols* symbols) {
    return lower_column_slice_expr(base, index, aliases, locals, symbols, {});
}

std::optional<std::string> lower_channel_slice_expr(const Expr& base, const Expr& index,
                                                    const std::vector<std::string>& aliases,
                                                    const std::map<std::string, std::string>& locals,
                                                    const Symbols* symbols,
                                                    const CppEmitOptions& options) {
    (void)symbols;
    if (base.kind != ExprKind::Name || index.kind != ExprKind::TupleLiteral ||
        index.children.size() != 3 || !is_full_slice_expr(index.children[0]) ||
        !is_full_slice_expr(index.children[1]) || index.children[2].kind == ExprKind::Slice) {
        return std::nullopt;
    }
    const auto local = locals.find(base.name);
    if (local == locals.end()) {
        return std::nullopt;
    }
    const std::vector<size_t> shape = explicit_array_shape(local->second);
    if (shape.size() != 3) {
        return std::nullopt;
    }
    const std::string lowered_base = lower_expr(base, aliases, locals, symbols, options);
    const std::string channel = lower_expr(index.children[2], aliases, locals, symbols, options);
    return "dudu::StridedSpan{" + lowered_base + ".data()->data()->data() + (" + channel +
           "), " + std::to_string(shape[0] * shape[1]) + ", " + std::to_string(shape[2]) + "}";
}

std::optional<std::string> lower_channel_slice_expr(
    const Expr& base, const Expr& index, const std::vector<std::string>& aliases,
    const std::map<std::string, std::string>& locals, const Symbols* symbols) {
    return lower_channel_slice_expr(base, index, aliases, locals, symbols, {});
}

} // namespace dudu
