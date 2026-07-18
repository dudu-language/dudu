#include "dudu/codegen/cpp_emit_functions.hpp"

#include "dudu/codegen/cpp_emit_declaration_support.hpp"
#include "dudu/codegen/cpp_emit_internal.hpp"
#include "dudu/codegen/cpp_lower.hpp"
#include "dudu/codegen/cpp_stmt_emit.hpp"
#include "dudu/core/ast_type.hpp"
#include "dudu/core/decorators.hpp"
#include "dudu/sema/sema_context.hpp"
#include "dudu/sema/sema_generics.hpp"

#include <sstream>
#include <string_view>

namespace dudu {
namespace {

std::string function_decorator_args(const FunctionDecl& fn, std::string_view name) {
    for (const Decorator& decorator : fn.decorators) {
        if (const std::optional<std::string> args = decorator_arg_list_display(decorator, name)) {
            return *args;
        }
    }
    return {};
}

bool emit_before_constants(const FunctionDecl& fn) {
    return has_decorator(fn, "constexpr");
}

bool generic_function(const FunctionDecl& fn) {
    return !generic_cpp_params_for_function(fn).empty();
}

bool should_emit_function(const FunctionDecl& fn, bool test_source) {
    return !test_source || fn.name != "main";
}

void emit_function_signature(std::ostringstream& out, const FunctionDecl& fn,
                             const std::vector<std::string>& aliases,
                             const CppEmitOptions& options) {
    if (has_decorator(fn, "extern_c")) {
        out << "extern \"C\" ";
    }
    if (has_decorator(fn, "cuda.global")) {
        out << "DUDU_CUDA_GLOBAL ";
    }
    if (has_decorator(fn, "cuda.device")) {
        out << "DUDU_CUDA_DEVICE ";
    }
    if (has_decorator(fn, "cuda.host")) {
        out << "DUDU_CUDA_HOST ";
    }
    if (has_decorator(fn, "shader.compute")) {
        out << "DUDU_SHADER_COMPUTE ";
    }
    const std::string section = cpp_emit_function_decorator_arg(fn, "section");
    if (!section.empty()) {
        out << "__attribute__((section(" << cpp_emit_string_literal(section) << "))) ";
    }
    const std::string workgroup = function_decorator_args(fn, "workgroup_size");
    if (!workgroup.empty()) {
        out << "DUDU_WORKGROUP_SIZE(" << workgroup << ") ";
    }
    if (has_decorator(fn, "inline")) {
        out << "inline ";
    }
    if (has_decorator(fn, "constexpr")) {
        out << "constexpr ";
    }
    const std::string& name =
        has_decorator(fn, "extern_c") ? fn.name : emitted_value_name(fn.name, options);
    out << lower_cpp_type(function_return_type_ref(fn), aliases, options) << ' ' << name << '(';
    for (size_t i = 0; i < fn.params.size(); ++i) {
        if (i > 0) {
            out << ", ";
        }
        if (cpp_emit_concrete_variadic_param(fn, fn.params[i])) {
            out << cpp_emit_concrete_variadic_pack_name(fn.params[i]) << "... "
                << fn.params[i].name;
        } else {
            out << lower_cpp_type(fn.params[i].type_ref, aliases, options)
                << (fn.params[i].variadic ? "... " : " ") << fn.params[i].name;
        }
    }
    out << ')';
}

} // namespace

std::string cpp_emit_string_literal(std::string text) {
    std::string out = "\"";
    for (const char c : text) {
        if (c == '"' || c == '\\') {
            out.push_back('\\');
        }
        out.push_back(c);
    }
    out.push_back('"');
    return out;
}

std::string cpp_emit_function_decorator_arg(const FunctionDecl& fn, std::string_view name) {
    for (const Decorator& decorator : fn.decorators) {
        if (const auto arg = decorator_first_string_literal_arg(decorator, name)) {
            return *arg;
        }
    }
    return {};
}

bool cpp_emit_concrete_variadic_param(const FunctionDecl& fn, const ParamDecl& param) {
    return param.variadic &&
           !generic_pack_param_named(fn.generic_params, type_ref_head_name(param.type_ref));
}

std::string cpp_emit_concrete_variadic_pack_name(const ParamDecl& param) {
    return "__DuduVariadic_" + param.name;
}

std::vector<std::string> cpp_emit_template_params_for_function(const FunctionDecl& fn) {
    std::vector<std::string> params = generic_cpp_params_for_function(fn);
    for (const ParamDecl& param : fn.params) {
        if (cpp_emit_concrete_variadic_param(fn, param)) {
            params.push_back(cpp_emit_concrete_variadic_pack_name(param) + "...");
        }
    }
    return params;
}

std::map<std::string, TypeRef> cpp_function_return_types(const ModuleAst& module) {
    std::map<std::string, TypeRef> out;
    for (const FunctionDecl& fn : module.functions) {
        out[fn.name] = function_return_type_ref(fn);
    }
    for (const ClassDecl& klass : module.classes) {
        for (const FunctionDecl& method : klass.methods) {
            out[klass.name + "." + method.name] = function_return_type_ref(method);
        }
    }
    for (const EnumDecl& en : module.enums) {
        for (const FunctionDecl& method : en.methods) {
            out[en.name + "." + method.name] = function_return_type_ref(method);
        }
    }
    return out;
}

bool cpp_function_visible_in_header(const FunctionDecl& fn, const CppEmitOptions& options) {
    return visible_in_cpp_header(fn.visibility) &&
           (options.expose_test_functions || !is_test_function(fn));
}

void emit_cpp_function_body(std::ostringstream& out, const FunctionDecl& fn,
                            const std::vector<std::string>& aliases,
                            const std::map<std::string, TypeRef>& function_returns,
                            const Symbols& symbols, const CppEmitOptions& options) {
    emit_cpp_template_parameters(out, cpp_emit_template_params_for_function(fn),
                                 generic_cpp_value_params_for_function(fn));
    emit_function_signature(out, fn, aliases, options);
    out << " {\n";
    CppLocalContext locals;
    const std::set<std::string> value_params = generic_value_params_for_function(fn);
    for (const std::string& param : fn.generic_params) {
        const std::string name = generic_param_base_name(param);
        if (!value_params.contains(name)) {
            locals.bind_type(name);
        }
    }
    std::map<std::string, TypeRef> local_type_refs;
    for (const ParamDecl& param : fn.params) {
        locals.bind(param.name);
        local_type_refs[param.name] = param.type_ref;
    }
    emit_block(out, fn.statements, 1, aliases, locals, local_type_refs,
               function_return_type_ref(fn), function_returns, &symbols, options);
    out << "}\n\n";
}

void emit_cpp_function_declarations(std::ostringstream& out, const ModuleAst& module,
                                    const std::vector<std::string>& aliases, bool header_only,
                                    bool test_source, const CppEmitOptions& options) {
    bool emitted = false;
    for (const FunctionDecl& fn : module.functions) {
        if (!should_emit_function(fn, test_source) ||
            (header_only && !cpp_function_visible_in_header(fn, options))) {
            continue;
        }
        emit_cpp_template_parameters(out, cpp_emit_template_params_for_function(fn),
                                     generic_cpp_value_params_for_function(fn));
        emit_function_signature(out, fn, aliases, options);
        out << ";\n";
        emitted = true;
    }
    if (emitted) {
        out << '\n';
    }
}

void emit_cpp_early_functions(std::ostringstream& out, const ModuleAst& module,
                              const std::vector<std::string>& aliases,
                              const std::map<std::string, TypeRef>& function_returns,
                              const Symbols& symbols, bool header_only, bool test_source,
                              const CppEmitOptions& options) {
    for (const FunctionDecl& fn : module.functions) {
        if (!emit_before_constants(fn) || !should_emit_function(fn, test_source) ||
            (header_only && !cpp_function_visible_in_header(fn, options))) {
            continue;
        }
        emit_cpp_function_body(out, fn, aliases, function_returns, symbols, options);
    }
}

void emit_cpp_header_generic_function_bodies(std::ostringstream& out, const ModuleAst& module,
                                             const std::vector<std::string>& aliases,
                                             const std::map<std::string, TypeRef>& function_returns,
                                             const Symbols& symbols,
                                             const CppEmitOptions& options) {
    for (const FunctionDecl& fn : module.functions) {
        if (generic_function(fn) && cpp_function_visible_in_header(fn, options)) {
            emit_cpp_function_body(out, fn, aliases, function_returns, symbols, options);
        }
    }
}

void emit_cpp_remaining_function_bodies(std::ostringstream& out, const ModuleAst& module,
                                        const std::vector<std::string>& aliases,
                                        const std::map<std::string, TypeRef>& function_returns,
                                        const Symbols& symbols, bool test_source,
                                        const CppEmitOptions& options) {
    for (const FunctionDecl& fn : module.functions) {
        if (!emit_before_constants(fn) && should_emit_function(fn, test_source)) {
            emit_cpp_function_body(out, fn, aliases, function_returns, symbols, options);
        }
    }
}

void emit_cpp_module_function_bodies(std::ostringstream& out, const ModuleAst& module,
                                     const std::vector<std::string>& aliases,
                                     const std::map<std::string, TypeRef>& function_returns,
                                     const Symbols& symbols, const CppEmitOptions& options) {
    for (const FunctionDecl& fn : module.functions) {
        if (!should_emit_function(fn, options.test_source)) {
            continue;
        }
        const bool body_owned_by_header = cpp_function_visible_in_header(fn, options) &&
                                          (emit_before_constants(fn) || generic_function(fn));
        if (!body_owned_by_header) {
            emit_cpp_function_body(out, fn, aliases, function_returns, symbols, options);
        }
    }
}

void emit_c_function_declarations(std::ostringstream& out, const ModuleAst& module) {
    for (const FunctionDecl& fn : module.functions) {
        if (!has_decorator(fn, "extern_c") || !cpp_function_visible_in_header(fn)) {
            continue;
        }
        out << lower_cpp_type(function_return_type_ref(fn)) << ' ' << fn.name << '(';
        if (fn.params.empty()) {
            out << "void";
        }
        for (size_t i = 0; i < fn.params.size(); ++i) {
            if (i > 0) {
                out << ", ";
            }
            out << lower_cpp_type(fn.params[i].type_ref) << (fn.params[i].variadic ? "... " : " ")
                << fn.params[i].name;
        }
        out << ");\n";
    }
}

} // namespace dudu
