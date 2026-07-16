#include "dudu/codegen/cpp_emit.hpp"

#include "dudu/codegen/cpp_emit_classes.hpp"
#include "dudu/codegen/cpp_emit_declaration_support.hpp"
#include "dudu/codegen/cpp_emit_enum_methods.hpp"
#include "dudu/codegen/cpp_emit_enums.hpp"
#include "dudu/codegen/cpp_emit_functions.hpp"
#include "dudu/codegen/cpp_emit_internal.hpp"
#include "dudu/codegen/cpp_emit_prelude.hpp"
#include "dudu/codegen/cpp_expr_emit.hpp"
#include "dudu/codegen/cpp_lower.hpp"
#include "dudu/codegen/cpp_stmt_emit.hpp"
#include "dudu/codegen/cpp_stmt_emit_support.hpp"
#include "dudu/core/array_shape.hpp"
#include "dudu/core/ast_type.hpp"
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

std::string native_alias_emit_name(const NativeTypeDecl& type, const std::string& stripped_name) {
    if (type.native_spelling.starts_with("struct ") || type.native_spelling.starts_with("union ") ||
        type.native_spelling.starts_with("enum ")) {
        return type.native_spelling;
    }
    return stripped_name;
}

void add_native_type_alias_mappings(CppEmitOptions& options, const ModuleAst& module,
                                    const std::vector<std::string>& aliases) {
    for (const std::string& alias : aliases) {
        if (alias.empty() || alias.front() != '!') {
            continue;
        }
        const std::string prefix = alias.substr(1) + ".";
        for (const NativeTypeDecl& type : module.native_types) {
            if (!type.name.starts_with(prefix)) {
                continue;
            }
            const std::string stripped = type.name.substr(prefix.size());
            options.generated_type_names.try_emplace(type.name,
                                                     native_alias_emit_name(type, stripped));
        }
    }
}

CppEmitOptions source_emit_options(CppEmitOptions options, const ModuleAst& module,
                                   const std::vector<std::string>& aliases) {
    add_native_type_alias_mappings(options, module, aliases);
    return options;
}

} // namespace

namespace {

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

} // namespace

std::string emit_cpp_header(const ModuleAst& module, const CppEmitOptions& options) {
    std::ostringstream out;
    const std::vector<std::string> aliases = namespace_aliases(module);
    const CppEmitOptions emit_options = source_emit_options(options, module, aliases);
    const std::map<std::string, TypeRef> function_returns = cpp_function_return_types(module);
    const Symbols symbols = collect_symbols(module);
    emit_generated_banner(out);
    out << "#pragma once\n\n";
    if (options.emit_prelude) {
        emit_prelude(out, module, true);
    }

    emit_aliases(out, module, emit_options);
    emit_enum_forward_declarations(out, module, emit_options);
    emit_class_forward_declarations(out, module, emit_options, true);
    emit_enum_method_declarations(out, module, aliases, true, emit_options);
    emit_cpp_function_declarations(out, module, aliases, true, false, emit_options);
    emit_value_enums(out, module, aliases, emit_options);
    emit_classes(out, module, aliases, function_returns, symbols, true, emit_options);
    emit_payload_enums(out, module, aliases, emit_options);
    emit_enum_method_definitions(out, module, aliases, function_returns, symbols, true,
                                 emit_options);
    emit_cpp_early_functions(out, module, aliases, function_returns, symbols, true, false,
                             emit_options);
    emit_constants(out, module, aliases, emit_options);
    emit_cpp_header_generic_function_bodies(out, module, aliases, function_returns, symbols,
                                            emit_options);
    emit_static_asserts(out, module, aliases);
    return out.str();
}

std::string emit_cpp_header(const ModuleAst& module) {
    return emit_cpp_header(module, {});
}

std::string emit_c_header(const ModuleAst& module) {
    std::ostringstream out;
    emit_generated_banner(out);
    out << "#pragma once\n\n"
        << "#include <stdbool.h>\n"
        << "#include <stddef.h>\n"
        << "#include <stdint.h>\n\n"
        << "#ifdef __cplusplus\n"
        << "extern \"C\" {\n"
        << "#endif\n\n";
    emit_c_function_declarations(out, module);
    out << "\n#ifdef __cplusplus\n"
        << "}\n"
        << "#endif\n";
    return out.str();
}

