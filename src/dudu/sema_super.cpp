#include "dudu/sema_super.hpp"

#include "dudu/ast_expr.hpp"
#include "dudu/ast_type.hpp"
#include "dudu/cpp_lower.hpp"
#include "dudu/sema_constructors.hpp"
#include "dudu/sema_expr_internal.hpp"
#include "dudu/sema_function_type.hpp"
#include "dudu/sema_inheritance.hpp"
#include "dudu/sema_methods.hpp"
#include "dudu/sema_methods_internal.hpp"
#include "dudu/source.hpp"

namespace dudu {

namespace {

[[noreturn]] void fail(const SourceLocation& location, const std::string& message) {
    throw CompileError(location, message);
}

TypeRef super_base_type(const FunctionScope& scope, const SourceLocation* location) {
    if (scope.current_class.empty()) {
        if (location != nullptr) {
            fail(*location, "super access outside class method");
        }
        return {};
    }
    const auto klass = scope.symbols.classes.find(scope.current_class);
    if (klass == scope.symbols.classes.end()) {
        return {};
    }
    if (klass->second->base_class_refs.empty()) {
        if (location != nullptr) {
            fail(*location, "super access requires a base class");
        }
        return {};
    }
    if (klass->second->base_class_refs.size() > 1) {
        if (location != nullptr) {
            fail(*location, "super access is ambiguous with multiple base classes");
        }
        return {};
    }
    return klass->second->base_class_refs.front().type_ref;
}

TypeRef super_init_base_type(const FunctionScope& scope, const SourceLocation* location) {
    if (scope.current_class.empty()) {
        if (location != nullptr) {
            fail(*location, "super access outside class method");
        }
        return {};
    }
    const auto klass = scope.symbols.classes.find(scope.current_class);
    if (klass == scope.symbols.classes.end()) {
        return {};
    }
    if (klass->second->base_class_refs.empty()) {
        if (location != nullptr) {
            fail(*location, "super access requires a base class");
        }
        return {};
    }
    if (klass->second->base_class_refs.size() == 1) {
        return klass->second->base_class_refs.front().type_ref;
    }
    std::vector<TypeRef> storage_bases;
    for (const BaseClassDecl& base_decl : klass->second->base_class_refs) {
        if (class_type_has_instance_storage(scope.symbols, base_decl.type_ref)) {
            storage_bases.push_back(base_decl.type_ref);
        }
    }
    if (storage_bases.size() == 1) {
        return storage_bases.front();
    }
    if (location != nullptr) {
        fail(*location, "super.init requires exactly one storage-bearing base class");
    }
    return {};
}

} // namespace

bool is_super_call(const std::string& callee) {
    return callee == "super" || starts_with(callee, "super.");
}

bool is_super_init_stmt(const Stmt& stmt) {
    return stmt.kind == StmtKind::Expr && is_member_callee(stmt.expr, "super", "init");
}

TypeRef infer_super_call_type_ref(const FunctionScope& scope, const Expr& expr,
                                  const std::string& callee, const SourceLocation* location) {
    const size_t dot = callee.find('.');
    if (dot == std::string::npos) {
        if (location != nullptr) {
            fail(*location, "super call requires a method name");
        }
        return {};
    }
    const std::string method_name = trim(callee.substr(dot + 1));
    if (method_name.empty()) {
        if (location != nullptr) {
            fail(*location, "super call requires a method name");
        }
        return {};
    }
    if (method_name == "init") {
        if (!scope.allow_super_init) {
            if (location != nullptr) {
                fail(*location, "super.init must be the first statement in init");
            }
            return {};
        }
        const TypeRef base = super_init_base_type(scope, location);
        if (!has_type_ref(base)) {
            return {};
        }
        const ClassDecl* base_class = class_for_receiver_type(scope.symbols, base);
        if (base_class == nullptr) {
            if (location != nullptr) {
                fail(*location,
                     "unknown base constructor type: " + substitute_type_ref_text(base, {}));
            }
            return {};
        }
        check_constructor_args_ast(scope, *base_class, expr.children, location);
        return void_type_ref(expr.location);
    }
    const TypeRef base = super_base_type(scope, location);
    if (!has_type_ref(base)) {
        return {};
    }
    FunctionSignature signature;
    if (!method_signature_for_type(scope.symbols, base, method_name, signature, location)) {
        return {};
    }
    const std::vector<FunctionSignature> signatures =
        method_signatures_for_type(scope.symbols, base, method_name);
    if (const auto match = matching_signature_ast(scope, signatures, expr.children)) {
        check_call_args_ast(scope, callee, *match, expr.children, location);
        return signature_return_type_ref(*match);
    }
    check_call_args_ast(scope, callee, signature, expr.children, location);
    return signature_return_type_ref(signature);
}

} // namespace dudu
