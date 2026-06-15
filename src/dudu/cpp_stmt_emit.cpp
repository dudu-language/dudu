#include "dudu/cpp_stmt_emit.hpp"

#include "dudu/array_shape.hpp"
#include "dudu/ast_expr.hpp"
#include "dudu/ast_type.hpp"
#include "dudu/control_flow.hpp"
#include "dudu/cpp_expr_emit.hpp"
#include "dudu/cpp_lower.hpp"
#include "dudu/cpp_match_emit.hpp"
#include "dudu/cpp_pointer_members.hpp"
#include "dudu/cpp_stmt_helpers.hpp"
#include "dudu/cpp_stmt_types.hpp"
#include "dudu/sema_context.hpp"
#include "dudu/sema_enum.hpp"
#include "dudu/sema_function_type.hpp"
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
                                     const std::vector<std::string>& aliases) {
    return effective_type == stmt.type ? lower_cpp_type(stmt.type_ref, aliases)
                                       : lower_cpp_type(effective_type, aliases);
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
                           const Symbols* symbols) {
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
            out << ' ' << lower_expr(stmt.value_expr, aliases, locals, symbols);
        }
        out << ";\n";
        return;
    }
    if (stmt.kind == StmtKind::Delete) {
        out << indent(depth) << "delete " << lower_expr(stmt.value_expr, aliases, locals, symbols)
            << ";\n";
        return;
    }
    if (stmt.kind == StmtKind::Assert) {
        out << indent(depth) << "if (!("
            << lower_expr(stmt.condition_expr, aliases, locals, symbols)
            << ")) { throw std::runtime_error(";
        if (has_expr(stmt.message_expr))
            out << lower_expr(stmt.message_expr, aliases, locals, symbols);
        else
            out << cpp_string_literal("assert failed: " + stmt.condition_expr.text);
        out << "); }\n";
        return;
    }
    if (stmt.kind == StmtKind::DebugAssert) {
        out << indent(depth) << "assert(("
            << lower_expr(stmt.condition_expr, aliases, locals, symbols) << ")";
        if (has_expr(stmt.message_expr))
            out << " && (" << lower_expr(stmt.message_expr, aliases, locals, symbols) << ")";
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
                out << " {" << lower_expr(stmt.value_expr, aliases, locals, symbols) << '}';
            } else {
                out << ' ' << lower_expr(stmt.value_expr, aliases, locals, symbols);
            }
        }
        out << ";\n";
        return;
    }
    if (stmt.kind == StmtKind::VarDecl) {
        const std::string& name = stmt.name;
        const ArrayShapeInference inferred =
            infer_array_literal_shape_type(stmt.type, stmt.value_expr);
        const std::string type =
            inferred.status == ArrayShapeStatus::Inferred ? inferred.type : stmt.type;
        locals[name] = type;
        out << indent(depth) << lower_declared_stmt_type(stmt, type, aliases) << ' ' << name;
        if (has_expr(stmt.value_expr)) {
            if (is_template_type(type, "Option") && stmt.value_expr.kind == ExprKind::NoneLiteral) {
                out << " = std::nullopt";
            } else if (is_fixed_array_type(type) && stmt.value_expr.kind == ExprKind::ListLiteral) {
                out << " = {" << lower_array_literal(stmt.value_expr, aliases, locals) << "}";
            } else if (is_template_type(type, "list") &&
                       stmt.value_expr.kind == ExprKind::ListLiteral &&
                       stmt.value_expr.children.empty()) {
                out << " = {}";
            } else if (is_template_type(type, "list") &&
                       stmt.value_expr.kind == ExprKind::ListLiteral) {
                out << " = {"
                    << join_lowered_exprs(stmt.value_expr.children, aliases, locals, ", ", symbols)
                    << "}";
            } else if (is_template_type(type, "dict") &&
                       stmt.value_expr.kind == ExprKind::DictLiteral) {
                out << " = {"
                    << join_lowered_exprs(stmt.value_expr.children, aliases, locals, ", ", symbols)
                    << "}";
            } else if (is_template_type(type, "set") &&
                       stmt.value_expr.kind == ExprKind::SetLiteral) {
                out << " = {"
                    << join_lowered_exprs(stmt.value_expr.children, aliases, locals, ", ", symbols)
                    << "}";
            } else if (stmt.value_expr.kind == ExprKind::TupleLiteral) {
                out << " = " << lower_declared_stmt_type(stmt, type, aliases) << "{"
                    << join_lowered_exprs(stmt.value_expr.children, aliases, locals, ", ", symbols)
                    << '}';
            } else {
                out << " = " << lower_expr(stmt.value_expr, aliases, locals, symbols);
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
                << "] = " << lower_expr(stmt.value_expr, aliases, locals, symbols) << ";\n";
            return;
        }
        if (stmt.target_expr.kind == ExprKind::Name && !stmt.target_expr.name.empty()) {
            const std::string& lhs = stmt.target_expr.name;
            const std::string value = lower_expr(stmt.value_expr, aliases, locals, symbols);
            if (locals.contains(lhs)) {
                out << indent(depth) << lhs << " = ";
                if (is_template_type(locals.at(lhs), "Option") &&
                    stmt.value_expr.kind == ExprKind::NoneLiteral) {
                    out << "std::nullopt";
                } else {
                    out << value;
                }
                out << ";\n";
            } else {
                const std::string inferred =
                    infer_emitted_local_type(stmt.value_expr, locals, function_returns);
                locals.emplace(lhs, inferred.empty() ? "auto" : inferred);
                out << indent(depth) << "auto " << lhs << " = " << value << ";\n";
            }
            return;
        }
        if (stmt.target_expr.kind != ExprKind::Unknown) {
            if (const auto call = lower_index_assignment_hook(stmt, aliases, locals, symbols)) {
                out << indent(depth) << *call << ";\n";
                return;
            }
            out << indent(depth) << lower_expr(stmt.target_expr, aliases, locals, symbols) << " = "
                << lower_expr(stmt.value_expr, aliases, locals, symbols) << ";\n";
            return;
        }
    }
    if (stmt.kind == StmtKind::CompoundAssign) {
        out << indent(depth) << lower_expr(stmt.target_expr, aliases, locals, symbols) << ' '
            << stmt.op << '=' << " " << lower_expr(stmt.value_expr, aliases, locals, symbols)
            << ";\n";
        return;
    }
    if (stmt.kind == StmtKind::Expr) {
        out << indent(depth) << lower_expr(stmt.expr, aliases, locals, symbols) << ";\n";
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
                    const Symbols* symbols) {
    emit_source_comment(out, stmt, depth);
    if (stmt.kind == StmtKind::If) {
        out << indent(depth) << if_keyword_for_condition(stmt.condition_expr) << " ("
            << lower_expr(stmt.condition_expr, aliases, locals, symbols) << ") {\n";
        emit_block(out, stmt.children, depth + 1, aliases, locals, return_type, function_returns,
                   symbols);
        out << indent(depth) << "}\n";
        return;
    }
    if (stmt.kind == StmtKind::Elif) {
        out << indent(depth) << "else " << if_keyword_for_condition(stmt.condition_expr) << " ("
            << lower_expr(stmt.condition_expr, aliases, locals, symbols) << ") {\n";
        emit_block(out, stmt.children, depth + 1, aliases, locals, return_type, function_returns,
                   symbols);
        out << indent(depth) << "}\n";
        return;
    }
    if (stmt.kind == StmtKind::Else) {
        out << indent(depth) << "else {\n";
        emit_block(out, stmt.children, depth + 1, aliases, locals, return_type, function_returns,
                   symbols);
        out << indent(depth) << "}\n";
        return;
    }
    if (stmt.kind == StmtKind::Match) {
        const std::string subject_type =
            infer_emitted_local_type(stmt.condition_expr, locals, function_returns);
        if (!subject_type.empty()) {
            const WrapperMatchType wrapper = wrapper_match_type(subject_type);
            if (wrapper.kind != WrapperMatchKind::None) {
                const std::string subject = "__dudu_match_" + std::to_string(stmt.location.line) +
                                            "_" + std::to_string(stmt.location.column);
                const std::string matched = subject + "_matched";
                out << indent(depth) << "auto&& " << subject << " = "
                    << lower_expr(stmt.condition_expr, aliases, locals, symbols) << ";\n";
                out << indent(depth) << "bool " << matched << " = false;\n";
                for (const Stmt& child : stmt.children) {
                    if (child.kind != StmtKind::Case) {
                        continue;
                    }
                    const std::optional<std::string> case_name =
                        wrapper_case_name(child.pattern_expr);
                    std::string condition = "true";
                    if (case_name && *case_name != "_") {
                        if (wrapper.kind == WrapperMatchKind::Option) {
                            condition = *case_name == "Some" ? subject + ".has_value()"
                                                             : "!" + subject + ".has_value()";
                        } else {
                            condition =
                                *case_name == "Ok" ? subject + ".ok" : "!" + subject + ".ok";
                        }
                    }
                    out << indent(depth) << "if (!" << matched << " && (" << condition << ")) {\n";
                    std::map<std::string, std::string> nested = locals;
                    if (case_name &&
                        (*case_name == "Some" || *case_name == "Ok" || *case_name == "Err")) {
                        if (const auto binding = wrapper_case_binding_name(child.pattern_expr)) {
                            if (*case_name == "Some" && wrapper.args.size() == 1) {
                                nested[*binding] = trim_copy(wrapper.args[0]);
                                out << indent(depth + 1) << "auto&& " << *binding << " = "
                                    << subject << ".value();\n";
                            } else if (*case_name == "Ok" && wrapper.args.size() == 2) {
                                nested[*binding] = trim_copy(wrapper.args[0]);
                                out << indent(depth + 1) << "auto&& " << *binding << " = "
                                    << subject << ".value;\n";
                            } else if (*case_name == "Err" && wrapper.args.size() == 2) {
                                nested[*binding] = trim_copy(wrapper.args[1]);
                                out << indent(depth + 1) << "auto&& " << *binding << " = "
                                    << subject << ".err;\n";
                            }
                        }
                    }
                    if (has_expr(child.guard_expr)) {
                        out << indent(depth + 1) << "if ("
                            << lower_expr(child.guard_expr, aliases, nested, symbols) << ") {\n";
                        out << indent(depth + 2) << matched << " = true;\n";
                        emit_block(out, child.children, depth + 2, aliases, nested, return_type,
                                   function_returns, symbols);
                        out << indent(depth + 1) << "}\n";
                    } else {
                        out << indent(depth + 1) << matched << " = true;\n";
                        emit_block(out, child.children, depth + 1, aliases, nested, return_type,
                                   function_returns, symbols);
                    }
                    out << indent(depth) << "}\n";
                }
                if (match_cases_return(stmt)) {
                    out << indent(depth) << "__builtin_unreachable();\n";
                }
                return;
            }
            const EnumDecl* en = enum_decl_for_type(symbols, subject_type);
            if (en != nullptr && (enum_has_payloads(*en) || match_has_guards(stmt))) {
                const std::string subject = "__dudu_match_" + std::to_string(stmt.location.line) +
                                            "_" + std::to_string(stmt.location.column);
                const std::string matched = subject + "_matched";
                out << indent(depth) << "auto&& " << subject << " = "
                    << lower_expr(stmt.condition_expr, aliases, locals, symbols) << ";\n";
                out << indent(depth) << "bool " << matched << " = false;\n";
                for (const Stmt& child : stmt.children) {
                    if (child.kind != StmtKind::Case) {
                        continue;
                    }
                    const std::optional<std::string> variant = enum_case_variant_name(child);
                    std::string condition = "true";
                    if (!variant || *variant == "_") {
                        condition = "true";
                    } else if (enum_has_payloads(*en)) {
                        condition = "std::holds_alternative<" + en->name + "::" + *variant + ">(" +
                                    subject + ".value)";
                    } else {
                        condition = subject + " == " + en->name + "::" + *variant;
                    }
                    out << indent(depth) << "if (!" << matched << " && (" << condition << ")) {\n";
                    std::map<std::string, std::string> nested = locals;
                    if (enum_has_payloads(*en) && variant && *variant != "_") {
                        if (const EnumValueDecl* value = enum_variant_decl(*en, *variant)) {
                            const std::string payload = "__dudu_case_" +
                                                        std::to_string(child.location.line) + "_" +
                                                        std::to_string(child.location.column);
                            out << indent(depth + 1) << "auto&& " << payload << " = std::get<"
                                << en->name << "::" << *variant << ">(" << subject << ".value);\n";
                            const std::vector<EnumCaseBinding> bindings =
                                enum_case_bindings(child, *value);
                            for (const EnumCaseBinding& binding : bindings) {
                                if (binding.field_index >= value->payload_fields.size()) {
                                    continue;
                                }
                                const EnumPayloadField& field =
                                    value->payload_fields[binding.field_index];
                                nested[binding.name] = field.type;
                                out << indent(depth + 1) << "auto&& " << binding.name << " = "
                                    << payload << "." << field.name << ";\n";
                            }
                        }
                    }
                    if (has_expr(child.guard_expr)) {
                        out << indent(depth + 1) << "if ("
                            << lower_expr(child.guard_expr, aliases, nested, symbols) << ") {\n";
                        out << indent(depth + 2) << matched << " = true;\n";
                        emit_block(out, child.children, depth + 2, aliases, nested, return_type,
                                   function_returns, symbols);
                        out << indent(depth + 1) << "}\n";
                    } else {
                        out << indent(depth + 1) << matched << " = true;\n";
                        emit_block(out, child.children, depth + 1, aliases, nested, return_type,
                                   function_returns, symbols);
                    }
                    out << indent(depth) << "}\n";
                }
                if (match_cases_return(stmt)) {
                    out << indent(depth) << "__builtin_unreachable();\n";
                }
                return;
            }
        }
        out << indent(depth) << "switch ("
            << lower_expr(stmt.condition_expr, aliases, locals, symbols) << ") {\n";
        for (const Stmt& child : stmt.children) {
            if (child.kind != StmtKind::Case) {
                continue;
            }
            if (is_wildcard_pattern_expr(child.pattern_expr)) {
                out << indent(depth) << "default:\n";
            } else {
                out << indent(depth) << "case "
                    << lower_expr(child.pattern_expr, aliases, locals, symbols) << ":\n";
            }
            out << indent(depth + 1) << "{\n";
            emit_block(out, child.children, depth + 2, aliases, locals, return_type,
                       function_returns, symbols);
            out << indent(depth + 2) << "break;\n";
            out << indent(depth + 1) << "}\n";
        }
        out << indent(depth) << "}\n";
        if (match_cases_return(stmt)) {
            out << indent(depth) << "__builtin_unreachable();\n";
        }
        return;
    }
    if (stmt.kind == StmtKind::Try) {
        out << indent(depth) << "try {\n";
        emit_block(out, stmt.children, depth + 1, aliases, locals, return_type, function_returns,
                   symbols);
        out << indent(depth) << "}\n";
        return;
    }
    if (stmt.kind == StmtKind::Except) {
        out << indent(depth);
        if (stmt.name.empty() || stmt.type.empty()) {
            out << "catch (...)";
        } else {
            out << "catch (const " << lower_cpp_type(stmt.type_ref, aliases) << "& " << stmt.name
                << ")";
        }
        out << " {\n";
        emit_block(out, stmt.children, depth + 1, aliases, locals, return_type, function_returns,
                   symbols);
        out << indent(depth) << "}\n";
        return;
    }
    if (stmt.kind == StmtKind::While) {
        out << indent(depth) << "while ("
            << lower_expr(stmt.condition_expr, aliases, locals, symbols) << ") {\n";
        emit_block(out, stmt.children, depth + 1, aliases, locals, return_type, function_returns,
                   symbols);
        out << indent(depth) << "}\n";
        return;
    }
    if (stmt.kind == StmtKind::For && has_expr(stmt.iterable_expr)) {
        std::string binding = stmt.name;
        const std::string range = lower_expr(stmt.iterable_expr, aliases, locals, symbols);
        std::string binding_type = "auto";
        if (!stmt.type.empty()) {
            binding_type = lower_cpp_type(stmt.type_ref, aliases);
            locals[stmt.name] = stmt.type;
        }
        if (stmt.iterable_expr.kind == ExprKind::Call && stmt.iterable_expr.name == "range") {
            const std::vector<Expr>& args = stmt.iterable_expr.children;
            const std::string start =
                args.size() == 1 ? "0" : lower_expr(args.at(0), aliases, locals, symbols);
            const std::string end = args.size() == 1
                                        ? lower_expr(args.at(0), aliases, locals, symbols)
                                        : lower_expr(args.at(1), aliases, locals, symbols);
            const std::string step =
                args.size() >= 3 ? lower_expr(args.at(2), aliases, locals, symbols) : "1";
            out << indent(depth) << "for (" << binding_type << ' ' << binding << " = " << start
                << "; " << binding << " < " << end << "; " << binding << " += " << step << ") {\n";
            emit_block(out, stmt.children, depth + 1, aliases, locals, return_type,
                       function_returns, symbols);
            out << indent(depth) << "}\n";
            return;
        }
        const std::string loop_type = stmt.type.empty() ? "auto&&" : binding_type;
        out << indent(depth) << "for (" << loop_type << ' ' << binding << " : " << range << ") {\n";
        emit_block(out, stmt.children, depth + 1, aliases, locals, return_type, function_returns,
                   symbols);
        out << indent(depth) << "}\n";
        return;
    }
    emit_simple_statement(out, stmt, depth, aliases, locals, return_type, function_returns,
                          symbols);
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
    std::map<std::string, std::string> locals = initial_locals;
    for (const Stmt& stmt : body) {
        emit_statement(out, stmt, depth, aliases, locals, return_type, function_returns, symbols);
    }
}

} // namespace dudu