std::string emit_cpp_source(const ModuleAst& module, const CppEmitOptions& options) {
    std::ostringstream out;
    const std::vector<std::string> aliases = namespace_aliases(module);
    const CppEmitOptions emit_options = source_emit_options(options, module, aliases);
    const std::map<std::string, TypeRef> function_returns = cpp_function_return_types(module);
    const Symbols symbols = collect_symbols(module);
    emit_generated_banner(out);
    if (emit_options.emit_prelude) {
        emit_prelude(out, module, true);
    }

    emit_aliases(out, module, emit_options);
    emit_enum_forward_declarations(out, module, emit_options);
    emit_class_forward_declarations(out, module, emit_options);
    emit_enum_method_declarations(out, module, aliases, false, emit_options);
    emit_cpp_function_declarations(out, module, aliases, false, emit_options.test_source,
                                   emit_options);
    emit_value_enums(out, module, aliases, emit_options);
    emit_classes(out, module, aliases, function_returns, symbols, false, emit_options);
    emit_payload_enums(out, module, aliases, emit_options);
    emit_enum_method_definitions(out, module, aliases, function_returns, symbols, false,
                                 emit_options);
    emit_cpp_early_functions(out, module, aliases, function_returns, symbols, false,
                             emit_options.test_source, emit_options);
    emit_constants(out, module, aliases, emit_options);

    emit_cpp_remaining_function_bodies(out, module, aliases, function_returns, symbols,
                                       emit_options.test_source, emit_options);
    emit_static_asserts(out, module, aliases);
    return out.str();
}

std::string emit_cpp_source(const ModuleAst& module) {
    return emit_cpp_source(module, {});
}

std::string emit_cpp_module_implementation(const ModuleAst& module, const CppEmitOptions& options) {
    std::ostringstream out;
    const std::vector<std::string> aliases = namespace_aliases(module);
    const CppEmitOptions emit_options = source_emit_options(options, module, aliases);
    const std::map<std::string, TypeRef> function_returns = cpp_function_return_types(module);
    const Symbols symbols = collect_symbols(module);

    ModuleAst source_local;
    for (const ClassDecl& klass : module.classes) {
        if (!visible_in_cpp_header(klass.visibility)) {
            source_local.classes.push_back(klass);
        }
    }
    for (const FunctionDecl& fn : module.functions) {
        if (!cpp_function_visible_in_header(fn, emit_options)) {
            source_local.functions.push_back(fn);
        }
    }

    emit_class_forward_declarations(out, source_local, emit_options);
    emit_private_enum_method_declarations(out, module, aliases, emit_options);
    emit_cpp_function_declarations(out, source_local, aliases, false, emit_options.test_source,
                                   emit_options);
    emit_classes(out, source_local, aliases, function_returns, symbols, false, emit_options);
    emit_public_class_method_definitions(out, module, aliases, function_returns, symbols,
                                         emit_options);
    emit_enum_method_definitions(out, module, aliases, function_returns, symbols, false,
                                 emit_options);

    emit_cpp_module_function_bodies(out, module, aliases, function_returns, symbols, emit_options);
    return out.str();
}

std::string emit_cpp_test_source(const ModuleAst& module, const std::string& filter,
                                 bool capture_output) {
    std::ostringstream out;
    const std::vector<std::string> aliases = namespace_aliases(module);
    const CppEmitOptions options = source_emit_options({}, module, aliases);
    const std::map<std::string, TypeRef> function_returns = cpp_function_return_types(module);
    const Symbols symbols = collect_symbols(module);
    emit_generated_banner(out);
    emit_prelude(out, module, true);

    emit_aliases(out, module, options);
    emit_enum_forward_declarations(out, module, options);
    emit_class_forward_declarations(out, module, options);
    emit_enum_method_declarations(out, module, aliases, false, options);
    emit_cpp_function_declarations(out, module, aliases, false, true, options);
    emit_classes(out, module, aliases, function_returns, symbols, false, options);
    emit_enums(out, module, aliases, options);
    emit_enum_method_definitions(out, module, aliases, function_returns, symbols, false, options);
    emit_cpp_early_functions(out, module, aliases, function_returns, symbols, false, true, options);
    emit_constants(out, module, aliases, options);

    emit_cpp_remaining_function_bodies(out, module, aliases, function_returns, symbols, true,
                                       options);
    emit_static_asserts(out, module, aliases);
    emit_test_harness(out, module, filter, capture_output);
    return out.str();
}

} // namespace dudu
