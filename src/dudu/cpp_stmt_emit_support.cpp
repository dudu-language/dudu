#include "dudu/cpp_stmt_emit_support.hpp"

#include "dudu/ast_type.hpp"
#include "dudu/cpp_expr_emit.hpp"
#include "dudu/cpp_lower.hpp"
#include "dudu/cpp_stmt_generic_methods.hpp"

#include <sstream>
#include <utility>

namespace dudu {

std::string join_names(const std::vector<std::string>& names) {
    std::ostringstream out;
    for (size_t i = 0; i < names.size(); ++i) {
        if (i > 0) {
            out << ", ";
        }
        out << names[i];
    }
    return out.str();
}

std::string_view compound_assign_op_text(CompoundAssignOp op) {
    switch (op) {
    case CompoundAssignOp::None:
        return "";
    case CompoundAssignOp::Add:
        return "+";
    case CompoundAssignOp::Sub:
        return "-";
    case CompoundAssignOp::Mul:
        return "*";
    case CompoundAssignOp::Div:
        return "/";
    case CompoundAssignOp::Mod:
        return "%";
    case CompoundAssignOp::BitAnd:
        return "&";
    case CompoundAssignOp::BitOr:
        return "|";
    case CompoundAssignOp::BitXor:
        return "^";
    case CompoundAssignOp::ShiftLeft:
        return "<<";
    case CompoundAssignOp::ShiftRight:
        return ">>";
    }
    return "";
}

EffectiveStmtType effective_stmt_type(const Stmt& stmt, const ArrayShapeInference& inferred) {
    if (inferred.status == ArrayShapeStatus::Inferred) {
        return {.ref = inferred.type_ref};
    }
    return {.ref = stmt.type_ref};
}

std::string lower_declared_stmt_type(const TypeRef& type, const std::vector<std::string>& aliases,
                                     const CppEmitOptions& options) {
    return lower_cpp_type(type, aliases, options);
}

std::string lower_emitted_expr(const Expr& expr, const std::vector<std::string>& aliases,
                               const CppLocalContext& locals,
                               const std::map<std::string, TypeRef>& local_type_refs,
                               const Symbols* symbols, const CppEmitOptions& options) {
    return lower_expr(expr, aliases, locals, local_type_refs, symbols, options);
}

std::string lower_expr_as_type_ref(const TypeRef& expected_type, const Expr& expr,
                                   const std::vector<std::string>& aliases,
                                   const CppLocalContext& locals,
                                   const std::map<std::string, TypeRef>& local_type_refs,
                                   const std::map<std::string, TypeRef>& function_returns,
                                   const Symbols* symbols, const CppEmitOptions& options) {
    if (is_fixed_array_type(expected_type) && expr.kind == ExprKind::ListLiteral) {
        return lower_fixed_array_literal_as_type_ref(expected_type, expr, aliases, locals,
                                                     local_type_refs, function_returns, symbols,
                                                     options);
    }
    if (const auto call = lower_expected_generic_method_call(expected_type, expr, aliases, locals,
                                                             local_type_refs, function_returns,
                                                             symbols, options)) {
        return *call;
    }
    return lower_emitted_expr(expr, aliases, locals, local_type_refs, symbols, options);
}

namespace {

TypeRef fixed_array_child_type_ref(const TypeRef& type) {
    const std::vector<std::string> shape = explicit_array_shape_text(type);
    const TypeRef element = explicit_array_element_type_ref(type);
    if (shape.size() <= 1 || !has_type_ref(element)) {
        return element;
    }
    TypeRef child;
    child.kind = TypeKind::FixedArray;
    for (size_t i = 1; i < shape.size(); ++i) {
        if (i > 1) {
            child.value += ", ";
        }
        child.value += shape[i];
    }
    TypeRef storage;
    storage.kind = TypeKind::Template;
    storage.name = "array";
    storage.children.push_back(element);
    child.children.push_back(std::move(storage));
    child.location = type.location;
    child.range = type.range;
    return child;
}

} // namespace

std::string lower_fixed_array_literal_as_type_ref(
    const TypeRef& expected_type, const Expr& expr, const std::vector<std::string>& aliases,
    const CppLocalContext& locals, const std::map<std::string, TypeRef>& local_type_refs,
    const std::map<std::string, TypeRef>& function_returns, const Symbols* symbols,
    const CppEmitOptions& options) {
    if (expr.kind != ExprKind::ListLiteral) {
        return lower_expr_as_type_ref(expected_type, expr, aliases, locals, local_type_refs,
                                      function_returns, symbols, options);
    }
    const TypeRef child_type = fixed_array_child_type_ref(expected_type);
    std::ostringstream out;
    out << lower_cpp_type(expected_type, aliases, options) << "{";
    for (size_t i = 0; i < expr.children.size(); ++i) {
        if (i > 0) {
            out << ", ";
        }
        out << lower_expr_as_type_ref(child_type, expr.children[i], aliases, locals,
                                      local_type_refs, function_returns, symbols, options);
    }
    out << "}";
    return out.str();
}

bool is_template_type(const TypeRef& type, std::string_view name) {
    return type.kind == TypeKind::Template && type.name == name;
}

TypeRef emitted_local_type_ref(const std::map<std::string, TypeRef>& local_type_refs,
                               std::string_view name, SourceLocation location) {
    const auto type_ref = local_type_refs.find(std::string(name));
    if (type_ref != local_type_refs.end()) {
        return type_ref->second;
    }
    TypeRef unknown;
    unknown.location = location;
    return unknown;
}

bool is_fixed_array_type(const TypeRef& type) {
    if (type.kind == TypeKind::Template && type.name == "array") {
        return true;
    }
    if (type.kind != TypeKind::FixedArray || type.children.empty()) {
        return false;
    }
    const TypeRef& storage = type.children.front();
    return storage.kind == TypeKind::Template && storage.name == "array";
}

} // namespace dudu
