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

std::optional<size_t> trailing_range_slice_prefix_count(const Expr& expr) {
    if (expr.kind != ExprKind::TupleLiteral || expr.children.empty()) {
        return std::nullopt;
    }
    const Expr& tail = expr.children.back();
    if (!is_simple_range_slice_expr(tail)) {
        return std::nullopt;
    }
    for (size_t i = 0; i + 1 < expr.children.size(); ++i) {
        if (is_slice_expr(expr.children[i])) {
            return std::nullopt;
        }
    }
    return expr.children.size() - 1;
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

bool is_matrix_patch_slice_expr(const Expr& expr) {
    return expr.kind == ExprKind::TupleLiteral && expr.children.size() == 2 &&
           is_simple_range_slice_expr(expr.children[0]) &&
           is_simple_range_slice_expr(expr.children[1]);
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
    if (is_full_multidim_slice_expr(index_expr) ||
        is_leading_range_full_tail_slice_expr(index_expr) ||
        is_matrix_patch_slice_expr(index_expr)) {
        return view_type_ref(location, "strided_span2", *element);
    }
    if (is_column_slice_expr(index_expr) ||
        trailing_full_slice_prefix_count(index_expr).has_value() ||
        trailing_range_slice_prefix_count(index_expr).has_value()) {
        return view_type_ref(location, "strided_span", *element);
    }
    if (is_simple_range_slice_expr(index_expr.children[0]) &&
        !is_slice_expr(index_expr.children[1])) {
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
    const TypeRef unwrapped_type_ref = unwrap_reference_and_const(receiver_type);
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
    if (is_full_multidim_slice_expr(index_expr)) {
        const std::vector<size_t> shape = explicit_array_shape(unwrapped_type_ref);
        const std::vector<std::string> shape_values =
            explicit_array_shape_values(unwrapped_type_ref);
        if (shape.size() == index_expr.children.size() ||
            shape_values.size() == index_expr.children.size()) {
            return array_element_template_type_ref(location, unwrapped_type_ref, "span");
        }
    }
    if (is_leading_range_full_tail_slice_expr(index_expr)) {
        const std::vector<size_t> shape = explicit_array_shape(unwrapped_type_ref);
        const std::vector<std::string> shape_values =
            explicit_array_shape_values(unwrapped_type_ref);
        if (shape.size() == index_expr.children.size() ||
            shape_values.size() == index_expr.children.size()) {
            return array_element_template_type_ref(location, unwrapped_type_ref, "span");
        }
    }
    if (is_matrix_patch_slice_expr(index_expr)) {
        const std::vector<size_t> shape = explicit_array_shape(unwrapped_type_ref);
        const std::vector<std::string> shape_values =
            explicit_array_shape_values(unwrapped_type_ref);
        if (shape.size() == 2 || shape_values.size() == 2) {
            return array_element_template_type_ref(location, unwrapped_type_ref, "strided_span2");
        }
    }
    if (is_channel_slice_expr(index_expr)) {
        const std::vector<size_t> shape = explicit_array_shape(unwrapped_type_ref);
        const std::vector<std::string> shape_values =
            explicit_array_shape_values(unwrapped_type_ref);
        if (shape.size() == 3 || shape_values.size() == 3) {
            return array_element_template_type_ref(location, unwrapped_type_ref, "strided_span");
        }
    }
    if (is_column_slice_expr(index_expr)) {
        const std::vector<size_t> shape = explicit_array_shape(unwrapped_type_ref);
        const std::vector<std::string> shape_values =
            explicit_array_shape_values(unwrapped_type_ref);
        if (shape.size() == 2 || shape_values.size() == 2) {
            return array_element_template_type_ref(location, unwrapped_type_ref, "strided_span");
        }
    }
    if (!has_step_slice(index_expr)) {
        const std::vector<size_t> shape = explicit_array_shape(unwrapped_type_ref);
        const std::vector<std::string> shape_values =
            explicit_array_shape_values(unwrapped_type_ref);
        const auto known_rank = [&](const size_t expected) {
            return shape.size() == expected || shape_values.size() == expected;
        };
        if (!shape.empty() || !shape_values.empty()) {
            if (const auto prefix_count = trailing_full_slice_prefix_count(index_expr)) {
                if (known_rank(*prefix_count + 1)) {
                    return array_element_template_type_ref(location, unwrapped_type_ref, "span");
                }
            }
            if (const auto prefix_count = trailing_range_slice_prefix_count(index_expr)) {
                if (known_rank(*prefix_count + 1)) {
                    return array_element_template_type_ref(location, unwrapped_type_ref, "span");
                }
            }
        }
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
