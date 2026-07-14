#include "dudu/codegen/cpp_emit_enum_methods.hpp"

#include "dudu/codegen/cpp_emit.hpp"
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

bool visible_in_header(const FunctionDecl& method) {
    return method.visibility != Visibility::Private;
}

bool generic_method(const FunctionDecl& method) {
    return !generic_cpp_params_for_function(method).empty();
}

void emit_template_params(std::ostringstream& out, const FunctionDecl& method) {
    const std::vector<std::string> params = generic_cpp_params_for_function(method);
    if (params.empty())
        return;
    const std::set<std::string> values = generic_cpp_value_params_for_function(method);
    out << "template <";
    for (size_t i = 0; i < params.size(); ++i) {
        if (i > 0)
            out << ", ";
        const std::string name = generic_param_base_name(params[i]);
        if (generic_param_is_pack(params[i])) {
            out << "typename... " << name;
        } else {
            out << (values.contains(name) ? "size_t " : "typename ") << name;
        }
    }
    out << ">\n";
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
            << (method.params[i].variadic ? "... " : " ") << method.params[i].name;
    }
    out << ')';
}

void emit_body(std::ostringstream& out, const EnumDecl& en, const FunctionDecl& method,
               const std::vector<std::string>& aliases,
               const std::map<std::string, TypeRef>& function_returns, const Symbols& symbols,
               const CppEmitOptions& options) {
    emit_template_params(out, method);
    emit_signature(out, en, method, aliases, options);
    out << " {\n";
    CppLocalContext locals;
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
            if ((header_only && !visible_in_header(method)) ||
                (header_only && generic_method(method))) {
                continue;
            }
            emit_template_params(out, method);
            emit_signature(out, en, method, aliases, options);
            out << ";\n";
            emitted = true;
        }
    }
    if (emitted)
        out << '\n';
}

void emit_enum_method_definitions(std::ostringstream& out, const ModuleAst& module,
                                  const std::vector<std::string>& aliases,
                                  const std::map<std::string, TypeRef>& function_returns,
                                  const Symbols& symbols, bool header_only,
                                  const CppEmitOptions& options) {
    for (const EnumDecl& en : module.enums) {
        for (const FunctionDecl& method : en.methods) {
            if (header_only != generic_method(method) ||
                (header_only && !visible_in_header(method))) {
                continue;
            }
            emit_body(out, en, method, aliases, function_returns, symbols, options);
        }
    }
}

} // namespace dudu
