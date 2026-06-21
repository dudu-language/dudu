#include "dudu/ast_expr.hpp"
#include "dudu/ast_type.hpp"
#include "dudu/cpp_lower.hpp"
#include "dudu/sema_common.hpp"
#include "dudu/sema_index.hpp"
#include "dudu/sema_method_templates.hpp"
#include "dudu/sema_methods.hpp"
#include "dudu/sema_methods_internal.hpp"
#include "dudu/sema_native.hpp"
#include "dudu/sema_scan.hpp"
#include "dudu/sema_scope.hpp"
#include "dudu/source.hpp"

namespace dudu {
namespace {

bool is_indexed_local_segment(const std::string& text) {
    const size_t index = text.find('[');
    return index != std::string::npos && text.back() == ']' &&
           is_plain_identifier(trim(text.substr(0, index)));
}

std::map<std::string, TypeRef> type_ref_substitutions(const std::vector<std::string>& params,
                                                      const std::vector<TypeRef>& args) {
    std::map<std::string, TypeRef> out;
    for (size_t i = 0; i < params.size() && i < args.size(); ++i) {
        out.emplace(params[i], args[i]);
    }
    return out;
}

std::string strip_c_type_tag(std::string type) {
    type = trim(std::move(type));
    for (std::string_view tag : {"struct ", "class ", "union ", "enum "}) {
        if (type.starts_with(tag)) {
            return trim(type.substr(tag.size()));
        }
    }
    return type;
}

TypeRef static_member_type_ref(const Symbols& symbols, const SourceLocation* location,
                               const std::string& type_name, const std::string& member) {
    const auto klass = symbols.classes.find(type_name);
    if (klass == symbols.classes.end()) {
        return {};
    }
    for (const ConstDecl& constant : klass->second->constants) {
        if (constant.name == member) {
            return constant.type_ref;
        }
    }
    for (const ConstDecl& field : klass->second->static_fields) {
        if (field.name == member) {
            return field.type_ref;
        }
    }
    if (location != nullptr) {
        sema_fail(*location, "unknown static member: " + type_name + "." + member);
    }
    return {};
}

} // namespace

TypeRef unwrap_receiver_type_ref(const Symbols& symbols, const TypeRef& type) {
    TypeRef current = type;
    while (true) {
        const std::string head = type_ref_head_name(current);
        const bool native_qualified =
            head.find('.') != std::string::npos || head.find("::") != std::string::npos;
        const bool exact_alias = symbols.alias_type_refs.contains(head);
        if (!native_qualified || exact_alias) {
            const TypeRef resolved = resolve_alias_ref(symbols, current);
            if (!type_ref_same_shape(resolved, current)) {
                current = resolved;
                continue;
            }
        }
        if (const auto inner =
                unary_type_child_ref(current, {TypeKind::Pointer, TypeKind::Reference})) {
            current = *inner;
            continue;
        }
        if (const auto inner = unary_type_child_ref(
                current, {TypeKind::Const, TypeKind::Volatile, TypeKind::Atomic, TypeKind::Storage,
                          TypeKind::Shared, TypeKind::Device})) {
            current = *inner;
            continue;
        }
        return current;
    }
}

std::string receiver_class_name(const Symbols& symbols, const TypeRef& type) {
    const TypeRef current = unwrap_receiver_type_ref(symbols, type);
    return strip_c_type_tag(type_ref_head_name(current));
}

const ClassDecl* class_for_receiver_type(const Symbols& symbols, const TypeRef& type) {
    const auto klass = symbols.classes.find(receiver_class_name(symbols, type));
    return klass == symbols.classes.end() ? nullptr : klass->second;
}

std::optional<TypeRef> field_type_ref_for_class(const Symbols& symbols, const ClassDecl& klass,
                                                const TypeRef& receiver_type,
                                                const std::string& field) {
    for (const FieldDecl& decl : klass.fields) {
        if (decl.name == field) {
            const std::vector<TypeRef> receiver_args = template_arg_refs_from_type(receiver_type);
            TypeRef type = substitute_type_ref(
                decl.type_ref, type_ref_substitutions(klass.generic_params, receiver_args));
            return substitute_receiver_template_type(type, receiver_args);
        }
    }
    for (const BaseClassDecl& base_decl : klass.base_class_refs) {
        const ClassDecl* base_class = class_for_receiver_type(symbols, base_decl.type_ref);
        if (base_class == nullptr) {
            continue;
        }
        if (const auto found =
                field_type_ref_for_class(symbols, *base_class, base_decl.type_ref, field)) {
            return found;
        }
    }
    return std::nullopt;
}

TypeRef member_expr_type_ref(const Symbols& symbols,
                             const std::map<std::string, TypeRef>& local_type_refs,
                             const SourceLocation* location, const Expr& expr,
                             std::string_view unknown_local_prefix,
                             std::string_view current_class) {
    const SourceLocation type_location = location == nullptr ? expr.location : *location;
    if (expr.kind == ExprKind::Name && !expr.name.empty()) {
        if (expr.name == "class") {
            if (!current_class.empty()) {
                return named_type_ref(std::string(current_class), expr.location);
            }
            if (location != nullptr) {
                sema_fail(*location, "class static access outside class");
            }
            return {};
        }
        if (const TypeRef local = local_type_ref(local_type_refs, expr.name, type_location);
            has_type_ref(local)) {
            return local;
        }
        if (symbols.classes.contains(expr.name)) {
            return named_type_ref(expr.name, expr.location);
        }
        if (location != nullptr && !unknown_local_prefix.empty()) {
            sema_fail(*location, std::string(unknown_local_prefix) + expr.name);
        }
        return {};
    }
    if (expr.kind == ExprKind::Index && expr.children.size() == 2) {
        const TypeRef receiver_type =
            member_expr_type_ref(symbols, local_type_refs, location, expr.children[0],
                                 unknown_local_prefix, current_class);
        if (!has_type_ref(receiver_type)) {
            return {};
        }
        const std::string label = expr_label(expr);
        return indexed_type_ref_from_type(symbols, type_location, receiver_type, expr.children[1],
                                          label.empty() ? "indexed expression" : label);
    }
    if (expr.kind == ExprKind::Member && expr.children.size() == 1 && !expr.name.empty()) {
        const Expr& receiver = expr.children.front();
        if (receiver.kind == ExprKind::Name && receiver.name == "class") {
            if (current_class.empty()) {
                if (location != nullptr) {
                    sema_fail(*location, "class static access outside class");
                }
                return {};
            }
            return static_member_type_ref(symbols, location, std::string(current_class), expr.name);
        }
        if (receiver.kind == ExprKind::Name && !local_type_refs.contains(receiver.name) &&
            symbols.classes.contains(receiver.name)) {
            return static_member_type_ref(symbols, location, receiver.name, expr.name);
        }
        const TypeRef receiver_type = member_expr_type_ref(
            symbols, local_type_refs, location, receiver, unknown_local_prefix, current_class);
        if (!has_type_ref(receiver_type)) {
            return {};
        }
        if (const auto field = field_type_ref_for_type(symbols, receiver_type, expr.name)) {
            return *field;
        }
        if (const auto swizzle = swizzle_type_ref_for_type(symbols, receiver_type, expr.name)) {
            return *swizzle;
        }
        if (type_ref_is_auto(receiver_type) || foreign_cpp_type_name(symbols, receiver_type)) {
            return named_type_ref("auto", expr.location);
        }
        if (location != nullptr) {
            const std::string label = expr_label(expr);
            const std::string receiver_type_text = type_ref_text(receiver_type);
            sema_fail(*location,
                      "unknown field: " +
                          (label.empty() ? receiver_type_text + "." + expr.name : label));
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

std::optional<TypeRef> field_type_ref_for_type(const Symbols& symbols, const TypeRef& receiver_type,
                                               const std::string& field) {
    const TypeRef resolved_type = resolve_alias_ref(symbols, receiver_type);
    const std::vector<TypeRef> result_args = template_type_arg_refs(resolved_type, "Result");
    if (!result_args.empty()) {
        if (field == "ok") {
            return named_type_ref("bool", receiver_type.location);
        }
        if (field == "value" && !result_args.empty()) {
            return result_args[0];
        }
        if (field == "err" && result_args.size() >= 2) {
            return result_args[1];
        }
    }
    const ClassDecl* klass = class_for_receiver_type(symbols, receiver_type);
    if (klass == nullptr) {
        return std::nullopt;
    }
    return field_type_ref_for_class(symbols, *klass, receiver_type, field);
}

} // namespace dudu
