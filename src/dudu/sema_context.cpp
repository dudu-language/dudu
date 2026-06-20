#include "dudu/sema_context.hpp"

#include "dudu/ast_type.hpp"
#include "dudu/cpp_lower.hpp"
#include "dudu/sema_function_type.hpp"
#include "dudu/sema_inheritance.hpp"
#include "dudu/source.hpp"

#include <algorithm>
#include <cctype>
#include <optional>
#include <set>
#include <sstream>
#include <utility>
#include <vector>

namespace dudu {
namespace {

bool is_builtin_type(const std::string& type) {
    static const std::set<std::string> builtins = {"bool", "char", "i8",   "i16", "i32",   "i64",
                                                   "u8",   "u16",  "u32",  "u64", "isize", "usize",
                                                   "f32",  "f64",  "void", "str", "cstr"};
    return builtins.contains(type);
}

std::string build_literal(const std::string& value) {
    if (value == "true" || value == "false") {
        return value;
    }
    if (!value.empty() && std::all_of(value.begin(), value.end(), [](char c) {
            return std::isdigit(static_cast<unsigned char>(c)) != 0;
        })) {
        return value;
    }
    if (value.size() >= 2 && value.front() == '"' && value.back() == '"') {
        return value;
    }
    return '"' + value + '"';
}

TypeRef build_value_type_ref(const std::string& value) {
    const std::string literal = build_literal(value);
    if (literal == "true" || literal == "false") {
        return named_type_ref("bool");
    }
    if (!literal.empty() && literal.front() == '"') {
        return named_type_ref("str");
    }
    return named_type_ref("i32");
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

[[noreturn]] void fail(const SourceLocation& location, const std::string& message) {
    throw CompileError(location, message);
}

void add_name(std::map<std::string, SourceLocation>& names, const std::string& name,
              const SourceLocation& location) {
    const auto [it, inserted] = names.emplace(name, location);
    if (!inserted) {
        fail(location, "duplicate declaration: " + name);
    }
}

std::string compute_base_type(const TypeRef& type) {
    switch (type.kind) {
    case TypeKind::Named:
    case TypeKind::Qualified:
        return type_ref_head_name(type);
    case TypeKind::Template:
        return type_ref_head_name(type);
    case TypeKind::Pointer:
    case TypeKind::Reference:
        if (!type.children.empty()) {
            return compute_base_type(type.children.front());
        }
        return {};
    case TypeKind::Const:
        return "const";
    case TypeKind::Volatile:
        return "volatile";
    case TypeKind::Atomic:
        return "atomic";
    case TypeKind::Device:
        return "device";
    case TypeKind::Storage:
        return "storage";
    case TypeKind::Shared:
        return "shared";
    case TypeKind::Static:
        return "static";
    case TypeKind::FixedArray:
    case TypeKind::PackExpansion:
        if (!type.children.empty()) {
            return compute_base_type(type.children.front());
        }
        return {};
    case TypeKind::Function:
        return type_ref_head_name(type);
    case TypeKind::Value:
        return type_ref_head_name(type);
    case TypeKind::Unknown:
        return {};
    }
    return {};
}

} // namespace

std::string trim(std::string text) {
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front())) != 0) {
        text.erase(text.begin());
    }
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back())) != 0) {
        text.pop_back();
    }
    return text;
}

std::string base_type(const TypeRef& type) {
    return compute_base_type(type);
}

bool known_type_ref(const Symbols& symbols, const TypeRef& type) {
    const std::string base = base_type(type);
    return !has_type_ref(type) || is_builtin_type(base) || symbols.types.contains(base) ||
           base.find('.') != std::string::npos || starts_with(base, "struct ") || base == "list" ||
           base == "array" || base == "span" || base == "strided_span" || base == "dict" ||
           base == "set" || base == "tuple" || base == "variant" || base == "Result" ||
           base == "Option" || base == "fn" || base == "const" || base == "atomic" ||
           base == "volatile" || base == "storage" || base == "shared" || base == "device";
}

