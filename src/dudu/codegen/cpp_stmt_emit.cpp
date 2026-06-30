#include "dudu/codegen/cpp_stmt_emit.hpp"

#include "dudu/codegen/cpp_expr_call_emit.hpp"
#include "dudu/codegen/cpp_expr_emit.hpp"
#include "dudu/codegen/cpp_expr_swizzles.hpp"
#include "dudu/codegen/cpp_lower.hpp"
#include "dudu/codegen/cpp_match_emit.hpp"
#include "dudu/codegen/cpp_pointer_members.hpp"
#include "dudu/codegen/cpp_stmt_emit_support.hpp"
#include "dudu/codegen/cpp_stmt_generic_methods.hpp"
#include "dudu/codegen/cpp_stmt_helpers.hpp"
#include "dudu/codegen/cpp_stmt_types.hpp"
#include "dudu/core/array_shape.hpp"
#include "dudu/core/ast_expr.hpp"
#include "dudu/core/ast_type.hpp"
#include "dudu/core/control_flow.hpp"
#include "dudu/core/source.hpp"
#include "dudu/sema/sema_common.hpp"
#include "dudu/sema/sema_context.hpp"
#include "dudu/sema/sema_enum.hpp"
#include "dudu/sema/sema_function_type.hpp"
#include "dudu/sema/sema_methods.hpp"

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

void emit_cpp_escape(std::ostringstream& out, const Stmt& stmt, int depth,
                     const std::map<std::string, TypeRef>& local_type_refs) {
    for (const std::string& line : stmt.cpp_lines) {
        out << indent(depth) << rewrite_pointer_members(line, local_type_refs) << '\n';
    }
}

void emit_source_comment(std::ostringstream& out, const Stmt& stmt, int depth) {
    out << indent(depth) << "// dudu: " << format_location(stmt.location) << '\n';
}

