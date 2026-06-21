#include "dudu/cpp_expr_slices.hpp"

#include "dudu/array_shape.hpp"
#include "dudu/ast_expr.hpp"
#include "dudu/cpp_expr_emit.hpp"
#include "dudu/sema_methods.hpp"

#include <numeric>

namespace dudu {
namespace {

bool is_full_slice_expr(const Expr& expr) {
    return expr.kind == ExprKind::Slice && expr.children.size() == 2 &&
           expr_missing(expr.children[0]) && expr_missing(expr.children[1]);
}

bool is_simple_range_slice_expr(const Expr& expr) {
    return expr.kind == ExprKind::Slice && expr.children.size() == 2 &&
           expr.children[1].kind != ExprKind::Slice;
}

std::string lower_slice_bound(const Expr& expr, const std::string& default_value,
                              const std::vector<std::string>& aliases,
                              const CppLocalContext& locals,
                              const std::map<std::string, TypeRef>& local_type_refs,
                              const Symbols* symbols, const CppEmitOptions& options) {
    return expr_missing(expr)
               ? default_value
               : lower_expr(expr, aliases, locals, local_type_refs, symbols, options);
}

std::vector<size_t> local_array_shape(const std::map<std::string, TypeRef>& local_type_refs,
                                      const std::string& name) {
    if (const auto local = local_type_refs.find(name); local != local_type_refs.end()) {
        return explicit_array_shape(local->second);
    }
    return {};
}

TypeRef unwrap_reference_and_const(TypeRef type) {
    while ((type.kind == TypeKind::Reference || type.kind == TypeKind::Const) &&
           type.children.size() == 1) {
        type = type.children.front();
    }
    return type;
}

std::vector<std::string>
local_array_shape_text(const std::map<std::string, TypeRef>& local_type_refs,
                       const std::string& name) {
    if (const auto local = local_type_refs.find(name); local != local_type_refs.end()) {
        return explicit_array_shape_text(unwrap_reference_and_const(local->second));
    }
    return {};
}

std::vector<std::string>
array_shape_text_for_expr(const Expr& expr, const std::map<std::string, TypeRef>& local_type_refs,
                          const Symbols* symbols, const CppLocalContext& locals) {
    if (expr.kind == ExprKind::Name) {
        return local_array_shape_text(local_type_refs, expr.name);
    }
    if (symbols == nullptr) {
        return {};
    }
    const TypeRef type =
        member_expr_type_ref(*symbols, local_type_refs, nullptr, expr, {}, locals.current_class);
    return explicit_array_shape_text(unwrap_reference_and_const(type));
}

std::string nested_array_data_expr(std::string base, size_t rank) {
    base += ".data()";
    for (size_t i = 1; i < rank; ++i) {
        base += "->data()";
    }
    return base;
}

std::string shape_element_count_expr(const std::vector<std::string>& shape) {
    return std::accumulate(shape.begin(), shape.end(), std::string{},
                           [](std::string lhs, const std::string& rhs) {
                               const std::string term = "(" + rhs + ")";
                               return lhs.empty() ? term : lhs + " * " + term;
                           });
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

std::optional<std::string> lower_matrix_row_range_slice_expr(
    const Expr& base, const Expr& index, const std::vector<std::string>& aliases,
    const CppLocalContext& locals, const std::map<std::string, TypeRef>& local_type_refs,
    const Symbols* symbols, const CppEmitOptions& options) {
    if (index.kind != ExprKind::TupleLiteral || index.children.size() != 2 ||
        !is_simple_range_slice_expr(index.children[0]) || !is_full_slice_expr(index.children[1])) {
        return std::nullopt;
    }
    const std::vector<std::string> shape =
        array_shape_text_for_expr(base, local_type_refs, symbols, locals);
    if (shape.size() != 2) {
        return std::nullopt;
    }
    const Expr& row_slice = index.children[0];
    const std::string row_start = lower_slice_bound(row_slice.children[0], "0", aliases, locals,
                                                    local_type_refs, symbols, options);
    const std::string row_end =
        lower_slice_bound(row_slice.children[1], "(" + shape[0] + ")", aliases, locals,
                          local_type_refs, symbols, options);
    const std::string lowered_base =
        lower_expr(base, aliases, locals, local_type_refs, symbols, options);
    return "std::span(" + nested_array_data_expr(lowered_base, shape.size()) + " + ((" + row_start +
           ") * (" + shape[1] + ")), ((" + row_end + ") - (" + row_start + ")) * (" + shape[1] +
           "))";
}

std::optional<std::string> lower_full_multidim_slice_expr(
    const Expr& base, const Expr& index, const std::vector<std::string>& aliases,
    const CppLocalContext& locals, const std::map<std::string, TypeRef>& local_type_refs,
    const Symbols* symbols, const CppEmitOptions& options) {
    if (index.kind != ExprKind::TupleLiteral || index.children.size() < 2) {
        return std::nullopt;
    }
    for (const Expr& child : index.children) {
        if (!is_full_slice_expr(child)) {
            return std::nullopt;
        }
    }
    const std::vector<std::string> shape =
        array_shape_text_for_expr(base, local_type_refs, symbols, locals);
    if (shape.size() != index.children.size()) {
        return std::nullopt;
    }
    const std::string lowered_base =
        lower_expr(base, aliases, locals, local_type_refs, symbols, options);
    return "std::span(" + nested_array_data_expr(lowered_base, shape.size()) + ", " +
           shape_element_count_expr(shape) + ")";
}

} // namespace dudu
