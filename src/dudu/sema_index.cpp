#include "dudu/sema_index.hpp"

#include "dudu/array_shape.hpp"
#include "dudu/ast_expr.hpp"
#include "dudu/ast_type.hpp"
#include "dudu/sema_common.hpp"
#include "dudu/sema_index_type_ref.hpp"
#include "dudu/sema_scope.hpp"

#include <optional>
#include <utility>

namespace dudu {
namespace {

TypeRef unwrap_reference_and_const(TypeRef type) {
    while ((type.kind == TypeKind::Reference || type.kind == TypeKind::Const) &&
           type.children.size() == 1) {
        TypeRef child = type.children.front();
        type = std::move(child);
    }
    return type;
}

size_t index_count_from_expr(const Expr& index_expr) {
    if (index_expr.kind == ExprKind::TupleLiteral) {
        return index_expr.children.empty() ? 1 : index_expr.children.size();
    }
    return 1;
}

bool is_slice_expr(const Expr& expr) {
    if (expr.kind == ExprKind::Slice) {
        return true;
    }
    if (expr.kind == ExprKind::TupleLiteral) {
        for (const Expr& child : expr.children) {
            if (is_slice_expr(child)) {
                return true;
            }
        }
    }
    return false;
}

bool has_step_slice(const Expr& expr) {
    if (expr.kind == ExprKind::Slice) {
        for (const Expr& child : expr.children) {
            if (child.kind == ExprKind::Slice || has_step_slice(child)) {
                return true;
            }
        }
    }
    if (expr.kind == ExprKind::TupleLiteral) {
        for (const Expr& child : expr.children) {
            if (has_step_slice(child)) {
                return true;
            }
        }
    }
    return false;
}

std::optional<size_t> trailing_full_slice_prefix_count(const Expr& expr) {
    if (expr.kind != ExprKind::TupleLiteral || expr.children.empty()) {
        return std::nullopt;
    }
    const Expr& tail = expr.children.back();
    if (tail.kind != ExprKind::Slice || tail.children.size() != 2 ||
        !missing_expr(tail.children[0]) || !missing_expr(tail.children[1])) {
        return std::nullopt;
    }
    for (size_t i = 0; i + 1 < expr.children.size(); ++i) {
        if (is_slice_expr(expr.children[i])) {
            return std::nullopt;
        }
    }
    return expr.children.size() - 1;
}

bool is_full_slice_expr(const Expr& expr) {
    return expr.kind == ExprKind::Slice && expr.children.size() == 2 &&
           missing_expr(expr.children[0]) && missing_expr(expr.children[1]);
}

bool is_column_slice_expr(const Expr& expr) {
    return expr.kind == ExprKind::TupleLiteral && expr.children.size() == 2 &&
           is_full_slice_expr(expr.children[0]) && !is_slice_expr(expr.children[1]);
}

bool is_channel_slice_expr(const Expr& expr) {
    return expr.kind == ExprKind::TupleLiteral && expr.children.size() == 3 &&
           is_full_slice_expr(expr.children[0]) && is_full_slice_expr(expr.children[1]) &&
           !is_slice_expr(expr.children[2]);
}

bool is_full_multidim_slice_expr(const Expr& expr) {
    if (expr.kind != ExprKind::TupleLiteral || expr.children.size() < 2) {
        return false;
    }
    for (const Expr& child : expr.children) {
        if (!is_full_slice_expr(child)) {
            return false;
        }
    }
    return true;
}

bool is_simple_range_slice_expr(const Expr& expr) {
    return expr.kind == ExprKind::Slice && expr.children.size() == 2 &&
           expr.children[1].kind != ExprKind::Slice;
}

bool is_leading_range_full_tail_slice_expr(const Expr& expr) {
    if (expr.kind != ExprKind::TupleLiteral || expr.children.size() < 2 ||
        !is_simple_range_slice_expr(expr.children[0])) {
        return false;
    }
    for (size_t i = 1; i < expr.children.size(); ++i) {
        if (!is_full_slice_expr(expr.children[i])) {
            return false;
        }
    }
    return true;
}

} // namespace

TypeRef indexed_value_type_ref(const Symbols& symbols,
                               const std::map<std::string, TypeRef>& local_type_refs,
                               const SourceLocation& location, const std::string& name,
                               const Expr& index_expr, std::string_view unknown_message) {
    const TypeRef type_ref = local_type_ref(local_type_refs, name, location);
    if (type_ref.kind == TypeKind::Unknown) {
        throw CompileError(location, std::string(unknown_message) + name);
    }
    return indexed_type_ref_from_type(symbols, location, type_ref, index_expr, name);
}

TypeRef indexed_type_ref_from_type(const Symbols& symbols, const SourceLocation& location,
                                   const TypeRef& raw_type, const Expr& index_expr,
                                   const std::string& label) {
    const TypeRef unwrapped_type_ref = unwrap_reference_and_const(raw_type);
    if (is_full_multidim_slice_expr(index_expr)) {
        const std::vector<size_t> shape = explicit_array_shape(unwrapped_type_ref);
        const std::vector<std::string> shape_text = explicit_array_shape_text(unwrapped_type_ref);
        if (shape.size() == index_expr.children.size() ||
            shape_text.size() == index_expr.children.size()) {
            return array_element_template_type_ref(location, unwrapped_type_ref, "span");
        }
    }
    if (is_leading_range_full_tail_slice_expr(index_expr)) {
        const std::vector<size_t> shape = explicit_array_shape(unwrapped_type_ref);
        const std::vector<std::string> shape_text = explicit_array_shape_text(unwrapped_type_ref);
        if (shape.size() == index_expr.children.size() ||
            shape_text.size() == index_expr.children.size()) {
            return array_element_template_type_ref(location, unwrapped_type_ref, "span");
        }
    }
    if (is_channel_slice_expr(index_expr)) {
        const std::vector<size_t> shape = explicit_array_shape(unwrapped_type_ref);
        if (shape.size() == 3) {
            return array_element_template_type_ref(location, unwrapped_type_ref, "strided_span");
        }
    }
    if (is_column_slice_expr(index_expr)) {
        const std::vector<size_t> shape = explicit_array_shape(unwrapped_type_ref);
        if (shape.size() == 2) {
            return array_element_template_type_ref(location, unwrapped_type_ref, "strided_span");
        }
    }
    if (!has_step_slice(index_expr)) {
        if (const std::vector<size_t> shape = explicit_array_shape(raw_type); !shape.empty()) {
            if (const auto prefix_count = trailing_full_slice_prefix_count(index_expr)) {
                if (*prefix_count + 1 == shape.size()) {
                    return array_element_template_type_ref(location, raw_type, "span");
                }
            }
        }
    }
    if (const auto indexed_ref = indexed_type_ref_from_type_ref_with_count(
            symbols, location, raw_type, index_count_from_expr(index_expr),
            is_slice_expr(index_expr), has_step_slice(index_expr), label)) {
        return *indexed_ref;
    }
    throw CompileError(location, "cannot index non-container: " + label);
}

} // namespace dudu