void emit_simple_statement(std::ostringstream& out, const Stmt& stmt, int depth,
                           const std::vector<std::string>& aliases, CppLocalContext& locals,
                           std::map<std::string, TypeRef>& local_type_refs,
                           const TypeRef& return_type_ref,
                           const std::map<std::string, TypeRef>& function_returns,
                           const Symbols* symbols, const CppEmitOptions& options) {
    if (stmt.kind == StmtKind::CppEscape) {
        emit_cpp_escape(out, stmt, depth, local_type_refs);
        return;
    }
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
            out << ' '
                << lower_emitted_expr(stmt.value_expr, aliases, locals, local_type_refs, symbols,
                                      options);
        }
        out << ";\n";
        return;
    }
    if (stmt.kind == StmtKind::Delete) {
        out << indent(depth) << "delete "
            << lower_emitted_expr(stmt.value_expr, aliases, locals, local_type_refs, symbols,
                                  options)
            << ";\n";
        return;
    }
    if (stmt.kind == StmtKind::Assert) {
        out << indent(depth) << "if (!("
            << lower_emitted_expr(stmt_condition_expr(stmt), aliases, locals, local_type_refs,
                                  symbols, options)
            << ")) { throw std::runtime_error(";
        if (has_stmt_message_expr(stmt))
            out << lower_emitted_expr(stmt_message_expr(stmt), aliases, locals, local_type_refs,
                                      symbols, options);
        else
            out << cpp_string_literal("assert failed: " + display_expr(stmt_condition_expr(stmt)));
        out << "); }\n";
        return;
    }
    if (stmt.kind == StmtKind::DebugAssert) {
        out << indent(depth) << "assert(("
            << lower_emitted_expr(stmt_condition_expr(stmt), aliases, locals, local_type_refs,
                                  symbols, options)
            << ")";
        if (has_stmt_message_expr(stmt))
            out << " && ("
                << lower_emitted_expr(stmt_message_expr(stmt), aliases, locals, local_type_refs,
                                      symbols, options)
                << ")";
        out << ");\n";
        return;
    }
    if (stmt.kind == StmtKind::Return) {
        out << indent(depth) << "return";
        if (has_expr(stmt.value_expr)) {
            if (is_template_type(return_type_ref, "Option") &&
                stmt.value_expr.kind == ExprKind::NoneLiteral) {
                out << " std::nullopt";
            } else if (stmt.value_expr.kind == ExprKind::TupleLiteral) {
                out << " {"
                    << lower_emitted_expr(stmt.value_expr, aliases, locals, local_type_refs,
                                          symbols, options)
                    << '}';
            } else {
                out << ' '
                    << lower_expr_as_type_ref(return_type_ref, stmt.value_expr, aliases, locals,
                                              local_type_refs, function_returns, symbols, options);
            }
        }
        out << ";\n";
        return;
    }
    if (stmt.kind == StmtKind::VarDecl) {
        const std::string& name = stmt.name;
        const TypeRef& declared_type = stmt_type_ref(stmt);
        const ArrayShapeInference inferred =
            infer_array_literal_shape_type(declared_type, stmt.value_expr);
        const EffectiveStmtType type = effective_stmt_type(stmt, inferred);
        locals.bind(name);
        local_type_refs[name] = type.ref;
        out << indent(depth) << lower_declared_stmt_type(type.ref, aliases, options) << ' ' << name;
        if (has_expr(stmt.value_expr)) {
            if (is_template_type(type.ref, "Option") &&
                stmt.value_expr.kind == ExprKind::NoneLiteral) {
                out << " = std::nullopt";
            } else if (is_fixed_array_type(type.ref) &&
                       stmt.value_expr.kind == ExprKind::ListLiteral) {
                out << " = "
                    << lower_fixed_array_literal_as_type_ref(type.ref, stmt.value_expr, aliases,
                                                             locals, local_type_refs,
                                                             function_returns, symbols, options);
            } else if (is_template_type(type.ref, "list") &&
                       stmt.value_expr.kind == ExprKind::ListLiteral &&
                       stmt.value_expr.children.empty()) {
                out << " = {}";
            } else if (is_template_type(type.ref, "list") &&
                       stmt.value_expr.kind == ExprKind::ListLiteral) {
                out << " = {"
                    << join_lowered_exprs(stmt.value_expr.children, aliases, locals,
                                          local_type_refs, ", ", symbols, options)
                    << "}";
            } else if (is_template_type(type.ref, "dict") &&
                       stmt.value_expr.kind == ExprKind::DictLiteral) {
                out << " = {"
                    << join_lowered_exprs(stmt.value_expr.children, aliases, locals,
                                          local_type_refs, ", ", symbols, options)
                    << "}";
            } else if (is_template_type(type.ref, "set") &&
                       stmt.value_expr.kind == ExprKind::SetLiteral) {
                out << " = {"
                    << join_lowered_exprs(stmt.value_expr.children, aliases, locals,
                                          local_type_refs, ", ", symbols, options)
                    << "}";
            } else if (stmt.value_expr.kind == ExprKind::TupleLiteral) {
                out << " = " << lower_declared_stmt_type(type.ref, aliases, options) << "{"
                    << join_lowered_exprs(stmt.value_expr.children, aliases, locals,
                                          local_type_refs, ", ", symbols, options)
                    << '}';
            } else {
                out << " = "
                    << lower_expr_as_type_ref(type.ref, stmt.value_expr, aliases, locals,
                                              local_type_refs, function_returns, symbols, options);
            }
        } else {
            out << "{}";
        }
        out << ";\n";
        return;
    }
    if (stmt.kind == StmtKind::Assign) {
        if (const std::vector<std::string> names = tuple_binding_names(stmt_target_expr(stmt));
            !names.empty()) {
            out << indent(depth) << "auto [" << join_names(names) << "] = "
                << lower_emitted_expr(stmt.value_expr, aliases, locals, local_type_refs, symbols,
                                      options)
                << ";\n";
            return;
        }
        if (stmt_target_expr(stmt).kind == ExprKind::Name && !stmt_target_expr(stmt).name.empty()) {
            const std::string& lhs = stmt_target_expr(stmt).name;
            if (local_type_refs.contains(lhs)) {
                const TypeRef lhs_type =
                    emitted_local_type_ref(local_type_refs, lhs, stmt_target_expr(stmt).location);
                const bool option_target = is_template_type(lhs_type, "Option");
                const std::string value =
                    lower_expr_as_type_ref(lhs_type, stmt.value_expr, aliases, locals,
                                           local_type_refs, function_returns, symbols, options);
                out << indent(depth) << lhs << " = ";
                if (option_target && stmt.value_expr.kind == ExprKind::NoneLiteral) {
                    out << "std::nullopt";
                } else {
                    out << value;
                }
                out << ";\n";
            } else {
                const std::string value = lower_emitted_expr(stmt.value_expr, aliases, locals,
                                                             local_type_refs, symbols, options);
                const TypeRef inferred_ref = infer_emitted_local_type_ref(
                    stmt.value_expr, local_type_refs, function_returns, symbols);
                locals.bind(lhs);
                if (has_type_ref(inferred_ref)) {
                    local_type_refs.emplace(lhs, inferred_ref);
                } else {
                    local_type_refs.emplace(
                        lhs, named_type_ref("auto", stmt_target_expr(stmt).location));
                }
                out << indent(depth) << "auto " << lhs << " = " << value << ";\n";
            }
            return;
        }
        if (has_stmt_target_expr(stmt)) {
            if (const auto swizzle = lower_swizzle_assignment(stmt, aliases, locals,
                                                              local_type_refs, symbols, options)) {
                out << indent(depth) << *swizzle << ";\n";
                return;
            }
            if (const auto call = lower_index_assignment_hook(stmt, aliases, locals,
                                                              local_type_refs, symbols, options)) {
                out << indent(depth) << *call << ";\n";
                return;
            }
            const TypeRef target_type = symbols == nullptr
                                            ? TypeRef{}
                                            : member_expr_type_ref(*symbols, local_type_refs,
                                                                   nullptr, stmt_target_expr(stmt));
            const std::string value =
                has_type_ref(target_type)
                    ? lower_expr_as_type_ref(target_type, stmt.value_expr, aliases, locals,
                                             local_type_refs, function_returns, symbols, options)
                    : lower_emitted_expr(stmt.value_expr, aliases, locals, local_type_refs, symbols,
                                         options);
            out << indent(depth)
                << lower_emitted_expr(stmt_target_expr(stmt), aliases, locals, local_type_refs,
                                      symbols, options)
                << " = " << value << ";\n";
            return;
        }
    }
    if (stmt.kind == StmtKind::CompoundAssign) {
        if (const auto call = lower_compound_index_assignment_hook(
                stmt, aliases, locals, local_type_refs, symbols, options)) {
            out << indent(depth) << *call << ";\n";
            return;
        }
        out << indent(depth)
            << lower_emitted_expr(stmt_target_expr(stmt), aliases, locals, local_type_refs, symbols,
                                  options)
            << ' ' << compound_assign_op_text(stmt.compound_op) << '=' << " "
            << lower_emitted_expr(stmt.value_expr, aliases, locals, local_type_refs, symbols,
                                  options)
            << ";\n";
        return;
    }
    if (stmt.kind == StmtKind::Expr) {
        out << indent(depth)
            << lower_emitted_expr(stmt.expr, aliases, locals, local_type_refs, symbols, options)
            << ";\n";
        return;
    }
    if (stmt.kind == StmtKind::Unknown) {
        throw CompileError(stmt.location, "unsupported statement kind: " +
                                              std::string(statement_kind_name(stmt.kind)));
    }
}

