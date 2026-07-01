#include "dudu/sema/sema_index.hpp"

#include "dudu/core/array_shape.hpp"
#include "dudu/core/ast_expr.hpp"
#include "dudu/core/ast_type.hpp"
#include "dudu/sema/sema_common.hpp"
#include "dudu/sema/sema_index_type_ref.hpp"
#include "dudu/sema/sema_methods_internal.hpp"
#include "dudu/sema/sema_scope.hpp"

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

bool is_full_slice_expr(const Expr& expr) {
    return expr.kind == ExprKind::Slice && expr.children.size() == 2 &&
           missing_expr(expr.children[0]) && missing_expr(expr.children[1]);
}

bool is_simple_range_slice_expr(const Expr& expr) {
    return expr.kind == ExprKind::Slice && expr.children.size() == 2 &&
           expr.children[1].kind != ExprKind::Slice;
}

std::optional<TypeRef> single_template_child(const TypeRef& type, std::string_view name) {
    const std::vector<TypeRef> args = template_type_arg_refs(type, name);
    if (args.size() == 1) {
        return args.front();
    }
    return std::nullopt;
}

TypeRef view_type_ref(const SourceLocation& location, std::string_view name,
                      const TypeRef& element) {
    TypeRef out;
    out.kind = TypeKind::Template;
    out.name = std::string(name);
    out.children.push_back(element);
    out.location = location;
    return out;
}

std::optional<TypeRef> fixed_array_view_type_ref(const SourceLocation& location,
                                                 const TypeRef& receiver_type,
                                                 const Expr& index_expr) {
    if (!is_slice_expr(index_expr)) {
        return std::nullopt;
    }
    const TypeRef unwrapped = unwrap_reference_and_const(receiver_type);
    const TypeRef element = explicit_array_element_type_ref(unwrapped);
    if (!has_type_ref(element)) {
        return std::nullopt;
    }
    const std::vector<size_t> shape = explicit_array_shape(unwrapped);
    const std::vector<std::string> shape_values = explicit_array_shape_values(unwrapped);
    const size_t rank = !shape.empty() ? shape.size() : shape_values.size();
    if (rank == 0 || index_count_from_expr(index_expr) > rank) {
        return std::nullopt;
    }
    return view_type_ref(location, "array_view", element);
}

std::optional<TypeRef> indexed_array_view_type_ref(const SourceLocation& location,
                                                   const TypeRef& receiver_type,
                                                   const Expr& index_expr) {
    const TypeRef unwrapped = unwrap_reference_and_const(receiver_type);
    const std::vector<TypeRef> args = template_type_arg_refs(unwrapped, "array_view");
    if (args.size() != 1) {
        return std::nullopt;
    }
    if (is_slice_expr(index_expr)) {
        return view_type_ref(location, "array_view", args.front());
    }
    return args.front();
}

std::optional<TypeRef> indexed_strided_span2_type_ref(const SourceLocation& location,
                                                      const TypeRef& receiver_type,
                                                      const Expr& index_expr) {
    const std::optional<TypeRef> element =
        single_template_child(unwrap_reference_and_const(receiver_type), "strided_span2");
    if (!element || index_expr.kind != ExprKind::TupleLiteral || index_expr.children.size() != 2) {
        return std::nullopt;
    }
    const Expr& row = index_expr.children[0];
    const Expr& col = index_expr.children[1];
    if ((is_full_slice_expr(row) && is_full_slice_expr(col)) ||
        (is_simple_range_slice_expr(row) && is_full_slice_expr(col)) ||
        (is_simple_range_slice_expr(row) && is_simple_range_slice_expr(col))) {
        return view_type_ref(location, "strided_span2", *element);
    }
    if ((is_full_slice_expr(row) && !is_slice_expr(col)) ||
        (!is_slice_expr(row) && is_full_slice_expr(col)) ||
        (!is_slice_expr(row) && is_simple_range_slice_expr(col)) ||
        (is_simple_range_slice_expr(row) && !is_slice_expr(col))) {
        return view_type_ref(location, "strided_span", *element);
    }
    return std::nullopt;
}

} // namespace

IndexOperatorTarget index_operator_target(const Expr& receiver) {
    return IndexOperatorTarget{
        .receiver = &receiver, .read_operator = "[]", .write_operator = "[]="};
}

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
                                   const TypeRef& receiver_type, const Expr& index_expr,
                                   const std::string& label) {
    if (const std::optional<TypeRef> array_view_index_type =
            indexed_array_view_type_ref(location, receiver_type, index_expr)) {
        return *array_view_index_type;
    }
    if (const std::optional<TypeRef> array_view_type =
            fixed_array_view_type_ref(location, receiver_type, index_expr)) {
        return *array_view_type;
    }
    if (const std::optional<TypeRef> span2_type =
            indexed_strided_span2_type_ref(location, receiver_type, index_expr)) {
        return *span2_type;
    }
    if (const auto indexed_ref = indexed_type_ref_from_type_ref_with_count(
            symbols, location, receiver_type, index_count_from_expr(index_expr),
            is_slice_expr(index_expr), has_step_slice(index_expr), label)) {
        return *indexed_ref;
    }
    if (class_for_receiver_type(symbols, receiver_type) != nullptr) {
        throw CompileError(location,
                           "no matching @operator(\"[]\") for indexed access to " + label);
    }
    throw CompileError(location, "cannot index non-container: " + label);
}

} // namespace dudu
