#include "dudu/sema_context.hpp"

#include "dudu/cpp_lower.hpp"
#include "dudu/source.hpp"

#include <cctype>
#include <optional>
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

std::string dunder_operator_name(const std::string& name) {
    static const std::map<std::string, std::string> names = {
        {"__add__", "+"},      {"__sub__", "-"},       {"__mul__", "*"}, {"__truediv__", "/"},
        {"__mod__", "%"},      {"__eq__", "=="},       {"__ne__", "!="}, {"__lt__", "<"},
        {"__le__", "<="},      {"__gt__", ">"},        {"__ge__", ">="}, {"__bool__", "bool"},
        {"__getitem__", "[]"}, {"__setitem__", "[]="},
    };
    const auto it = names.find(name);
    return it == names.end() ? std::string{} : it->second;
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
        }
    }
    for (const ClassDecl& klass : module.classes) {
        for (const Decorator& decorator : klass.decorators) {
            check_class_decorator(decorator);
        }
        std::set<std::string> fields;
        for (const FieldDecl& field : klass.fields) {
            if (!fields.insert(field.name).second) {
                fail(field.location, "duplicate field: " + field.name);
            }
            check_supported_type_shape(field.location, field.type_ref);
            check_known_type_ref(symbols, field.location, field.type_ref, "unknown field type: ");
        }
        for (const ConstDecl& constant : klass.constants) {
            if (!fields.insert(constant.name).second) {
                fail(constant.location, "duplicate class member: " + constant.name);
            }
            check_supported_type_shape(constant.location, constant.type_ref);
            check_known_type_ref(symbols, constant.location, constant.type_ref,
                                 "unknown class constant type: ");
        }
        for (const ConstDecl& field : klass.static_fields) {
            if (!fields.insert(field.name).second) {
                fail(field.location, "duplicate class member: " + field.name);
            }
            check_supported_type_shape(field.location, field.type_ref);
            check_known_type_ref(symbols, field.location, field.type_ref,
                                 "unknown static field type: ");
        }
        for (const FunctionDecl& method : klass.methods) {
            if (const std::string op = dunder_operator_name(method.name); !op.empty()) {
                fail(method.location, "Dudu operator methods use @operator(\"" + op +
                                          "\") on a normal method name, not " + method.name);
            }
            if (!fields.insert(method.name).second) {
                fail(method.location, "duplicate class member: " + method.name);
            }
            const bool is_static = method.params.empty() || method.params.front().name != "self";
            if ((is_constructor_method(method) || is_destructor_method(method)) &&
                (method.params.empty() || method.params.front().name != "self")) {
                fail(method.location, method.name + " requires self parameter");
            }
            if (is_operator_method(method)) {
                if (is_static) {
                    fail(method.location, "operator methods cannot be static");
                }
                if (operator_decorator_arg(method) == "bool") {
                    if (method.params.size() != 1 || method.params.front().name != "self") {
                        fail(method.location, "bool operator methods require only self");
                    }
                    if (method.return_type.empty() || method.return_type != "bool") {
                        fail(method.location, "bool operator methods must return bool");
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
                check_known_type_ref(symbols, param.location, param.type_ref,
                                     "unknown parameter type: ");
            }
            if (!method.return_type.empty()) {
                check_supported_type_shape(method.location, method.return_type_ref);
            }
            check_known_type_ref(symbols, method.location, method.return_type_ref,
                                 "unknown return type: ");
        }
    }
    for (const FunctionDecl& fn : module.functions) {
        for (const Decorator& decorator : fn.decorators) {
            check_function_decorator(module, decorator);
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
            check_known_type_ref(symbols, param.location, param.type_ref,
                                 "unknown parameter type: ");
        }
        if (!fn.return_type.empty()) {
            check_supported_type_shape(fn.location, fn.return_type_ref);
        }
        check_known_type_ref(symbols, fn.location, fn.return_type_ref, "unknown return type: ");
    }
}

} // namespace dudu
