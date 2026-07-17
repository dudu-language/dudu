#include "dudu/native/native_header_scope.hpp"

#include "dudu/core/ast_type.hpp"
#include "dudu/core/text.hpp"

#include <algorithm>
#include <optional>
#include <set>

namespace dudu {
namespace {

bool scan_has_type(const NativeHeaderScan& scan, const std::string& name) {
    for (const NativeTypeDecl& type : scan.types)
        if (type.name == name)
            return true;
    for (const ClassDecl& klass : scan.classes)
        if (klass.name == name)
            return true;
    return false;
}

bool dudu_builtin_type(const std::string& name) {
    static const std::set<std::string> names = {
        "auto", "bool", "char", "cstr", "f32", "f64", "i8", "i16", "i32",
        "i64",  "isize", "str", "u8",   "u16", "u32", "u64", "usize", "void",
    };
    return names.contains(name);
}

std::optional<std::string>
class_scoped_type_name(const NativeHeaderScan& scan,
                       const std::vector<NativeClassScope>* classes,
                       const std::string& name) {
    if (classes == nullptr) {
        return std::nullopt;
    }
    for (auto it = classes->rbegin(); it != classes->rend(); ++it) {
        const ClassDecl* klass = it->declaration_index
                                     ? &scan.classes[*it->declaration_index]
                                     : nullptr;
        if (klass != nullptr &&
            std::ranges::find(klass->generic_params, name) != klass->generic_params.end()) {
            return name;
        }
        const size_t class_dot = it->name.rfind('.');
        const std::string_view class_short_name =
            class_dot == std::string::npos ? std::string_view{it->name}
                                           : std::string_view{it->name}.substr(class_dot + 1);
        if (name == class_short_name) {
            return it->name;
        }
        const size_t dot = name.find('.');
        const std::string_view first =
            dot == std::string::npos ? std::string_view{name}
                                     : std::string_view{name}.substr(0, dot);
        if (klass != nullptr &&
            std::ranges::any_of(klass->type_aliases, [&](const TypeAliasDecl& alias) {
                return alias.name == first;
            })) {
            return it->name + "." + name;
        }
        const std::string nested = it->name + "." + name;
        if (scan_has_type(scan, nested)) {
            return nested;
        }
    }
    return std::nullopt;
}

std::optional<std::string>
namespace_scoped_type_name(const NativeHeaderScan& scan,
                           const std::vector<std::pair<int, std::string>>& namespaces,
                           const std::string& name) {
    for (size_t count = namespaces.size(); count > 0; --count) {
        std::string scoped;
        for (size_t i = 0; i < count; ++i) {
            scoped += namespaces[i].second + ".";
        }
        scoped += name;
        if (scan_has_type(scan, scoped)) {
            return scoped;
        }
    }
    return std::nullopt;
}

size_t top_level_associated_dot(std::string_view type) {
    size_t result = std::string_view::npos;
    int bracket_depth = 0;
    for (size_t i = 0; i < type.size(); ++i) {
        if (type[i] == '[') {
            ++bracket_depth;
        } else if (type[i] == ']') {
            --bracket_depth;
        } else if (type[i] == '.' && bracket_depth == 0) {
            result = i;
        }
    }
    return result;
}

std::string qualify_scoped_type_impl(const NativeHeaderScan& scan,
                                     const std::vector<std::pair<int, std::string>>& namespaces,
                                     const std::vector<NativeClassScope>* classes,
                                     std::string type) {
    type = trim_string(std::move(type));
    if (type.empty())
        return type;
    if (type.front() == '*' || type.front() == '&')
        return std::string(1, type.front()) +
               qualify_scoped_type_impl(scan, namespaces, classes, type.substr(1));
    for (const char* wrapper : {"const", "volatile", "atomic", "storage", "shared", "device"}) {
        const std::string prefix = std::string(wrapper) + "[";
        if (starts_with(type, prefix) && ends_with(type, "]"))
            return prefix +
                   qualify_scoped_type_impl(
                       scan, namespaces, classes,
                       type.substr(prefix.size(), type.size() - prefix.size() - 1)) +
                   "]";
    }
    const size_t open = type.find('[');
    const size_t associated_dot = top_level_associated_dot(type);
    if (associated_dot != std::string::npos && associated_dot > 0 &&
        type[associated_dot - 1] == ']') {
        return qualify_scoped_type_impl(scan, namespaces, classes, type.substr(0, associated_dot)) +
               type.substr(associated_dot);
    }
    if (open != std::string::npos && ends_with(type, "]")) {
        std::string out =
            qualify_scoped_type_impl(scan, namespaces, classes, type.substr(0, open)) + "[";
        bool first = true;
        for (std::string arg :
             split_top_level_args(type.substr(open + 1, type.size() - open - 2))) {
            if (!first) {
                out += ", ";
            }
            first = false;
            out += qualify_scoped_type_impl(scan, namespaces, classes, std::move(arg));
        }
        out.push_back(']');
        return out;
    }
    if (dudu_builtin_type(type)) {
        return type;
    }
    if (const auto scoped = class_scoped_type_name(scan, classes, type)) {
        return *scoped;
    }
    if (const auto scoped = namespace_scoped_type_name(scan, namespaces, type)) {
        return *scoped;
    }
    if (type.find('.') != std::string::npos)
        return type;
    return type;
}

std::string generic_param_name(std::string name) {
    while (name.ends_with("...")) {
        name.resize(name.size() - 3);
    }
    return trim_string(std::move(name));
}

void add_generic_params(std::set<std::string>& protected_names,
                        const std::vector<std::string>& params) {
    for (const std::string& param : params) {
        protected_names.insert(generic_param_name(param));
    }
}

std::vector<std::string> enclosing_scopes(std::string name, bool include_name) {
    std::vector<std::string> scopes;
    if (!include_name) {
        const size_t dot = name.rfind('.');
        if (dot == std::string::npos) {
            return scopes;
        }
        name.resize(dot);
    }
    while (!name.empty()) {
        scopes.push_back(name);
        const size_t dot = name.rfind('.');
        if (dot == std::string::npos) {
            break;
        }
        name.resize(dot);
    }
    return scopes;
}

void qualify_completed_type_ref(TypeRef& type, const NativeHeaderScan& scan,
                                const std::vector<std::string>& scopes,
                                const std::set<std::string>& protected_names) {
    for (TypeRef& child : type.children) {
        qualify_completed_type_ref(child, scan, scopes, protected_names);
    }
    if (type.kind != TypeKind::Named && type.kind != TypeKind::Qualified &&
        type.kind != TypeKind::Template) {
        return;
    }
    const std::string name = trim_string(type.name);
    for (const std::string& protected_name : protected_names) {
        if (name == protected_name) {
            return;
        }
        for (const std::string& scope : scopes) {
            if (name == scope + "." + protected_name) {
                type.kind = TypeKind::Named;
                type.name = protected_name;
                return;
            }
        }
    }
    if (name.empty() || name.find('.') != std::string::npos ||
        name.find("::") != std::string::npos || dudu_builtin_type(name) ||
        scan_has_type(scan, name)) {
        return;
    }
    for (const std::string& scope : scopes) {
        const std::string candidate = scope + "." + name;
        if (scan_has_type(scan, candidate)) {
            type.name = candidate;
            return;
        }
    }
}

void qualify_default_args(std::vector<TypeRef>& defaults, const NativeHeaderScan& scan,
                          const std::vector<std::string>& scopes,
                          const std::set<std::string>& protected_names) {
    for (TypeRef& type : defaults) {
        qualify_completed_type_ref(type, scan, scopes, protected_names);
    }
}

void qualify_function(FunctionDecl& fn, const NativeHeaderScan& scan,
                      const std::vector<std::string>& scopes,
                      std::set<std::string> protected_names) {
    add_generic_params(protected_names, fn.generic_params);
    qualify_default_args(fn.generic_default_args, scan, scopes, protected_names);
    qualify_completed_type_ref(fn.receiver_type_ref, scan, scopes, protected_names);
    qualify_completed_type_ref(fn.return_type_ref, scan, scopes, protected_names);
    for (ParamDecl& param : fn.params) {
        qualify_completed_type_ref(param.type_ref, scan, scopes, protected_names);
    }
}

} // namespace

std::string join_scope(const std::vector<std::pair<int, std::string>>& namespaces,
                       const std::string& name) {
    std::string out;
    for (const auto& [depth, ns] : namespaces) {
        (void)depth;
        out += ns + ".";
    }
    return out + name;
}

std::string class_name(const NativeHeaderScan& scan,
                       const std::vector<std::pair<int, std::string>>& namespaces,
                       const std::vector<NativeClassScope>& classes,
                       const std::string& name) {
    (void)scan;
    if (!classes.empty())
        return classes.back().name + "." + name;
    return join_scope(namespaces, name);
}

std::string qualify_scoped_type(const NativeHeaderScan& scan,
                                const std::vector<std::pair<int, std::string>>& namespaces,
                                std::string type) {
    return qualify_scoped_type_impl(scan, namespaces, nullptr, std::move(type));
}

std::string qualify_scoped_type(const NativeHeaderScan& scan,
                                const std::vector<std::pair<int, std::string>>& namespaces,
                                const std::vector<NativeClassScope>& classes,
                                std::string type) {
    return qualify_scoped_type_impl(scan, namespaces, &classes, std::move(type));
}

std::vector<std::string> qualify_scoped_types(
    const NativeHeaderScan& scan, const std::vector<std::pair<int, std::string>>& namespaces,
    const std::vector<NativeClassScope>& classes, std::vector<std::string> types) {
    for (std::string& type : types)
        type = qualify_scoped_type(scan, namespaces, classes, std::move(type));
    return types;
}

std::vector<std::string>
qualify_scoped_types(const NativeHeaderScan& scan,
                     const std::vector<std::pair<int, std::string>>& namespaces,
                     std::vector<std::string> types) {
    for (std::string& type : types)
        type = qualify_scoped_type(scan, namespaces, std::move(type));
    return types;
}

void qualify_completed_native_scan(NativeHeaderScan& scan) {
    for (NativeTypeDecl& type : scan.types) {
        std::set<std::string> protected_names;
        add_generic_params(protected_names, type.generic_params);
        const std::vector<std::string> scopes = enclosing_scopes(type.name, false);
        qualify_completed_type_ref(type.type_ref, scan, scopes, protected_names);
        qualify_default_args(type.generic_default_args, scan, scopes, protected_names);
    }
    for (NativeValueDecl& value : scan.values) {
        qualify_completed_type_ref(value.type_ref, scan, enclosing_scopes(value.name, false), {});
    }
    for (NativeFunctionDecl& fn : scan.functions) {
        std::set<std::string> protected_names;
        add_generic_params(protected_names, fn.template_params);
        const std::vector<std::string> scopes = enclosing_scopes(fn.name, false);
        qualify_default_args(fn.template_default_args, scan, scopes, protected_names);
        qualify_completed_type_ref(fn.return_type_ref, scan, scopes, protected_names);
        for (TypeRef& param : fn.param_type_refs) {
            qualify_completed_type_ref(param, scan, scopes, protected_names);
        }
    }
    for (ClassDecl& klass : scan.classes) {
        std::set<std::string> protected_names;
        add_generic_params(protected_names, klass.generic_params);
        const std::vector<std::string> class_scopes = enclosing_scopes(klass.name, true);
        const std::vector<std::string> declaration_scopes = enclosing_scopes(klass.name, false);
        qualify_default_args(klass.generic_default_args, scan, declaration_scopes,
                             protected_names);
        qualify_default_args(klass.native_specialization_args, scan, declaration_scopes,
                             protected_names);
        qualify_default_args(klass.native_specialization_requirements, scan, declaration_scopes,
                             protected_names);
        for (BaseClassDecl& base : klass.base_class_refs) {
            qualify_completed_type_ref(base.type_ref, scan, declaration_scopes, protected_names);
        }
        for (TypeAliasDecl& alias : klass.type_aliases) {
            std::set<std::string> alias_names = protected_names;
            add_generic_params(alias_names, alias.generic_params);
            qualify_completed_type_ref(alias.type_ref, scan, class_scopes, alias_names);
            qualify_default_args(alias.generic_default_args, scan, class_scopes, alias_names);
        }
        for (FieldDecl& field : klass.fields) {
            qualify_completed_type_ref(field.type_ref, scan, class_scopes, protected_names);
        }
        for (ConstDecl& field : klass.static_fields) {
            qualify_completed_type_ref(field.type_ref, scan, class_scopes, protected_names);
        }
        for (FunctionDecl& method : klass.methods) {
            qualify_function(method, scan, class_scopes, protected_names);
        }
    }
}

} // namespace dudu
