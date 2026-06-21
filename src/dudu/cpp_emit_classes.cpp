#include "dudu/cpp_emit_classes.hpp"

#include "dudu/ast_expr.hpp"
#include "dudu/ast_type.hpp"
#include "dudu/cpp_expr_emit.hpp"
#include "dudu/cpp_lower.hpp"
#include "dudu/cpp_stmt_emit.hpp"
#include "dudu/decorators.hpp"
#include "dudu/sema_context.hpp"
#include "dudu/sema_generics.hpp"
#include "dudu/sema_inheritance.hpp"

#include <set>
#include <sstream>
#include <string_view>

namespace dudu {
namespace {

bool type_ref_mentions_class(const TypeRef& type, const std::string& name) {
    if (type_ref_head_name(type) == name) {
        return true;
    }
    for (const TypeRef& child : type.children) {
        if (type_ref_mentions_class(child, name)) {
            return true;
        }
    }
    return false;
}

std::string class_lookup_name(const Symbols& symbols, TypeRef type) {
    type = resolve_alias_ref(symbols, std::move(type));
    return type_ref_head_name(type);
}

void visit_class(const std::vector<ClassDecl>& classes, size_t index, std::set<size_t>& visiting,
                 std::set<size_t>& emitted, std::vector<size_t>& order) {
    if (emitted.contains(index) || visiting.contains(index)) {
        return;
    }
    visiting.insert(index);

    for (const BaseClassDecl& base_decl : classes[index].base_class_refs) {
        for (size_t dep = 0; dep < classes.size(); ++dep) {
            if (dep != index && type_ref_mentions_class(base_decl.type_ref, classes[dep].name)) {
                visit_class(classes, dep, visiting, emitted, order);
            }
        }
    }

    for (const FieldDecl& field : classes[index].fields) {
        for (size_t dep = 0; dep < classes.size(); ++dep) {
            if (dep != index && type_ref_mentions_class(field.type_ref, classes[dep].name)) {
                visit_class(classes, dep, visiting, emitted, order);
            }
        }
    }

    visiting.erase(index);
    emitted.insert(index);
    order.push_back(index);
}

std::vector<size_t> class_emit_order(const std::vector<ClassDecl>& classes) {
    std::set<size_t> visiting;
    std::set<size_t> emitted;
    std::vector<size_t> order;
    for (size_t i = 0; i < classes.size(); ++i) {
        visit_class(classes, i, visiting, emitted, order);
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

bool method_has_decorator(const FunctionDecl& method, std::string_view name) {
    return has_decorator(method.decorators, name);
}

std::string function_decorator_arg(const FunctionDecl& fn, std::string_view name) {
    for (const Decorator& decorator : fn.decorators) {
        if (const std::optional<std::string> arg =
                decorator_first_string_literal_arg(decorator, name)) {
            return *arg;
        }
    }
    return {};
}

bool is_constructor_method(const FunctionDecl& method) {
    return method.name == "init";
}

bool is_destructor_method(const FunctionDecl& method) {
    return method.name == "drop";
}

std::string operator_name(const FunctionDecl& method) {
    const std::string op = function_decorator_arg(method, "operator");
    if (!op.empty()) {
        if (op == "[]=") {
            return method.name;
        }
        return op == "bool" ? "operator bool" : "operator" + op;
    }
    return method.name;
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

const Expr* super_init_expr(const FunctionDecl& method) {
    if (!is_constructor_method(method) || method.statements.empty()) {
        return nullptr;
    }
    const Stmt& first = method.statements.front();
    if (first.kind == StmtKind::Expr && is_member_callee(first.expr, "super", "init")) {
        return &first.expr;
    }
    return nullptr;
}

const BaseClassDecl* super_init_base_decl(const Symbols& symbols, const std::string& class_name) {
    const auto klass = symbols.classes.find(class_name);
    if (klass == symbols.classes.end() || klass->second->base_class_refs.empty()) {
        return nullptr;
    }
    if (klass->second->base_class_refs.size() == 1) {
        return &klass->second->base_class_refs.front();
    }
    const BaseClassDecl* storage_base = nullptr;
    for (const BaseClassDecl& base : klass->second->base_class_refs) {
        if (class_type_has_instance_storage(symbols, base.type_ref)) {
            if (storage_base != nullptr) {
                return nullptr;
            }
            storage_base = &base;
        }
    }
    return storage_base;
}

std::string join_lowered_args(const std::vector<Expr>& args,
                              const std::vector<std::string>& aliases,
                              const CppLocalContext& locals) {
    std::ostringstream out;
    for (size_t i = 0; i < args.size(); ++i) {
        if (i > 0) {
            out << ", ";
        }
        out << lower_cpp_expr_ast(args[i], aliases, locals);
    }
    return out.str();
}

bool method_is_instance(const FunctionDecl& method) {
    return !method.params.empty() && method.params.front().name == "self";
}

bool has_drop_method(const ClassDecl& klass) {
    for (const FunctionDecl& method : klass.methods) {
        if (is_destructor_method(method)) {
            return true;
        }
    }
    return false;
}

bool class_is_polymorphic(const Symbols& symbols, const ClassDecl& klass,
                          std::set<std::string>& seen) {
    if (!seen.insert(klass.name).second) {
        return false;
    }
    for (const FunctionDecl& method : klass.methods) {
        if (method_is_instance(method) &&
            (method_has_decorator(method, "virtual") || method_has_decorator(method, "abstract"))) {
            return true;
        }
    }
    for (const BaseClassDecl& base_decl : klass.base_class_refs) {
        const auto parent = symbols.classes.find(class_lookup_name(symbols, base_decl.type_ref));
        if (parent != symbols.classes.end() &&
            class_is_polymorphic(symbols, *parent->second, seen)) {
            return true;
        }
    }
    return false;
}

bool class_is_polymorphic(const Symbols& symbols, const ClassDecl& klass) {
    std::set<std::string> seen;
    return class_is_polymorphic(symbols, klass, seen);
}

bool visible_in_header(Visibility visibility) {
    return visibility != Visibility::Private;
}

void emit_template_params(std::ostringstream& out, const std::vector<std::string>& params,
                          std::string_view prefix = {},
                          const std::set<std::string>& value_params = {}) {
    if (params.empty()) {
        return;
    }
    out << prefix << "template <";
    for (size_t i = 0; i < params.size(); ++i) {
        if (i > 0) {
            out << ", ";
        }
        out << (value_params.contains(params[i]) ? "size_t " : "typename ") << params[i];
    }
    out << ">\n";
}

std::string_view class_section_for_method(Visibility visibility) {
    return visibility == Visibility::Private ? "private" : "public";
}

void emit_method(std::ostringstream& out, const std::string& class_name,
                 const std::string& source_class_name, const FunctionDecl& method,
                 const std::vector<std::string>& aliases,
                 const std::map<std::string, TypeRef>& function_returns, const Symbols& symbols,
                 const CppEmitOptions& options) {
    const size_t first_param =
        !method.params.empty() && method.params.front().name == "self" ? 1 : 0;
    emit_template_params(out, method.generic_params, "    ",
                         generic_value_params_for_function(method));
    if (is_constructor_method(method)) {
        out << "    " << class_name << '(';
    } else if (is_destructor_method(method)) {
        const auto klass = symbols.classes.find(source_class_name);
        if (klass != symbols.classes.end() && class_is_polymorphic(symbols, *klass->second)) {
            out << "    virtual ";
        } else {
            out << "    ";
        }
        out << "~" << class_name << '(';
    } else {
        out << "    ";
        if (first_param == 0) {
            out << "static ";
        }
        if (first_param == 1 &&
            (method_has_decorator(method, "virtual") || method_has_decorator(method, "abstract"))) {
            out << "virtual ";
        }
        const std::string lowered_name = operator_name(method);
        if (lowered_name == "operator bool") {
            out << "explicit " << lowered_name << '(';
        } else {
            out << lower_cpp_type(function_return_type_ref(method), aliases, options) << ' '
                << lowered_name << '(';
        }
    }
    for (size_t i = first_param; i < method.params.size(); ++i) {
        if (i > first_param) {
            out << ", ";
        }
        out << lower_cpp_type(method.params[i].type_ref, aliases, options) << ' '
            << method.params[i].name;
    }
    CppLocalContext locals;
    std::map<std::string, TypeRef> local_type_refs;
    locals.current_class = class_name;
    local_type_refs["class"] = named_type_ref(class_name, method.location);
    const auto klass = symbols.classes.find(source_class_name);
    if (klass != symbols.classes.end() && klass->second->base_class_refs.size() == 1) {
        locals.super_class =
            lower_cpp_type(klass->second->base_class_refs.front().type_ref, aliases, options);
        local_type_refs["super"] = klass->second->base_class_refs.front().type_ref;
    }
    if (first_param == 1) {
        locals.bind(method.params.front().name);
        local_type_refs[method.params.front().name] = method.params.front().type_ref;
    }
    for (size_t i = first_param; i < method.params.size(); ++i) {
        locals.bind(method.params[i].name);
        local_type_refs[method.params[i].name] = method.params[i].type_ref;
    }
    out << ")";
    if (is_constructor_method(method)) {
        if (const Expr* super_init = super_init_expr(method)) {
            if (const BaseClassDecl* base = super_init_base_decl(symbols, class_name)) {
                out << " : " << lower_cpp_type(base->type_ref, aliases, options) << "("
                    << join_lowered_args(super_init->children, aliases, locals) << ")";
            }
        }
    }
    if (method_has_decorator(method, "override")) {
        out << " override";
    }
    if (method_has_decorator(method, "abstract")) {
        out << " = 0;\n";
        return;
    }
    out << " {\n";
    if (first_param == 1) {
        out << "        auto& self = *this;\n";
    }
    if (is_constructor_method(method) && super_init_expr(method) != nullptr) {
        std::vector<Stmt> body(method.statements.begin() + 1, method.statements.end());
        emit_block(out, body, 2, aliases, locals, local_type_refs, function_return_type_ref(method),
                   function_returns, &symbols, options);
    } else {
        emit_block(out, method.statements, 2, aliases, locals, local_type_refs,
                   function_return_type_ref(method), function_returns, &symbols, options);
    }
    out << "    }\n";
}

void emit_class_constant_decl(std::ostringstream& out, const ConstDecl& constant,
                              const std::vector<std::string>& aliases,
                              const CppEmitOptions& options) {
    const std::string lowered_type = lower_cpp_type(constant.type_ref, aliases, options);
    const bool pointer = type_ref_contains_kind(constant.type_ref, TypeKind::Pointer);
    out << "    static ";
    out << (pointer ? lowered_type + " const " : "const " + lowered_type + " ");
    out << constant.name << ";\n";
}

void emit_class_constant_definition(std::ostringstream& out, const std::string& class_name,
                                    const ConstDecl& constant,
                                    const std::vector<std::string>& aliases,
                                    const CppEmitOptions& options) {
    const std::string lowered_type = lower_cpp_type(constant.type_ref, aliases, options);
    const bool pointer = type_ref_contains_kind(constant.type_ref, TypeKind::Pointer);
    const bool runtime_address =
        pointer || type_ref_contains_kind(constant.type_ref, TypeKind::Volatile);
    out << "inline ";
    if (runtime_address && pointer) {
        out << lowered_type << " const " << class_name << "::" << constant.name;
    } else {
        out << (runtime_address ? "const " : "constexpr ") << lowered_type << ' ' << class_name
            << "::" << constant.name;
    }
    out << " = " << lower_cpp_expr_ast(constant.value_expr, aliases) << ";\n";
}

} // namespace

void emit_classes(std::ostringstream& out, const ModuleAst& module,
                  const std::vector<std::string>& aliases,
                  const std::map<std::string, TypeRef>& function_returns, const Symbols& symbols,
                  bool header_only, const CppEmitOptions& options) {
    for (const size_t index : class_emit_order(module.classes)) {
        const ClassDecl& klass = module.classes[index];
        const std::string& class_name = emitted_name(klass, options);
        if (header_only && !visible_in_header(klass.visibility)) {
            continue;
        }
        emit_template_params(out, klass.generic_params, {}, generic_value_params_for_class(klass));
        out << class_opening(klass, aliases, options) << " {\n";
        for (const FieldDecl& field : klass.fields) {
            out << "    " << lower_cpp_type(field.type_ref, aliases, options) << ' ' << field.name;
            if (!has_expr(field.value_expr)) {
                out << "{}";
            } else {
                out << " = " << lower_cpp_expr_ast(field.value_expr, aliases);
            }
            out << ";\n";
        }
        for (const ConstDecl& field : klass.static_fields) {
            out << "    inline static " << lower_cpp_type(field.type_ref, aliases, options) << ' '
                << field.name << " = " << lower_cpp_expr_ast(field.value_expr, aliases) << ";\n";
        }
        for (const ConstDecl& constant : klass.constants) {
            emit_class_constant_decl(out, constant, aliases, options);
        }
        if (class_is_polymorphic(symbols, klass) && !has_drop_method(klass)) {
            out << "    virtual ~" << class_name << "() = default;\n";
        }
        std::string_view current_section = "public";
        for (const FunctionDecl& method : klass.methods) {
            const std::string_view method_section = class_section_for_method(method.visibility);
            if (method_section != current_section) {
                out << method_section << ":\n";
                current_section = method_section;
            }
            emit_method(out, class_name, klass.name, method, aliases, function_returns, symbols,
                        options);
        }
        out << "};\n\n";
        for (const ConstDecl& constant : klass.constants) {
            emit_class_constant_definition(out, class_name, constant, aliases, options);
        }
        if (!klass.constants.empty()) {
            out << '\n';
        }
    }
}

} // namespace dudu
