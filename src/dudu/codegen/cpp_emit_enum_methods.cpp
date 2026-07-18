#include "dudu/codegen/cpp_emit_enum_methods.hpp"

#include "dudu/codegen/cpp_emit.hpp"
#include "dudu/codegen/cpp_emit_declaration_support.hpp"
#include "dudu/codegen/cpp_lower.hpp"
#include "dudu/codegen/cpp_stmt_emit.hpp"
#include "dudu/codegen/cpp_stmt_emit_support.hpp"
#include "dudu/core/ast_type.hpp"
#include "dudu/sema/sema_body_substitution.hpp"
#include "dudu/sema/sema_function_type.hpp"
#include "dudu/sema/sema_generics.hpp"

#include <set>
#include <sstream>

namespace dudu {
namespace {

bool generic_method(const FunctionDecl& method) {
    return !generic_cpp_params_for_function(method).empty();
}

TypeRef substitute_self(const EnumDecl& en, const TypeRef& type) {
    return substitute_type_ref(type, {{"Self", named_type_ref(en.name, type.location)}});
}

void emit_signature(std::ostringstream& out, const EnumDecl& en, const FunctionDecl& method,
                    const std::vector<std::string>& aliases, const CppEmitOptions& options) {
    out << lower_cpp_type(substitute_self(en, function_return_type_ref(method)), aliases, options)
        << ' ' << emitted_enum_method_name(en, method, options) << '(';
    for (size_t i = 0; i < method.params.size(); ++i) {
        if (i > 0)
            out << ", ";
        out << lower_cpp_type(substitute_self(en, method.params[i].type_ref), aliases, options)
            << (method.params[i].variadic ? "... " : " ")
            << emitted_local_name(method.params[i].name);
    }
    out << ')';
}

void emit_body(std::ostringstream& out, const EnumDecl& en, const FunctionDecl& method,
               const std::vector<std::string>& aliases,
               const std::map<std::string, TypeRef>& function_returns, const Symbols& symbols,
               const CppEmitOptions& options) {
    emit_cpp_template_parameters(out, generic_cpp_params_for_function(method),
                                 generic_cpp_value_params_for_function(method));
    emit_signature(out, en, method, aliases, options);
    out << " {\n";
    CppLocalContext locals;
    const std::set<std::string> value_params = generic_value_params_for_function(method);
    for (const std::string& param : method.generic_params) {
        const std::string name = generic_param_base_name(param);
        if (!value_params.contains(name)) {
            locals.bind_type(name);
        }
    }
    std::map<std::string, TypeRef> local_types;
    for (const ParamDecl& param : method.params) {
        locals.bind(param.name);
        local_types[param.name] = substitute_self(en, param.type_ref);
    }
    const std::vector<Stmt> body = substitute_body_types(
        method.statements, {{"Self", named_type_ref(en.name, method.location)}});
    emit_block(out, body, 1, aliases, locals, local_types,
               substitute_self(en, function_return_type_ref(method)), function_returns, &symbols,
               options);
    out << "}\n\n";
}

} // namespace

std::string emitted_enum_method_name(const EnumDecl& en, const FunctionDecl& method,
                                     const CppEmitOptions& options) {
    if (!method.cpp_name.empty())
        return method.cpp_name;
    return emitted_type_name(en.name, options) + "__" + method.name;
}

void emit_enum_method_declarations(std::ostringstream& out, const ModuleAst& module,
                                   const std::vector<std::string>& aliases, bool header_only,
                                   const CppEmitOptions& options) {
    bool emitted = false;
    for (const EnumDecl& en : module.enums) {
        for (const FunctionDecl& method : en.methods) {
            if ((header_only && !visible_in_cpp_header(method.visibility)) ||
                (header_only && generic_method(method))) {
                continue;
            }
            emit_cpp_template_parameters(out, generic_cpp_params_for_function(method),
                                         generic_cpp_value_params_for_function(method));
            emit_signature(out, en, method, aliases, options);
            out << ";\n";
            emitted = true;
        }
    }
    if (emitted)
        out << '\n';
}

void emit_generic_enum_method_declarations(std::ostringstream& out, const ModuleAst& module,
                                           const std::vector<std::string>& aliases,
                                           const CppEmitOptions& options) {
    bool emitted = false;
    for (const EnumDecl& en : module.enums) {
        for (const FunctionDecl& method : en.methods) {
            if (!visible_in_cpp_header(method.visibility) || !generic_method(method)) {
                continue;
            }
            emit_cpp_template_parameters(out, generic_cpp_params_for_function(method),
                                         generic_cpp_value_params_for_function(method));
            emit_signature(out, en, method, aliases, options);
            out << ";\n";
            emitted = true;
        }
    }
    if (emitted) {
        out << '\n';
    }
}

void emit_private_enum_method_declarations(std::ostringstream& out, const ModuleAst& module,
                                           const std::vector<std::string>& aliases,
                                           const CppEmitOptions& options) {
    bool emitted = false;
    for (const EnumDecl& en : module.enums) {
        for (const FunctionDecl& method : en.methods) {
            if (visible_in_cpp_header(method.visibility)) {
                continue;
            }
            emit_cpp_template_parameters(out, generic_cpp_params_for_function(method),
                                         generic_cpp_value_params_for_function(method));
            emit_signature(out, en, method, aliases, options);
            out << ";\n";
            emitted = true;
        }
    }
    if (emitted) {
        out << '\n';
    }
}

void emit_enum_method_definitions(std::ostringstream& out, const ModuleAst& module,
                                  const std::vector<std::string>& aliases,
                                  const std::map<std::string, TypeRef>& function_returns,
                                  const Symbols& symbols, bool header_only,
                                  const CppEmitOptions& options) {
    for (const EnumDecl& en : module.enums) {
        for (const FunctionDecl& method : en.methods) {
            const bool emit_in_header =
                generic_method(method) && visible_in_cpp_header(method.visibility);
            if (header_only != emit_in_header) {
                continue;
            }
            emit_body(out, en, method, aliases, function_returns, symbols, options);
        }
    }
}

void emit_enum_method_dispatch_overloads(std::ostringstream& out, const ModuleAst& module,
                                         const std::vector<std::string>& aliases,
                                         const CppEmitOptions& options) {
    for (const EnumDecl& en : module.enums) {
        for (const FunctionDecl& method : en.methods) {
            if (!visible_in_cpp_header(method.visibility)) {
                continue;
            }
            const bool instance = !method.params.empty() && method.params.front().name == "self";
            emit_cpp_template_parameters(out, generic_cpp_params_for_function(method),
                                         generic_cpp_value_params_for_function(method));
            out << "inline "
                << lower_cpp_type(substitute_self(en, function_return_type_ref(method)), aliases,
                                  options)
                << " dudu_dispatch_" << (instance ? "instance_" : "static_") << method.name << '(';
            bool emitted = false;
            if (!instance) {
                out << "std::type_identity<" << emitted_type_name(en.name, options) << ">";
                emitted = true;
            }
            for (const ParamDecl& param : method.params) {
                if (emitted) {
                    out << ", ";
                }
                out << lower_cpp_type(substitute_self(en, param.type_ref), aliases, options) << ' '
                    << emitted_local_name(param.name);
                emitted = true;
            }
            out << ") { return " << emitted_enum_method_name(en, method, options);
            const std::vector<std::string> generic_params = generic_cpp_params_for_function(method);
            if (!generic_params.empty()) {
                out << '<';
                for (size_t i = 0; i < generic_params.size(); ++i) {
                    if (i > 0) {
                        out << ", ";
                    }
                    out << generic_param_base_name(generic_params[i]);
                    if (generic_param_is_pack(generic_params[i])) {
                        out << "...";
                    }
                }
                out << '>';
            }
            out << '(';
            for (size_t i = 0; i < method.params.size(); ++i) {
                if (i > 0) {
                    out << ", ";
                }
                out << emitted_local_name(method.params[i].name);
            }
            out << "); }\n";
        }
    }
    if (!module.enums.empty()) {
        out << '\n';
    }
}

} // namespace dudu
