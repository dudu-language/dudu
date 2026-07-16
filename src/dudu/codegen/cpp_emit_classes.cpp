#include "dudu/codegen/cpp_emit_classes.hpp"

#include "dudu/codegen/cpp_emit_class_methods.hpp"
#include "dudu/codegen/cpp_emit_declaration_support.hpp"
#include "dudu/codegen/cpp_emit_internal.hpp"
#include "dudu/codegen/cpp_expr_emit.hpp"
#include "dudu/codegen/cpp_lower.hpp"
#include "dudu/codegen/cpp_stmt_emit_support.hpp"
#include "dudu/core/ast_type.hpp"
#include "dudu/core/decorators.hpp"
#include "dudu/core/naming.hpp"
#include "dudu/sema/sema_context.hpp"
#include "dudu/sema/sema_generics.hpp"

#include <algorithm>
#include <sstream>
#include <string_view>
#include <unordered_map>

namespace dudu {
namespace {

using ClassIndex = std::unordered_map<std::string_view, size_t>;

std::string_view named_type_head(const TypeRef& type) {
    switch (type.kind) {
    case TypeKind::Named:
    case TypeKind::Qualified:
    case TypeKind::Template:
    case TypeKind::Associated:
        return type.name.str();
    default:
        return {};
    }
}

void collect_class_dependencies(const TypeRef& type, const ClassIndex& class_index,
                                bool require_complete_type, std::vector<size_t>& out) {
    if (require_complete_type &&
        (type.kind == TypeKind::Pointer || type.kind == TypeKind::Reference)) {
        return;
    }
    if (const auto found = class_index.find(named_type_head(type)); found != class_index.end()) {
        out.push_back(found->second);
    }
    for (const TypeRef& child : type.children) {
        collect_class_dependencies(child, class_index, require_complete_type, out);
    }
}

enum class ClassVisitState : unsigned char { Unvisited, Visiting, Emitted };

void visit_class(const std::vector<std::vector<size_t>>& dependencies, size_t index,
                 std::vector<ClassVisitState>& states, std::vector<size_t>& order) {
    if (states[index] != ClassVisitState::Unvisited) {
        return;
    }
    states[index] = ClassVisitState::Visiting;
    for (const size_t dependency : dependencies[index]) {
        if (dependency != index) {
            visit_class(dependencies, dependency, states, order);
        }
    }
    states[index] = ClassVisitState::Emitted;
    order.push_back(index);
}

std::vector<size_t> class_emit_order(const std::vector<ClassDecl>& classes) {
    ClassIndex class_index;
    class_index.reserve(classes.size());
    for (size_t index = 0; index < classes.size(); ++index) {
        class_index.emplace(classes[index].name, index);
    }

    std::vector<std::vector<size_t>> dependencies(classes.size());
    for (size_t index = 0; index < classes.size(); ++index) {
        std::vector<size_t>& class_dependencies = dependencies[index];
        for (const BaseClassDecl& base : classes[index].base_class_refs) {
            collect_class_dependencies(base.type_ref, class_index, false, class_dependencies);
        }
        for (const FieldDecl& field : classes[index].fields) {
            collect_class_dependencies(field.type_ref, class_index, true, class_dependencies);
        }
        std::ranges::sort(class_dependencies);
        class_dependencies.erase(std::unique(class_dependencies.begin(), class_dependencies.end()),
                                 class_dependencies.end());
    }

    std::vector<ClassVisitState> states(classes.size(), ClassVisitState::Unvisited);
    std::vector<size_t> order;
    order.reserve(classes.size());
    for (size_t i = 0; i < classes.size(); ++i) {
        visit_class(dependencies, i, states, order);
    }
    return order;
}

std::string decorator_arg(const ClassDecl& klass, std::string_view name) {
    for (const Decorator& decorator : klass.decorators) {
        if (const std::optional<std::string> arg = decorator_first_arg_display(decorator, name)) {
            return *arg;
        }
    }
    return {};
}

bool class_has_decorator(const ClassDecl& klass, std::string_view name) {
    return has_decorator(klass.decorators, name);
}

std::string class_opening(const ClassDecl& klass, const std::vector<std::string>& aliases,
                          const CppEmitOptions& options) {
    const std::string& class_name = emitted_name(klass, options);
    auto with_bases = [&](std::string opening) {
        if (klass.base_class_refs.empty()) {
            return opening;
        }
        opening += " : ";
        for (size_t i = 0; i < klass.base_class_refs.size(); ++i) {
            if (i > 0) {
                opening += ", ";
            }
            opening +=
                "public " + lower_cpp_type(klass.base_class_refs[i].type_ref, aliases, options);
        }
        return opening;
    };
    const bool packed = class_has_decorator(klass, "packed");
    const std::string alignment = decorator_arg(klass, "align");
    if (packed && !alignment.empty()) {
        return with_bases("struct __attribute__((packed, aligned(" + alignment + "))) " +
                          class_name);
    }
    if (packed) {
        return with_bases("struct __attribute__((packed)) " + class_name);
    }
    if (!alignment.empty()) {
        return with_bases("struct alignas(" + alignment + ") " + class_name);
    }
    return with_bases("struct " + class_name);
}

void emit_class_constant_decl(std::ostringstream& out, const std::string& source_class_name,
                              const ConstDecl& constant, const std::vector<std::string>& aliases,
                              const CppEmitOptions& options) {
    const std::string lowered_type = lower_cpp_type(constant.type_ref, aliases, options);
    const bool pointer = type_ref_contains_kind(constant.type_ref, TypeKind::Pointer);
    out << "    static ";
    out << (pointer ? lowered_type + " const " : "const " + lowered_type + " ");
    out << emitted_member_name(source_class_name, constant.name, options) << ";\n";
}

void emit_class_constant_definition(std::ostringstream& out, const std::string& source_class_name,
                                    const std::string& class_name, const ConstDecl& constant,
                                    const std::vector<std::string>& aliases,
                                    const CppEmitOptions& options) {
    const std::string lowered_type = lower_cpp_type(constant.type_ref, aliases, options);
    const bool pointer = type_ref_contains_kind(constant.type_ref, TypeKind::Pointer);
    const bool runtime_address =
        pointer || type_ref_contains_kind(constant.type_ref, TypeKind::Volatile);
    out << "inline ";
    const std::string name = emitted_member_name(source_class_name, constant.name, options);
    if (runtime_address && pointer) {
        out << lowered_type << " const " << class_name << "::" << name;
    } else {
        out << (runtime_address ? "const " : "constexpr ") << lowered_type << ' ' << class_name
            << "::" << name;
    }
    out << " = " << lower_cpp_expr_ast(constant.value_expr, aliases, CppLocalContext{}, options)
        << ";\n";
}

} // namespace

void emit_classes(std::ostringstream& out, const ModuleAst& module,
                  const std::vector<std::string>& aliases,
                  const std::map<std::string, TypeRef>& function_returns, const Symbols& symbols,
                  bool header_only, const CppEmitOptions& options) {
    const std::vector<size_t> emit_order = class_emit_order(module.classes);
    for (const size_t index : emit_order) {
        const ClassDecl& klass = module.classes[index];
        const std::string& class_name = emitted_name(klass, options);
        if (header_only && !visible_in_cpp_header(klass.visibility)) {
            continue;
        }
        emit_cpp_template_parameters(out, generic_cpp_params_for_class(klass),
                                     generic_cpp_value_params_for_class(klass));
        out << class_opening(klass, aliases, options) << " {\n";
        for (const FieldDecl& field : klass.fields) {
            out << "    " << lower_cpp_type(field.type_ref, aliases, options) << ' '
                << emitted_member_name(klass.name, field.name, options);
            if (has_expr(field.value_expr)) {
                out << " = ";
                if (is_template_type(field.type_ref, "Option") &&
                    field.value_expr.kind == ExprKind::NoneLiteral) {
                    out << "std::nullopt";
                } else {
                    out << lower_expr(field.value_expr, aliases, CppLocalContext{}, &symbols,
                                      options);
                }
            } else if (field.type_ref.kind != TypeKind::Reference) {
                out << "{}";
            }
            out << ";\n";
        }
        for (const ConstDecl& field : klass.static_fields) {
            out << "    inline static " << lower_cpp_type(field.type_ref, aliases, options) << ' '
                << emitted_member_name(klass.name, field.name, options) << " = "
                << lower_expr(field.value_expr, aliases, CppLocalContext{}, &symbols, options)
                << ";\n";
        }
        for (const ConstDecl& constant : klass.constants) {
            emit_class_constant_decl(out, klass.name, constant, aliases, options);
        }
        emit_class_method_members(out, klass, class_name, aliases, function_returns, symbols,
                                  options);
        out << "};\n\n";
        for (const ConstDecl& constant : klass.constants) {
            emit_class_constant_definition(out, klass.name, class_name, constant, aliases, options);
        }
        if (!klass.constants.empty()) {
            out << '\n';
        }
    }
    if (!header_only) {
        for (const size_t index : emit_order) {
            const ClassDecl& klass = module.classes[index];
            emit_out_of_line_class_methods(out, klass, emitted_name(klass, options), aliases,
                                           function_returns, symbols, options);
        }
    }
}

void emit_public_class_method_definitions(std::ostringstream& out, const ModuleAst& module,
                                          const std::vector<std::string>& aliases,
                                          const std::map<std::string, TypeRef>& function_returns,
                                          const Symbols& symbols, const CppEmitOptions& options) {
    const std::vector<size_t> emit_order = class_emit_order(module.classes);
    for (const size_t index : emit_order) {
        const ClassDecl& klass = module.classes[index];
        if (!visible_in_cpp_header(klass.visibility)) {
            continue;
        }
        emit_out_of_line_class_methods(out, klass, emitted_name(klass, options), aliases,
                                       function_returns, symbols, options);
    }
}

} // namespace dudu
