#include "dudu/cpp_emit_enums.hpp"

#include "dudu/cpp_expr_emit.hpp"
#include "dudu/cpp_lower.hpp"

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

} // namespace

void emit_enum_forward_declarations(std::ostringstream& out, const ModuleAst& module) {
    for (const EnumDecl& en : module.enums) {
        if (enum_has_payload_fields(en)) {
            out << "struct " << en.name << ";\n";
            continue;
        }
        out << "enum class " << en.name;
        if (!en.underlying_type.empty()) {
            out << " : " << lower_cpp_type(en.underlying_type_ref);
        }
        out << ";\n";
    }
    if (!module.enums.empty()) {
        out << '\n';
    }
}

void emit_enums(std::ostringstream& out, const ModuleAst& module,
                const std::vector<std::string>& aliases) {
    for (const EnumDecl& en : module.enums) {
        if (enum_has_payload_fields(en)) {
            out << "struct " << en.name << " {\n";
            for (const EnumValueDecl& value : en.values) {
                out << "    struct " << value.name << " {\n";
                for (const EnumPayloadField& field : value.payload_fields) {
                    out << "        " << lower_cpp_type(field.type_ref, aliases) << " "
                        << field.name << "{};\n";
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
            continue;
        }
        out << "enum class " << en.name;
        if (!en.underlying_type.empty()) {
            out << " : " << lower_cpp_type(en.underlying_type_ref, aliases);
        }
        out << " {\n";
        for (const EnumValueDecl& value : en.values) {
            out << "    " << value.name;
            if (!value.value.empty()) {
                out << " = " << lower_cpp_expr_ast(value.value_expr, aliases);
            }
            out << ",\n";
        }
        out << "};\n\n";
    }
}

} // namespace dudu
