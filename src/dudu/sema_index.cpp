#include "dudu/sema_index.hpp"

#include "dudu/array_shape.hpp"
#include "dudu/cpp_lower.hpp"
#include "dudu/sema_ops.hpp"
#include "dudu/type_compat.hpp"

#include <optional>
#include <sstream>

namespace dudu {
namespace {

std::string unwrap_reference_and_const(std::string type) {
    type = trim(std::move(type));
    if (!type.empty() && type.front() == '&') {
        type = trim(type.substr(1));
    }
    if (starts_with(type, "const[") && type.back() == ']') {
        type = trim(type.substr(6, type.size() - 7));
    }
    return type;
}

bool foreign_indexable_type(const std::string& type) {
    return type.empty() || type == "auto" || type.find('.') != std::string::npos ||
           type.find("::") != std::string::npos;
}

size_t top_level_colon(const std::string& text) {
    int depth = 0;
    char quote = '\0';
    bool escaped = false;
    for (size_t i = 0; i < text.size(); ++i) {
        const char c = text[i];
        if (quote != '\0') {
            if (escaped) {
                escaped = false;
            } else if (c == '\\') {
                escaped = true;
            } else if (c == quote) {
                quote = '\0';
            }
            continue;
        }
        if (c == '"' || c == '\'') {
            quote = c;
            continue;
        }
        if (c == '[' || c == '(' || c == '{') {
            ++depth;
        } else if (c == ']' || c == ')' || c == '}') {
            --depth;
        } else if (c == ':' && depth == 0) {
            return i;
        }
    }
    return std::string::npos;
}

size_t find_matching_bracket(std::string_view text, const size_t open) {
    int depth = 1;
    size_t cursor = open + 1;
    while (cursor < text.size() && depth > 0) {
        if (text[cursor] == '[') {
            ++depth;
        } else if (text[cursor] == ']') {
            --depth;
        }
        ++cursor;
    }
    return depth == 0 ? cursor - 1 : std::string::npos;
}

std::optional<std::string> canonical_array_element_type(const std::string& type) {
    if (!starts_with(type, "array[")) {
        return std::nullopt;
    }
    const size_t element_close = find_matching_bracket(type, 5);
    if (element_close == std::string::npos || element_close + 1 >= type.size() ||
        type[element_close + 1] != '[' || !ends_with(type, "]")) {
        return std::nullopt;
    }
    return trim(type.substr(6, element_close - 6));
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

std::string indexed_type_from_type_with_count(const Symbols& symbols,
                                              const SourceLocation& location,
                                              const std::string& raw_type, const size_t index_count,
                                              const bool is_slice, const bool has_step,
                                              const std::string& label) {
    std::string type = resolve_alias(symbols, raw_type);
    if (foreign_indexable_type(type)) {
        return "auto";
    }
    bool pointer_index = false;
    if (starts_with(type, "*")) {
        type = trim(type.substr(1));
        pointer_index = true;
    }
    for (const char* wrapper : {"storage", "shared", "device", "volatile", "atomic"}) {
        const std::string prefix = std::string(wrapper) + "[";
        if (starts_with(type, prefix) && type.back() == ']') {
            type = trim(type.substr(prefix.size(), type.size() - prefix.size() - 1));
            break;
        }
    }
    if (pointer_index) {
        return unwrap_reference_and_const(type);
    }
    if (starts_with(type, "list[") && type.back() == ']') {
        return trim(type.substr(5, type.size() - 6));
    }
    if (const std::vector<size_t> shape = explicit_array_shape(type); !shape.empty()) {
        if (is_slice) {
            if (shape.size() != 1) {
                throw CompileError(location,
                                   "array slicing requires one-dimensional fixed array: " + label);
            }
            if (has_step) {
                throw CompileError(location, "array slice step is not supported: " + label);
            }
            return "span[" + explicit_array_element_type(type) + "]";
        }
        if (index_count > shape.size()) {
            throw CompileError(location, "too many indices for array: " + label);
        }
        const std::string element = explicit_array_element_type(type);
        if (index_count == shape.size()) {
            return element;
        }
        return shaped_array_type(element,
                                 std::vector<size_t>(shape.begin() + index_count, shape.end()));
    }
    if (const auto element = canonical_array_element_type(type)) {
        return *element;
    }
    if (starts_with(type, "dict[") && type.back() == ']') {
        const std::vector<std::string> args = split_top_level(type.substr(5, type.size() - 6));
        if (args.size() == 2) {
            return args[1];
        }
    }
    if (const auto signature = dudu_operator_signature(symbols, "[]", type)) {
        return signature->return_type;
    }
    const size_t type_index = type.find('[');
    if (type_index != std::string::npos && type.back() == ']') {
        return trim(type.substr(0, type_index));
    }
    throw CompileError(location, "cannot index non-container: " + label);
}

} // namespace

std::string indexed_value_type(const Symbols& symbols,
                               const std::map<std::string, std::string>& locals,
                               const SourceLocation& location, const std::string& name,
                               const std::string& index_expr, std::string_view unknown_message) {
    const auto local = locals.find(name);
    if (local == locals.end()) {
        throw CompileError(location, std::string(unknown_message) + name);
    }
    return indexed_type_from_type(symbols, location, local->second, index_expr, name);
}

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

std::string indexed_type_from_type(const Symbols& symbols, const SourceLocation& location,
                                   const std::string& raw_type, const std::string& index_expr,
                                   const std::string& label) {
    const size_t index_count = index_expr.empty() ? 1 : split_top_level_args(index_expr).size();
    const size_t colon = top_level_colon(index_expr);
    const bool has_slice = colon != std::string::npos;
    return indexed_type_from_type_with_count(symbols, location, raw_type, index_count, has_slice,
                                             false, label);
}

std::string indexed_type_from_type(const Symbols& symbols, const SourceLocation& location,
                                   const std::string& raw_type, const Expr& index_expr,
                                   const std::string& label) {
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
    if (starts_with(type, "list[") && type.back() == ']') {
        return trim(type.substr(5, type.size() - 6));
    }
    if (starts_with(type, "span[") && type.back() == ']') {
        return trim(type.substr(5, type.size() - 6));
    }
    if (const auto element = canonical_array_element_type(type)) {
        return *element;
    }
    const size_t type_index = type.find('[');
    if (type_index != std::string::npos && type.back() == ']') {
        return trim(type.substr(0, type_index));
    }
    return {};
}

void check_iterable_binding(const Symbols& symbols,
                            const std::map<std::string, std::string>& locals,
                            const SourceLocation& location, const std::string& binding_type,
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
    const std::string element = iterable_value_type(symbols, locals, name);
    if (element.empty()) {
        throw CompileError(location, "cannot iterate non-container: " + name);
    }
    if (!type_assignment_allowed(binding_type, element) &&
        !type_assignment_allowed(resolve_alias(symbols, binding_type),
                                 resolve_alias(symbols, element))) {
        throw CompileError(location, "loop binding expects " + binding_type + ", got " + element);
    }
}

} // namespace dudu
