#include "dudu/codegen/cpp_runtime_features.hpp"

#include "dudu/core/ast_type.hpp"
#include "dudu/core/decorators.hpp"

namespace dudu {
namespace {

void inspect_type(const TypeRef& type, CppRuntimeFeatures& features) {
    const std::string name = type_ref_head_name(type);
    if (type.kind == TypeKind::FixedArray || name == "array") {
        features.fixed_array = true;
    } else if (type.kind == TypeKind::Atomic) {
        features.atomic = true;
    } else if (type.kind == TypeKind::Function) {
        features.function = true;
    }

    if (name == "str") {
        features.string = true;
    } else if (name == "list") {
        features.vector = true;
    } else if (name == "dict") {
        features.unordered_map = true;
    } else if (name == "set") {
        features.unordered_set = true;
    } else if (name == "Option") {
        features.optional = true;
    } else if (name == "Result") {
        features.result = true;
    } else if (name == "variant") {
        features.variant = true;
    } else if (name == "tuple") {
        features.tuples = true;
    } else if (name == "fn") {
        features.function = true;
    } else if (name == "span") {
        features.span = true;
    } else if (name == "array_view") {
        features.array_view = true;
        features.indexing = true;
        features.vector = true;
        features.fixed_array = true;
        features.algorithm = true;
        features.exceptions = true;
    } else if (name == "strided_span") {
        features.strided_span = true;
    } else if (name == "slice") {
        features.indexing = true;
    }
    for (const TypeRef& child : type.children) {
        inspect_type(child, features);
    }
}

void inspect_expr(const Expr& expr, CppRuntimeFeatures& features) {
    if (expr.type_ref) {
        inspect_type(*expr.type_ref, features);
    }
    switch (expr.kind) {
    case ExprKind::StringLiteral:
        features.string = true;
        break;
    case ExprKind::ListLiteral:
        features.vector = true;
        break;
    case ExprKind::DictLiteral:
        features.unordered_map = true;
        break;
    case ExprKind::SetLiteral:
        features.unordered_set = true;
        break;
    case ExprKind::TupleLiteral:
        features.tuples = true;
        break;
    case ExprKind::Slice:
    case ExprKind::Ellipsis:
    case ExprKind::NewAxis:
        features.indexing = true;
        break;
    default:
        break;
    }
    const std::string name = expr.name.str();
    if (name == "print") {
        features.hosted_print = true;
    } else if (name == "min" || name == "max") {
        features.min_max = true;
        features.algorithm = true;
    } else if (name == "str") {
        features.string = true;
        features.string_stream = true;
    } else if (name == "Ok" || name == "Err") {
        features.result = true;
    } else if (name == "malloc" || name == "free") {
        features.cstdlib = true;
    }
    if (expr.callee) {
        for (const Expr& child : *expr.callee) {
            inspect_expr(child, features);
        }
    }
    if (expr.template_args) {
        for (const Expr& child : *expr.template_args) {
            inspect_expr(child, features);
        }
    }
    if (expr.template_type_args) {
        for (const TypeRef& type : *expr.template_type_args) {
            inspect_type(type, features);
        }
    }
    for (const Expr& child : expr.children) {
        inspect_expr(child, features);
    }
}

void inspect_stmt(const Stmt& stmt, CppRuntimeFeatures& features) {
    if (stmt.type_ref) {
        inspect_type(*stmt.type_ref, features);
    }
    if (stmt.kind == StmtKind::Assert) {
        features.exceptions = true;
    } else if (stmt.kind == StmtKind::DebugAssert) {
        features.assertions = true;
    } else if (stmt.kind == StmtKind::Raise || stmt.kind == StmtKind::Try ||
               stmt.kind == StmtKind::Except) {
        features.exceptions = true;
    }
    inspect_expr(stmt.expr, features);
    inspect_expr(stmt.value_expr, features);
    if (stmt.target_expr) {
        inspect_expr(*stmt.target_expr, features);
    }
    if (stmt.condition_expr) {
        inspect_expr(*stmt.condition_expr, features);
    }
    if (stmt.message_expr) {
        inspect_expr(*stmt.message_expr, features);
    }
    if (stmt.iterable_expr) {
        inspect_expr(*stmt.iterable_expr, features);
    }
    if (stmt.pattern_expr) {
        inspect_expr(*stmt.pattern_expr, features);
    }
    if (stmt.guard_expr) {
        inspect_expr(*stmt.guard_expr, features);
    }
    for (const Stmt& child : stmt.children) {
        inspect_stmt(child, features);
    }
}

void inspect_function(const FunctionDecl& fn, CppRuntimeFeatures& features) {
    for (const ParamDecl& param : fn.params) {
        inspect_type(param.type_ref, features);
    }
    inspect_type(function_return_type_ref(fn), features);
    for (const Decorator& decorator : fn.decorators) {
        inspect_expr(decorator.expr, features);
    }
    if (has_decorator(fn, "shader.compute") || has_decorator(fn, "cuda.global") ||
        has_decorator(fn, "cuda.device") || has_decorator(fn, "cuda.host") ||
        has_decorator(fn, "workgroup_size")) {
        features.shader = true;
    }
    for (const Stmt& stmt : fn.statements) {
        inspect_stmt(stmt, features);
    }
}

void inspect_module(const ModuleAst& module, CppRuntimeFeatures& features) {
    for (const TypeAliasDecl& alias : module.aliases) {
        inspect_type(alias.type_ref, features);
    }
    for (const EnumDecl& en : module.enums) {
        inspect_type(en.underlying_type_ref, features);
        for (const EnumValueDecl& value : en.values) {
            inspect_expr(value.value_expr, features);
            if (!value.payload_fields.empty()) {
                features.variant = true;
            }
            for (const EnumPayloadField& field : value.payload_fields) {
                inspect_type(field.type_ref, features);
            }
        }
        for (const FunctionDecl& method : en.methods) {
            inspect_function(method, features);
        }
    }
    for (const ClassDecl& klass : module.classes) {
        for (const BaseClassDecl& base : klass.base_class_refs) {
            inspect_type(base.type_ref, features);
        }
        for (const FieldDecl& field : klass.fields) {
            inspect_type(field.type_ref, features);
            inspect_expr(field.value_expr, features);
        }
        for (const ConstDecl& constant : klass.constants) {
            inspect_type(constant.type_ref, features);
            inspect_expr(constant.value_expr, features);
        }
        for (const ConstDecl& field : klass.static_fields) {
            inspect_type(field.type_ref, features);
            inspect_expr(field.value_expr, features);
        }
        for (const FunctionDecl& method : klass.methods) {
            inspect_function(method, features);
        }
    }
    for (const ConstDecl& constant : module.constants) {
        inspect_type(constant.type_ref, features);
        inspect_expr(constant.value_expr, features);
    }
    for (const FunctionDecl& fn : module.functions) {
        inspect_function(fn, features);
    }
    for (const StaticAssertDecl& assertion : module.static_asserts) {
        inspect_expr(assertion.expression_expr, features);
    }
}

} // namespace

CppRuntimeFeatures cpp_runtime_features(const ModuleAst& module) {
    CppRuntimeFeatures features;
    inspect_module(module, features);
    for (const ModuleAst& unit : module.module_units) {
        inspect_module(unit, features);
    }
    if (features.indexing) {
        features.algorithm = features.algorithm || features.array_view;
    }
    for (const auto& [name, value] : module.build_values) {
        (void)name;
        if (!value.empty() && value.front() == '"') {
            features.string_view = true;
        }
    }
    return features;
}

} // namespace dudu
