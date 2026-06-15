#include "dudu/sema_context.hpp"

#include "dudu/cpp_lower.hpp"
#include "dudu/source.hpp"

#include <cctype>
#include <optional>
#include <sstream>
#include <utility>

namespace dudu {
namespace {

bool is_builtin_type(const std::string& type) {
    static const std::set<std::string> builtins = {"bool", "i8",   "i16", "i32",   "i64",   "u8",
                                                   "u16",  "u32",  "u64", "isize", "usize", "f32",
                                                   "f64",  "void", "str", "cstr"};
    return builtins.contains(type);
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

void check_supported_type_shape(const SourceLocation& location, const TypeRef& type) {
    if (type.kind == TypeKind::Template && type.name == "tuple" &&
        (type.children.empty() || type.children.size() > 8)) {
        const SourceLocation error_location = type.location.line > 0 ? type.location : location;
        fail(error_location,
             "tuple supports 1 to 8 elements, got " + std::to_string(type.children.size()));
    }
    for (const TypeRef& child : type.children) {
        check_supported_type_shape(location, child);
    }
}

bool decorator_is_call(std::string_view text, std::string_view name) {
    return !text.empty() && starts_with(text, std::string(name) + "(") && text.back() == ')';
}

bool has_decorator(const FunctionDecl& fn, std::string_view name) {
    for (const Decorator& decorator : fn.decorators) {
        if (trim(decorator.text) == name) {
            return true;
        }
    }
    return false;
}

bool is_test_decorator(const FunctionDecl& fn) {
    for (const Decorator& decorator : fn.decorators) {
        const std::string text = trim(decorator.text);
        if (text == "test" || text == "test.ignore" || text == "test.should_panic" ||
            decorator_is_call(text, "test.should_panic"))
            return true;
    }
    return false;
}

bool is_virtual_like(const FunctionDecl& fn) {
    return has_decorator(fn, "virtual") || has_decorator(fn, "abstract");
}

FunctionSignature method_signature_without_self(const FunctionDecl& method) {
    FunctionSignature signature;
    const size_t first_param =
        !method.params.empty() && method.params.front().name == "self" ? 1 : 0;
    for (size_t i = first_param; i < method.params.size(); ++i) {
        signature.params.push_back(method.params[i].type);
    }
    signature.return_type = method.return_type.empty() ? "void" : method.return_type;
    return signature;
}

const FunctionDecl* find_method_decl(const Symbols& symbols, const std::string& type,
                                     const std::string& name) {
    const auto klass = symbols.classes.find(base_type(type));
    if (klass == symbols.classes.end()) {
        return nullptr;
    }
    for (const FunctionDecl& method : klass->second->methods) {
        if (method.name == name) {
            return &method;
        }
    }
    for (const std::string& base : klass->second->base_classes) {
        if (const FunctionDecl* method = find_method_decl(symbols, base, name)) {
            return method;
        }
    }
    return nullptr;
}

bool same_signature(const FunctionSignature& a, const FunctionSignature& b) {
    return a.return_type == b.return_type && a.params == b.params;
}

std::string method_key_without_self(const FunctionDecl& method) {
    const FunctionSignature signature = method_signature_without_self(method);
    std::ostringstream out;
    out << method.name << '(';
    for (size_t i = 0; i < signature.params.size(); ++i) {
        if (i > 0) {
            out << ", ";
        }
        out << signature.params[i];
    }
    out << ") -> " << signature.return_type;
    return out.str();
}

const ClassDecl* dudu_class_for_base(const Symbols& symbols, const std::string& base) {
    const auto found = symbols.classes.find(base_type(base));
    return found == symbols.classes.end() ? nullptr : found->second;
}

bool class_has_instance_storage(const Symbols& symbols, const ClassDecl& klass,
                                std::set<std::string>& seen) {
    if (!seen.insert(klass.name).second) {
        return false;
    }
    if (!klass.fields.empty()) {
        return true;
    }
    for (const std::string& base : klass.base_classes) {
        if (const ClassDecl* parent = dudu_class_for_base(symbols, base)) {
            if (class_has_instance_storage(symbols, *parent, seen)) {
                return true;
            }
        }
    }
    return false;
}

bool class_has_instance_storage(const Symbols& symbols, const ClassDecl& klass) {
    std::set<std::string> seen;
    return class_has_instance_storage(symbols, klass, seen);
}

bool class_has_abstract_method(const Symbols& symbols, const ClassDecl& klass,
                               std::set<std::string>& seen) {
    if (!seen.insert(klass.name).second) {
        return false;
    }
    for (const FunctionDecl& method : klass.methods) {
        if (has_decorator(method, "abstract")) {
            return true;
        }
    }
    for (const std::string& base : klass.base_classes) {
        if (const ClassDecl* parent = dudu_class_for_base(symbols, base)) {
            if (class_has_abstract_method(symbols, *parent, seen)) {
                return true;
            }
        }
    }
    return false;
}

bool class_has_abstract_method(const Symbols& symbols, const ClassDecl& klass) {
    std::set<std::string> seen;
    return class_has_abstract_method(symbols, klass, seen);
}

bool interface_like_base(const Symbols& symbols, const ClassDecl& klass) {
    return !class_has_instance_storage(symbols, klass) && class_has_abstract_method(symbols, klass);
}

void collect_inherited_fields(const Symbols& symbols, const ClassDecl& klass,
                              std::map<std::string, std::string>& fields,
                              std::set<std::string>& seen) {
    if (!seen.insert(klass.name).second) {
        return;
    }
    for (const FieldDecl& field : klass.fields) {
        fields.emplace(field.name, klass.name);
    }
    for (const std::string& base : klass.base_classes) {
        if (const ClassDecl* parent = dudu_class_for_base(symbols, base)) {
            collect_inherited_fields(symbols, *parent, fields, seen);
        }
    }
}

void collect_concrete_methods(const Symbols& symbols, const ClassDecl& klass,
                              std::map<std::string, std::string>& methods,
                              std::set<std::string>& seen) {
    if (!seen.insert(klass.name).second) {
        return;
    }
    for (const FunctionDecl& method : klass.methods) {
        const bool is_static = method.params.empty() || method.params.front().name != "self";
        if (!is_static && !has_decorator(method, "abstract")) {
            methods.emplace(method_key_without_self(method), klass.name);
        }
    }
    for (const std::string& base : klass.base_classes) {
        if (const ClassDecl* parent = dudu_class_for_base(symbols, base)) {
            collect_concrete_methods(symbols, *parent, methods, seen);
        }
    }
}

void check_multiple_inheritance_rules(const Symbols& symbols, const ClassDecl& klass) {
    if (klass.base_classes.size() <= 1) {
        return;
    }
    size_t storage_bases = 0;
    std::map<std::string, std::string> inherited_fields;
    std::map<std::string, std::string> inherited_concrete_methods;
    std::set<std::string> derived_overrides;
    for (const FunctionDecl& method : klass.methods) {
        if (has_decorator(method, "override")) {
            derived_overrides.insert(method_key_without_self(method));
        }
    }

    for (const std::string& base : klass.base_classes) {
        const ClassDecl* parent = dudu_class_for_base(symbols, base);
        if (parent == nullptr) {
            continue;
        }
        const bool has_storage = class_has_instance_storage(symbols, *parent);
        if (has_storage) {
            ++storage_bases;
            if (storage_bases > 1) {
                fail(klass.location,
                     "multiple inheritance allows at most one storage-bearing Dudu base");
            }
        } else if (!interface_like_base(symbols, *parent)) {
            fail(klass.location,
                 "multiple inheritance non-storage bases must be abstract interface-like classes");
        }

        std::map<std::string, std::string> fields;
        std::set<std::string> field_seen;
        collect_inherited_fields(symbols, *parent, fields, field_seen);
        for (const auto& [name, owner] : fields) {
            if (const auto existing = inherited_fields.find(name);
                existing != inherited_fields.end() && existing->second != owner) {
                fail(klass.location, "duplicate inherited field: " + name);
            }
            inherited_fields.emplace(name, owner);
        }

        std::map<std::string, std::string> methods;
        std::set<std::string> method_seen;
        collect_concrete_methods(symbols, *parent, methods, method_seen);
        for (const auto& [key, owner] : methods) {
            if (const auto existing = inherited_concrete_methods.find(key);
                existing != inherited_concrete_methods.end() && existing->second != owner &&
                !derived_overrides.contains(key)) {
                fail(klass.location, "ambiguous inherited concrete method: " + key);
            }
            inherited_concrete_methods.emplace(key, owner);
        }
    }
}

std::string decorator_arg(const Decorator& decorator, std::string_view name) {
    const std::string text = trim(decorator.text);
    const std::string prefix = std::string(name) + "(";
    if (text.empty() || !starts_with(text, prefix) || text.back() != ')') {
        return {};
    }
    std::string value = trim(text.substr(prefix.size(), text.size() - prefix.size() - 1));
    if (value.size() >= 2 && ((value.front() == '"' && value.back() == '"') ||
                              (value.front() == '\'' && value.back() == '\''))) {
        value = value.substr(1, value.size() - 2);
    }
    return value;
}

std::string operator_decorator_arg(const FunctionDecl& fn) {
    for (const Decorator& decorator : fn.decorators) {
        if (std::string value = decorator_arg(decorator, "operator"); !value.empty()) {
            return value;
        }
    }
    return {};
}

bool is_operator_method(const FunctionDecl& method) {
    return !operator_decorator_arg(method).empty();
}

bool is_comparison_operator_method(const FunctionDecl& method) {
    static const std::set<std::string> ops = {"==", "!=", "<", "<=", ">", ">="};
    return ops.contains(operator_decorator_arg(method));
}

bool is_reserved_dunder_name(const std::string& name) {
    return name.size() > 4 && starts_with(name, "__") && name.ends_with("__");
}

void check_generic_params(const SourceLocation& location, const std::vector<std::string>& params) {
    std::set<std::string> seen;
    for (const std::string& param : params) {
        if (!seen.insert(param).second) {
            fail(location, "duplicate generic parameter: " + param);
        }
    }
}

Symbols with_generic_params(Symbols symbols, const std::vector<std::string>& params) {
    for (const std::string& param : params) {
        symbols.types.insert(param);
    }
    return symbols;
}

bool is_constructor_method(const FunctionDecl& method) {
    return method.name == "init";
}

bool is_destructor_method(const FunctionDecl& method) {
    return method.name == "drop";
}

bool is_c_abi_primitive(const std::string& type, bool allow_void) {
    if (type == "void") {
        return allow_void;
    }
    static const std::set<std::string> primitives = {"bool",  "i8",  "i16", "i32", "i64",
                                                     "u8",    "u16", "u32", "u64", "isize",
                                                     "usize", "f32", "f64", "cstr"};
    return primitives.contains(type);
}

bool is_c_abi_type(std::string type, bool allow_void) {
    type = trim(std::move(type));
    if (type.empty() || type.front() == '&' || type == "str" ||
        type.find('.') != std::string::npos) {
        return false;
    }
    if (type.front() == '*') {
        return is_c_abi_type(type.substr(1), false) || starts_with(trim(type.substr(1)), "struct ");
    }
    return is_c_abi_primitive(type, allow_void);
}

void check_extern_c_signature(const FunctionDecl& fn) {
    const std::string return_type = fn.return_type.empty() ? "void" : fn.return_type;
    if (!is_c_abi_type(return_type, true)) {
        fail(fn.location, "@extern_c return type is not C ABI safe: " + return_type);
    }
    for (const ParamDecl& param : fn.params) {
        if (!is_c_abi_type(param.type, false)) {
            fail(param.location, "@extern_c parameter type is not C ABI safe: " + param.type);
        }
    }
}

std::string target_mode(const ModuleAst& module) {
    const auto found = module.build_values.find("TARGET_MODE");
    if (found == module.build_values.end()) {
        return "hosted";
    }
    std::string value = trim(found->second);
    if (value.size() >= 2 && value.front() == '"' && value.back() == '"') {
        return value.substr(1, value.size() - 2);
    }
    return value;
}

void check_target_decorator_mode(const ModuleAst& module, const Decorator& decorator,
                                 const std::string& text) {
    if (!module.target_mode_explicit) {
        return;
    }
    const std::string mode = target_mode(module);
    if ((text == "cuda.global" || text == "cuda.device" || text == "cuda.host") && mode != "cuda") {
        fail(decorator.location, "@" + text + " requires [target] mode = \"cuda\"");
    }
    if ((text == "shader.compute" || decorator_is_call(text, "workgroup_size")) &&
        mode != "shader") {
        fail(decorator.location, "@" + text + " requires [target] mode = \"shader\"");
    }
}

void check_class_decorator(const Decorator& decorator) {
    const std::string text = trim(decorator.text);
    if (text == "packed" || decorator_is_call(text, "align")) {
        return;
    }
    fail(decorator.location, "unknown class decorator: @" + text);
}

void check_function_decorator(const ModuleAst& module, const Decorator& decorator) {
    const std::string text = trim(decorator.text);
    if (text == "inline" || text == "constexpr" || text == "extern_c" || text == "cuda.global" ||
        text == "cuda.device" || text == "cuda.host" || text == "shader.compute" ||
        text == "virtual" || text == "override" || text == "abstract" ||
        decorator_is_call(text, "operator") || text == "test" || text == "test.ignore" ||
        text == "test.should_panic" || decorator_is_call(text, "test.should_panic") ||
        decorator_is_call(text, "workgroup_size") || decorator_is_call(text, "section")) {
        check_target_decorator_mode(module, decorator, text);
        return;
    }
    fail(decorator.location, "unknown function decorator: @" + text);
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

std::string base_type(std::string type) {
    type = trim(std::move(type));
    while (!type.empty() && (type.front() == '*' || type.front() == '&')) {
        type = trim(type.substr(1));
    }
    const size_t bracket = type.find('[');
    if (bracket != std::string::npos) {
        return trim(type.substr(0, bracket));
    }
    return type;
}

bool known_type(const Symbols& symbols, const std::string& type) {
    if (starts_with(trim(type), "fn(")) {
        return true;
    }
    const std::string base = base_type(type);
    return base.empty() || is_builtin_type(base) || symbols.types.contains(base) ||
           base.find('.') != std::string::npos || starts_with(base, "struct ") || base == "list" ||
           base == "array" || base == "span" || base == "dict" || base == "set" ||
           base == "tuple" || base == "Result" || base == "Option" || base == "fn" ||
           base == "const" || base == "atomic" || base == "volatile" || base == "storage" ||
           base == "shared" || base == "device";
}

std::optional<std::pair<std::string, SourceLocation>> unknown_type_ref(const Symbols& symbols,
                                                                       const TypeRef& type) {
    switch (type.kind) {
    case TypeKind::Named:
    case TypeKind::Qualified:
        return known_type(symbols, type.text)
                   ? std::nullopt
                   : std::optional<std::pair<std::string, SourceLocation>>{
                         std::pair{type.text, type.location}};
    case TypeKind::Value:
        return std::nullopt;
    case TypeKind::Template:
        if (!known_type(symbols, type.name)) {
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
        for (const TypeRef& child : type.children) {
            if (const auto unknown = unknown_type_ref(symbols, child)) {
                return unknown;
            }
        }
        return std::nullopt;
    case TypeKind::Unknown:
        return known_type(symbols, type.text)
                   ? std::nullopt
                   : std::optional<std::pair<std::string, SourceLocation>>{
                         std::pair{type.text, type.location}};
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

std::string resolve_alias(const Symbols& symbols, std::string type) {
    type = trim(std::move(type));
    for (size_t guard = 0; guard < symbols.aliases.size(); ++guard) {
        const auto found = symbols.aliases.find(type);
        if (found == symbols.aliases.end()) {
            return type;
        }
        type = trim(found->second);
    }
    return type;
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

std::vector<std::string> tuple_types(const Symbols& symbols, std::string type) {
    type = resolve_alias(symbols, std::move(type));
    if (!starts_with(type, "tuple[") || type.back() != ']') {
        return {};
    }
    return split_top_level(type.substr(6, type.size() - 7));
}

Symbols collect_symbols(const ModuleAst& module) {
    Symbols symbols;
    for (const ImportDecl& import : module.imports) {
        if ((import.kind == ImportKind::ForeignC || import.kind == ImportKind::ForeignCpp) &&
            !import.alias.empty()) {
            symbols.native_import_prefixes.insert(import.alias);
        }
        if (import.kind == ImportKind::ForeignCpp && !import.alias.empty() &&
            import.module_path.find('/') == std::string::npos &&
            import.module_path.find('\\') == std::string::npos) {
            symbols.native_template_fallback_prefixes.insert(import.alias);
        }
    }
    std::map<std::string, SourceLocation> names;
    for (const char* type : {"bool", "i8", "i16", "i32", "i64", "u8", "u16", "u32", "u64", "isize",
                             "usize", "f32", "f64", "void", "str", "cstr"}) {
        symbols.types.insert(type);
    }
    for (const TypeAliasDecl& alias : module.aliases) {
        add_name(names, alias.name, alias.location);
        symbols.types.insert(alias.name);
        symbols.aliases[alias.name] = alias.type;
        symbols.alias_type_refs[alias.name] = alias.type_ref;
    }
    for (const NativeTypeDecl& type : module.native_types) {
        symbols.types.insert(type.name);
        if (!type.type.empty()) {
            symbols.aliases[type.name] = type.type;
            symbols.alias_type_refs[type.name] = parse_type_text(type.type, type.location);
        }
    }
    for (const NativeValueDecl& value : module.native_values) {
        add_name(names, value.name, value.location);
        symbols.native_values[value.name] = value.type;
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
            symbols.native_values[klass.name + "." + constant.name] = constant.type;
        }
    }
    for (const ClassDecl& klass : module.native_classes) {
        symbols.types.insert(klass.name);
        const auto [it, inserted] = symbols.native_classes.emplace(klass.name, klass);
        (void)inserted;
        symbols.classes[klass.name] = &it->second;
    }
    for (const FunctionDecl& fn : module.functions) {
        add_name(names, fn.name, fn.location);
        FunctionSignature signature;
        for (const ParamDecl& param : fn.params) {
            signature.params.push_back(param.type);
        }
        signature.return_type = fn.return_type.empty() ? "void" : fn.return_type;
        symbols.functions[fn.name] = signature.return_type;
        symbols.function_signatures[fn.name] = std::move(signature);
        symbols.function_decls[fn.name] = &fn;
    }
    for (const NativeFunctionDecl& fn : module.native_functions) {
        FunctionSignature signature;
        signature.params = fn.params;
        signature.return_type = fn.return_type.empty() ? "auto" : fn.return_type;
        signature.variadic = fn.variadic;
        symbols.native_function_signatures[fn.name].push_back(std::move(signature));
    }
    for (const ConstDecl& constant : module.constants) {
        add_name(names, constant.name, constant.location);
    }
    return symbols;
}

void check_declarations(const ModuleAst& module, const Symbols& symbols) {
    for (const TypeAliasDecl& alias : module.aliases) {
        check_supported_type_shape(alias.location, alias.type_ref);
        check_known_type_ref(symbols, alias.location, alias.type_ref,
                             "unknown type alias target: ");
    }
    for (const EnumDecl& en : module.enums) {
        if (!en.underlying_type.empty()) {
            check_supported_type_shape(en.location, en.underlying_type_ref);
            check_known_type_ref(symbols, en.location, en.underlying_type_ref,
                                 "unknown enum underlying type: ");
        }
        std::set<std::string> values;
        for (const EnumValueDecl& value : en.values) {
            if (!values.insert(value.name).second) {
                fail(value.location, "duplicate enum value: " + value.name);
            }
            std::set<std::string> payload_fields;
            for (const EnumPayloadField& field : value.payload_fields) {
                check_supported_type_shape(field.location, field.type_ref);
                check_known_type_ref(symbols, field.location, field.type_ref,
                                     "unknown enum payload field type: ");
                if (!value.tuple_payload && !payload_fields.insert(field.name).second) {
                    fail(field.location, "duplicate enum payload field: " + en.name + "." +
                                             value.name + "." + field.name);
                }
            }
            if (!value.payload_fields.empty()) {
                fail(value.location,
                     "payload enum lowering requires match support: " + en.name + "." +
                         value.name);
            }
        }
    }
    for (const ClassDecl& klass : module.classes) {
        check_generic_params(klass.location, klass.generic_params);
        const Symbols class_symbols = with_generic_params(symbols, klass.generic_params);
        for (const Decorator& decorator : klass.decorators) {
            check_class_decorator(decorator);
        }
        std::set<std::string> bases;
        for (const std::string& base : klass.base_classes) {
            const TypeRef base_ref = parse_type_text(base, klass.location);
            check_supported_type_shape(klass.location, base_ref);
            check_known_type_ref(class_symbols, klass.location, base_ref,
                                 "unknown base class: ");
            if (!bases.insert(base).second) {
                fail(klass.location, "duplicate base class: " + base);
            }
        }
        check_multiple_inheritance_rules(symbols, klass);
        std::set<std::string> fields;
        for (const FieldDecl& field : klass.fields) {
            if (!fields.insert(field.name).second) {
                fail(field.location, "duplicate field: " + field.name);
            }
            check_supported_type_shape(field.location, field.type_ref);
            check_known_type_ref(class_symbols, field.location, field.type_ref,
                                 "unknown field type: ");
        }
        for (const ConstDecl& constant : klass.constants) {
            if (!fields.insert(constant.name).second) {
                fail(constant.location, "duplicate class member: " + constant.name);
            }
            check_supported_type_shape(constant.location, constant.type_ref);
            check_known_type_ref(class_symbols, constant.location, constant.type_ref,
                                 "unknown class constant type: ");
        }
        for (const ConstDecl& field : klass.static_fields) {
            if (!fields.insert(field.name).second) {
                fail(field.location, "duplicate class member: " + field.name);
            }
            check_supported_type_shape(field.location, field.type_ref);
            check_known_type_ref(class_symbols, field.location, field.type_ref,
                                 "unknown static field type: ");
        }
        for (const FunctionDecl& method : klass.methods) {
            check_generic_params(method.location, method.generic_params);
            const Symbols method_symbols =
                with_generic_params(class_symbols, method.generic_params);
            if (is_reserved_dunder_name(method.name)) {
                fail(method.location,
                     "reserved Python-style dunder method name: " + method.name +
                         "; use normal Dudu names and decorators such as @operator(...)");
            }
            if (!fields.insert(method.name).second) {
                fail(method.location, "duplicate class member: " + method.name);
            }
            const bool is_static = method.params.empty() || method.params.front().name != "self";
            const bool is_abstract = has_decorator(method, "abstract");
            const bool is_virtual = has_decorator(method, "virtual");
            const bool is_override = has_decorator(method, "override");
            if ((is_abstract || is_virtual || is_override) && is_static) {
                fail(method.location, "inheritance method decorators require self");
            }
            if (is_abstract && !method.statements.empty()) {
                fail(method.location, "@abstract methods cannot have a body");
            }
            if (!is_abstract && method.statements.empty()) {
                fail(method.location, "bodyless method requires @abstract: " + method.name);
            }
            if ((is_constructor_method(method) || is_destructor_method(method)) &&
                (method.params.empty() || method.params.front().name != "self")) {
                fail(method.location, method.name + " requires self parameter");
            }
            if (is_operator_method(method)) {
                if (is_static) {
                    fail(method.location, "operator methods cannot be static");
                }
                const std::string op = operator_decorator_arg(method);
                if (op == "bool") {
                    if (method.params.size() != 1 || method.params.front().name != "self") {
                        fail(method.location, "bool operator methods require only self");
                    }
                    if (method.return_type.empty() || method.return_type != "bool") {
                        fail(method.location, "bool operator methods must return bool");
                    }
                } else if (op == "[]=") {
                    if (method.params.size() != 3 || method.params.front().name != "self") {
                        fail(method.location,
                             "indexed assignment operator methods require self, index, and value");
                    }
                    if (!method.return_type.empty() && method.return_type != "void") {
                        fail(method.location,
                             "indexed assignment operator methods must return void");
                    }
                } else if (method.params.size() != 2 || method.params.front().name != "self") {
                    fail(method.location, "operator methods require self and one parameter");
                } else if (is_comparison_operator_method(method) &&
                           (method.return_type.empty() || method.return_type != "bool")) {
                    fail(method.location, "comparison operator methods must return bool");
                }
            }
            if (is_constructor_method(method) && !method.return_type.empty() &&
                method.return_type != "void") {
                fail(method.location, method.name + " cannot declare a return type");
            }
            if (is_destructor_method(method)) {
                if (method.params.size() != 1) {
                    fail(method.location, method.name + " cannot take parameters");
                }
                if (!method.return_type.empty() && method.return_type != "void") {
                    fail(method.location, method.name + " cannot declare a return type");
                }
            }
            if (is_override) {
                const FunctionDecl* base_method = nullptr;
                for (const std::string& base : klass.base_classes) {
                    base_method = find_method_decl(symbols, base, method.name);
                    if (base_method != nullptr) {
                        break;
                    }
                }
                if (base_method == nullptr) {
                    fail(method.location, "@override method has no matching base method: " +
                                              klass.name + "." + method.name);
                }
                if (!same_signature(method_signature_without_self(method),
                                    method_signature_without_self(*base_method))) {
                    fail(method.location, "@override signature does not match base method: " +
                                              klass.name + "." + method.name);
                }
                if (!is_virtual_like(*base_method)) {
                    fail(method.location,
                         "@override target must be @virtual or @abstract: " + klass.name + "." +
                             method.name);
                }
            }
            for (const Decorator& decorator : method.decorators) {
                check_function_decorator(module, decorator);
            }
            if (has_decorator(method, "extern_c")) {
                fail(method.location, "@extern_c is only valid on free functions");
            }
            if (is_test_decorator(method)) {
                fail(method.location, "@test is only valid on free functions");
            }
            std::set<std::string> params;
            for (const ParamDecl& param : method.params) {
                if (!params.insert(param.name).second) {
                    fail(param.location, "duplicate parameter: " + param.name);
                }
                check_supported_type_shape(param.location, param.type_ref);
                check_known_type_ref(method_symbols, param.location, param.type_ref,
                                     "unknown parameter type: ");
            }
            if (!method.return_type.empty()) {
                check_supported_type_shape(method.location, method.return_type_ref);
            }
            check_known_type_ref(method_symbols, method.location, method.return_type_ref,
                                 "unknown return type: ");
        }
    }
    for (const FunctionDecl& fn : module.functions) {
        check_generic_params(fn.location, fn.generic_params);
        const Symbols function_symbols = with_generic_params(symbols, fn.generic_params);
        for (const Decorator& decorator : fn.decorators) {
            check_function_decorator(module, decorator);
        }
        if (has_decorator(fn, "virtual") || has_decorator(fn, "override") ||
            has_decorator(fn, "abstract")) {
            fail(fn.location, "inheritance method decorators are only valid on methods");
        }
        if (has_decorator(fn, "extern_c")) {
            check_extern_c_signature(fn);
        }
        if (is_test_decorator(fn)) {
            const std::string return_type = fn.return_type.empty() ? "void" : fn.return_type;
            if (!fn.params.empty()) {
                fail(fn.location, "@test functions cannot take parameters");
            }
            if (return_type != "void" && return_type != "bool" && return_type != "i32") {
                fail(fn.location, "@test return type must be void, bool, or i32");
            }
        }
        std::set<std::string> params;
        for (const ParamDecl& param : fn.params) {
            if (!params.insert(param.name).second) {
                fail(param.location, "duplicate parameter: " + param.name);
            }
            check_supported_type_shape(param.location, param.type_ref);
            check_known_type_ref(function_symbols, param.location, param.type_ref,
                                 "unknown parameter type: ");
        }
        if (!fn.return_type.empty()) {
            check_supported_type_shape(fn.location, fn.return_type_ref);
        }
        check_known_type_ref(function_symbols, fn.location, fn.return_type_ref,
                             "unknown return type: ");
    }
}

} // namespace dudu
