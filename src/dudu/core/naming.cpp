#include "dudu/core/naming.hpp"

#include "dudu/core/decorators.hpp"
#include "dudu/sema/sema.hpp"

#include <cctype>
#include <string_view>

namespace dudu {
namespace {

bool is_pascal_case(const std::string& name) {
    if (name.empty() || std::isupper(static_cast<unsigned char>(name.front())) == 0) {
        return false;
    }
    for (const char c : name) {
        if (std::isalnum(static_cast<unsigned char>(c)) == 0) {
            return false;
        }
    }
    return true;
}

bool is_reserved_dunder_name(const std::string& name) {
    return name.size() > 4 && name.starts_with("__") && name.ends_with("__");
}

[[noreturn]] void fail_naming(const SourceLocation& location, std::string_view rule,
                              const std::string& name) {
    throw CompileError(location, std::string(rule) + ": " + name);
}

bool has_decorator(const FunctionDecl& fn, std::string_view name) {
    return dudu::has_decorator(fn.decorators, name);
}

} // namespace

bool is_dudu_snake_case(const std::string& name) {
    std::string_view text = name;
    if (text.size() > 1 && text.front() == '_' && text[1] != '_') {
        text.remove_prefix(1);
    }
    if (text.empty() || std::islower(static_cast<unsigned char>(text.front())) == 0) {
        return false;
    }
    for (const char c : text) {
        if (std::islower(static_cast<unsigned char>(c)) == 0 &&
            std::isdigit(static_cast<unsigned char>(c)) == 0 && c != '_') {
            return false;
        }
    }
    return true;
}

bool is_dudu_all_caps(const std::string& name) {
    if (name.empty()) {
        return false;
    }
    for (const char c : name) {
        if (std::isupper(static_cast<unsigned char>(c)) == 0 &&
            std::isdigit(static_cast<unsigned char>(c)) == 0 && c != '_') {
            return false;
        }
    }
    return true;
}

bool is_constructor_method(const FunctionDecl& method) {
    return method.name == "init";
}

bool is_destructor_method(const FunctionDecl& method) {
    return method.name == "drop";
}

void check_naming(const ModuleAst& module) {
    for (const TypeAliasDecl& alias : module.aliases) {
        if (!is_pascal_case(alias.name)) {
            fail_naming(alias.location, "type names must be PascalCase", alias.name);
        }
    }
    for (const EnumDecl& en : module.enums) {
        if (!is_pascal_case(en.name)) {
            fail_naming(en.location, "type names must be PascalCase", en.name);
        }
        for (const EnumValueDecl& value : en.values) {
            if (!is_pascal_case(value.name)) {
                fail_naming(value.location, "enum values must be PascalCase", value.name);
            }
            if (!value.tuple_payload) {
                for (const EnumPayloadField& field : value.payload_fields) {
                    if (!is_dudu_snake_case(field.name)) {
                        fail_naming(field.location, "enum payload field names must be snake_case",
                                    field.name);
                    }
                }
            }
        }
    }
    for (const ClassDecl& klass : module.classes) {
        if (!is_pascal_case(klass.name)) {
            fail_naming(klass.location, "type names must be PascalCase", klass.name);
        }
        for (const FieldDecl& field : klass.fields) {
            if (!is_dudu_snake_case(field.name)) {
                fail_naming(field.location, "field names must be snake_case", field.name);
            }
        }
        for (const FunctionDecl& method : klass.methods) {
            if (is_reserved_dunder_name(method.name)) {
                throw CompileError(method.location,
                                   "reserved Python-style dunder method name: " + method.name +
                                       "; use normal Dudu names and decorators such as "
                                       "@operator(...)");
            }
            if (!is_dudu_snake_case(method.name)) {
                fail_naming(method.location, "function names must be snake_case", method.name);
            }
            for (const ParamDecl& param : method.params) {
                if (!is_dudu_snake_case(param.name)) {
                    fail_naming(param.location, "parameter names must be snake_case", param.name);
                }
            }
        }
    }
    for (const FunctionDecl& fn : module.functions) {
        if (is_reserved_dunder_name(fn.name)) {
            throw CompileError(fn.location,
                               "reserved Python-style dunder function name: " + fn.name +
                                   "; use normal Dudu names and decorators such as @operator(...)");
        }
        if (!has_decorator(fn, "extern_c") && !is_dudu_snake_case(fn.name)) {
            fail_naming(fn.location, "function names must be snake_case", fn.name);
        }
        for (const ParamDecl& param : fn.params) {
            if (!is_dudu_snake_case(param.name)) {
                fail_naming(param.location, "parameter names must be snake_case", param.name);
            }
        }
    }
    for (const ConstDecl& constant : module.constants) {
        if (!is_dudu_all_caps(constant.name)) {
            fail_naming(constant.location, "constant names must be ALL_CAPS", constant.name);
        }
    }
    for (const ClassDecl& klass : module.classes) {
        for (const ConstDecl& constant : klass.constants) {
            if (!is_dudu_all_caps(constant.name)) {
                fail_naming(constant.location, "constant names must be ALL_CAPS", constant.name);
            }
        }
        for (const ConstDecl& field : klass.static_fields) {
            if (!is_dudu_snake_case(field.name)) {
                fail_naming(field.location, "static field names must be snake_case", field.name);
            }
        }
    }
}

} // namespace dudu
