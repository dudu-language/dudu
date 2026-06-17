#include "dudu/cpp_stmt_emit.hpp"

#include "dudu/array_shape.hpp"
#include "dudu/ast_expr.hpp"
#include "dudu/ast_type.hpp"
#include "dudu/control_flow.hpp"
#include "dudu/cpp_expr_call_emit.hpp"
#include "dudu/cpp_expr_emit.hpp"
#include "dudu/cpp_expr_swizzles.hpp"
#include "dudu/cpp_lower.hpp"
#include "dudu/cpp_match_emit.hpp"
#include "dudu/cpp_pointer_members.hpp"
#include "dudu/cpp_stmt_helpers.hpp"
#include "dudu/cpp_stmt_types.hpp"
#include "dudu/sema_context.hpp"
#include "dudu/sema_enum.hpp"
#include "dudu/sema_function_type.hpp"
#include "dudu/sema_generics.hpp"
#include "dudu/sema_methods.hpp"
#include "dudu/sema_methods_internal.hpp"
#include "dudu/source.hpp"

#include <algorithm>
#include <cctype>
#include <map>
#include <optional>
#include <sstream>
#include <string_view>
#include <utility>
#include <vector>

namespace dudu {
namespace {

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

std::string lower_declared_stmt_type(const Stmt& stmt, const std::string& effective_type,
                                     const std::vector<std::string>& aliases,
                                     const CppEmitOptions& options) {
    return effective_type == stmt.type ? lower_cpp_type(stmt.type_ref, aliases, options)
                                       : lower_cpp_type(effective_type, aliases, options);
}

std::optional<std::vector<TypeRef>> infer_expected_method_type_args(
    const Symbols& symbols, const std::string& receiver_type, const std::string& method_name,
    const std::vector<std::string>& arg_types, const std::string& expected_type) {
    const std::string type = unwrap_receiver_type(symbols, receiver_type);
    const auto klass = symbols.classes.find(type);
    if (klass == symbols.classes.end()) {
        return std::nullopt;
    }
    for (const FunctionDecl& method : klass->second->methods) {
        if (method.name != method_name || method.generic_params.empty()) {
            continue;
        }
        const size_t first_param =
            !method.params.empty() && method.params.front().name == "self" ? 1 : 0;
        return infer_generic_method_type_args_from_types(
            method, type + "." + method_name, arg_types, first_param, expected_type, nullptr);
    }
    for (const std::string& base : klass->second->base_classes) {
        if (const auto inferred = infer_expected_method_type_args(symbols, base, method_name,
                                                                  arg_types, expected_type)) {
            return inferred;
        }
    }
    return std::nullopt;
}

std::optional<std::string>
lower_expected_generic_method_call(const std::string& expected_type, const Expr& expr,
                                   const std::vector<std::string>& aliases,
                                   const std::map<std::string, std::string>& locals,
                                   const std::map<std::string, std::string>& function_returns,
                                   const Symbols* symbols, const CppEmitOptions& options) {
    if (symbols == nullptr || expected_type.empty() || expr.kind != ExprKind::Call ||
        expr.callee.empty() || expr.callee.front().kind != ExprKind::Member ||
        expr.callee.front().children.size() != 1) {
        return std::nullopt;
    }
    const Expr& member = expr.callee.front();
    const Expr& receiver = member.children.front();
    std::string receiver_type = infer_emitted_local_type(receiver, locals, function_returns);
    if (receiver_type.empty()) {
        receiver_type = member_expr_type(*symbols, locals, nullptr, receiver);
    }
    if (receiver_type.empty() || receiver_type == "auto") {
        return std::nullopt;
    }
    std::vector<std::string> arg_types;
    arg_types.reserve(expr.children.size());
    for (const Expr& arg : expr.children) {
        const std::string arg_type = infer_emitted_local_type(arg, locals, function_returns);
        if (arg_type.empty() || arg_type == "auto") {
            return std::nullopt;
        }
        arg_types.push_back(arg_type);
    }
    const auto type_args = infer_expected_method_type_args(*symbols, receiver_type, member.name,
                                                           arg_types, expected_type);
    if (!type_args) {
        return std::nullopt;
    }
    std::ostringstream lowered_args;
    for (size_t i = 0; i < type_args->size(); ++i) {
        if (i > 0) {
            lowered_args << ", ";
        }
        lowered_args << lower_cpp_type((*type_args)[i], aliases, options);
    }
    return lower_callee_expr(expr, aliases, locals, symbols, options) + "<" + lowered_args.str() +
           ">(" + join_lowered_exprs(expr.children, aliases, locals, ", ", symbols, options) + ")";
}

std::string lower_expr_as_type(const std::string& expected_type, const Expr& expr,
                               const std::vector<std::string>& aliases,
                               const std::map<std::string, std::string>& locals,
                               const std::map<std::string, std::string>& function_returns,
                               const Symbols* symbols, const CppEmitOptions& options) {
    if (const auto call = lower_expected_generic_method_call(expected_type, expr, aliases, locals,
                                                             function_returns, symbols, options)) {
        return *call;
    }
    return lower_expr(expr, aliases, locals, symbols, options);
}

bool is_template_type(std::string_view type, std::string_view name) {
    const TypeRef parsed = parse_type_text(type);
    return parsed.kind == TypeKind::Template && parsed.name == name;
}

bool is_fixed_array_type(std::string_view type) {
    const TypeRef parsed = parse_type_text(type);
    if (parsed.kind == TypeKind::Template && parsed.name == "array") {
        return true;
    }
    if (parsed.kind != TypeKind::FixedArray || parsed.children.empty()) {
        return false;
    }
    const TypeRef& storage = parsed.children.front();
    return storage.kind == TypeKind::Template && storage.name == "array";
}

void emit_cpp_escape(std::ostringstream& out, const std::string& body_text, int depth) {
    std::istringstream body(body_text);
    std::string line;
    while (std::getline(body, line)) {
        if (!trim_copy(line).empty()) {
            out << indent(depth) << trim_copy(line) << '\n';
        }
    }
}

void emit_source_comment(std::ostringstream& out, const Stmt& stmt, int depth) {
    out << indent(depth) << "// dudu: " << format_location(stmt.location) << '\n';
}

void emit_simple_statement(std::ostringstream& out, const Stmt& stmt, int depth,
                           const std::vector<std::string>& aliases,
                           std::map<std::string, std::string>& locals,
                           const std::string& return_type,
                           const std::map<std::string, std::string>& function_returns,
                           const Symbols* symbols, const CppEmitOptions& options) {
    if (stmt.kind == StmtKind::CppEscape) {
        emit_cpp_escape(out, stmt.value, depth);
        return;
    }
    const std::string text = trim_copy(stmt.text);
    if (stmt.kind == StmtKind::Pass) {
        out << indent(depth) << "(void)0;\n";
        return;
    }
    if (stmt.kind == StmtKind::Break) {
        out << indent(depth) << "break;\n";
        return;
    }
    if (stmt.kind == StmtKind::Continue) {
        out << indent(depth) << "continue;\n";
        return;
    }
    if (stmt.kind == StmtKind::Raise) {
        out << indent(depth) << "throw";
        if (has_expr(stmt.value_expr)) {
            out << ' ' << lower_expr(stmt.value_expr, aliases, locals, symbols, options);
        }
        out << ";\n";
        return;
    }
    if (stmt.kind == StmtKind::Delete) {
        out << indent(depth) << "delete "
            << lower_expr(stmt.value_expr, aliases, locals, symbols, options) << ";\n";
        return;
    }
    if (stmt.kind == StmtKind::Assert) {
        out << indent(depth) << "if (!("
            << lower_expr(stmt.condition_expr, aliases, locals, symbols, options)
            << ")) { throw std::runtime_error(";
        if (has_expr(stmt.message_expr))
            out << lower_expr(stmt.message_expr, aliases, locals, symbols, options);
        else
            out << cpp_string_literal("assert failed: " + stmt.condition_expr.text);
        out << "); }\n";
        return;
    }
    if (stmt.kind == StmtKind::DebugAssert) {
        out << indent(depth) << "assert(("
            << lower_expr(stmt.condition_expr, aliases, locals, symbols, options) << ")";
        if (has_expr(stmt.message_expr))
            out << " && (" << lower_expr(stmt.message_expr, aliases, locals, symbols, options)
                << ")";
        out << ");\n";
        return;
    }
    if (stmt.kind == StmtKind::Return) {
        out << indent(depth) << "return";
        if (has_expr(stmt.value_expr)) {
            if (is_template_type(return_type, "Option") &&
                stmt.value_expr.kind == ExprKind::NoneLiteral) {
                out << " std::nullopt";
            } else if (stmt.value_expr.kind == ExprKind::TupleLiteral) {
                out << " {" << lower_expr(stmt.value_expr, aliases, locals, symbols, options)
                    << '}';
            } else {
                out << ' '
                    << lower_expr_as_type(return_type, stmt.value_expr, aliases, locals,
                                          function_returns, symbols, options);
            }
        }
        out << ";\n";
        return;
    }
    if (stmt.kind == StmtKind::VarDecl) {
        const std::string& name = stmt.name;
        const ArrayShapeInference inferred =
            infer_array_literal_shape_type(stmt.type_ref, stmt.value_expr);
        const std::string type =
            inferred.status == ArrayShapeStatus::Inferred ? inferred.type : stmt.type;
        locals[name] = type;
        out << indent(depth) << lower_declared_stmt_type(stmt, type, aliases, options) << ' '
            << name;
        if (has_expr(stmt.value_expr)) {
            if (is_template_type(type, "Option") && stmt.value_expr.kind == ExprKind::NoneLiteral) {
                out << " = std::nullopt";
            } else if (is_fixed_array_type(type) && stmt.value_expr.kind == ExprKind::ListLiteral) {
                out << " = {"
                    << lower_array_literal(stmt.value_expr, aliases, locals, symbols, options)
                    << "}";
            } else if (is_template_type(type, "list") &&
                       stmt.value_expr.kind == ExprKind::ListLiteral &&
                       stmt.value_expr.children.empty()) {
                out << " = {}";
            } else if (is_template_type(type, "list") &&
                       stmt.value_expr.kind == ExprKind::ListLiteral) {
                out << " = {"
                    << join_lowered_exprs(stmt.value_expr.children, aliases, locals, ", ", symbols,
                                          options)
                    << "}";
            } else if (is_template_type(type, "dict") &&
                       stmt.value_expr.kind == ExprKind::DictLiteral) {
                out << " = {"
                    << join_lowered_exprs(stmt.value_expr.children, aliases, locals, ", ", symbols,
                                          options)
                    << "}";
            } else if (is_template_type(type, "set") &&
                       stmt.value_expr.kind == ExprKind::SetLiteral) {
                out << " = {"
                    << join_lowered_exprs(stmt.value_expr.children, aliases, locals, ", ", symbols,
                                          options)
                    << "}";
            } else if (stmt.value_expr.kind == ExprKind::TupleLiteral) {
                out << " = " << lower_declared_stmt_type(stmt, type, aliases, options) << "{"
                    << join_lowered_exprs(stmt.value_expr.children, aliases, locals, ", ", symbols,
                                          options)
                    << '}';
            } else {
                out << " = "
                    << lower_expr_as_type(type, stmt.value_expr, aliases, locals, function_returns,
                                          symbols, options);
            }
        } else {
            out << "{}";
        }
        out << ";\n";
        return;
    }
    if (stmt.kind == StmtKind::Assign) {
        if (const std::vector<std::string> names = tuple_binding_names(stmt.target_expr);
            !names.empty()) {
            out << indent(depth) << "auto [" << join_names(names)
                << "] = " << lower_expr(stmt.value_expr, aliases, locals, symbols, options)
                << ";\n";
            return;
        }
        if (stmt.target_expr.kind == ExprKind::Name && !stmt.target_expr.name.empty()) {
            const std::string& lhs = stmt.target_expr.name;
            if (locals.contains(lhs)) {
                const std::string value =
                    lower_expr_as_type(locals.at(lhs), stmt.value_expr, aliases, locals,
                                       function_returns, symbols, options);
                out << indent(depth) << lhs << " = ";
                if (is_template_type(locals.at(lhs), "Option") &&
                    stmt.value_expr.kind == ExprKind::NoneLiteral) {
                    out << "std::nullopt";
                } else {
                    out << value;
                }
                out << ";\n";
            } else {
                const std::string value =
                    lower_expr(stmt.value_expr, aliases, locals, symbols, options);
                const std::string inferred =
                    infer_emitted_local_type(stmt.value_expr, locals, function_returns);
                locals.emplace(lhs, inferred.empty() ? "auto" : inferred);
                out << indent(depth) << "auto " << lhs << " = " << value << ";\n";
            }
            return;
        }
        if (stmt.target_expr.kind != ExprKind::Unknown) {
            if (const auto swizzle =
                    lower_swizzle_assignment(stmt, aliases, locals, symbols, options)) {
                out << indent(depth) << *swizzle << ";\n";
                return;
            }
            if (const auto call =
                    lower_index_assignment_hook(stmt, aliases, locals, symbols, options)) {
                out << indent(depth) << *call << ";\n";
                return;
            }
            const std::string target_type =
                symbols == nullptr ? std::string{}
                                   : member_expr_type(*symbols, locals, nullptr, stmt.target_expr);
            out << indent(depth) << lower_expr(stmt.target_expr, aliases, locals, symbols, options)
                << " = "
                << lower_expr_as_type(target_type, stmt.value_expr, aliases, locals,
                                      function_returns, symbols, options)
                << ";\n";
            return;
        }
    }
    if (stmt.kind == StmtKind::CompoundAssign) {
        out << indent(depth) << lower_expr(stmt.target_expr, aliases, locals, symbols, options)
            << ' ' << stmt.op << '=' << " "
            << lower_expr(stmt.value_expr, aliases, locals, symbols, options) << ";\n";
        return;
    }
    if (stmt.kind == StmtKind::Expr) {
        out << indent(depth) << lower_expr(stmt.expr, aliases, locals, symbols, options) << ";\n";
        return;
    }
    if (stmt.kind == StmtKind::Unknown) {
        throw CompileError(stmt.location, "unsupported statement: " + trim_copy(stmt.text));
    }
}

void emit_statement(std::ostringstream& out, const Stmt& stmt, int depth,
                    const std::vector<std::string>& aliases,
                    std::map<std::string, std::string>& locals, const std::string& return_type,
                    const std::map<std::string, std::string>& function_returns,
                    const Symbols* symbols, const CppEmitOptions& options) {
    emit_source_comment(out, stmt, depth);
    if (stmt.kind == StmtKind::If) {
        out << indent(depth) << if_keyword_for_condition(stmt.condition_expr) << " ("
            << lower_expr(stmt.condition_expr, aliases, locals, symbols, options) << ") {\n";
        emit_block(out, stmt.children, depth + 1, aliases, locals, return_type, function_returns,
                   symbols, options);
        out << indent(depth) << "}\n";
        return;
    }
    if (stmt.kind == StmtKind::Elif) {
        out << indent(depth) << "else " << if_keyword_for_condition(stmt.condition_expr) << " ("
            << lower_expr(stmt.condition_expr, aliases, locals, symbols, options) << ") {\n";
        emit_block(out, stmt.children, depth + 1, aliases, locals, return_type, function_returns,
                   symbols, options);
        out << indent(depth) << "}\n";
        return;
    }
    if (stmt.kind == StmtKind::Else) {
        out << indent(depth) << "else {\n";
        emit_block(out, stmt.children, depth + 1, aliases, locals, return_type, function_returns,
                   symbols, options);
        out << indent(depth) << "}\n";
        return;
    }
    if (stmt.kind == StmtKind::Match) {
        emit_match_statement(out, stmt, depth, aliases, locals, return_type, function_returns,
                             symbols, options);
        return;
    }
    if (stmt.kind == StmtKind::Try) {
        out << indent(depth) << "try {\n";
        emit_block(out, stmt.children, depth + 1, aliases, locals, return_type, function_returns,
                   symbols, options);
        out << indent(depth) << "}\n";
        return;
    }
    if (stmt.kind == StmtKind::Except) {
        out << indent(depth);
        if (stmt.name.empty() || stmt.type.empty()) {
            out << "catch (...)";
        } else {
            out << "catch (const " << lower_cpp_type(stmt.type_ref, aliases, options) << "& "
                << stmt.name << ")";
        }
        out << " {\n";
        emit_block(out, stmt.children, depth + 1, aliases, locals, return_type, function_returns,
                   symbols, options);
        out << indent(depth) << "}\n";
        return;
    }
    if (stmt.kind == StmtKind::While) {
        out << indent(depth) << "while ("
            << lower_expr(stmt.condition_expr, aliases, locals, symbols, options) << ") {\n";
        emit_block(out, stmt.children, depth + 1, aliases, locals, return_type, function_returns,
                   symbols, options);
        out << indent(depth) << "}\n";
        return;
    }
    if (stmt.kind == StmtKind::For && has_expr(stmt.iterable_expr)) {
        std::string binding = stmt.name;
        const std::string range = lower_expr(stmt.iterable_expr, aliases, locals, symbols, options);
        std::string binding_type = "auto";
        if (!stmt.type.empty()) {
            binding_type = lower_cpp_type(stmt.type_ref, aliases, options);
            locals[stmt.name] = stmt.type;
        }
        if (stmt.iterable_expr.kind == ExprKind::Call && stmt.iterable_expr.name == "range") {
            const std::vector<Expr>& args = stmt.iterable_expr.children;
            const std::string start =
                args.size() == 1 ? "0" : lower_expr(args.at(0), aliases, locals, symbols, options);
            const std::string end = args.size() == 1
                                        ? lower_expr(args.at(0), aliases, locals, symbols, options)
                                        : lower_expr(args.at(1), aliases, locals, symbols, options);
            const std::string step =
                args.size() >= 3 ? lower_expr(args.at(2), aliases, locals, symbols, options) : "1";
            out << indent(depth) << "for (" << binding_type << ' ' << binding << " = " << start
                << "; " << binding << " < " << end << "; " << binding << " += " << step << ") {\n";
            emit_block(out, stmt.children, depth + 1, aliases, locals, return_type,
                       function_returns, symbols, options);
            out << indent(depth) << "}\n";
            return;
        }
        const std::string loop_type = stmt.type.empty() ? "auto&&" : binding_type;
        out << indent(depth) << "for (" << loop_type << ' ' << binding << " : " << range << ") {\n";
        emit_block(out, stmt.children, depth + 1, aliases, locals, return_type, function_returns,
                   symbols, options);
        out << indent(depth) << "}\n";
        return;
    }
    emit_simple_statement(out, stmt, depth, aliases, locals, return_type, function_returns, symbols,
                          options);
}

} // namespace

void emit_block(std::ostringstream& out, const std::vector<Stmt>& body, int depth,
                const std::vector<std::string>& aliases) {
    emit_block(out, body, depth, aliases, {});
}

void emit_block(std::ostringstream& out, const std::vector<Stmt>& body, int depth,
                const std::vector<std::string>& aliases,
                const std::map<std::string, std::string>& initial_locals,
                const std::string& return_type,
                const std::map<std::string, std::string>& function_returns,
                const Symbols* symbols) {
    emit_block(out, body, depth, aliases, initial_locals, return_type, function_returns, symbols,
               {});
}

void emit_block(std::ostringstream& out, const std::vector<Stmt>& body, int depth,
                const std::vector<std::string>& aliases,
                const std::map<std::string, std::string>& initial_locals,
                const std::string& return_type,
                const std::map<std::string, std::string>& function_returns, const Symbols* symbols,
                const CppEmitOptions& options) {
    std::map<std::string, std::string> locals = initial_locals;
    for (const Stmt& stmt : body) {
        emit_statement(out, stmt, depth, aliases, locals, return_type, function_returns, symbols,
                       options);
    }
}

} // namespace dudu
