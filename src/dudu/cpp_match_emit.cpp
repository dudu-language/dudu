#include "dudu/cpp_match_emit.hpp"

#include "dudu/ast_type.hpp"
#include "dudu/control_flow.hpp"
#include "dudu/cpp_expr_emit.hpp"
#include "dudu/cpp_lower.hpp"
#include "dudu/cpp_stmt_emit.hpp"
#include "dudu/cpp_stmt_helpers.hpp"
#include "dudu/cpp_stmt_types.hpp"
#include "dudu/sema_enum.hpp"

#include <sstream>

namespace dudu {
bool match_has_guards(const Stmt& stmt) {
    for (const Stmt& child : stmt.children) {
        if (child.kind == StmtKind::Case && has_expr(child.guard_expr)) {
            return true;
        }
    }
    return false;
}

bool match_cases_return(const Stmt& stmt) {
    if (stmt.children.empty()) {
        return false;
    }
    for (const Stmt& child : stmt.children) {
        if (child.kind != StmtKind::Case || !block_guarantees_return(child.children)) {
            return false;
        }
    }
    return true;
}

void emit_match_statement(std::ostringstream& out, const Stmt& stmt, int depth,
                          const std::vector<std::string>& aliases,
                          const std::map<std::string, std::string>& locals,
                          const std::string& return_type,
                          const std::map<std::string, std::string>& function_returns,
                          const Symbols* symbols, const CppEmitOptions& options) {
    static const std::map<std::string, TypeRef> no_type_refs;
    emit_match_statement(out, stmt, depth, aliases, locals, no_type_refs, return_type,
                         function_returns, symbols, options);
}

void emit_match_statement(std::ostringstream& out, const Stmt& stmt, int depth,
                          const std::vector<std::string>& aliases,
                          const std::map<std::string, std::string>& locals,
                          const std::map<std::string, TypeRef>& local_type_refs,
                          const std::string& return_type,
                          const std::map<std::string, std::string>& function_returns,
                          const Symbols* symbols, const CppEmitOptions& options) {
    const std::string subject_type =
        infer_emitted_local_type(stmt.condition_expr, locals, local_type_refs, function_returns);
    if (!subject_type.empty()) {
        const WrapperMatchType wrapper = wrapper_match_type(subject_type);
        if (wrapper.kind != WrapperMatchKind::None) {
            const std::string subject = "__dudu_match_" + std::to_string(stmt.location.line) + "_" +
                                        std::to_string(stmt.location.column);
            const std::string matched = subject + "_matched";
            out << indent(depth) << "auto&& " << subject << " = "
                << lower_expr(stmt.condition_expr, aliases, locals, symbols, options) << ";\n";
            out << indent(depth) << "bool " << matched << " = false;\n";
            for (const Stmt& child : stmt.children) {
                if (child.kind != StmtKind::Case) {
                    continue;
                }
                const std::optional<std::string> case_name = wrapper_case_name(child.pattern_expr);
                std::string condition = "true";
                if (case_name && *case_name != "_") {
                    if (wrapper.kind == WrapperMatchKind::Option) {
                        condition = *case_name == "Some" ? subject + ".has_value()"
                                                         : "!" + subject + ".has_value()";
                    } else {
                        condition = *case_name == "Ok" ? subject + ".ok" : "!" + subject + ".ok";
                    }
                }
                out << indent(depth) << "if (!" << matched << " && (" << condition << ")) {\n";
                std::map<std::string, std::string> nested = locals;
                std::map<std::string, TypeRef> nested_type_refs = local_type_refs;
                if (case_name &&
                    (*case_name == "Some" || *case_name == "Ok" || *case_name == "Err")) {
                    if (const auto binding = wrapper_case_binding_name(child.pattern_expr)) {
                        if (*case_name == "Some" && wrapper.args.size() == 1 &&
                            wrapper.arg_refs.size() == 1) {
                            nested[*binding] = trim_copy(wrapper.args[0]);
                            nested_type_refs[*binding] = wrapper.arg_refs[0];
                            out << indent(depth + 1) << "auto&& " << *binding << " = " << subject
                                << ".value();\n";
                        } else if (*case_name == "Ok" && wrapper.args.size() == 2 &&
                                   wrapper.arg_refs.size() == 2) {
                            nested[*binding] = trim_copy(wrapper.args[0]);
                            nested_type_refs[*binding] = wrapper.arg_refs[0];
                            out << indent(depth + 1) << "auto&& " << *binding << " = " << subject
                                << ".value;\n";
                        } else if (*case_name == "Err" && wrapper.args.size() == 2 &&
                                   wrapper.arg_refs.size() == 2) {
                            nested[*binding] = trim_copy(wrapper.args[1]);
                            nested_type_refs[*binding] = wrapper.arg_refs[1];
                            out << indent(depth + 1) << "auto&& " << *binding << " = " << subject
                                << ".err;\n";
                        }
                    }
                }
                if (has_expr(child.guard_expr)) {
                    out << indent(depth + 1) << "if ("
                        << lower_expr(child.guard_expr, aliases, nested, symbols, options)
                        << ") {\n";
                    out << indent(depth + 2) << matched << " = true;\n";
                    emit_block(out, child.children, depth + 2, aliases, nested, nested_type_refs,
                               return_type, function_returns, symbols, options);
                    out << indent(depth + 1) << "}\n";
                } else {
                    out << indent(depth + 1) << matched << " = true;\n";
                    emit_block(out, child.children, depth + 1, aliases, nested, nested_type_refs,
                               return_type, function_returns, symbols, options);
                }
                out << indent(depth) << "}\n";
            }
            if (match_cases_return(stmt)) {
                out << indent(depth) << "__builtin_unreachable();\n";
            }
            return;
        }
        const EnumDecl* en =
            symbols == nullptr ? nullptr : enum_decl_for_type(*symbols, subject_type);
        if (en != nullptr && (enum_has_payloads(*en) || match_has_guards(stmt))) {
            const std::string subject = "__dudu_match_" + std::to_string(stmt.location.line) + "_" +
                                        std::to_string(stmt.location.column);
            const std::string matched = subject + "_matched";
            out << indent(depth) << "auto&& " << subject << " = "
                << lower_expr(stmt.condition_expr, aliases, locals, symbols, options) << ";\n";
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
                    const std::string enum_name = emitted_type_name(en->name, options);
                    condition = "std::holds_alternative<" + enum_name + "::" + *variant + ">(" +
                                subject + ".value)";
                } else {
                    condition =
                        subject + " == " + emitted_type_name(en->name, options) + "::" + *variant;
                }
                out << indent(depth) << "if (!" << matched << " && (" << condition << ")) {\n";
                std::map<std::string, std::string> nested = locals;
                std::map<std::string, TypeRef> nested_type_refs = local_type_refs;
                if (enum_has_payloads(*en) && variant && *variant != "_") {
                    if (const EnumValueDecl* value = enum_variant_decl(*en, *variant)) {
                        const std::string payload = "__dudu_case_" +
                                                    std::to_string(child.location.line) + "_" +
                                                    std::to_string(child.location.column);
                        const std::string enum_name = emitted_type_name(en->name, options);
                        out << indent(depth + 1) << "auto&& " << payload << " = std::get<"
                            << enum_name << "::" << *variant << ">(" << subject << ".value);\n";
                        const std::vector<EnumCaseBinding> bindings =
                            enum_case_bindings(child, *value);
                        for (const EnumCaseBinding& binding : bindings) {
                            if (binding.field_index >= value->payload_fields.size()) {
                                continue;
                            }
                            const EnumPayloadField& field =
                                value->payload_fields[binding.field_index];
                            nested[binding.name] = type_ref_text(field.type_ref);
                            nested_type_refs[binding.name] = field.type_ref;
                            out << indent(depth + 1) << "auto&& " << binding.name << " = "
                                << payload << "." << field.name << ";\n";
                        }
                    }
                }
                if (has_expr(child.guard_expr)) {
                    out << indent(depth + 1) << "if ("
                        << lower_expr(child.guard_expr, aliases, nested, symbols, options)
                        << ") {\n";
                    out << indent(depth + 2) << matched << " = true;\n";
                    emit_block(out, child.children, depth + 2, aliases, nested, nested_type_refs,
                               return_type, function_returns, symbols, options);
                    out << indent(depth + 1) << "}\n";
                } else {
                    out << indent(depth + 1) << matched << " = true;\n";
                    emit_block(out, child.children, depth + 1, aliases, nested, nested_type_refs,
                               return_type, function_returns, symbols, options);
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
        << lower_expr(stmt.condition_expr, aliases, locals, symbols, options) << ") {\n";
    for (const Stmt& child : stmt.children) {
        if (child.kind != StmtKind::Case) {
            continue;
        }
        if (is_wildcard_pattern_expr(child.pattern_expr)) {
            out << indent(depth) << "default:\n";
        } else {
            out << indent(depth) << "case "
                << lower_expr(child.pattern_expr, aliases, locals, symbols, options) << ":\n";
        }
        out << indent(depth + 1) << "{\n";
        emit_block(out, child.children, depth + 2, aliases, locals, local_type_refs, return_type,
                   function_returns, symbols, options);
        out << indent(depth + 2) << "break;\n";
        out << indent(depth + 1) << "}\n";
    }
    out << indent(depth) << "}\n";
    if (match_cases_return(stmt)) {
        out << indent(depth) << "__builtin_unreachable();\n";
    }
}

void emit_match_statement(std::ostringstream& out, const Stmt& stmt, int depth,
                          const std::vector<std::string>& aliases,
                          const std::map<std::string, std::string>& locals,
                          const std::string& return_type,
                          const std::map<std::string, std::string>& function_returns,
                          const Symbols* symbols) {
    emit_match_statement(out, stmt, depth, aliases, locals, return_type, function_returns, symbols,
                         {});
}

} // namespace dudu
