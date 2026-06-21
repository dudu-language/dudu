#include "dudu/cpp_expr_slices.hpp"

#include "dudu/array_shape.hpp"
#include "dudu/ast_expr.hpp"
#include "dudu/cpp_expr_emit.hpp"

#include <numeric>

namespace dudu {
namespace {

bool is_full_slice_expr(const Expr& expr) {
    return expr.kind == ExprKind::Slice && expr.children.size() == 2 &&
           expr_missing(expr.children[0]) && expr_missing(expr.children[1]);
}

std::vector<size_t> local_array_shape(const std::map<std::string, TypeRef>& local_type_refs,
                                      const std::string& name) {
    if (const auto local = local_type_refs.find(name); local != local_type_refs.end()) {
        return explicit_array_shape(local->second);
    }
    return {};
}

std::string nested_array_data_expr(std::string base, size_t rank) {
    base += ".data()";
    for (size_t i = 1; i < rank; ++i) {
        base += "->data()";
    }
    return base;
}

size_t shape_element_count(const std::vector<size_t>& shape) {
    return std::accumulate(shape.begin(), shape.end(), size_t{1},
                           [](size_t lhs, size_t rhs) { return lhs * rhs; });
}

} // namespace

std::optional<std::string> lower_trailing_full_slice_expr(
    const Expr& base, const Expr& index, const std::vector<std::string>& aliases,
    const CppLocalContext& locals, const std::map<std::string, TypeRef>& local_type_refs,
    const Symbols* symbols, const CppEmitOptions& options) {
    if (index.kind != ExprKind::TupleLiteral || index.children.empty() ||
        !is_full_slice_expr(index.children.back())) {
        return std::nullopt;
    }
    std::string row = lower_expr(base, aliases, locals, local_type_refs, symbols, options);
    for (size_t i = 0; i + 1 < index.children.size(); ++i) {
        if (index.children[i].kind == ExprKind::Slice) {
            return std::nullopt;
        }
        row += "[" +
               lower_expr(index.children[i], aliases, locals, local_type_refs, symbols, options) +
               "]";
    }
    return "std::span(&(" + row + ")[0], (" + row + ").size())";
}

std::optional<std::string>
lower_column_slice_expr(const Expr& base, const Expr& index,
                        const std::vector<std::string>& aliases, const CppLocalContext& locals,
                        const std::map<std::string, TypeRef>& local_type_refs,
                        const Symbols* symbols, const CppEmitOptions& options) {
    (void)symbols;
    if (base.kind != ExprKind::Name || index.kind != ExprKind::TupleLiteral ||
        index.children.size() != 2 || !is_full_slice_expr(index.children[0]) ||
        index.children[1].kind == ExprKind::Slice) {
        return std::nullopt;
    }
    const std::vector<size_t> shape = local_array_shape(local_type_refs, base.name);
    if (shape.size() != 2) {
        return std::nullopt;
    }
    const std::string lowered_base =
        lower_expr(base, aliases, locals, local_type_refs, symbols, options);
    const std::string column =
        lower_expr(index.children[1], aliases, locals, local_type_refs, symbols, options);
    return "dudu::StridedSpan{" + lowered_base + ".data()->data() + (" + column + "), " +
           std::to_string(shape[0]) + ", " + std::to_string(shape[1]) + "}";
}

std::optional<std::string>
lower_channel_slice_expr(const Expr& base, const Expr& index,
                         const std::vector<std::string>& aliases, const CppLocalContext& locals,
                         const std::map<std::string, TypeRef>& local_type_refs,
                         const Symbols* symbols, const CppEmitOptions& options) {
    (void)symbols;
    if (base.kind != ExprKind::Name || index.kind != ExprKind::TupleLiteral ||
        index.children.size() != 3 || !is_full_slice_expr(index.children[0]) ||
        !is_full_slice_expr(index.children[1]) || index.children[2].kind == ExprKind::Slice) {
        return std::nullopt;
    }
    const std::vector<size_t> shape = local_array_shape(local_type_refs, base.name);
    if (shape.size() != 3) {
        return std::nullopt;
    }
    const std::string lowered_base =
        lower_expr(base, aliases, locals, local_type_refs, symbols, options);
    const std::string channel =
        lower_expr(index.children[2], aliases, locals, local_type_refs, symbols, options);
    return "dudu::StridedSpan{" + lowered_base + ".data()->data()->data() + (" + channel + "), " +
           std::to_string(shape[0] * shape[1]) + ", " + std::to_string(shape[2]) + "}";
}

std::optional<std::string> lower_full_multidim_slice_expr(
    const Expr& base, const Expr& index, const std::vector<std::string>& aliases,
    const CppLocalContext& locals, const std::map<std::string, TypeRef>& local_type_refs,
    const Symbols* symbols, const CppEmitOptions& options) {
    if (base.kind != ExprKind::Name || index.kind != ExprKind::TupleLiteral ||
        index.children.size() < 2) {
        return std::nullopt;
    }
    for (const Expr& child : index.children) {
        if (!is_full_slice_expr(child)) {
            return std::nullopt;
        }
    }
    const std::vector<size_t> shape = local_array_shape(local_type_refs, base.name);
    if (shape.size() != index.children.size()) {
        return std::nullopt;
    }
    const std::string lowered_base =
        lower_expr(base, aliases, locals, local_type_refs, symbols, options);
    return "std::span(" + nested_array_data_expr(lowered_base, shape.size()) + ", " +
           std::to_string(shape_element_count(shape)) + ")";
}

} // namespace dudu
