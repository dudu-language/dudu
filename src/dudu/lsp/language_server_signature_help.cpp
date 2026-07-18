#include "dudu/lsp/language_server_signature_help.hpp"

#include "dudu/core/ast_type.hpp"
#include "dudu/core/ast_visit.hpp"
#include "dudu/lsp/language_server_ast_walk.hpp"
#include "dudu/lsp/language_server_call_site.hpp"
#include "dudu/lsp/language_server_class_members.hpp"
#include "dudu/lsp/language_server_documentation.hpp"
#include "dudu/lsp/language_server_json.hpp"
#include "dudu/lsp/language_server_local_context.hpp"
#include "dudu/lsp/language_server_macros.hpp"
#include "dudu/lsp/language_server_navigation.hpp"
#include "dudu/lsp/language_server_operator.hpp"
#include "dudu/lsp/language_server_support.hpp"
#include "dudu/lsp/language_server_symbols.hpp"
#include "dudu/sema/sema_context.hpp"
#include "dudu/sema/sema_function_type.hpp"
#include "dudu/sema/sema_methods.hpp"

#include <exception>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace dudu {
namespace {

struct SignatureCandidate {
    std::string label;
    std::string documentation;
    std::vector<Symbol::Parameter> parameters;
};

SignatureCandidate signature_candidate(const Symbol& symbol) {
    return {.label = symbol.detail,
            .documentation = symbol_documentation_markdown(symbol),
            .parameters = symbol.parameters};
}

Symbol instantiated_method_symbol(const DuduMethodInstantiation& instantiation) {
    const FunctionDecl& method = *instantiation.method;
    Symbol symbol = method_symbol(method, instantiation.owner != nullptr &&
                                              instantiation.owner->native_declaration);
    for (size_t i = 0;
         i < symbol.parameters.size() && i < instantiation.signature.param_type_refs.size(); ++i) {
        const size_t default_value = symbol.parameters[i].label.find(" = ");
        const std::string suffix = default_value == std::string::npos
                                       ? std::string{}
                                       : symbol.parameters[i].label.substr(default_value);
        symbol.parameters[i].label = method.params[i].name + ": " +
                                     type_ref_text(instantiation.signature.param_type_refs[i]) +
                                     suffix;
    }
    std::ostringstream detail;
    detail << "def " << method.name << '(';
    for (size_t i = 0; i < symbol.parameters.size(); ++i) {
        if (i > 0) {
            detail << ", ";
        }
        detail << symbol.parameters[i].label;
    }
    detail << ") -> " << type_ref_text(instantiation.signature.return_type_ref);
    symbol.detail = detail.str();
    return symbol;
}

bool position_in_range(const LspPosition& position, const SourceRange& range) {
    const int line = position.line + 1;
    const int column = position.character + 1;
    if (line < range.start.line || line > range.end.line) {
        return false;
    }
    if (line == range.start.line && column < range.start.column) {
        return false;
    }
    return line != range.end.line || column <= range.end.column;
}

const Expr* index_expr_at(const ModuleAst& module, const LspPosition& position) {
    const Expr* selected = nullptr;
    const auto visit_statements = [&](const std::vector<Stmt>& statements) {
        visit_lsp_stmt_tree(statements, [&](const Stmt& stmt) {
            visit_stmt_expressions(stmt, [&](const Expr& root) {
                visit_lsp_expr_tree(
                    root,
                    [&](const Expr& expr) {
                        if (expr.kind == ExprKind::Index &&
                            position_in_range(position, expr.range)) {
                            selected = &expr;
                        }
                    },
                    [](const TypeRef&) {});
            });
        });
    };
    for (const FunctionDecl& function : module.functions) {
        visit_statements(function.statements);
    }
    for (const ClassDecl& klass : module.classes) {
        for (const FunctionDecl& method : klass.methods) {
            visit_statements(method.statements);
        }
    }
    return selected;
}

int index_active_parameter(const Expr& index, const LspPosition& position) {
    if (index.children.size() != 2 || index.children[1].kind != ExprKind::TupleLiteral) {
        return 0;
    }
    const int line = position.line + 1;
    const int column = position.character + 1;
    int active = 0;
    for (size_t i = 1; i < index.children[1].children.size(); ++i) {
        const SourceLocation& start = index.children[1].children[i].location;
        if (start.line < line || (start.line == line && start.column <= column)) {
            active = static_cast<int>(i);
        }
    }
    return active;
}

std::optional<std::string> index_signature_help_json(const ModuleAst& current,
                                                     const LspPosition& position) {
    const Expr* index = index_expr_at(current, position);
    if (index == nullptr) {
        return std::nullopt;
    }
    const std::optional<Symbol> selected =
        dudu_operator_symbol_for_expr(current, *index, position.line + 1);
    if (!selected) {
        return std::nullopt;
    }
    std::ostringstream out;
    out << "{\"signatures\":[{\"label\":\"" << json_escape(selected->detail) << "\"";
    write_markdown_documentation(out, selected->doc_comment);
    out << "}],\"activeSignature\":0,\"activeParameter\":"
        << index_active_parameter(*index, position) << "}";
    return out.str();
}

ProjectIndexSnapshot signature_index(const Document& doc) {
    try {
        return project_index_for_document(doc, true);
    } catch (const std::exception&) {
        return {};
    }
}

void add_constructor_candidates(std::vector<SignatureCandidate>& signatures,
                                const ModuleAst& current, const std::string& call_name) {
    const auto add_from_classes = [&](const std::vector<ClassDecl>& classes) {
        for (const ClassDecl& klass : classes) {
            if (klass.name == call_name) {
                signatures.push_back(
                    {.label = constructor_detail(klass),
                     .documentation = documentation_markdown(constructor_doc_comment(klass)),
                     .parameters = constructor_symbol_parameters(klass)});
            }
        }
    };
    add_from_classes(current.classes);
    add_from_classes(current.native_classes);
}

void add_member_candidates(std::vector<SignatureCandidate>& signatures, const ModuleAst& current,
                           const LspCallSite& call, const Json* params) {
    const size_t dot = call.name.rfind('.');
    if (dot == std::string::npos || dot == 0 || dot + 1 >= call.name.size()) {
        return;
    }
    const std::string receiver = call.name.substr(0, dot);
    const std::string member = call.name.substr(dot + 1);
    for (const Symbol& symbol : class_member_symbols_for_owner(current, receiver)) {
        if (symbol.name == member && (symbol.kind == lsp_symbol_kind::Method ||
                                      symbol.kind == lsp_symbol_kind::Constructor)) {
            signatures.push_back(signature_candidate(symbol));
        }
    }
    const TypeRef type_ref = local_type_ref_before_cursor(current, receiver, params);
    if (!has_type_ref(type_ref)) {
        return;
    }
    const Symbols symbols = collect_symbols(current);
    for (const DuduMethodInstantiation& instantiation :
         dudu_method_instantiations_for_type(symbols, type_ref, member, {})) {
        if (instantiation.method == nullptr) {
            continue;
        }
        signatures.push_back(signature_candidate(instantiated_method_symbol(instantiation)));
    }
}

} // namespace

