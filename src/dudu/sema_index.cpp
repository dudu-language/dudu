#include "dudu/sema_index.hpp"

#include "dudu/array_shape.hpp"
#include "dudu/ast_parse_utils.hpp"
#include "dudu/ast_type.hpp"
#include "dudu/cpp_lower.hpp"
#include "dudu/sema_common.hpp"
#include "dudu/sema_ops.hpp"
#include "dudu/type_compat.hpp"

#include <optional>
#include <sstream>

namespace dudu {
namespace {

std::string unwrap_reference_and_const(std::string type) {
    type = trim(std::move(type));
    TypeRef parsed = parse_type_text(type);
    if (parsed.kind == TypeKind::Reference && parsed.children.size() == 1) {
        type = substitute_type_ref_text(parsed.children.front(), {});
        parsed = parse_type_text(type);
    }
    if (parsed.kind == TypeKind::Const && parsed.children.size() == 1) {
        type = substitute_type_ref_text(parsed.children.front(), {});
    }
    return type;
}

bool foreign_indexable_type(const std::string& type) {
    return type.empty() || type == "auto" || type.find('.') != std::string::npos ||
           type.find("::") != std::string::npos;
}

std::optional<std::string> fixed_array_element_type(const TypeRef& type) {
    if (type.kind != TypeKind::FixedArray || type.children.empty()) {
        return std::nullopt;
    }
    const TypeRef& storage = type.children.front();
    if (storage.kind == TypeKind::Template && storage.name == "array" &&
        !storage.children.empty()) {
        return substitute_type_ref_text(storage.children.front(), {});
    }
    return substitute_type_ref_text(storage, {});
}

std::optional<std::string> single_template_child_type(const TypeRef& type, std::string_view name) {
    if (type.kind == TypeKind::Template && type.name == name && type.children.size() == 1) {
        return substitute_type_ref_text(type.children.front(), {});
    }
    return std::nullopt;
}

std::string shaped_array_type(const std::string& element_type, const std::vector<size_t>& shape) {
    std::ostringstream out;
    out << "array[" << element_type << "][";
    for (size_t i = 0; i < shape.size(); ++i) {
        if (i > 0) {
            out << ", ";
        }
        out << shape[i];
    }
    out << "]";
    return out.str();
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

std::string indexed_type_from_type_with_count(const Symbols& symbols,
                                              const SourceLocation& location,
                                              const std::string& raw_type, const size_t index_count,
                                              const bool is_slice, const bool has_step,
                                              const std::string& label) {
    std::string type = resolve_alias(symbols, raw_type);
    type = unwrap_reference_and_const(std::move(type));
    if (foreign_indexable_type(type)) {
        return "auto";
    }
    bool pointer_index = false;
    if (const TypeRef parsed = parse_type_text(type);
        parsed.kind == TypeKind::Pointer && parsed.children.size() == 1) {
        type = substitute_type_ref_text(parsed.children.front(), {});
        pointer_index = true;
    }
    for (const TypeKind kind : {TypeKind::Storage, TypeKind::Shared, TypeKind::Device,
                                TypeKind::Volatile, TypeKind::Atomic}) {
        if (const auto inner = unary_type_child_text(type, kind)) {
            type = *inner;
            break;
        }
    }
    if (pointer_index) {
        return unwrap_reference_and_const(type);
    }
    if (const auto element = single_template_type_arg_text(type, "list")) {
        return *element;
    }
    const TypeRef type_ref = parse_type_text(type);
    if (const std::vector<size_t> shape = explicit_array_shape(type_ref); !shape.empty()) {
        const std::string array_element = explicit_array_element_type(type_ref);
        if (is_slice) {
            if (shape.size() != 1) {
                throw CompileError(location,
                                   "array slicing requires one-dimensional fixed array: " + label);
            }
            if (has_step) {
                return "strided_span[" + array_element + "]";
            }
            return "span[" + array_element + "]";
        }
        if (index_count > shape.size()) {
            throw CompileError(location, "too many indices for array: " + label);
        }
        const std::string element = fixed_array_element_type(type_ref).value_or(array_element);
        if (index_count == shape.size()) {
            return element;
        }
        return shaped_array_type(element,
                                 std::vector<size_t>(shape.begin() + index_count, shape.end()));
    }
    if (const auto element = fixed_array_element_type(type_ref)) {
        return *element;
    }
    const std::vector<std::string> dict_args = template_type_arg_texts(type, "dict");
    if (dict_args.size() == 2) {
        return dict_args[1];
    }
    if (const auto signature = dudu_operator_signature(symbols, "[]", type)) {
        return signature->return_type;
    }
    if (type_ref.kind == TypeKind::Template && type_ref.children.size() == 1) {
        return substitute_type_ref_text(type_ref.children.front(), {});
    }
    throw CompileError(location, "cannot index non-container: " + label);
}

std::optional<std::string>
indexed_type_from_type_ref_with_count(const SourceLocation& location, const TypeRef& raw_type,
                                      const size_t index_count, const bool is_slice,
                                      const bool has_step, const std::string& label) {
    const TypeRef* type = &raw_type;
    while ((type->kind == TypeKind::Reference || type->kind == TypeKind::Const) &&
           type->children.size() == 1) {
        type = &type->children.front();
    }
    bool pointer_index = false;
    if (type->kind == TypeKind::Pointer && type->children.size() == 1) {
        type = &type->children.front();
        pointer_index = true;
    }
    while ((type->kind == TypeKind::Reference || type->kind == TypeKind::Const ||
            type->kind == TypeKind::Storage || type->kind == TypeKind::Shared ||
            type->kind == TypeKind::Device || type->kind == TypeKind::Volatile ||
            type->kind == TypeKind::Atomic) &&
           type->children.size() == 1) {
        type = &type->children.front();
    }
    if (pointer_index) {
        return substitute_type_ref_text(*type, {});
    }
    if (const auto element = single_template_child_type(*type, "list")) {
        return *element;
    }
    if (const std::vector<size_t> shape = explicit_array_shape(*type); !shape.empty()) {
        if (is_slice) {
            if (shape.size() != 1) {
                throw CompileError(location,
                                   "array slicing requires one-dimensional fixed array: " + label);
            }
            const std::string element = explicit_array_element_type(*type);
            return (has_step ? "strided_span[" : "span[") + element + "]";
        }
        if (index_count > shape.size()) {
            throw CompileError(location, "too many indices for array: " + label);
        }
        const std::string element =
            fixed_array_element_type(*type).value_or(explicit_array_element_type(*type));
        if (index_count == shape.size()) {
            return element;
        }
        return shaped_array_type(element,
                                 std::vector<size_t>(shape.begin() + index_count, shape.end()));
    }
    if (const auto element = fixed_array_element_type(*type)) {
        return *element;
    }
    if (type->kind == TypeKind::Template && type->name == "dict" && type->children.size() == 2) {
        return substitute_type_ref_text(type->children[1], {});
    }
    if (type->kind == TypeKind::Template && type->children.size() == 1) {
        return substitute_type_ref_text(type->children.front(), {});
    }
    return std::nullopt;
}

std::optional<std::string> iterable_type_from_type_ref(TypeRef type) {
    while ((type.kind == TypeKind::Reference || type.kind == TypeKind::Const) &&
           type.children.size() == 1) {
        type = type.children.front();
    }
    if (const auto element = single_template_child_type(type, "list")) {
        return *element;
    }
    if (const auto element = single_template_child_type(type, "span")) {
        return *element;
    }
    if (const auto element = single_template_child_type(type, "strided_span")) {
        return *element;
    }
    if (const auto element = fixed_array_element_type(type)) {
        return *element;
    }
    if (type.kind == TypeKind::Template && type.children.size() == 1) {
        return substitute_type_ref_text(type.children.front(), {});
    }
    return std::nullopt;
}

} // namespace

std::string indexed_value_type(const Symbols& symbols,
                               const std::map<std::string, std::string>& locals,
                               const SourceLocation& location, const std::string& name,
                               const Expr& index_expr, std::string_view unknown_message) {
    const auto local = locals.find(name);
    if (local == locals.end()) {
        throw CompileError(location, std::string(unknown_message) + name);
    }
    return indexed_type_from_type(symbols, location, local->second, index_expr, name);
}

std::string indexed_value_type(const Symbols& symbols,
                               const std::map<std::string, std::string>& locals,
                               const std::map<std::string, TypeRef>& local_type_refs,
                               const SourceLocation& location, const std::string& name,
                               const Expr& index_expr, std::string_view unknown_message) {
    const auto local = locals.find(name);
    if (local == locals.end()) {
        throw CompileError(location, std::string(unknown_message) + name);
    }
    if (const auto type_ref = local_type_refs.find(name); type_ref != local_type_refs.end()) {
        if (is_channel_slice_expr(index_expr)) {
            const std::vector<size_t> shape = explicit_array_shape(type_ref->second);
            if (shape.size() == 3) {
                return "strided_span[" + explicit_array_element_type(type_ref->second) + "]";
            }
        }
        if (is_column_slice_expr(index_expr)) {
            const std::vector<size_t> shape = explicit_array_shape(type_ref->second);
            if (shape.size() == 2) {
                return "strided_span[" + explicit_array_element_type(type_ref->second) + "]";
            }
        }
        if (!has_step_slice(index_expr)) {
            if (const std::vector<size_t> shape = explicit_array_shape(type_ref->second);
                !shape.empty()) {
                if (const auto prefix_count = trailing_full_slice_prefix_count(index_expr)) {
                    if (*prefix_count + 1 == shape.size()) {
                        return "span[" + explicit_array_element_type(type_ref->second) + "]";
                    }
                }
            }
        }
        if (const auto indexed = indexed_type_from_type_ref_with_count(
                location, type_ref->second, index_count_from_expr(index_expr),
                is_slice_expr(index_expr), has_step_slice(index_expr), name)) {
            return *indexed;
        }
    }
    return indexed_type_from_type(symbols, location, local->second, index_expr, name);
}

std::string indexed_type_from_type(const Symbols& symbols, const SourceLocation& location,
                                   const std::string& raw_type, const Expr& index_expr,
                                   const std::string& label) {
    std::string type = resolve_alias(symbols, raw_type);
    const std::string unwrapped_input_type = unwrap_reference_and_const(type);
    const TypeRef unwrapped_type_ref = parse_type_text(unwrapped_input_type);
    if (is_channel_slice_expr(index_expr)) {
        const std::vector<size_t> shape = explicit_array_shape(unwrapped_type_ref);
        if (shape.size() == 3) {
            return "strided_span[" + explicit_array_element_type(unwrapped_type_ref) + "]";
        }
    }
    if (is_column_slice_expr(index_expr)) {
        const std::vector<size_t> shape = explicit_array_shape(unwrapped_type_ref);
        if (shape.size() == 2) {
            return "strided_span[" + explicit_array_element_type(unwrapped_type_ref) + "]";
        }
    }
    if (!has_step_slice(index_expr)) {
        const TypeRef type_ref = parse_type_text(type);
        if (const std::vector<size_t> shape = explicit_array_shape(type_ref); !shape.empty()) {
            if (const auto prefix_count = trailing_full_slice_prefix_count(index_expr)) {
                if (*prefix_count + 1 == shape.size()) {
                    return "span[" + explicit_array_element_type(type_ref) + "]";
                }
            }
        }
    }
    if (const auto indexed = indexed_type_from_type_ref_with_count(
            location, parse_type_text(type), index_count_from_expr(index_expr),
            is_slice_expr(index_expr), has_step_slice(index_expr), label)) {
        return *indexed;
    }
    return indexed_type_from_type_with_count(
        symbols, location, raw_type, index_count_from_expr(index_expr), is_slice_expr(index_expr),
        has_step_slice(index_expr), label);
}

std::string iterable_value_type(const Symbols& symbols,
                                const std::map<std::string, std::string>& locals,
                                const std::string& name) {
    const auto local = locals.find(name);
    if (local == locals.end()) {
        return {};
    }
    const std::string type = unwrap_reference_and_const(resolve_alias(symbols, local->second));
    if (const auto element = iterable_type_from_type_ref(parse_type_text(type))) {
        return *element;
    }
    return {};
}

std::string iterable_value_type(const Symbols& symbols,
                                const std::map<std::string, std::string>& locals,
                                const std::map<std::string, TypeRef>& local_type_refs,
                                const std::string& name) {
    if (const auto type_ref = local_type_refs.find(name); type_ref != local_type_refs.end()) {
        if (const auto element = iterable_type_from_type_ref(type_ref->second)) {
            return *element;
        }
    }
    return iterable_value_type(symbols, locals, name);
}

void check_iterable_binding(const Symbols& symbols,
                            const std::map<std::string, std::string>& locals,
                            const std::map<std::string, TypeRef>& local_type_refs,
                            const SourceLocation& location, const TypeRef& binding_type,
                            const Expr& iterable) {
    if (iterable.kind == ExprKind::Call && iterable.name == "range") {
        return;
    }
    if (iterable.kind != ExprKind::Name) {
        return;
    }
    const std::string& name = iterable.name;
    if (!locals.contains(name)) {
        throw CompileError(location, "iteration over unknown local: " + name);
    }
    const std::string element = iterable_value_type(symbols, locals, local_type_refs, name);
    if (element.empty()) {
        throw CompileError(location, "cannot iterate non-container: " + name);
    }
    const TypeRef element_type = parse_type_text(element, location);
    if (!type_assignment_allowed(binding_type, element_type) &&
        !type_assignment_allowed(resolve_alias(symbols, binding_type.text),
                                 resolve_alias(symbols, element))) {
        throw CompileError(location,
                           "loop binding expects " + binding_type.text + ", got " + element);
    }
}

} // namespace dudu
