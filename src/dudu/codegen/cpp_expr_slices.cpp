#include "dudu/codegen/cpp_expr_slices.hpp"

#include "dudu/core/array_shape.hpp"
#include "dudu/core/ast_expr.hpp"
#include "dudu/core/ast_type.hpp"
#include "dudu/codegen/cpp_expr_emit.hpp"
#include "dudu/sema/sema_methods.hpp"

#include <numeric>
#include <utility>

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

bool is_slice_expr(const Expr& expr) {
    return expr.kind == ExprKind::Slice;
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

TypeRef unwrap_reference_and_const(TypeRef type) {
    while ((type.kind == TypeKind::Reference || type.kind == TypeKind::Const) &&
           type.children.size() == 1) {
        TypeRef child = type.children.front();
        type = std::move(child);
    }
    return type;
}

std::vector<std::string>
local_array_shape_values(const std::map<std::string, TypeRef>& local_type_refs,
                         const std::string& name) {
    if (const auto local = local_type_refs.find(name); local != local_type_refs.end()) {
        return explicit_array_shape_values(unwrap_reference_and_const(local->second));
    }
    return {};
}

std::vector<std::string>
array_shape_values_for_expr(const Expr& expr, const std::map<std::string, TypeRef>& local_type_refs,
                            const Symbols* symbols, const CppLocalContext& locals) {
    if (expr.kind == ExprKind::Name) {
        return local_array_shape_values(local_type_refs, expr.name);
    }
    if (symbols == nullptr) {
        return {};
    }
    const TypeRef type =
        member_expr_type_ref(*symbols, local_type_refs, nullptr, expr, {}, locals.current_class);
    return explicit_array_shape_values(unwrap_reference_and_const(type));
}

bool expr_type_is_strided_span2(const Expr& expr,
                                const std::map<std::string, TypeRef>& local_type_refs,
                                const Symbols* symbols, const CppLocalContext& locals) {
    TypeRef type;
    if (expr.kind == ExprKind::Name) {
        if (const auto local = local_type_refs.find(expr.name); local != local_type_refs.end()) {
            type = local->second;
        }
    } else if (symbols != nullptr) {
        type = member_expr_type_ref(*symbols, local_type_refs, nullptr, expr, {},
                                    locals.current_class);
    }
    return template_type_arg_refs(unwrap_reference_and_const(type), "strided_span2").size() == 1;
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

std::string lower_slice_value_expr(const Expr& expr, const std::vector<std::string>& aliases,
                                   const CppLocalContext& locals,
                                   const std::map<std::string, TypeRef>& local_type_refs,
                                   const Symbols* symbols, const CppEmitOptions& options) {
    if (expr.kind != ExprKind::Slice || expr.children.size() != 2) {
        throw CompileError(expr.location, "malformed slice expression");
    }
    const Expr* start_expr = &expr.children[0];
    const Expr* end_expr = &expr.children[1];
    const Expr* step_expr = nullptr;
    if (end_expr->kind == ExprKind::Slice && end_expr->children.size() == 2) {
        step_expr = &end_expr->children[1];
        end_expr = &end_expr->children[0];
    }
    const bool has_start = !expr_missing(*start_expr);
    const bool has_end = !expr_missing(*end_expr);
    const bool has_step = step_expr != nullptr && !expr_missing(*step_expr);
    const auto lower_or_zero = [&](const Expr& bound) {
        return expr_missing(bound) ? std::string{"0"}
                                   : lower_expr(bound, aliases, locals, local_type_refs, symbols,
                                                options);
    };
    const std::string start = lower_or_zero(*start_expr);
    const std::string end = lower_or_zero(*end_expr);
    const std::string step =
        has_step ? lower_expr(*step_expr, aliases, locals, local_type_refs, symbols, options)
                 : std::string{"1"};
    return "dudu::Slice{" + std::string(has_start ? "true" : "false") + ", " +
           std::string(has_end ? "true" : "false") + ", " +
           std::string(has_step ? "true" : "false") + ", " + start + ", " + end + ", " + step +
           "}";
}

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

std::optional<std::string> lower_trailing_range_slice_expr(
    const Expr& base, const Expr& index, const std::vector<std::string>& aliases,
    const CppLocalContext& locals, const std::map<std::string, TypeRef>& local_type_refs,
    const Symbols* symbols, const CppEmitOptions& options) {
    if (index.kind != ExprKind::TupleLiteral || index.children.empty() ||
        !is_simple_range_slice_expr(index.children.back())) {
        return std::nullopt;
    }
    for (size_t i = 0; i + 1 < index.children.size(); ++i) {
        if (index.children[i].kind == ExprKind::Slice) {
            return std::nullopt;
        }
    }
    const std::vector<std::string> shape =
        array_shape_values_for_expr(base, local_type_refs, symbols, locals);
    if (shape.size() != index.children.size()) {
        return std::nullopt;
    }
    std::string row = lower_expr(base, aliases, locals, local_type_refs, symbols, options);
    for (size_t i = 0; i + 1 < index.children.size(); ++i) {
        row += "[" +
               lower_expr(index.children[i], aliases, locals, local_type_refs, symbols, options) +
               "]";
    }
    const Expr& tail = index.children.back();
    const std::string start = lower_slice_bound(tail.children[0], "0", aliases, locals,
                                                local_type_refs, symbols, options);
    const std::string end = lower_slice_bound(tail.children[1], "(" + shape.back() + ")", aliases,
                                              locals, local_type_refs, symbols, options);
    return "std::span(&(" + row + ")[" + start + "], (" + end + ") - (" + start + "))";
}

std::optional<std::string>
lower_column_slice_expr(const Expr& base, const Expr& index,
                        const std::vector<std::string>& aliases, const CppLocalContext& locals,
                        const std::map<std::string, TypeRef>& local_type_refs,
                        const Symbols* symbols, const CppEmitOptions& options) {
    if (index.kind != ExprKind::TupleLiteral || index.children.size() != 2 ||
        !is_full_slice_expr(index.children[0]) || index.children[1].kind == ExprKind::Slice) {
        return std::nullopt;
    }
    const std::vector<std::string> shape =
        array_shape_values_for_expr(base, local_type_refs, symbols, locals);
    if (shape.size() != 2) {
        return std::nullopt;
    }
    const std::string lowered_base =
        lower_expr(base, aliases, locals, local_type_refs, symbols, options);
    const std::string column =
        lower_expr(index.children[1], aliases, locals, local_type_refs, symbols, options);
    return "dudu::StridedSpan{" + lowered_base + ".data()->data() + (" + column + "), " + shape[0] +
           ", " + shape[1] + "}";
}

std::optional<std::string>
lower_channel_slice_expr(const Expr& base, const Expr& index,
                         const std::vector<std::string>& aliases, const CppLocalContext& locals,
                         const std::map<std::string, TypeRef>& local_type_refs,
                         const Symbols* symbols, const CppEmitOptions& options) {
    if (index.kind != ExprKind::TupleLiteral || index.children.size() != 3 ||
        !is_full_slice_expr(index.children[0]) || !is_full_slice_expr(index.children[1]) ||
        index.children[2].kind == ExprKind::Slice) {
        return std::nullopt;
    }
    const std::vector<std::string> shape =
        array_shape_values_for_expr(base, local_type_refs, symbols, locals);
    if (shape.size() != 3) {
        return std::nullopt;
    }
    const std::string lowered_base =
        lower_expr(base, aliases, locals, local_type_refs, symbols, options);
    const std::string channel =
        lower_expr(index.children[2], aliases, locals, local_type_refs, symbols, options);
    return "dudu::StridedSpan{" + lowered_base + ".data()->data()->data() + (" + channel + "), " +
           "((" + shape[0] + ") * (" + shape[1] + ")), " + shape[2] + "}";
}

std::optional<std::string> lower_leading_range_full_tail_slice_expr(
    const Expr& base, const Expr& index, const std::vector<std::string>& aliases,
    const CppLocalContext& locals, const std::map<std::string, TypeRef>& local_type_refs,
    const Symbols* symbols, const CppEmitOptions& options) {
    if (index.kind != ExprKind::TupleLiteral || index.children.size() < 2 ||
        !is_simple_range_slice_expr(index.children[0])) {
        return std::nullopt;
    }
    for (size_t i = 1; i < index.children.size(); ++i) {
        if (!is_full_slice_expr(index.children[i])) {
            return std::nullopt;
        }
    }
    const std::vector<std::string> shape =
        array_shape_values_for_expr(base, local_type_refs, symbols, locals);
    if (shape.size() != index.children.size()) {
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
    const std::string trailing_count = shape_element_count_expr({shape.begin() + 1, shape.end()});
    return "std::span(" + nested_array_data_expr(lowered_base, shape.size()) + " + ((" + row_start +
           ") * (" + trailing_count + ")), ((" + row_end + ") - (" + row_start + ")) * (" +
           trailing_count + "))";
}

std::optional<std::string> lower_matrix_patch_slice_expr(
    const Expr& base, const Expr& index, const std::vector<std::string>& aliases,
    const CppLocalContext& locals, const std::map<std::string, TypeRef>& local_type_refs,
    const Symbols* symbols, const CppEmitOptions& options) {
    if (index.kind != ExprKind::TupleLiteral || index.children.size() != 2 ||
        !is_simple_range_slice_expr(index.children[0]) ||
        !is_simple_range_slice_expr(index.children[1])) {
        return std::nullopt;
    }
    const std::vector<std::string> shape =
        array_shape_values_for_expr(base, local_type_refs, symbols, locals);
    if (shape.size() != 2) {
        return std::nullopt;
    }
    const Expr& row_slice = index.children[0];
    const Expr& col_slice = index.children[1];
    const std::string row_start = lower_slice_bound(row_slice.children[0], "0", aliases, locals,
                                                    local_type_refs, symbols, options);
    const std::string row_end =
        lower_slice_bound(row_slice.children[1], "(" + shape[0] + ")", aliases, locals,
                          local_type_refs, symbols, options);
    const std::string col_start = lower_slice_bound(col_slice.children[0], "0", aliases, locals,
                                                    local_type_refs, symbols, options);
    const std::string col_end =
        lower_slice_bound(col_slice.children[1], "(" + shape[1] + ")", aliases, locals,
                          local_type_refs, symbols, options);
    const std::string lowered_base =
        lower_expr(base, aliases, locals, local_type_refs, symbols, options);
    return "dudu::StridedSpan2{" + nested_array_data_expr(lowered_base, shape.size()) + " + ((" +
           row_start + ") * (" + shape[1] + ")) + (" + col_start + "), (" + row_end + ") - (" +
           row_start + "), (" + col_end + ") - (" + col_start + "), " + shape[1] + "}";
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
        array_shape_values_for_expr(base, local_type_refs, symbols, locals);
    if (shape.size() != index.children.size()) {
        return std::nullopt;
    }
    const std::string lowered_base =
        lower_expr(base, aliases, locals, local_type_refs, symbols, options);
    return "std::span(" + nested_array_data_expr(lowered_base, shape.size()) + ", " +
           shape_element_count_expr(shape) + ")";
}

std::optional<std::string> lower_strided_span2_slice_expr(
    const Expr& base, const Expr& index, const std::vector<std::string>& aliases,
    const CppLocalContext& locals, const std::map<std::string, TypeRef>& local_type_refs,
    const Symbols* symbols, const CppEmitOptions& options) {
    if (index.kind != ExprKind::TupleLiteral || index.children.size() != 2 ||
        !expr_type_is_strided_span2(base, local_type_refs, symbols, locals)) {
        return std::nullopt;
    }
    const Expr& row = index.children[0];
    const Expr& col = index.children[1];
    const std::string lowered_base =
        lower_expr(base, aliases, locals, local_type_refs, symbols, options);
    const auto bound = [&](const Expr& slice, const size_t child,
                           const std::string& default_value) {
        return lower_slice_bound(slice.children[child], default_value, aliases, locals,
                                 local_type_refs, symbols, options);
    };
    if (is_full_slice_expr(row) && is_full_slice_expr(col)) {
        return lowered_base;
    }
    if (!row.children.empty() && is_simple_range_slice_expr(row) && is_full_slice_expr(col)) {
        const std::string start = bound(row, 0, "0");
        const std::string end = bound(row, 1, "(" + lowered_base + ").rows");
        return "dudu::StridedSpan2{" + lowered_base + ".data + (" + start + ") * (" + lowered_base +
               ").row_stride, (" + end + ") - (" + start + "), (" + lowered_base + ").cols, (" +
               lowered_base + ").row_stride}";
    }
    if (is_full_slice_expr(row) && !is_slice_expr(col)) {
        const std::string col_index =
            lower_expr(col, aliases, locals, local_type_refs, symbols, options);
        return "dudu::StridedSpan{" + lowered_base + ".data + (" + col_index + "), (" +
               lowered_base + ").rows, (" + lowered_base + ").row_stride}";
    }
    if (!is_slice_expr(row) && is_full_slice_expr(col)) {
        const std::string row_index =
            lower_expr(row, aliases, locals, local_type_refs, symbols, options);
        return "(" + lowered_base + ")[(" + row_index + ")]";
    }
    if (!is_slice_expr(row) && is_simple_range_slice_expr(col)) {
        const std::string row_index =
            lower_expr(row, aliases, locals, local_type_refs, symbols, options);
        const std::string start = bound(col, 0, "0");
        const std::string end = bound(col, 1, "(" + lowered_base + ").cols");
        return "dudu::StridedSpan{" + lowered_base + ".data + (" + row_index + ") * (" +
               lowered_base + ").row_stride + (" + start + "), (" + end + ") - (" + start + "), 1}";
    }
    if (is_simple_range_slice_expr(row) && !is_slice_expr(col)) {
        const std::string start = bound(row, 0, "0");
        const std::string end = bound(row, 1, "(" + lowered_base + ").rows");
        const std::string col_index =
            lower_expr(col, aliases, locals, local_type_refs, symbols, options);
        return "dudu::StridedSpan{" + lowered_base + ".data + (" + start + ") * (" + lowered_base +
               ").row_stride + (" + col_index + "), (" + end + ") - (" + start + "), (" +
               lowered_base + ").row_stride}";
    }
    if (is_simple_range_slice_expr(row) && is_simple_range_slice_expr(col)) {
        const std::string row_start = bound(row, 0, "0");
        const std::string row_end = bound(row, 1, "(" + lowered_base + ").rows");
        const std::string col_start = bound(col, 0, "0");
        const std::string col_end = bound(col, 1, "(" + lowered_base + ").cols");
        return "dudu::StridedSpan2{" + lowered_base + ".data + (" + row_start + ") * (" +
               lowered_base + ").row_stride + (" + col_start + "), (" + row_end + ") - (" +
               row_start + "), (" + col_end + ") - (" + col_start + "), (" + lowered_base +
               ").row_stride}";
    }
    return std::nullopt;
}

} // namespace dudu
