#include "dudu/sema_methods.hpp"

#include "dudu/ast_type.hpp"
#include "dudu/cpp_lower.hpp"
#include "dudu/sema_builtin_methods.hpp"
#include "dudu/sema_index.hpp"
#include "dudu/sema_method_templates.hpp"
#include "dudu/sema_scan.hpp"
#include "dudu/source.hpp"

#include <optional>
#include <set>
#include <string_view>

namespace dudu {
namespace {

[[noreturn]] void fail(const SourceLocation& location, const std::string& message) {
    throw CompileError(location, message);
}

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

bool is_indexed_local_segment(const std::string& text) {
    const size_t index = text.find('[');
    return index != std::string::npos && text.back() == ']' &&
           is_plain_identifier(trim(text.substr(0, index)));
}

bool method_is_static(const FunctionDecl& method) {
    return method.params.empty() || method.params.front().name != "self";
}

std::string template_method_name(const std::string& method_name) {
    const size_t open = method_name.find('[');
    return open == std::string::npos ? method_name : method_name.substr(0, open);
}

std::vector<std::string> template_method_args(const std::string& method_name) {
    const size_t open = method_name.find('[');
    if (open == std::string::npos || method_name.back() != ']') {
        return {};
    }
    return split_top_level_args(method_name.substr(open + 1, method_name.size() - open - 2));
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
            fail(*location, unknown_local_prefix + first);
        }
        return {};
    }
    return local->second;
}

} // namespace

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
                    fail(*location, "unknown static member: " + path);
                }
                return {};
            }
            start = next + 1;
        }
        return {};
    }
    std::string type = first_path_type(symbols, locals, location, current, unknown_local_prefix);
    if (type.empty())
        return {};
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
                fail(*location, "unknown field: " + path);
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

std::optional<std::string_view> swizzle_component_set(const std::string& swizzle) {
    if (swizzle.size() < 2 || swizzle.size() > 4) {
        return std::nullopt;
    }
    for (const std::string_view set :
         {std::string_view("xyzw"), std::string_view("rgba"), std::string_view("stpq")}) {
        bool matches = true;
        for (const char ch : swizzle) {
            if (set.find(ch) == std::string_view::npos) {
                matches = false;
                break;
            }
        }
        if (matches) {
            return set;
        }
    }
    return std::nullopt;
}

std::optional<std::string> swizzle_type_for_type(const Symbols& symbols,
                                                 const std::string& receiver_type,
                                                 const std::string& swizzle) {
    const auto component_set = swizzle_component_set(swizzle);
    if (!component_set) {
        return std::nullopt;
    }
    const std::string class_name = unwrap_receiver_type(symbols, receiver_type);
    const auto klass = symbols.classes.find(class_name);
    if (klass == symbols.classes.end()) {
        return std::nullopt;
    }
    size_t component_count = 0;
    for (const char ch : *component_set) {
        if (field_type_for_class(symbols, *klass->second, receiver_type, std::string(1, ch))) {
            ++component_count;
        }
    }
    if (component_count != swizzle.size()) {
        return std::nullopt;
    }
    for (const char ch : swizzle) {
        if (!field_type_for_class(symbols, *klass->second, receiver_type, std::string(1, ch))) {
            return std::nullopt;
        }
    }
    return class_name;
}

std::optional<std::string> swizzle_assignment_type_for_type(const Symbols& symbols,
                                                            const SourceLocation& location,
                                                            const std::string& receiver_type,
                                                            const std::string& swizzle) {
    const auto component_set = swizzle_component_set(swizzle);
    if (!component_set) {
        return std::nullopt;
    }
    std::set<char> seen;
    for (const char ch : swizzle) {
        if (!seen.insert(ch).second) {
            fail(location, "swizzle assignment cannot repeat component: " + swizzle);
        }
    }
    return swizzle_type_for_type(symbols, receiver_type, swizzle);
}

