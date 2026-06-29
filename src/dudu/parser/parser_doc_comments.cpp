#include "dudu/parser/parser_doc_comments.hpp"

#include <map>
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
    while (end > begin && (text[end - 1] == ' ' || text[end - 1] == '\t' ||
                           text[end - 1] == '\r')) {
        --end;
    }
    return std::string(text.substr(begin, end - begin));
}

std::map<int, std::string> leading_doc_comments(std::string_view source) {
    std::map<int, std::string> docs_by_decl_line;
    std::vector<std::string> pending;
    int line_number = 1;
    size_t start = 0;
    while (start <= source.size()) {
        const size_t end = source.find('\n', start);
        const std::string_view line =
            end == std::string_view::npos ? source.substr(start)
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

void attach_docs_to_function(FunctionDecl& fn,
                             const std::map<int, std::string>& docs_by_decl_line) {
    fn.doc_comment = doc_for_line(docs_by_decl_line, fn.location);
}

void attach_docs_to_class(ClassDecl& klass,
                          const std::map<int, std::string>& docs_by_decl_line) {
    klass.doc_comment = doc_for_line(docs_by_decl_line, klass.location);
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

void attach_docs_to_module(ModuleAst& module,
                           const std::map<int, std::string>& docs_by_decl_line) {
    for (TypeAliasDecl& alias : module.aliases) {
        alias.doc_comment = doc_for_line(docs_by_decl_line, alias.location);
    }
    for (EnumDecl& en : module.enums) {
        en.doc_comment = doc_for_line(docs_by_decl_line, en.location);
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

void attach_leading_doc_comments(ModuleAst& module, std::string_view source) {
    attach_docs_to_module(module, leading_doc_comments(source));
}

} // namespace dudu
