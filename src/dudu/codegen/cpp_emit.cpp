#include "dudu/codegen/cpp_emit.hpp"

#include "dudu/core/array_shape.hpp"
#include "dudu/core/ast_type.hpp"
#include "dudu/codegen/cpp_emit_classes.hpp"
#include "dudu/codegen/cpp_emit_enums.hpp"
#include "dudu/codegen/cpp_emit_internal.hpp"
#include "dudu/codegen/cpp_emit_prelude.hpp"
#include "dudu/codegen/cpp_expr_emit.hpp"
#include "dudu/codegen/cpp_lower.hpp"
#include "dudu/codegen/cpp_stmt_emit.hpp"
#include "dudu/codegen/cpp_stmt_emit_support.hpp"
#include "dudu/core/decorators.hpp"
#include "dudu/sema/sema_context.hpp"
#include "dudu/sema/sema_generics.hpp"

#include <map>
#include <set>
#include <sstream>
#include <string_view>
#include <vector>

namespace dudu {
namespace {
void emit_aliases(std::ostringstream& out, const ModuleAst& module, const CppEmitOptions& options) {
    for (const TypeAliasDecl& alias : module.aliases) {
        out << "using " << emitted_name(alias, options) << " = " << lower_cpp_type(alias.type_ref)
            << ";\n";
    }
    if (!module.aliases.empty()) {
        out << '\n';
    }
}

} // namespace

bool cpp_emit_function_has_decorator(const FunctionDecl& fn, std::string_view name) {
    return has_decorator(fn.decorators, name);
}

bool cpp_emit_function_is_test(const FunctionDecl& fn) {
    for (const Decorator& decorator : fn.decorators) {
        if (decorator_matches(decorator, "test") || decorator_matches(decorator, "test.ignore") ||
            decorator_matches(decorator, "test.should_panic") ||
            decorator_call_matches(decorator, "test.should_panic")) {
            return true;
        }
    }
    return false;
}

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

namespace {

std::string cpp_emit_function_decorator_args(const FunctionDecl& fn, std::string_view name) {
    for (const Decorator& decorator : fn.decorators) {
        if (const std::optional<std::string> args = decorator_arg_list_display(decorator, name)) {
            return *args;
        }
    }
    return {};
}

bool visible_in_header(Visibility visibility) {
    return visibility != Visibility::Private;
}

bool visible_function_in_header(const FunctionDecl& fn, const CppEmitOptions& options = {}) {
    return visible_in_header(fn.visibility) &&
           (options.expose_test_functions || !cpp_emit_function_is_test(fn));
}

bool emit_before_constants(const FunctionDecl& fn) {
    return cpp_emit_function_has_decorator(fn, "constexpr");
}

void emit_template_params(std::ostringstream& out, const std::vector<std::string>& params,
                          const std::set<std::string>& value_params = {}) {
    if (params.empty()) {
        return;
    }
    out << "template <";
    for (size_t i = 0; i < params.size(); ++i) {
        if (i > 0) {
            out << ", ";
        }
        out << (value_params.contains(params[i]) ? "size_t " : "typename ") << params[i];
    }
    out << ">\n";
}

std::map<std::string, TypeRef> function_return_types(const ModuleAst& module) {
    std::map<std::string, TypeRef> out;
    for (const FunctionDecl& fn : module.functions) {
        out[fn.name] = function_return_type_ref(fn);
    }
    for (const ClassDecl& klass : module.classes) {
        for (const FunctionDecl& method : klass.methods) {
            out[klass.name + "." + method.name] = function_return_type_ref(method);
        }
    }
    return out;
}

void emit_constants(std::ostringstream& out, const ModuleAst& module,
                    const std::vector<std::string>& aliases, const CppEmitOptions& options) {
    for (const ConstDecl& constant : module.constants) {
        TypeRef type_ref = constant.type_ref;
        const ArrayShapeInference inferred =
            infer_array_literal_shape_type(constant.type_ref, constant.value_expr);
        if (inferred.status == ArrayShapeStatus::Inferred) {
            type_ref = inferred.type_ref;
        }
        const std::string& name = emitted_name(constant, options);
        const std::string lowered_type = lower_cpp_type(type_ref, aliases, options);
        const bool pointer = type_ref_contains_kind(type_ref, TypeKind::Pointer);
        const bool runtime_address =
            pointer || type_ref_contains_kind(type_ref, TypeKind::Volatile);
        out << "inline ";
        if (runtime_address && pointer) {
            out << lowered_type << " const " << name;
        } else {
            out << (runtime_address ? "const " : "constexpr ") << lowered_type << ' ' << name;
        }
        out << " = ";
        if (inferred.status == ArrayShapeStatus::Inferred) {
            out << lower_fixed_array_literal_as_type_ref(type_ref, constant.value_expr, aliases,
                                                         CppLocalContext{}, {}, {}, nullptr,
                                                         options);
        } else {
            out << lower_cpp_expr_ast(constant.value_expr, aliases, CppLocalContext{}, options);
        }
        out << ";\n";
    }
    if (!module.constants.empty()) {
        out << '\n';
    }
}

void emit_static_asserts(std::ostringstream& out, const ModuleAst& module,
                         const std::vector<std::string>& aliases) {
    for (const StaticAssertDecl& assertion : module.static_asserts) {
        out << "static_assert"
            << lower_cpp_expr_ast(assertion.expression_expr, aliases, CppLocalContext{}, {})
            << ";\n";
    }
    if (!module.static_asserts.empty()) {
        out << '\n';
    }
}

void emit_function_signature(std::ostringstream& out, const FunctionDecl& fn,
                             const std::vector<std::string>& aliases,
                             const CppEmitOptions& options = {}) {
    if (cpp_emit_function_has_decorator(fn, "extern_c")) {
        out << "extern \"C\" ";
    }
    if (cpp_emit_function_has_decorator(fn, "cuda.global")) {
        out << "DUDU_CUDA_GLOBAL ";
    }
    if (cpp_emit_function_has_decorator(fn, "cuda.device")) {
        out << "DUDU_CUDA_DEVICE ";
    }
    if (cpp_emit_function_has_decorator(fn, "cuda.host")) {
        out << "DUDU_CUDA_HOST ";
    }
    if (cpp_emit_function_has_decorator(fn, "shader.compute")) {
        out << "DUDU_SHADER_COMPUTE ";
    }
    const std::string section = cpp_emit_function_decorator_arg(fn, "section");
    if (!section.empty()) {
        out << "__attribute__((section(" << cpp_emit_string_literal(section) << "))) ";
    }
    const std::string workgroup = cpp_emit_function_decorator_args(fn, "workgroup_size");
    if (!workgroup.empty()) {
        out << "DUDU_WORKGROUP_SIZE(" << workgroup << ") ";
    }
    if (cpp_emit_function_has_decorator(fn, "inline")) {
        out << "inline ";
    }
    if (cpp_emit_function_has_decorator(fn, "constexpr")) {
        out << "constexpr ";
    }
    const std::string& name = cpp_emit_function_has_decorator(fn, "extern_c")
                                  ? fn.name
                                  : emitted_value_name(fn.name, options);
    out << lower_cpp_type(function_return_type_ref(fn), aliases, options) << ' ' << name << '(';
    for (size_t i = 0; i < fn.params.size(); ++i) {
        if (i > 0) {
            out << ", ";
        }
        out << lower_cpp_type(fn.params[i].type_ref, aliases, options) << ' ' << fn.params[i].name;
    }
    out << ')';
}

void emit_function_body(std::ostringstream& out, const FunctionDecl& fn,
                        const std::vector<std::string>& aliases,
                        const std::map<std::string, TypeRef>& function_returns,
                        const Symbols& symbols, const CppEmitOptions& options = {}) {
    emit_template_params(out, fn.generic_params, generic_value_params_for_function(fn));
    emit_function_signature(out, fn, aliases, options);
    out << " {\n";
    CppLocalContext locals;
    std::map<std::string, TypeRef> local_type_refs;
    for (const ParamDecl& param : fn.params) {
        locals.bind(param.name);
        local_type_refs[param.name] = param.type_ref;
    }
    emit_block(out, fn.statements, 1, aliases, locals, local_type_refs,
               function_return_type_ref(fn), function_returns, &symbols, options);
    out << "}\n\n";
}

bool should_emit_function(const FunctionDecl& fn, bool test_source) {
    return !test_source || fn.name != "main";
}

void emit_function_declarations(std::ostringstream& out, const ModuleAst& module,
                                const std::vector<std::string>& aliases, bool header_only,
                                bool test_source = false, const CppEmitOptions& options = {}) {
    bool emitted = false;
    for (const FunctionDecl& fn : module.functions) {
        if (!should_emit_function(fn, test_source)) {
            continue;
        }
        if (header_only && !visible_function_in_header(fn, options)) {
            continue;
        }
        emit_template_params(out, fn.generic_params, generic_value_params_for_function(fn));
        emit_function_signature(out, fn, aliases, options);
        out << ";\n";
        emitted = true;
    }
    if (emitted) {
        out << '\n';
    }
}

void emit_class_forward_declarations(std::ostringstream& out, const ModuleAst& module,
                                     const CppEmitOptions& options = {}) {
    if (module.classes.empty()) {
        return;
    }
    for (const ClassDecl& klass : module.classes) {
        emit_template_params(out, klass.generic_params, generic_value_params_for_class(klass));
        out << "struct " << emitted_name(klass, options) << ";\n";
    }
    out << '\n';
}

void emit_early_functions(std::ostringstream& out, const ModuleAst& module,
                          const std::vector<std::string>& aliases,
                          const std::map<std::string, TypeRef>& function_returns,
                          const Symbols& symbols, bool header_only, bool test_source = false,
                          const CppEmitOptions& options = {}) {
    for (const FunctionDecl& fn : module.functions) {
        if (!emit_before_constants(fn) || !should_emit_function(fn, test_source)) {
            continue;
        }
        if (header_only && !visible_function_in_header(fn, options)) {
            continue;
        }
        emit_function_body(out, fn, aliases, function_returns, symbols, options);
    }
}

} // namespace

std::string emit_cpp_header(const ModuleAst& module, const CppEmitOptions& options) {
    std::ostringstream out;
    const std::vector<std::string> aliases = namespace_aliases(module);
    const std::map<std::string, TypeRef> function_returns = function_return_types(module);
    const Symbols symbols = collect_symbols(module);
    out << "#pragma once\n\n";
    if (options.emit_prelude) {
        emit_includes(out, module);
        emit_result_prelude(out, module);
    }

    emit_aliases(out, module, options);
    emit_enum_forward_declarations(out, module, options);
    emit_classes(out, module, aliases, function_returns, symbols, true, options);
    emit_enums(out, module, aliases, options);
    emit_early_functions(out, module, aliases, function_returns, symbols, true, false, options);
    emit_constants(out, module, aliases, options);

    for (const FunctionDecl& fn : module.functions) {
        if (emit_before_constants(fn)) {
            continue;
        }
        if (!visible_function_in_header(fn, options)) {
            continue;
        }
        emit_template_params(out, fn.generic_params, generic_value_params_for_function(fn));
        emit_function_signature(out, fn, aliases, options);
        out << ";\n";
    }
    emit_static_asserts(out, module, aliases);
    return out.str();
}

std::string emit_cpp_header(const ModuleAst& module) {
    return emit_cpp_header(module, {});
}

std::string emit_c_header(const ModuleAst& module) {
    std::ostringstream out;
    out << "#pragma once\n\n"
        << "#include <stdbool.h>\n"
        << "#include <stddef.h>\n"
        << "#include <stdint.h>\n\n"
        << "#ifdef __cplusplus\n"
        << "extern \"C\" {\n"
        << "#endif\n\n";
    for (const FunctionDecl& fn : module.functions) {
        if (!cpp_emit_function_has_decorator(fn, "extern_c") || !visible_function_in_header(fn)) {
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
            out << lower_cpp_type(fn.params[i].type_ref) << ' ' << fn.params[i].name;
        }
        out << ");\n";
    }
    out << "\n#ifdef __cplusplus\n"
        << "}\n"
        << "#endif\n";
    return out.str();
}

std::string emit_cpp_source(const ModuleAst& module, const CppEmitOptions& options) {
    std::ostringstream out;
    const std::vector<std::string> aliases = namespace_aliases(module);
    const std::map<std::string, TypeRef> function_returns = function_return_types(module);
    const Symbols symbols = collect_symbols(module);
    if (options.emit_prelude) {
        emit_includes(out, module);
        emit_result_prelude(out, module);
    }

    emit_aliases(out, module, options);
    emit_enum_forward_declarations(out, module, options);
    emit_class_forward_declarations(out, module, options);
    emit_function_declarations(out, module, aliases, false, options.test_source, options);
    emit_classes(out, module, aliases, function_returns, symbols, false, options);
    emit_enums(out, module, aliases, options);
    emit_early_functions(out, module, aliases, function_returns, symbols, false,
                         options.test_source, options);
    emit_constants(out, module, aliases, options);

    for (const FunctionDecl& fn : module.functions) {
        if (emit_before_constants(fn) || !should_emit_function(fn, options.test_source)) {
            continue;
        }
        emit_function_body(out, fn, aliases, function_returns, symbols, options);
    }
    emit_static_asserts(out, module, aliases);
    return out.str();
}

std::string emit_cpp_source(const ModuleAst& module) {
    return emit_cpp_source(module, {});
}

std::string emit_cpp_test_source(const ModuleAst& module, const std::string& filter,
                                 bool capture_output) {
    std::ostringstream out;
    const std::vector<std::string> aliases = namespace_aliases(module);
    const std::map<std::string, TypeRef> function_returns = function_return_types(module);
    const Symbols symbols = collect_symbols(module);
    emit_includes(out, module);
    emit_result_prelude(out, module);

    emit_aliases(out, module, {});
    emit_enum_forward_declarations(out, module);
    emit_class_forward_declarations(out, module);
    emit_function_declarations(out, module, aliases, false, true);
    emit_classes(out, module, aliases, function_returns, symbols);
    emit_enums(out, module, aliases);
    emit_early_functions(out, module, aliases, function_returns, symbols, false, true);
    emit_constants(out, module, aliases, {});

    for (const FunctionDecl& fn : module.functions) {
        if (emit_before_constants(fn) || !should_emit_function(fn, true)) {
            continue;
        }
        emit_function_body(out, fn, aliases, function_returns, symbols);
    }
    emit_static_asserts(out, module, aliases);
    emit_test_harness(out, module, filter, capture_output);
    return out.str();
}

} // namespace dudu