void emit_statement(std::ostringstream& out, const Stmt& stmt, int depth,
                    const std::vector<std::string>& aliases, CppLocalContext& locals,
                    std::map<std::string, TypeRef>& local_type_refs, const TypeRef& return_type_ref,
                    const std::map<std::string, TypeRef>& function_returns, const Symbols* symbols,
                    const CppEmitOptions& options) {
    emit_source_comment(out, stmt, depth);
    if (stmt.kind == StmtKind::If) {
        out << indent(depth) << if_keyword_for_condition(stmt_condition_expr(stmt)) << " ("
            << lower_emitted_expr(stmt_condition_expr(stmt), aliases, locals, local_type_refs,
                                  symbols, options)
            << ") {\n";
        emit_block(out, stmt.children, depth + 1, aliases, locals, local_type_refs, return_type_ref,
                   function_returns, symbols, options);
        out << indent(depth) << "}\n";
        return;
    }
    if (stmt.kind == StmtKind::Elif) {
        out << indent(depth) << "else " << if_keyword_for_condition(stmt_condition_expr(stmt))
            << " ("
            << lower_emitted_expr(stmt_condition_expr(stmt), aliases, locals, local_type_refs,
                                  symbols, options)
            << ") {\n";
        emit_block(out, stmt.children, depth + 1, aliases, locals, local_type_refs, return_type_ref,
                   function_returns, symbols, options);
        out << indent(depth) << "}\n";
        return;
    }
    if (stmt.kind == StmtKind::Else) {
        out << indent(depth) << "else {\n";
        emit_block(out, stmt.children, depth + 1, aliases, locals, local_type_refs, return_type_ref,
                   function_returns, symbols, options);
        out << indent(depth) << "}\n";
        return;
    }
    if (stmt.kind == StmtKind::Match) {
        emit_match_statement(out, stmt, depth, aliases, locals, local_type_refs, return_type_ref,
                             function_returns, symbols, options);
        return;
    }
    if (stmt.kind == StmtKind::Try) {
        out << indent(depth) << "try {\n";
        emit_block(out, stmt.children, depth + 1, aliases, locals, local_type_refs, return_type_ref,
                   function_returns, symbols, options);
        out << indent(depth) << "}\n";
        return;
    }
    if (stmt.kind == StmtKind::Except) {
        out << indent(depth);
        if (stmt.name.empty() || !has_stmt_type_ref(stmt)) {
            out << "catch (...)";
        } else {
            out << "catch (const " << lower_cpp_type(stmt_type_ref(stmt), aliases, options) << "& "
                << stmt.name << ")";
        }
        out << " {\n";
        emit_block(out, stmt.children, depth + 1, aliases, locals, local_type_refs, return_type_ref,
                   function_returns, symbols, options);
        out << indent(depth) << "}\n";
        return;
    }
    if (stmt.kind == StmtKind::While) {
        out << indent(depth) << "while ("
            << lower_emitted_expr(stmt_condition_expr(stmt), aliases, locals, local_type_refs,
                                  symbols, options)
            << ") {\n";
        emit_block(out, stmt.children, depth + 1, aliases, locals, local_type_refs, return_type_ref,
                   function_returns, symbols, options);
        out << indent(depth) << "}\n";
        return;
    }
    if (stmt.kind == StmtKind::For && has_stmt_iterable_expr(stmt)) {
        std::string binding = stmt.name;
        const std::string range = lower_emitted_expr(stmt_iterable_expr(stmt), aliases, locals,
                                                     local_type_refs, symbols, options);
        std::string binding_type = "auto";
        locals.bind(stmt.name);
        if (has_stmt_type_ref(stmt)) {
            binding_type = lower_cpp_type(stmt_type_ref(stmt), aliases, options);
            local_type_refs[stmt.name] = stmt_type_ref(stmt);
        }
        if (direct_callee_name(stmt_iterable_expr(stmt)) == "range") {
            const std::vector<Expr>& args = stmt_iterable_expr(stmt).children;
            const std::string start = args.size() == 1
                                          ? "0"
                                          : lower_emitted_expr(args.at(0), aliases, locals,
                                                               local_type_refs, symbols, options);
            const std::string end = args.size() == 1
                                        ? lower_emitted_expr(args.at(0), aliases, locals,
                                                             local_type_refs, symbols, options)
                                        : lower_emitted_expr(args.at(1), aliases, locals,
                                                             local_type_refs, symbols, options);
            const std::string step = args.size() >= 3
                                         ? lower_emitted_expr(args.at(2), aliases, locals,
                                                              local_type_refs, symbols, options)
                                         : "1";
            out << indent(depth) << "for (" << binding_type << ' ' << binding << " = " << start
                << "; " << binding << " < " << end << "; " << binding << " += " << step << ") {\n";
            emit_block(out, stmt.children, depth + 1, aliases, locals, local_type_refs,
                       return_type_ref, function_returns, symbols, options);
            out << indent(depth) << "}\n";
            return;
        }
        const std::string loop_type = has_stmt_type_ref(stmt) ? binding_type : "auto&&";
        out << indent(depth) << "for (" << loop_type << ' ' << binding << " : " << range << ") {\n";
        emit_block(out, stmt.children, depth + 1, aliases, locals, local_type_refs, return_type_ref,
                   function_returns, symbols, options);
        out << indent(depth) << "}\n";
        return;
    }
    emit_simple_statement(out, stmt, depth, aliases, locals, local_type_refs, return_type_ref,
                          function_returns, symbols, options);
}

} // namespace

void emit_block(std::ostringstream& out, const std::vector<Stmt>& body, int depth,
                const std::vector<std::string>& aliases, const CppLocalContext& initial_locals,
                const std::map<std::string, TypeRef>& initial_local_type_refs,
                const TypeRef& return_type_ref,
                const std::map<std::string, TypeRef>& function_returns, const Symbols* symbols,
                const CppEmitOptions& options) {
    CppLocalContext locals = initial_locals;
    std::map<std::string, TypeRef> local_type_refs = initial_local_type_refs;
    for (const Stmt& stmt : body) {
        emit_statement(out, stmt, depth, aliases, locals, local_type_refs, return_type_ref,
                       function_returns, symbols, options);
    }
}

} // namespace dudu
