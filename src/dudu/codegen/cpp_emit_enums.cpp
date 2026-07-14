#include "dudu/codegen/cpp_emit_enums.hpp"

#include "dudu/core/ast_type.hpp"
#include "dudu/codegen/cpp_expr_emit.hpp"
#include "dudu/codegen/cpp_lower.hpp"

#include <sstream>

namespace dudu {
namespace {

bool enum_has_payload_fields(const EnumDecl& en) {
    for (const EnumValueDecl& value : en.values) {
        if (!value.payload_fields.empty()) {
            return true;
        }
    }
    return false;
}

bool enum_has_underlying_type(const EnumDecl& en) {
    return has_type_ref(en.underlying_type_ref);
}

} // namespace

void emit_enum_forward_declarations(std::ostringstream& out, const ModuleAst& module,
                                    const CppEmitOptions& options) {
    for (const EnumDecl& en : module.enums) {
        const std::string& name = emitted_name(en, options);
        if (enum_has_payload_fields(en)) {
            out << "struct " << name << ";\n";
            continue;
        }
        out << "enum class " << name;
        if (enum_has_underlying_type(en)) {
            out << " : " << lower_cpp_type(en.underlying_type_ref, options);
        }
        out << ";\n";
    }
    if (!module.enums.empty()) {
        out << '\n';
    }
}

void emit_value_enums(std::ostringstream& out, const ModuleAst& module,
                      const std::vector<std::string>& aliases, const CppEmitOptions& options) {
    for (const EnumDecl& en : module.enums) {
        if (enum_has_payload_fields(en)) {
            continue;
        }
        const std::string& name = emitted_name(en, options);
        out << "enum class " << name;
        if (enum_has_underlying_type(en)) {
            out << " : " << lower_cpp_type(en.underlying_type_ref, aliases, options);
        }
        out << " {\n";
        for (const EnumValueDecl& value : en.values) {
            out << "    " << value.name;
            if (has_expr(value.value_expr)) {
                out << " = "
                    << lower_cpp_expr_ast(value.value_expr, aliases, CppLocalContext{}, options);
            }
            out << ",\n";
        }
        out << "};\n\n";
    }
}

void emit_payload_enums(std::ostringstream& out, const ModuleAst& module,
                        const std::vector<std::string>& aliases,
                        const CppEmitOptions& options) {
    for (const EnumDecl& en : module.enums) {
        if (!enum_has_payload_fields(en)) {
            continue;
        }
        const std::string& name = emitted_name(en, options);
        out << "struct " << name << " {\n";
        for (const EnumValueDecl& value : en.values) {
            out << "    struct " << value.name << " {\n";
            for (const EnumPayloadField& field : value.payload_fields) {
                out << "        " << lower_cpp_type(field.type_ref, aliases, options) << " "
                    << emitted_member_name(en.name + "_" + value.name, field.name, options)
                    << "{};\n";
            }
            out << "    };\n";
        }
        out << "    std::variant<";
        for (size_t i = 0; i < en.values.size(); ++i) {
            if (i > 0) {
                out << ", ";
            }
            out << en.values[i].name;
        }
        out << "> value;\n";
        out << "};\n\n";
    }
}

void emit_enums(std::ostringstream& out, const ModuleAst& module,
                const std::vector<std::string>& aliases, const CppEmitOptions& options) {
    emit_value_enums(out, module, aliases, options);
    emit_payload_enums(out, module, aliases, options);
}

} // namespace dudu
