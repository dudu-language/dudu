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
#include "dudu/source.hpp"

namespace dudu {
namespace {

bool is_indexed_local_segment(const std::string& text) {
    const size_t index = text.find('[');
    return index != std::string::npos && text.back() == ']' &&
           is_plain_identifier(trim(text.substr(0, index)));
}

std::map<std::string, std::string> type_substitutions(const std::vector<std::string>& params,
                                                      const std::vector<std::string>& args) {
    std::map<std::string, std::string> out;
    for (size_t i = 0; i < params.size() && i < args.size(); ++i) {
        out.emplace(params[i], trim(args[i]));
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

std::string static_member_type(const Symbols& symbols, const SourceLocation* location,
                               const std::string& type_name, const std::string& member) {
    const auto klass = symbols.classes.find(type_name);
    if (klass == symbols.classes.end()) {
        return {};
    }
    for (const ConstDecl& constant : klass->second->constants) {
        if (constant.name == member) {
            return type_ref_text(constant.type_ref);
        }
    }
    for (const ConstDecl& field : klass->second->static_fields) {
        if (field.name == member) {
            return type_ref_text(field.type_ref);
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
        return base_type(strip_c_type_tag(type));
    }
}

std::optional<std::string> field_type_for_class(const Symbols& symbols, const ClassDecl& klass,
                                                const std::string& receiver_type,
                                                const std::string& field) {
    for (const FieldDecl& decl : klass.fields) {
        if (decl.name == field) {
            const std::vector<std::string> receiver_args = template_args_from_type(receiver_type);
            TypeRef type = substitute_type_ref(
                decl.type_ref, type_substitutions(klass.generic_params, receiver_args));
            const std::string receiver_substituted = substitute_receiver_template_type(
                substitute_type_ref_text(type, {}), receiver_args);
            return receiver_substituted;
        }
    }
    for (const BaseClassDecl& base_decl : klass.base_class_refs) {
        const std::string base = type_ref_text(base_decl.type_ref);
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

std::string member_path_type_from_string(const Symbols& symbols,
                                         const std::map<std::string, std::string>& locals,
                                         const SourceLocation* location, const std::string& path,
                                         std::string unknown_local_prefix) {
    const SourceLocation parse_location = location == nullptr ? SourceLocation{} : *location;
    if (path.find('.') == std::string::npos) {
        if (is_indexed_local_segment(path)) {
            const Expr indexed = parse_expr_text(path, parse_location);
            return member_expr_type(symbols, locals, location, indexed, unknown_local_prefix);
        }
        if (const auto local = locals.find(path); local != locals.end()) {
            return local->second;
        }
        if (location != nullptr && !unknown_local_prefix.empty()) {
            sema_fail(*location, unknown_local_prefix + path);
        }
        return {};
    }
    const Expr expr = parse_expr_text(path, parse_location);
    if (expr.kind == ExprKind::Unknown) {
        if (location != nullptr && !unknown_local_prefix.empty()) {
            sema_fail(*location, unknown_local_prefix + path);
        }
        return {};
    }
    const std::string type =
        member_expr_type(symbols, locals, location, expr, unknown_local_prefix);
    if (type.empty() && location != nullptr && !unknown_local_prefix.empty()) {
        const size_t dot = path.find('.');
        if (dot != std::string::npos) {
            const std::string first = path.substr(0, dot);
            if (!locals.contains(first) && !symbols.classes.contains(first)) {
                sema_fail(*location, unknown_local_prefix + first);
            }
        } else {
            if (location != nullptr) {
                sema_fail(*location, unknown_local_prefix + path);
            }
        }
    }
    return type;
}

std::string member_expr_type(const Symbols& symbols,
                             const std::map<std::string, std::string>& locals,
                             const SourceLocation* location, const Expr& expr,
                             std::string_view unknown_local_prefix,
                             std::string_view current_class) {
    if (expr.kind == ExprKind::Name && !expr.name.empty()) {
        if (expr.name == "class") {
            if (!current_class.empty()) {
                return std::string(current_class);
            }
            if (location != nullptr) {
                sema_fail(*location, "class static access outside class");
            }
            return {};
        }
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
        const std::string receiver_type = member_expr_type(
            symbols, locals, location, expr.children[0], unknown_local_prefix, current_class);
        if (receiver_type.empty()) {
            return {};
        }
        const std::string label = display_expr(expr);
        return indexed_type_from_type(symbols, location == nullptr ? SourceLocation{} : *location,
                                      receiver_type, expr.children[1],
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
            return static_member_type(symbols, location, std::string(current_class), expr.name);
        }
        if (receiver.kind == ExprKind::Name && !locals.contains(receiver.name) &&
            symbols.classes.contains(receiver.name)) {
            return static_member_type(symbols, location, receiver.name, expr.name);
        }
        const std::string receiver_type = member_expr_type(symbols, locals, location, receiver,
                                                           unknown_local_prefix, current_class);
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
            const std::string label = display_expr(expr);
            sema_fail(*location, "unknown field: " +
                                     (label.empty() ? receiver_type + "." + expr.name : label));
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
    const std::vector<TypeRef> result_args =
        template_type_arg_refs(parse_type_text(resolved), "Result");
    if (!result_args.empty()) {
        if (field == "ok") {
            return "bool";
        }
        if (field == "value" && !result_args.empty()) {
            return substitute_type_ref_text(result_args[0], {});
        }
        if (field == "err" && result_args.size() >= 2) {
            return substitute_type_ref_text(result_args[1], {});
        }
    }
    const auto klass = symbols.classes.find(unwrap_receiver_type(symbols, receiver_type));
    if (klass == symbols.classes.end()) {
        return std::nullopt;
    }
    return field_type_for_class(symbols, *klass->second, receiver_type, field);
}

} // namespace dudu