std::optional<std::pair<std::string, SourceLocation>> unknown_type_ref(const Symbols& symbols,
                                                                       const TypeRef& type) {
    switch (type.kind) {
    case TypeKind::Named:
    case TypeKind::Qualified:
        return known_type_ref(symbols, type)
                   ? std::nullopt
                   : std::optional<std::pair<std::string, SourceLocation>>{
                         std::pair{type_ref_head_name(type), type.location}};
    case TypeKind::Value:
        return std::nullopt;
    case TypeKind::Template:
        if (!known_type_ref(symbols, type)) {
            return std::pair{type.name, type.location};
        }
        for (const TypeRef& child : type.children) {
            if (const auto unknown = unknown_type_ref(symbols, child)) {
                return unknown;
            }
        }
        return std::nullopt;
    case TypeKind::Pointer:
    case TypeKind::Reference:
    case TypeKind::Const:
    case TypeKind::Volatile:
    case TypeKind::Atomic:
    case TypeKind::Device:
    case TypeKind::Storage:
    case TypeKind::Shared:
    case TypeKind::Static:
    case TypeKind::FixedArray:
    case TypeKind::Function:
    case TypeKind::PackExpansion:
        for (const TypeRef& child : type.children) {
            if (const auto unknown = unknown_type_ref(symbols, child)) {
                return unknown;
            }
        }
        return std::nullopt;
    case TypeKind::Unknown:
        return std::nullopt;
    }
    return std::nullopt;
}

void check_known_type_ref(const Symbols& symbols, const SourceLocation& location,
                          const TypeRef& type, const std::string& message) {
    if (const auto unknown = unknown_type_ref(symbols, type)) {
        const SourceLocation error_location = unknown->second.line > 0 ? unknown->second : location;
        fail(error_location, message + unknown->first);
    }
}

TypeRef resolve_alias_ref_impl(const Symbols& symbols, TypeRef type, std::set<std::string>& seen) {
    std::vector<std::string> inserted_names;
    while (true) {
        if (type.kind != TypeKind::Named && type.kind != TypeKind::Qualified) {
            break;
        }
        const std::string name = type.name;
        if (!seen.insert(name).second) {
            break;
        }
        inserted_names.push_back(name);
        const auto found = symbols.alias_type_refs.find(name);
        if (found == symbols.alias_type_refs.end()) {
            break;
        }
        type = found->second;
    }
    for (TypeRef& child : type.children) {
        child = resolve_alias_ref_impl(symbols, std::move(child), seen);
    }
    for (const std::string& name : inserted_names) {
        seen.erase(name);
    }
    return type;
}

TypeRef resolve_alias_ref(const Symbols& symbols, TypeRef type) {
    std::set<std::string> seen;
    return resolve_alias_ref_impl(symbols, std::move(type), seen);
}

std::vector<std::string> split_top_level(std::string text) {
    std::vector<std::string> out;
    int depth = 0;
    char quote = '\0';
    bool escaped = false;
    size_t start = 0;
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
        if (c == '(' || c == '[' || c == '{') {
            ++depth;
        } else if (c == ')' || c == ']' || c == '}') {
            --depth;
        } else if (c == ',' && depth == 0) {
            out.push_back(trim(text.substr(start, i - start)));
            start = i + 1;
        }
    }
    out.push_back(trim(text.substr(start)));
    return out;
}

size_t find_top_level_char(const std::string& text, char wanted) {
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
        if (c == '(' || c == '[' || c == '{') {
            ++depth;
        } else if (c == ')' || c == ']' || c == '}') {
            --depth;
        } else if (c == wanted && depth == 0) {
            return i;
        }
    }
    return std::string::npos;
}

void add_native_path_prefix(Symbols& symbols, const std::string& name) {
    const size_t dot = name.find('.');
    if (dot != std::string::npos && dot > 0) {
        symbols.native_path_prefixes.insert(name.substr(0, dot));
    }
}