bool method_signature_for_type(const Symbols& symbols, std::string receiver_type,
                               const std::string& method_name, FunctionSignature& signature,
                               const SourceLocation* location) {
    if (builtin_cpp_method_signature(symbols, receiver_type, method_name, signature)) {
        return true;
    }
    const std::string templated_receiver = receiver_template_type(symbols, receiver_type);
    const std::vector<std::string> receiver_args = template_args_from_type(templated_receiver);
    const std::string type = unwrap_receiver_type(symbols, std::move(receiver_type));
    const std::string lookup_name = template_method_name(method_name);
    const std::vector<std::string> method_args = template_method_args(method_name);
    const auto klass = symbols.classes.find(type);
    if (klass == symbols.classes.end()) {
        return false;
    }
    for (const FunctionDecl& method : klass->second->methods) {
        if (method.name != lookup_name) {
            continue;
        }
        if (method.generic_params.size() != method_args.size()) {
            if (location != nullptr) {
                fail(*location, "method " + type + "." + lookup_name + " expects " +
                                    std::to_string(method.generic_params.size()) +
                                    " type arguments, got " + std::to_string(method_args.size()));
            }
            return false;
        }
        const size_t first_param =
            !method.params.empty() && method.params.front().name == "self" ? 1 : 0;
        for (size_t i = first_param; i < method.params.size(); ++i) {
            std::string param_type = substitute_method_template_type(
                method.params[i].type, method.generic_params, method_args);
            param_type = substitute_class_template_type(
                std::move(param_type), klass->second->generic_params, receiver_args);
            signature.params.push_back(
                substitute_receiver_template_type(std::move(param_type), receiver_args));
        }
        signature.return_type = method.return_type.empty()
                                    ? "void"
                                    : substitute_method_template_type(
                                          method.return_type, method.generic_params, method_args);
        signature.return_type = substitute_class_template_type(
            std::move(signature.return_type), klass->second->generic_params, receiver_args);
        signature.return_type =
            substitute_receiver_template_type(std::move(signature.return_type), receiver_args);
        return true;
    }
    for (const std::string& base : klass->second->base_classes) {
        if (method_signature_for_type(symbols, base, method_name, signature, nullptr)) {
            return true;
        }
    }
    if (location != nullptr) {
        fail(*location, "unknown method: " + type + "." + method_name);
    }
    return false;
}

std::vector<FunctionSignature> method_signatures_for_type(const Symbols& symbols,
                                                          std::string receiver_type,
                                                          const std::string& method_name) {
    FunctionSignature builtin;
    if (builtin_cpp_method_signature(symbols, receiver_type, method_name, builtin)) {
        return {builtin};
    }
    std::vector<FunctionSignature> out;
    const std::string templated_receiver = receiver_template_type(symbols, receiver_type);
    const std::vector<std::string> receiver_args = template_args_from_type(templated_receiver);
    const std::string type = unwrap_receiver_type(symbols, std::move(receiver_type));
    const std::string lookup_name = template_method_name(method_name);
    const std::vector<std::string> method_args = template_method_args(method_name);
    const auto klass = symbols.classes.find(type);
    if (klass == symbols.classes.end()) {
        return out;
    }
    for (const FunctionDecl& method : klass->second->methods) {
        if (method.name != lookup_name) {
            continue;
        }
        if (method.generic_params.size() != method_args.size()) {
            continue;
        }
        FunctionSignature signature;
        const size_t first_param =
            !method.params.empty() && method.params.front().name == "self" ? 1 : 0;
        for (size_t i = first_param; i < method.params.size(); ++i) {
            std::string param_type = substitute_method_template_type(
                method.params[i].type, method.generic_params, method_args);
            param_type = substitute_class_template_type(
                std::move(param_type), klass->second->generic_params, receiver_args);
            signature.params.push_back(
                substitute_receiver_template_type(std::move(param_type), receiver_args));
        }
        signature.return_type = method.return_type.empty()
                                    ? "void"
                                    : substitute_method_template_type(
                                          method.return_type, method.generic_params, method_args);
        signature.return_type = substitute_class_template_type(
            std::move(signature.return_type), klass->second->generic_params, receiver_args);
        signature.return_type =
            substitute_receiver_template_type(std::move(signature.return_type), receiver_args);
        out.push_back(std::move(signature));
    }
    for (const std::string& base : klass->second->base_classes) {
        std::vector<FunctionSignature> base_signatures =
            method_signatures_for_type(symbols, base, method_name);
        out.insert(out.end(), base_signatures.begin(), base_signatures.end());
    }
    return out;
}

bool static_method_signature_for_type(const Symbols& symbols, const std::string& type_name,
                                      const std::string& method_name, FunctionSignature& signature,
                                      const SourceLocation* location) {
    const auto klass = symbols.classes.find(type_name);
    if (klass == symbols.classes.end()) {
        return false;
    }
    for (const FunctionDecl& method : klass->second->methods) {
        if (method.name != method_name || !method_is_static(method)) {
            continue;
        }
        for (const ParamDecl& param : method.params) {
            signature.params.push_back(param.type);
        }
        signature.return_type = method.return_type.empty() ? "void" : method.return_type;
        return true;
    }
    if (location != nullptr) {
        fail(*location, "unknown static method: " + type_name + "." + method_name);
    }
    return false;
}

} // namespace dudu
