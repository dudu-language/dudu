#include "dudu/parser/parser_doc_comments.hpp"

#include "dudu/core/ast_expr.hpp"

#include <algorithm>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

namespace dudu {
namespace {

std::string trim_doc_text(std::string_view text) {
    size_t begin = 0;
    while (begin < text.size() && (text[begin] == ' ' || text[begin] == '\t')) {
        ++begin;
    }
    size_t end = text.size();
    while (end > begin &&
           (text[end - 1] == ' ' || text[end - 1] == '\t' || text[end - 1] == '\r')) {
        --end;
    }
    return std::string(text.substr(begin, end - begin));
}

std::string rtrim_doc_text(std::string_view text) {
    size_t end = text.size();
    while (end > 0 && (text[end - 1] == ' ' || text[end - 1] == '\t' || text[end - 1] == '\r')) {
        --end;
    }
    return std::string(text.substr(0, end));
}

std::map<int, std::string> leading_doc_comments(std::string_view source) {
    std::map<int, std::string> docs_by_decl_line;
    std::vector<std::string> pending;
    int line_number = 1;
    size_t start = 0;
    while (start <= source.size()) {
        const size_t end = source.find('\n', start);
        const std::string_view line = end == std::string_view::npos
                                          ? source.substr(start)
                                          : source.substr(start, end - start);
        const std::string trimmed = trim_doc_text(line);
        if (trimmed.empty()) {
            pending.clear();
        } else if (trimmed.starts_with("#")) {
            pending.push_back(trim_doc_text(std::string_view(trimmed).substr(1)));
        } else if (!pending.empty()) {
            std::ostringstream joined;
            for (size_t i = 0; i < pending.size(); ++i) {
                if (i > 0) {
                    joined << '\n';
                }
                joined << pending[i];
            }
            docs_by_decl_line[line_number] = joined.str();
            pending.clear();
        }
        if (end == std::string_view::npos) {
            break;
        }
        start = end + 1;
        ++line_number;
    }
    return docs_by_decl_line;
}

std::string doc_for_line(const std::map<int, std::string>& docs_by_decl_line,
                         const SourceLocation& location) {
    const auto found = docs_by_decl_line.find(location.line);
    return found == docs_by_decl_line.end() ? std::string{} : found->second;
}

bool blank_doc_line(const std::string& line) {
    return std::all_of(line.begin(), line.end(),
                       [](char c) { return c == ' ' || c == '\t' || c == '\r'; });
}

std::vector<std::string> doc_lines(std::string_view text) {
    std::vector<std::string> lines;
    size_t start = 0;
    while (start <= text.size()) {
        const size_t end = text.find('\n', start);
        lines.emplace_back(end == std::string_view::npos ? text.substr(start)
                                                         : text.substr(start, end - start));
        if (end == std::string_view::npos) {
            break;
        }
        start = end + 1;
    }
    return lines;
}

size_t leading_indent(const std::string& line) {
    size_t count = 0;
    while (count < line.size() && (line[count] == ' ' || line[count] == '\t')) {
        ++count;
    }
    return count;
}

std::optional<std::string> docstring_from_statements(std::vector<Stmt>& statements) {
    if (statements.empty() || statements.front().kind != StmtKind::Expr ||
        statements.front().expr.kind != ExprKind::StringLiteral) {
        return std::nullopt;
    }
    std::string docstring = normalize_docstring_text(statements.front().expr.value);
    statements.erase(statements.begin());
    return docstring;
}

void attach_docs_to_function(FunctionDecl& fn,
                             const std::map<int, std::string>& docs_by_decl_line) {
    fn.doc_comment = doc_for_line(docs_by_decl_line, fn.location);
    if (std::optional<std::string> docstring = docstring_from_statements(fn.statements);
        docstring.has_value()) {
        fn.doc_comment = std::move(*docstring);
    }
}

void attach_docs_to_class(ClassDecl& klass, const std::map<int, std::string>& docs_by_decl_line) {
    if (klass.doc_comment.empty()) {
        klass.doc_comment = doc_for_line(docs_by_decl_line, klass.location);
    }
    for (FieldDecl& field : klass.fields) {
        field.doc_comment = doc_for_line(docs_by_decl_line, field.location);
    }
    for (ConstDecl& constant : klass.constants) {
        constant.doc_comment = doc_for_line(docs_by_decl_line, constant.location);
    }
    for (ConstDecl& field : klass.static_fields) {
        field.doc_comment = doc_for_line(docs_by_decl_line, field.location);
    }
    for (FunctionDecl& method : klass.methods) {
        attach_docs_to_function(method, docs_by_decl_line);
    }
}

void attach_docs_to_module(ModuleAst& module, const std::map<int, std::string>& docs_by_decl_line) {
    for (TypeAliasDecl& alias : module.aliases) {
        alias.doc_comment = doc_for_line(docs_by_decl_line, alias.location);
    }
    for (EnumDecl& en : module.enums) {
        if (en.doc_comment.empty()) {
            en.doc_comment = doc_for_line(docs_by_decl_line, en.location);
        }
        for (EnumValueDecl& value : en.values) {
            value.doc_comment = doc_for_line(docs_by_decl_line, value.location);
        }
    }
    for (ClassDecl& klass : module.classes) {
        attach_docs_to_class(klass, docs_by_decl_line);
    }
    for (FunctionDecl& fn : module.functions) {
        attach_docs_to_function(fn, docs_by_decl_line);
    }
    for (ConstDecl& constant : module.constants) {
        constant.doc_comment = doc_for_line(docs_by_decl_line, constant.location);
    }
}

} // namespace

std::string normalize_docstring_text(std::string_view text) {
    std::vector<std::string> lines = doc_lines(text);
    while (!lines.empty() && blank_doc_line(lines.front())) {
        lines.erase(lines.begin());
    }
    while (!lines.empty() && blank_doc_line(lines.back())) {
        lines.pop_back();
    }
    size_t indent = std::string::npos;
    for (size_t i = 1; i < lines.size(); ++i) {
        if (!blank_doc_line(lines[i])) {
            indent = std::min(indent, leading_indent(lines[i]));
        }
    }
    if (indent != std::string::npos && indent > 0) {
        for (size_t i = 1; i < lines.size(); ++i) {
            lines[i].erase(0, std::min(indent, lines[i].size()));
        }
    }
    std::ostringstream out;
    for (size_t i = 0; i < lines.size(); ++i) {
        if (i > 0) {
            out << '\n';
        }
        out << (i == 0 ? trim_doc_text(lines[i]) : rtrim_doc_text(lines[i]));
    }
    return out.str();
}

void attach_leading_doc_comments(ModuleAst& module, std::string_view source) {
    attach_docs_to_module(module, leading_doc_comments(source));
}

} // namespace dudu
