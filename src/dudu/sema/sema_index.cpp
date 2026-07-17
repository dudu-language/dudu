#include "dudu/sema/sema_index.hpp"

#include "dudu/core/array_shape.hpp"
#include "dudu/core/ast_expr.hpp"
#include "dudu/core/ast_type.hpp"
#include "dudu/core/shape_value_expr.hpp"
#include "dudu/native/native_signature_match.hpp"
#include "dudu/sema/sema_common.hpp"
#include "dudu/sema/sema_index_type_ref.hpp"
#include "dudu/sema/sema_methods.hpp"
#include "dudu/sema/sema_methods_internal.hpp"
#include "dudu/sema/sema_native.hpp"
#include "dudu/sema/sema_scope.hpp"

#include <algorithm>
#include <exception>
#include <optional>
#include <sstream>
#include <utility>
#include <vector>

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

size_t consumed_axis_count_from_expr(const Expr& index_expr) {
    if (index_expr.kind == ExprKind::TupleLiteral) {
        size_t count = 0;
        for (const Expr& child : index_expr.children) {
            count += consumed_axis_count_from_expr(child);
        }
        return count;
    }
    return (index_expr.kind == ExprKind::Ellipsis || index_expr.kind == ExprKind::NewAxis) ? 0 : 1;
}

