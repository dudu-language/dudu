#include "dudu/naming.hpp"

#include "dudu/sema.hpp"

#include <cctype>

namespace dudu {
namespace {

bool starts_with(std::string_view text, std::string_view prefix) {
    return text.substr(0, prefix.size()) == prefix;
}

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

[[noreturn]] void fail_naming(const SourceLocation& location, std::string_view rule,
                              const std::string& name) {
    throw CompileError(location, std::string(rule) + ": " + name);
}

} // namespace

bool is_dudu_snake_case(const std::string& name) {
    if (name.empty() || std::islower(static_cast<unsigned char>(name.front())) == 0) {
        return name.size() > 4 && starts_with(name, "__") && name.substr(name.size() - 2) == "__";
    }
    for (const char c : name) {
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
            if (!is_dudu_snake_case(value.name)) {
                fail_naming(value.location, "enum values must be snake_case", value.name);
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
        if (!is_dudu_snake_case(fn.name)) {
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
}

} // namespace dudu
