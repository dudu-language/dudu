#include "dudu/sema_context.hpp"

#include "dudu/cpp_lower.hpp"
#include "dudu/source.hpp"

#include <cctype>

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

void check_supported_type_shape(const SourceLocation& location, std::string type);

void check_type_args(const SourceLocation& location, const std::string& args) {
    for (const std::string& arg : split_top_level(args)) {
        check_supported_type_shape(location, arg);
    }
}

void check_supported_type_shape(const SourceLocation& location, std::string type) {
    type = trim(std::move(type));
    while (!type.empty() && (type.front() == '*' || type.front() == '&')) {
        type = trim(type.substr(1));
    }
    const size_t open = type.find('[');
    if (open == std::string::npos || type.back() != ']') {
        return;
    }
    const std::string name = trim(type.substr(0, open));
    const std::string args = type.substr(open + 1, type.size() - open - 2);
    const std::vector<std::string> parts = split_top_level(args);
    if (name == "tuple" && (parts.empty() || parts.size() > 8)) {
        fail(location, "tuple supports 1 to 8 elements, got " + std::to_string(parts.size()));
    }
    check_type_args(location, args);
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
           base == "dict" || base == "set" || base == "tuple" || base == "Result" ||
           base == "Option" || base == "fn" || base == "const" || base == "atomic" ||
           base == "volatile" || base == "storage" || base == "shared" || base == "device";
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
        if (c == '(' || c == '[') {
            ++depth;
        } else if (c == ')' || c == ']') {
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
        if (c == '(' || c == '[') {
            ++depth;
        } else if (c == ')' || c == ']') {
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
    std::map<std::string, SourceLocation> names;
    for (const char* type : {"bool", "i8", "i16", "i32", "i64", "u8", "u16", "u32", "u64", "isize",
                             "usize", "f32", "f64", "void", "str", "cstr"}) {
        symbols.types.insert(type);
    }
    for (const TypeAliasDecl& alias : module.aliases) {
        add_name(names, alias.name, alias.location);
        symbols.types.insert(alias.name);
        symbols.aliases[alias.name] = alias.type;
    }
    for (const EnumDecl& en : module.enums) {
        add_name(names, en.name, en.location);
        symbols.types.insert(en.name);
    }
    for (const ClassDecl& klass : module.classes) {
        add_name(names, klass.name, klass.location);
        symbols.types.insert(klass.name);
        symbols.classes[klass.name] = &klass;
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
    for (const ConstDecl& constant : module.constants) {
        add_name(names, constant.name, constant.location);
    }
    return symbols;
}

void check_declarations(const ModuleAst& module, const Symbols& symbols) {
    for (const TypeAliasDecl& alias : module.aliases) {
        check_supported_type_shape(alias.location, alias.type);
        if (!known_type(symbols, alias.type)) {
            fail(alias.location, "unknown type alias target: " + alias.type);
        }
    }
    for (const EnumDecl& en : module.enums) {
        if (!en.underlying_type.empty() && !known_type(symbols, en.underlying_type)) {
            fail(en.location, "unknown enum underlying type: " + en.underlying_type);
        }
        std::set<std::string> values;
        for (const EnumValueDecl& value : en.values) {
            if (!values.insert(value.name).second) {
                fail(value.location, "duplicate enum value: " + value.name);
            }
        }
    }
    for (const ClassDecl& klass : module.classes) {
        std::set<std::string> fields;
        for (const FieldDecl& field : klass.fields) {
            if (!fields.insert(field.name).second) {
                fail(field.location, "duplicate field: " + field.name);
            }
            check_supported_type_shape(field.location, field.type);
            if (!known_type(symbols, field.type)) {
                fail(field.location, "unknown field type: " + field.type);
            }
        }
        for (const FunctionDecl& method : klass.methods) {
            std::set<std::string> params;
            for (const ParamDecl& param : method.params) {
                if (!params.insert(param.name).second) {
                    fail(param.location, "duplicate parameter: " + param.name);
                }
                check_supported_type_shape(param.location, param.type);
                if (!known_type(symbols, param.type)) {
                    fail(param.location, "unknown parameter type: " + param.type);
                }
            }
            check_supported_type_shape(method.location,
                                       method.return_type.empty() ? "void" : method.return_type);
            if (!known_type(symbols, method.return_type.empty() ? "void" : method.return_type)) {
                fail(method.location, "unknown return type: " + method.return_type);
            }
        }
    }
    for (const FunctionDecl& fn : module.functions) {
        std::set<std::string> params;
        for (const ParamDecl& param : fn.params) {
            if (!params.insert(param.name).second) {
                fail(param.location, "duplicate parameter: " + param.name);
            }
            check_supported_type_shape(param.location, param.type);
            if (!known_type(symbols, param.type)) {
                fail(param.location, "unknown parameter type: " + param.type);
            }
        }
        check_supported_type_shape(fn.location, fn.return_type.empty() ? "void" : fn.return_type);
        if (!known_type(symbols, fn.return_type.empty() ? "void" : fn.return_type)) {
            fail(fn.location, "unknown return type: " + fn.return_type);
        }
    }
}

} // namespace dudu
