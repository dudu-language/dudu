#include "dudu/codegen/cpp_expr_slices.hpp"

#include "dudu/codegen/cpp_expr_emit.hpp"
#include "dudu/core/array_shape.hpp"
#include "dudu/core/ast_expr.hpp"
#include "dudu/core/ast_type.hpp"
#include "dudu/sema/sema_methods.hpp"

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

bool contains_slice_expr(const Expr& expr) {
    if (expr.kind == ExprKind::Slice) {
        return true;
    }
    for (const Expr& child : expr.children) {
        if (contains_slice_expr(child)) {
            return true;
        }
    }
    return false;
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

bool expr_type_is_array_view(const Expr& expr,
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
    return template_type_arg_refs(unwrap_reference_and_const(type), "array_view").size() == 1;
}

bool expr_type_is_fixed_array(const Expr& expr,
                              const std::map<std::string, TypeRef>& local_type_refs,
                              const Symbols* symbols, const CppLocalContext& locals) {
    return !array_shape_values_for_expr(expr, local_type_refs, symbols, locals).empty();
}

std::vector<Expr> index_arg_exprs(const Expr& index) {
    if (index.kind == ExprKind::TupleLiteral) {
        return index.children;
    }
    return {index};
}

std::string lower_slice_spec_expr(const Expr& expr, const std::vector<std::string>& aliases,
                                  const CppLocalContext& locals,
                                  const std::map<std::string, TypeRef>& local_type_refs,
                                  const Symbols* symbols, const CppEmitOptions& options) {
    if (expr.kind == ExprKind::Slice) {
        return "dudu::SliceSpec::range(" +
               lower_slice_value_expr(expr, aliases, locals, local_type_refs, symbols, options) +
               ")";
    }
    return "dudu::SliceSpec::at(" +
           lower_expr(expr, aliases, locals, local_type_refs, symbols, options) + ")";
}

std::string lower_slice_specs(const std::vector<Expr>& args,
                              const std::vector<std::string>& aliases,
                              const CppLocalContext& locals,
                              const std::map<std::string, TypeRef>& local_type_refs,
                              const Symbols* symbols, const CppEmitOptions& options) {
    std::string out = "{";
    for (size_t i = 0; i < args.size(); ++i) {
        if (i > 0) {
            out += ", ";
        }
        out += lower_slice_spec_expr(args[i], aliases, locals, local_type_refs, symbols, options);
    }
    out += "}";
    return out;
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
        return expr_missing(bound)
                   ? std::string{"0"}
                   : lower_expr(bound, aliases, locals, local_type_refs, symbols, options);
    };
    const std::string start = lower_or_zero(*start_expr);
    const std::string end = lower_or_zero(*end_expr);
    const std::string step =
        has_step ? lower_expr(*step_expr, aliases, locals, local_type_refs, symbols, options)
                 : std::string{"1"};
    return "dudu::Slice{" + std::string(has_start ? "true" : "false") + ", " +
           std::string(has_end ? "true" : "false") + ", " +
           std::string(has_step ? "true" : "false") + ", " + start + ", " + end + ", " + step + "}";
}

std::optional<std::string> lower_generic_array_view_index_expr(
    const Expr& base, const Expr& index, const std::vector<std::string>& aliases,
    const CppLocalContext& locals, const std::map<std::string, TypeRef>& local_type_refs,
    const Symbols* symbols, const CppEmitOptions& options) {
    const bool array_view = expr_type_is_array_view(base, local_type_refs, symbols, locals);
    const bool fixed_array = expr_type_is_fixed_array(base, local_type_refs, symbols, locals);
    if (!array_view && !fixed_array) {
        return std::nullopt;
    }

    const std::vector<Expr> args = index_arg_exprs(index);
    const bool has_slice = contains_slice_expr(index);
    const std::string lowered_base =
        lower_expr(base, aliases, locals, local_type_refs, symbols, options);

    if (has_slice) {
        return "dudu::array_view_slice(" + lowered_base + ", " +
               lower_slice_specs(args, aliases, locals, local_type_refs, symbols, options) + ")";
    }
    if (!array_view) {
        return std::nullopt;
    }
    if (args.size() == 1) {
        return lowered_base + "[" +
               lower_expr(args.front(), aliases, locals, local_type_refs, symbols, options) + "]";
    }
    std::string coords = "{";
    for (size_t i = 0; i < args.size(); ++i) {
        if (i > 0) {
            coords += ", ";
        }
        coords += lower_expr(args[i], aliases, locals, local_type_refs, symbols, options);
    }
    coords += "}";
    return lowered_base + ".at(" + coords + ")";
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
