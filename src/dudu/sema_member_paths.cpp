#include "dudu/sema_methods.hpp"

#include "dudu/ast_type.hpp"
#include "dudu/cpp_lower.hpp"
#include "dudu/sema_common.hpp"
#include "dudu/sema_index.hpp"
#include "dudu/sema_methods_internal.hpp"
#include "dudu/sema_method_templates.hpp"
#include "dudu/sema_native.hpp"
#include "dudu/sema_scan.hpp"
#include "dudu/source.hpp"

namespace dudu {
namespace {

bool is_indexed_local_segment(const std::string& text) {
    const size_t index = text.find('[');
    return index != std::string::npos && text.back() == ']' &&
           is_plain_identifier(trim(text.substr(0, index)));
}

std::string first_path_type(const Symbols& symbols,
                            const std::map<std::string, std::string>& locals,
                            const SourceLocation* location, const std::string& first,
                            const std::string& unknown_local_prefix) {
    if (is_indexed_local_segment(first)) {
        const size_t index = first.find('[');
        const std::string name = trim(first.substr(0, index));
        const std::string index_expr = first.substr(index + 1, first.size() - index - 2);
        const Expr parsed_index =
            parse_expr_text(index_expr, location == nullptr ? SourceLocation{} : *location);
        if (location == nullptr && !locals.contains(name)) {
            return {};
        }
        return indexed_value_type(
            symbols, locals, location == nullptr ? SourceLocation{} : *location, name, parsed_index,
            unknown_local_prefix.empty() ? "indexed access to unknown local: "
                                         : unknown_local_prefix);
    }
    const auto local = locals.find(first);
    if (local == locals.end()) {
        if (location != nullptr && !unknown_local_prefix.empty()) {
            sema_fail(*location, unknown_local_prefix + first);
        }
        return {};
    }
    return local->second;
}

std::string static_member_type(const Symbols& symbols, const SourceLocation* location,
                               const std::string& type_name, const std::string& member) {
    const auto klass = symbols.classes.find(type_name);
    if (klass == symbols.classes.end()) {
        return {};
    }
    for (const ConstDecl& constant : klass->second->constants) {
        if (constant.name == member) {
            return constant.type;
        }
    }
    for (const ConstDecl& field : klass->second->static_fields) {
        if (field.name == member) {
            return field.type;
        }
    }
    if (location != nullptr) {
        sema_fail(*location, "unknown static member: " + type_name + "." + member);
    }
    return {};
}

} // namespace

std::string unwrap_receiver_type(const Symbols& symbols, std::string type) {
    type = resolve_alias(symbols, std::move(type));
    while (true) {
        type = trim(std::move(type));
        const TypeRef parsed = parse_type_text(type);
        if (const auto inner =
                unary_type_child_text(parsed, {TypeKind::Pointer, TypeKind::Reference})) {
            type = *inner;
            continue;
        }
        if (const auto inner = unary_type_child_text(
                parsed, {TypeKind::Const, TypeKind::Volatile, TypeKind::Atomic, TypeKind::Storage,
                         TypeKind::Shared, TypeKind::Device})) {
            type = *inner;
            continue;
        }
        return base_type(type);
    }
}

std::optional<std::string> field_type_for_class(const Symbols& symbols, const ClassDecl& klass,
                                                const std::string& receiver_type,
                                                const std::string& field) {
    for (const FieldDecl& decl : klass.fields) {
        if (decl.name == field) {
            const std::vector<std::string> receiver_args = template_args_from_type(receiver_type);
            std::string type =
                substitute_class_template_type(decl.type, klass.generic_params, receiver_args);
            return substitute_receiver_template_type(std::move(type), receiver_args);
        }
    }
    for (const std::string& base : klass.base_classes) {
        const auto base_class = symbols.classes.find(unwrap_receiver_type(symbols, base));
        if (base_class == symbols.classes.end()) {
            continue;
        }
        if (const auto found = field_type_for_class(symbols, *base_class->second, base, field)) {
            return found;
        }
    }
    return std::nullopt;
}

std::string member_path_type(const Symbols& symbols,
                             const std::map<std::string, std::string>& locals,
                             const SourceLocation* location, const std::string& path,
                             std::string unknown_local_prefix) {
    const size_t dot = path.find('.');
    if (dot == std::string::npos) {
        if (const auto local = locals.find(path); local != locals.end()) {
            return local->second;
        }
        return {};
    }
    std::string current = path.substr(0, dot);
    if (symbols.classes.contains(current)) {
        size_t start = dot + 1;
        const ClassDecl* klass = symbols.classes.at(current);
        while (start < path.size()) {
            const size_t next = path.find('.', start);
            const std::string member =
                path.substr(start, next == std::string::npos ? next : next - start);
            bool found = false;
            for (const ConstDecl& constant : klass->constants) {
                if (constant.name == member) {
                    if (next == std::string::npos) {
                        return constant.type;
                    }
                    const auto next_class = symbols.classes.find(base_type(constant.type));
                    if (next_class == symbols.classes.end()) {
                        return {};
                    }
                    klass = next_class->second;
                    found = true;
                    break;
                }
            }
            for (const ConstDecl& field : klass->static_fields) {
                if (field.name == member) {
                    if (next == std::string::npos) {
                        return field.type;
                    }
                    const auto next_class = symbols.classes.find(base_type(field.type));
                    if (next_class == symbols.classes.end()) {
                        return {};
                    }
                    klass = next_class->second;
                    found = true;
                    break;
                }
            }
            if (!found) {
                if (location != nullptr) {
                    sema_fail(*location, "unknown static member: " + path);
                }
                return {};
            }
            start = next + 1;
        }
        return {};
    }
    std::string type = first_path_type(symbols, locals, location, current, unknown_local_prefix);
    if (type.empty()) {
        return {};
    }
    size_t start = dot + 1;
    while (start < path.size()) {
        const size_t next = path.find('.', start);
        const std::string field =
            path.substr(start, next == std::string::npos ? next : next - start);
        const auto klass = symbols.classes.find(unwrap_receiver_type(symbols, type));
        if (klass == symbols.classes.end()) {
            return {};
        }
        if (const auto found = field_type_for_class(symbols, *klass->second, type, field)) {
            type = *found;
        } else {
            if (location != nullptr) {
                sema_fail(*location, "unknown field: " + path);
            }
            return {};
        }
        if (next == std::string::npos) {
            return type;
        }
        start = next + 1;
    }
    return type;
}

std::string member_expr_type(const Symbols& symbols,
                             const std::map<std::string, std::string>& locals,
                             const SourceLocation* location, const Expr& expr,
                             std::string_view unknown_local_prefix) {
    if (expr.kind == ExprKind::Name && !expr.name.empty()) {
        if (const auto local = locals.find(expr.name); local != locals.end()) {
            return local->second;
        }
        if (symbols.classes.contains(expr.name)) {
            return expr.name;
        }
        if (location != nullptr && !unknown_local_prefix.empty()) {
            sema_fail(*location, std::string(unknown_local_prefix) + expr.name);
        }
        return {};
    }
    if (expr.kind == ExprKind::Index && expr.children.size() == 2) {
        const std::string receiver_type =
            member_expr_type(symbols, locals, location, expr.children[0], unknown_local_prefix);
        if (receiver_type.empty()) {
            return {};
        }
        return indexed_type_from_type(symbols, location == nullptr ? SourceLocation{} : *location,
                                      receiver_type, expr.children[1],
                                      expr.text.empty() ? "indexed expression" : expr.text);
    }
    if (expr.kind == ExprKind::Member && expr.children.size() == 1 && !expr.name.empty()) {
        const Expr& receiver = expr.children.front();
        if (receiver.kind == ExprKind::Name && !locals.contains(receiver.name) &&
            symbols.classes.contains(receiver.name)) {
            return static_member_type(symbols, location, receiver.name, expr.name);
        }
        const std::string receiver_type =
            member_expr_type(symbols, locals, location, receiver, unknown_local_prefix);
        if (receiver_type.empty()) {
            return {};
        }
        if (const auto field = field_type_for_type(symbols, receiver_type, expr.name)) {
            return *field;
        }
        if (const auto swizzle = swizzle_type_for_type(symbols, receiver_type, expr.name)) {
            return *swizzle;
        }
        if (receiver_type == "auto" ||
            foreign_cpp_type_name(symbols, resolve_alias(symbols, receiver_type))) {
            return "auto";
        }
        if (location != nullptr) {
            sema_fail(
                *location, "unknown field: " +
                               (expr.text.empty() ? receiver_type + "." + expr.name : expr.text));
        }
    }
    return {};
}

bool is_member_path(const std::string& path) {
    if (path.find('.') == std::string::npos) {
        return false;
    }
    for (const std::string& part : split_top_level(path)) {
        if (part != path) {
            return false;
        }
    }
    size_t start = 0;
    while (start < path.size()) {
        const size_t dot = path.find('.', start);
        const std::string part = path.substr(start, dot == std::string::npos ? dot : dot - start);
        const std::string trimmed = trim(part);
        if (!is_plain_identifier(trimmed) && !is_indexed_local_segment(trimmed)) {
            return false;
        }
        if (dot == std::string::npos) {
            return true;
        }
        start = dot + 1;
    }
    return false;
}

std::optional<std::string> field_type_for_type(const Symbols& symbols,
                                               const std::string& receiver_type,
                                               const std::string& field) {
    const std::string resolved = resolve_alias(symbols, receiver_type);
    const std::vector<std::string> result_args = template_type_arg_texts(resolved, "Result");
    if (!result_args.empty()) {
        if (field == "ok") {
            return "bool";
        }
        if (field == "value" && !result_args.empty()) {
            return result_args[0];
        }
        if (field == "err" && result_args.size() >= 2) {
            return result_args[1];
        }
    }
    const auto klass = symbols.classes.find(unwrap_receiver_type(symbols, receiver_type));
    if (klass == symbols.classes.end()) {
        return std::nullopt;
    }
    return field_type_for_class(symbols, *klass->second, receiver_type, field);
}

} // namespace dudu