std::string signature_help_json(const Document* doc, const Json* params) {
    if (doc == nullptr) {
        return "{\"signatures\":[],\"activeSignature\":0,\"activeParameter\":0}";
    }
    const ProjectIndexSnapshot index = signature_index(*doc);
    if (index) {
        const ModuleAst& current = index->visible_unit_for_path(doc->path);
        if (const std::optional<std::string> index_help =
                index_signature_help_json(current, lsp_position(params))) {
            return *index_help;
        }
    }
    const LspCallSite call = lsp_call_site_at(*doc, params);
    if (call.name.empty()) {
        return "{\"signatures\":[],\"activeSignature\":0,\"activeParameter\":0}";
    }
    std::vector<SignatureCandidate> signatures;
    if (index) {
        const ModuleAst& current = index->visible_unit_for_path(doc->path);
        const std::optional<MacroEditorCall> macro =
            macro_call_for_reference(*index, current, call.name);
        if (macro) {
            signatures.push_back({.label = macro->signature,
                                  .documentation = macro->documentation,
                                  .parameters = {}});
        }
        const Symbols semantic_symbols = collect_symbols(current);
        add_member_candidates(signatures, current, call, params);
        add_constructor_candidates(signatures, current, call.name);
        for (const Symbol& symbol : symbols_for_module(current, true)) {
            if (!macro && symbol.name == call.name && symbol.kind == lsp_symbol_kind::Function) {
                signatures.push_back(signature_candidate(symbol));
            }
        }
        for (const NativeValueDecl& value : current.native_values) {
            if (value.name != call.name) {
                continue;
            }
            FunctionSignature signature;
            if (!parse_function_type_or_alias(semantic_symbols, native_value_type_ref(value),
                                              signature)) {
                continue;
            }
            std::string label = function_type(signature);
            if (label.starts_with("fn")) {
                label.replace(0, 2, call.name);
            }
            signatures.push_back(
                {.label = std::move(label), .documentation = value.doc_comment, .parameters = {}});
        }
    }
    std::ostringstream out;
    out << "{\"signatures\":[";
    for (size_t index = 0; index < signatures.size(); ++index) {
        if (index > 0) {
            out << ",";
        }
        out << "{\"label\":\"" << json_escape(signatures[index].label) << "\"";
        write_markdown_documentation(out, signatures[index].documentation);
        if (!signatures[index].parameters.empty()) {
            out << ",\"parameters\":[";
            for (size_t parameter = 0; parameter < signatures[index].parameters.size();
                 ++parameter) {
                if (parameter > 0) {
                    out << ',';
                }
                const Symbol::Parameter& item = signatures[index].parameters[parameter];
                out << "{\"label\":\"" << json_escape(item.label) << "\"";
                write_markdown_documentation(out, item.documentation);
                out << '}';
            }
            out << ']';
        }
        out << "}";
    }
    out << "],\"activeSignature\":0,\"activeParameter\":" << call.parameter << "}";
    return out.str();
}

} // namespace dudu
