#include "dudu/lsp/language_server_lint_unreachable.hpp"

#include "dudu/lsp/language_server_lint_common.hpp"

namespace dudu {
namespace {

bool statement_sequence_exits(const std::vector<Stmt>& statements);

bool statement_exits(const std::vector<Stmt>& statements, size_t index, size_t& consumed_until) {
    const Stmt& stmt = statements[index];
    consumed_until = index;
    if (stmt.kind == StmtKind::Return || stmt.kind == StmtKind::Raise ||
        stmt.kind == StmtKind::Break || stmt.kind == StmtKind::Continue) {
        return true;
    }
    if (stmt.kind != StmtKind::If) {
        return false;
    }

    bool saw_else = false;
    bool all_branches_exit = true;
    size_t cursor = index;
    while (cursor < statements.size()) {
        const StmtKind kind = statements[cursor].kind;
        if (cursor == index) {
            if (kind != StmtKind::If) {
                break;
            }
        } else if (kind != StmtKind::Elif && kind != StmtKind::Else) {
            break;
        }
        if (kind == StmtKind::Else) {
            saw_else = true;
        }
        all_branches_exit =
            all_branches_exit && statement_sequence_exits(statements[cursor].children);
        consumed_until = cursor;
        ++cursor;
        if (kind == StmtKind::Else) {
            break;
        }
    }
    return saw_else && all_branches_exit;
}

bool statement_sequence_exits(const std::vector<Stmt>& statements) {
    for (size_t i = 0; i < statements.size(); ++i) {
        size_t consumed_until = i;
        if (statement_exits(statements, i, consumed_until)) {
            return true;
        }
        i = consumed_until;
    }
    return false;
}

void lint_unreachable_statement_sequence(const std::vector<Stmt>& statements, const Document& doc,
                                         std::vector<Diagnostic>& out) {
    bool after_terminator = false;
    bool reported_after_terminator = false;
    for (size_t i = 0; i < statements.size(); ++i) {
        const Stmt& stmt = statements[i];
        const bool in_document = lint_same_source_file(stmt.location.file, doc.path);
        if (after_terminator && !reported_after_terminator && in_document) {
            out.push_back({.location = stmt.location,
                           .message = "unreachable statement after terminating statement",
                           .source = "dudu/lint",
                           .severity = 2,
                           .code = "dudu.lint.unreachable",
                           .data_name = "",
                           .fix_range = lint_delete_line_range(stmt.location, doc),
                           .related_information = {}});
            reported_after_terminator = true;
        }

        size_t consumed_until = i;
        const bool exits = statement_exits(statements, i, consumed_until);
        for (size_t child_index = i; child_index <= consumed_until; ++child_index) {
            lint_unreachable_statement_sequence(statements[child_index].children, doc, out);
        }
        if (exits && in_document) {
            after_terminator = true;
        }
        i = consumed_until;
    }
}

void lint_unreachable_function(const FunctionDecl& fn, const Document& doc,
                               std::vector<Diagnostic>& out) {
    lint_unreachable_statement_sequence(fn.statements, doc, out);
}

void lint_unreachable_class(const ClassDecl& klass, const Document& doc,
                            std::vector<Diagnostic>& out) {
    for (const FunctionDecl& method : klass.methods) {
        lint_unreachable_function(method, doc, out);
    }
}

} // namespace

void lint_unreachable_module(const ModuleAst& module, const Document& doc,
                             std::vector<Diagnostic>& out) {
    for (const FunctionDecl& fn : module.functions) {
        lint_unreachable_function(fn, doc, out);
    }
    for (const ClassDecl& klass : module.classes) {
        lint_unreachable_class(klass, doc, out);
    }
    for (const ModuleAst& unit : module.module_units) {
        lint_unreachable_module(unit, doc, out);
    }
}

} // namespace dudu