Symbols collect_symbols(const ModuleAst& module) {
    Symbols symbols;
    for (const ImportDecl& import : module.imports) {
        if (import.kind == ImportKind::ForeignCpp && !import.alias.empty() &&
            import.module_path.find('/') == std::string::npos &&
            import.module_path.find('\\') == std::string::npos) {
            // Standard and system headers often expose callable templates that
            // Clang reports incompletely through the scanner. Keep this limited
            // to explicit template-call syntax so ordinary calls still require
            // scanner metadata.
            symbols.native_explicit_template_prefixes.insert(import.alias);
        }
    }
    for (const std::string& prefix : module.module_import_prefixes) {
        symbols.module_import_prefixes.insert(prefix);
    }
    std::map<std::string, SourceLocation> names;
    for (const char* type : {"bool", "char", "i8", "i16", "i32", "i64", "u8", "u16", "u32", "u64",
                             "isize", "usize", "f32", "f64", "void", "str", "cstr"}) {
        symbols.types.insert(type);
    }
    for (const TypeAliasDecl& alias : module.aliases) {
        add_name(names, alias.name, alias.location);
        symbols.types.insert(alias.name);
        symbols.alias_type_refs[alias.name] = alias.type_ref;
    }
    for (const NativeTypeDecl& type : module.native_types) {
        symbols.types.insert(type.name);
        symbols.native_types.insert(type.name);
        add_native_path_prefix(symbols, type.name);
        if ((has_type_ref(type.type_ref) || !type.native_spelling.empty()) &&
            !symbols.alias_type_refs.contains(type.name)) {
            symbols.alias_type_refs[type.name] = native_type_alias_type_ref(type);
        }
    }
    for (const NativeValueDecl& value : module.native_values) {
        add_name(names, value.name, value.location);
        symbols.native_value_type_refs[value.name] = native_value_type_ref(value);
        if (value.enum_constant) {
            symbols.native_enum_values.insert(value.name);
        }
        add_native_path_prefix(symbols, value.name);
    }
    std::map<std::string, std::string> build_values = {{"DEBUG", "false"},
                                                       {"RENDER_BACKEND", "\"vulkan\""}};
    for (const auto& [name, value] : module.build_values) {
        build_values[name] = value;
    }
    for (const auto& [name, value] : build_values) {
        const std::string symbol_name = "build." + name;
        symbols.native_value_type_refs[symbol_name] = build_value_type_ref(value);
    }
    symbols.native_path_prefixes.insert("build");
    for (const NativeNamespaceDecl& ns : module.native_namespaces) {
        symbols.native_path_prefixes.insert(ns.name);
    }
    for (const EnumDecl& en : module.enums) {
        add_name(names, en.name, en.location);
        symbols.types.insert(en.name);
        symbols.enums[en.name] = &en;
    }
    for (const ClassDecl& klass : module.classes) {
        add_name(names, klass.name, klass.location);
        symbols.types.insert(klass.name);
        symbols.classes[klass.name] = &klass;
        for (const ConstDecl& constant : klass.constants) {
            const std::string symbol_name = klass.name + "." + constant.name;
            symbols.native_value_type_refs[symbol_name] = constant.type_ref;
        }
    }
    for (const ClassDecl& klass : module.native_classes) {
        symbols.types.insert(klass.name);
        add_native_path_prefix(symbols, klass.name);
        const auto [it, inserted] = symbols.native_classes.emplace(klass.name, klass);
        (void)inserted;
        symbols.classes[klass.name] = &it->second;
        const std::string untagged = strip_c_type_tag(klass.name);
        if (untagged != klass.name) {
            symbols.types.insert(untagged);
            symbols.classes[untagged] = &it->second;
        }
    }
    for (const FunctionDecl& fn : module.functions) {
        add_name(names, fn.name, fn.location);
        FunctionSignature signature;
        std::vector<TypeRef> param_types;
        param_types.reserve(fn.params.size());
        for (const ParamDecl& param : fn.params) {
            param_types.push_back(param.type_ref);
        }
        set_signature_param_types(signature, std::move(param_types));
        set_signature_return_type(signature, function_return_type_ref(fn));
        symbols.function_signatures[fn.name] = std::move(signature);
        symbols.function_decls[fn.name] = &fn;
    }
    for (const NativeFunctionDecl& fn : module.native_functions) {
        FunctionSignature signature;
        signature.template_params = fn.template_params;
        set_signature_param_types(signature, native_function_param_type_refs(fn));
        set_signature_return_type(signature, native_function_return_type_ref(fn));
        signature.min_params = fn.min_params;
        signature.variadic = fn.variadic;
        symbols.native_function_signatures[fn.name].push_back(std::move(signature));
        add_native_path_prefix(symbols, fn.name);
    }
    for (const ConstDecl& constant : module.constants) {
        add_name(names, constant.name, constant.location);
    }
    return symbols;
}

} // namespace dudu