bool is_view_index_expr(const Expr& expr) {
    if (expr.kind == ExprKind::Slice || expr.kind == ExprKind::Ellipsis ||
        expr.kind == ExprKind::NewAxis) {
        return true;
    }
    if (expr.kind == ExprKind::TupleLiteral) {
        for (const Expr& child : expr.children) {
            if (is_view_index_expr(child)) {
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

TypeRef view_type_ref(const SourceLocation& location, std::string_view name, const TypeRef& element,
                      const std::vector<TypeRef>& shape = {}) {
    TypeRef base;
    base.kind = TypeKind::Template;
    base.name = std::string(name);
    base.children.push_back(element);
    base.location = location;
    if (shape.empty()) {
        return base;
    }

    TypeRef out;
    out.kind = TypeKind::Shaped;
    out.children.push_back(std::move(base));
    out.children.insert(out.children.end(), shape.begin(), shape.end());
    out.location = location;
    std::ostringstream value;
    for (size_t i = 0; i < shape.size(); ++i) {
        if (i > 0) {
            value << ", ";
        }
        value << substitute_type_ref_text(shape[i], {});
    }
    out.value = value.str();
    return out;
}

TypeRef shape_value_type_ref(const SourceLocation& location, std::string value) {
    TypeRef out;
    out.kind = TypeKind::Value;
    out.value = shape_value_expr_substitute(std::move(value), {});
    out.location = location;
    out.range.start = location;
    out.range.end = location;
    return out;
}

TypeRef dyn_shape_ref(const SourceLocation& location) {
    return named_type_ref("dyn", location);
}

std::optional<long long> int_expr_value(const Expr& expr) {
    try {
        if (expr.kind == ExprKind::IntLiteral && !expr.value.empty()) {
            return std::stoll(expr.value);
        }
        if (expr.kind == ExprKind::Unary && expr.op == "-" && expr.children.size() == 1 &&
            expr.children.front().kind == ExprKind::IntLiteral &&
            !expr.children.front().value.empty()) {
            return -std::stoll(expr.children.front().value);
        }
    } catch (const std::exception&) {
    }
    return std::nullopt;
}

std::optional<long long> nonnegative_int_expr(const Expr& expr) {
    const std::optional<long long> value = int_expr_value(expr);
    return value && *value >= 0 ? value : std::nullopt;
}

std::optional<long long> nonnegative_dim_value(const TypeRef& dim) {
    if (dim.kind != TypeKind::Value || dim.value.empty()) {
        return std::nullopt;
    }
    const std::optional<long long> value = shape_value_expr_eval(dim.value);
    if (!value || *value < 0) {
        return std::nullopt;
    }
    return value;
}

const Expr* slice_step_expr(const Expr& slice) {
    if (slice.kind != ExprKind::Slice || slice.children.size() != 2) {
        return nullptr;
    }
    const Expr& end = slice.children[1];
    if (end.kind == ExprKind::Slice && end.children.size() == 2) {
        return &end.children[1];
    }
    return nullptr;
}

const Expr& slice_end_expr(const Expr& slice) {
    const Expr& end = slice.children[1];
    if (end.kind == ExprKind::Slice && end.children.size() == 2) {
        return end.children[0];
    }
    return end;
}

bool full_axis_slice(const Expr& slice) {
    if (slice.kind != ExprKind::Slice || slice.children.size() != 2) {
        return false;
    }
    return expr_missing(slice.children[0]) && expr_missing(slice_end_expr(slice)) &&
           slice_step_expr(slice) == nullptr;
}

TypeRef slice_extent_ref(const SourceLocation& location, const TypeRef& source_dim,
                         const Expr& slice) {
    if (full_axis_slice(slice)) {
        return source_dim;
    }

    const Expr& start_expr = slice.children[0];
    const Expr& end_expr = slice_end_expr(slice);
    const Expr* step_expr = slice_step_expr(slice);
    const std::optional<long long> literal_step =
        step_expr == nullptr || expr_missing(*step_expr) ? std::optional<long long>{1}
                                                        : int_expr_value(*step_expr);
    if (literal_step && *literal_step <= 0) {
        throw CompileError(step_expr->location, "fixed-array slice step must be positive");
    }
    const std::optional<long long> start =
        expr_missing(start_expr) ? std::optional<long long>{0} : nonnegative_int_expr(start_expr);
    const std::optional<long long> step = literal_step;
    if (!start || !step || *step <= 0) {
        return dyn_shape_ref(location);
    }

    std::optional<long long> end;
    if (expr_missing(end_expr)) {
        end = nonnegative_dim_value(source_dim);
    } else {
        end = nonnegative_int_expr(end_expr);
    }
    if (end) {
        long long clipped_start = *start;
        long long clipped_end = *end;
        if (const std::optional<long long> dim = nonnegative_dim_value(source_dim)) {
            clipped_start = std::min(clipped_start, *dim);
            clipped_end = std::min(clipped_end, *dim);
        }
        const long long width = std::max<long long>(0, clipped_end - clipped_start);
        return shape_value_type_ref(location, std::to_string((width + *step - 1) / *step));
    }

    if (expr_missing(end_expr) && *step == 1 && *start == 0) {
        return source_dim;
    }
    if (expr_missing(end_expr) && *step == 1 && source_dim.kind == TypeKind::Value &&
        !source_dim.value.empty()) {
        return shape_value_type_ref(location,
                                    "(" + source_dim.value + ") - " + std::to_string(*start));
    }
    return dyn_shape_ref(location);
}

std::vector<TypeRef> result_view_shape_refs(const SourceLocation& location,
                                            const std::vector<TypeRef>& source_shape,
                                            const Expr& index_expr) {
    if (source_shape.empty()) {
        return {};
    }
    const std::vector<Expr> args = index_arg_exprs(index_expr);
    size_t consumed = 0;
    bool saw_ellipsis = false;
    for (const Expr& arg : args) {
        if (arg.kind == ExprKind::Ellipsis) {
            saw_ellipsis = true;
            continue;
        }
        consumed += consumed_axis_count_from_expr(arg);
    }
    const size_t ellipsis_fill =
        saw_ellipsis && source_shape.size() > consumed ? source_shape.size() - consumed : 0;

    std::vector<TypeRef> out;
    size_t axis = 0;
    for (const Expr& arg : args) {
        if (arg.kind == ExprKind::NewAxis) {
            out.push_back(shape_value_type_ref(location, "1"));
            continue;
        }
        if (arg.kind == ExprKind::Ellipsis) {
            for (size_t i = 0; i < ellipsis_fill && axis < source_shape.size(); ++i) {
                out.push_back(source_shape[axis++]);
            }
            continue;
        }
        if (axis >= source_shape.size()) {
            return {};
        }
        if (arg.kind == ExprKind::Slice) {
            out.push_back(slice_extent_ref(location, source_shape[axis], arg));
        }
        ++axis;
    }
    while (axis < source_shape.size()) {
        out.push_back(source_shape[axis++]);
    }
    return out;
}

std::vector<TypeRef> shaped_type_shape_refs(const TypeRef& type) {
    if (type.kind != TypeKind::Shaped || type.children.size() < 2) {
        return {};
    }
    return std::vector<TypeRef>(type.children.begin() + 1, type.children.end());
}

std::optional<TypeRef> fixed_array_view_type_ref(const SourceLocation& location,
                                                 const TypeRef& receiver_type,
                                                 const Expr& index_expr) {
    if (!is_view_index_expr(index_expr)) {
        return std::nullopt;
    }
    const TypeRef unwrapped = unwrap_reference_and_const(receiver_type);
    const TypeRef element = explicit_array_element_type_ref(unwrapped);
    if (!has_type_ref(element)) {
        return std::nullopt;
    }
    const std::vector<TypeRef> shape = explicit_array_shape_refs(unwrapped);
    if (shape.empty() || consumed_axis_count_from_expr(index_expr) > shape.size()) {
        return std::nullopt;
    }
    return view_type_ref(location, "array_view", element,
                         result_view_shape_refs(location, shape, index_expr));
}

std::optional<TypeRef> indexed_array_view_type_ref(const SourceLocation& location,
                                                   const TypeRef& receiver_type,
                                                   const Expr& index_expr) {
    const TypeRef unwrapped = unwrap_reference_and_const(receiver_type);
    const std::vector<TypeRef> args = template_type_arg_refs(unwrapped, "array_view");
    if (args.size() != 1) {
        return std::nullopt;
    }
    if (is_view_index_expr(index_expr)) {
        return view_type_ref(
            location, "array_view", args.front(),
            result_view_shape_refs(location, shaped_type_shape_refs(unwrapped), index_expr));
    }
    return args.front();
}

} // namespace

IndexOperatorTarget index_operator_target(const Expr& receiver) {
    return IndexOperatorTarget{
        .receiver = &receiver, .read_operator = "[]", .write_operator = "[]="};
}

std::optional<FunctionSignature>
native_subscript_signature(const FunctionScope& scope, const TypeRef& receiver_type,
                           const Expr& receiver, const std::vector<Expr>& args,
                           const SourceLocation* location) {
    if (!foreign_cpp_class_type(scope.symbols, receiver_type)) {
        return std::nullopt;
    }
    const std::vector<FunctionSignature> candidates =
        method_signatures_for_type(scope.symbols, receiver_type, "operator[]", {});
    if (candidates.empty()) {
        return std::nullopt;
    }
    return match_native_method_signature(scope, expr_label(receiver) + "[]", candidates, {},
                                         receiver, args, location);
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
    if (const auto indexed_ref = indexed_type_ref_from_type_ref_with_count(
            symbols, location, receiver_type, index_count_from_expr(index_expr),
            is_view_index_expr(index_expr), has_step_slice(index_expr), label)) {
        return *indexed_ref;
    }
    if (class_for_receiver_type(symbols, receiver_type) != nullptr) {
        throw CompileError(location,
                           "no matching @operator(\"[]\") for indexed access to " + label);
    }
    throw CompileError(location, "cannot index non-container: " + label);
}

} // namespace dudu
