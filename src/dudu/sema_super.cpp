#include "dudu/sema_super.hpp"

#include "dudu/ast_expr.hpp"
#include "dudu/cpp_lower.hpp"
#include "dudu/sema_constructors.hpp"
#include "dudu/sema_inheritance.hpp"
#include "dudu/sema_methods.hpp"
#include "dudu/source.hpp"

namespace dudu {

namespace {

[[noreturn]] void fail(const SourceLocation& location, const std::string& message) {
    throw CompileError(location, message);
}

std::string super_base_type(const FunctionScope& scope, const SourceLocation* location) {
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
    if (klass->second->base_classes.empty()) {
        if (location != nullptr) {
            fail(*location, "super access requires a base class");
        }
        return {};
    }
    if (klass->second->base_classes.size() > 1) {
        if (location != nullptr) {
            fail(*location, "super access is ambiguous with multiple base classes");
        }
        return {};
    }
    return klass->second->base_classes.front();
}

std::string super_init_base_type(const FunctionScope& scope, const SourceLocation* location) {
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
    if (klass->second->base_classes.empty()) {
        if (location != nullptr) {
            fail(*location, "super access requires a base class");
        }
        return {};
    }
    if (klass->second->base_classes.size() == 1) {
        return klass->second->base_classes.front();
    }
    std::vector<std::string> storage_bases;
    for (const std::string& base : klass->second->base_classes) {
        if (class_type_has_instance_storage(scope.symbols, base)) {
            storage_bases.push_back(base);
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

std::string infer_super_call_ast(const FunctionScope& scope, const Expr& expr,
                                 const std::string& callee, const SourceLocation* location,
                                 const SuperCheckCallbacks& callbacks) {
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
        const std::string base = super_init_base_type(scope, location);
        if (base.empty()) {
            return {};
        }
        const auto base_class = scope.symbols.classes.find(base_type(base));
        if (base_class == scope.symbols.classes.end()) {
            if (location != nullptr) {
                fail(*location, "unknown base constructor type: " + base);
            }
            return {};
        }
        check_constructor_args_ast(
            scope, *base_class->second, expr.children, location, callbacks.infer_expr,
            [&](const std::string& expected, const Expr& value, const std::string& got) {
                return callbacks.can_assign(scope, expected, value, got);
            });
        return "void";
    }
    const std::string base = super_base_type(scope, location);
    if (base.empty()) {
        return {};
    }
    FunctionSignature signature;
    if (!method_signature_for_type(scope.symbols, base, method_name, signature, location)) {
        return {};
    }
    const std::vector<FunctionSignature> signatures =
        method_signatures_for_type(scope.symbols, base, method_name);
    if (const auto match = callbacks.matching_signature(scope, signatures, expr.children)) {
        callbacks.check_call_args(scope, callee, *match, expr.children, location);
        return match->return_type;
    }
    callbacks.check_call_args(scope, callee, signature, expr.children, location);
    return signature.return_type;
}

} // namespace dudu
