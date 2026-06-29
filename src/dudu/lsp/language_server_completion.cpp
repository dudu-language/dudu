#include "dudu/lsp/language_server_completion.hpp"

#include "dudu/core/ast_type.hpp"
#include "dudu/lsp/language_server_class_members.hpp"
#include "dudu/lsp/language_server_json.hpp"
#include "dudu/lsp/language_server_local_context.hpp"
#include "dudu/lsp/language_server_navigation.hpp"
#include "dudu/lsp/language_server_support.hpp"
#include "dudu/lsp/language_server_symbols.hpp"
#include "dudu/native/native_headers.hpp"
#include "dudu/parser/lexer.hpp"
#include "dudu/project/module_names.hpp"

#include <algorithm>
#include <filesystem>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace dudu {
namespace {

int completion_kind(int symbol_kind) {
    switch (symbol_kind) {
    case 5:
        return 7;
    case 6:
        return 2;
    case 8:
        return 5;
    case 9:
        return 4;
    case 10:
        return 13;
    case 12:
        return 3;
    case 14:
        return 21;
    default:
        return 6;
    }
}

std::string completion_documentation(const std::string& label, const std::string& detail) {
    if (detail == "snippet") {
        return "Dudu snippet for `" + label + "`.";
    }
    if (detail == "keyword") {
        return "Dudu keyword `" + label + "`.";
    }
    if (detail == "type") {
        return "Built-in Dudu type `" + label + "`.";
    }
    if (!detail.empty()) {
        return detail;
    }
    return label;
}

void write_documentation(std::ostringstream& out, std::string_view documentation) {
    if (documentation.empty()) {
        return;
    }
    out << ",\"documentation\":{\"kind\":\"markdown\",\"value\":\"" << json_escape(documentation)
        << "\"}";
}

void write_completion_item(std::ostringstream& out, std::string_view label, int kind,
                           std::string_view detail, std::string_view documentation = {}) {
    out << "{\"label\":\"" << json_escape(label) << "\",\"kind\":" << kind;
    if (!detail.empty()) {
        out << ",\"detail\":\"" << json_escape(detail) << "\"";
    }
    write_documentation(out, documentation);
    out << "}";
}

std::string completion_items_json(const std::vector<Symbol>& symbols) {
    std::ostringstream out;
    out << "[";
    bool first = true;
    for (const Symbol& symbol : symbols) {
        if (!first) {
            out << ",";
        }
        first = false;
        out << "{\"label\":\"" << json_escape(symbol.name)
            << "\",\"kind\":" << completion_kind(symbol.kind) << ",\"detail\":\""
            << json_escape(symbol.detail) << "\"";
        write_documentation(out, symbol.doc_comment);
        out << "}";
    }
    out << "]";
    return out.str();
}

std::optional<std::string> module_completion_json(const ProjectIndex& index,
                                                  const ModuleAst& current,
                                                  const std::string& target) {
    for (const ImportDecl& import : current.imports) {
        if (import.kind != ImportKind::Module) {
            continue;
        }
        const std::string bound = bound_import_name(import);
        const bool matches = import.alias.empty()
                                 ? (target == import.module_path || target == bound)
                                 : target == bound;
        if (!matches) {
            continue;
        }
        const ModuleAst* imported = index.imported_unit(current, import);
        if (imported != nullptr) {
            return completion_items_json(symbols_for_module(*imported, true));
        }
        return "[]";
    }
    return std::nullopt;
}

const ProjectIndex* completion_index(const Document& doc) {
    try {
        return &project_index_for_document(doc, true);
    } catch (const std::exception&) {
    }
    return nullptr;
}

std::string member_completion_json(const ProjectIndex& index, const ModuleAst& current,
                                   const std::string& target, const Json* params) {
    if (const std::optional<std::string> module_result =
            module_completion_json(index, current, target)) {
        return *module_result;
    }
    if (std::vector<Symbol> class_members = class_member_symbols_for_owner(current, target);
        !class_members.empty()) {
        return completion_items_json(class_members);
    }
    const std::string prefix = target + ".";
    std::vector<Symbol> prefixed_symbols;
    for (Symbol symbol : symbols_for_module(current, true)) {
        if (symbol.name.rfind(prefix, 0) != 0 || symbol.name.size() == prefix.size()) {
            continue;
        }
        symbol.name = symbol.name.substr(prefix.size());
        prefixed_symbols.push_back(std::move(symbol));
    }
    if (!prefixed_symbols.empty()) {
        return completion_items_json(prefixed_symbols);
    }
    const TypeRef type_ref = local_type_ref_before_cursor(current, target, params);
    if (!has_type_ref(type_ref)) {
        return "[]";
    }
    const ModuleAst& module = index.merged_module();
    std::ostringstream out;
    out << "[";
    bool first = true;
    const auto add = [&](std::string_view label, int kind, std::string_view detail,
                         std::string_view documentation = {}) {
        if (!first) {
            out << ",";
        }
        first = false;
        write_completion_item(out, label, kind, detail, documentation);
    };
    const std::set<std::string> candidate_types = member_candidate_types(module, type_ref);
    for (const ClassDecl& klass : module.classes) {
        if (!candidate_types.contains(klass.name)) {
            continue;
        }
        for (const FieldDecl& field : klass.fields) {
            add(field.name, 5, field.name + ": " + type_ref_text(field.type_ref),
                field.doc_comment);
        }
        for (const ConstDecl& constant : klass.constants) {
            add(constant.name, 21, constant.name + ": " + type_ref_text(constant.type_ref),
                constant.doc_comment);
        }
        for (const ConstDecl& field : klass.static_fields) {
            add(field.name, 5, field.name + ": " + type_ref_text(field.type_ref),
                field.doc_comment);
        }
        for (const FunctionDecl& method : klass.methods) {
            add(method.name, is_constructor_method_name(method.name) ? 4 : 2,
                function_detail(method), method.doc_comment);
        }
        break;
    }
    for (const ClassDecl& klass : module.native_classes) {
        if (!candidate_types.contains(klass.name)) {
            continue;
        }
        for (const FieldDecl& field : klass.fields) {
            add(field.name, 5, field.name + ": " + type_ref_text(field.type_ref),
                field.doc_comment);
        }
        for (const FunctionDecl& method : klass.methods) {
            add(method.name, is_constructor_method_name(method.name) ? 4 : 2,
                function_detail(method), method.doc_comment);
        }
        break;
    }
    out << "]";
    return out.str();
}

struct CallSite {
    std::string name;
    int parameter = 0;
};

struct SignatureCandidate {
    std::string label;
    std::string documentation;
};

void add_constructor_signature_candidates(std::vector<SignatureCandidate>& signatures,
                                          const ModuleAst& current, const std::string& call_name) {
    const auto add_from_classes = [&](const std::vector<ClassDecl>& classes) {
        for (const ClassDecl& klass : classes) {
            if (klass.name == call_name) {
                signatures.push_back({.label = constructor_detail(klass),
                                      .documentation = constructor_doc_comment(klass)});
            }
        }
    };
    add_from_classes(current.classes);
    add_from_classes(current.native_classes);
}

void add_member_signature_candidates(std::vector<SignatureCandidate>& signatures,
                                     const ProjectIndex& index, const ModuleAst& current,
                                     const CallSite& call, const Json* params) {
    const size_t dot = call.name.rfind('.');
    if (dot == std::string::npos || dot == 0 || dot + 1 >= call.name.size()) {
        return;
    }
    const std::string receiver = call.name.substr(0, dot);
    const std::string member = call.name.substr(dot + 1);
    for (const Symbol& symbol : class_member_symbols_for_owner(current, receiver)) {
        if (symbol.name == member && (symbol.kind == lsp_symbol_kind::Method ||
                                      symbol.kind == lsp_symbol_kind::Constructor)) {
            signatures.push_back({.label = symbol.detail, .documentation = symbol.doc_comment});
        }
    }
    const TypeRef type_ref = local_type_ref_before_cursor(current, receiver, params);
    if (!has_type_ref(type_ref)) {
        return;
    }
    const ModuleAst& module = index.merged_module();
    const std::set<std::string> candidate_types = member_candidate_types(module, type_ref);
    const auto add_from_classes = [&](const std::vector<ClassDecl>& classes) {
        for (const ClassDecl& klass : classes) {
            if (!candidate_types.contains(klass.name)) {
                continue;
            }
            for (const FunctionDecl& method : klass.methods) {
                if (method.name == member) {
                    signatures.push_back(
                        {.label = function_detail(method), .documentation = method.doc_comment});
                }
            }
        }
    };
    add_from_classes(module.classes);
    add_from_classes(module.native_classes);
}

bool signature_token_before_cursor(const Token& token, int line, int character) {
    if (token.kind == TokenKind::Newline || token.kind == TokenKind::Indent ||
        token.kind == TokenKind::Dedent || token.kind == TokenKind::End) {
        return false;
    }
    if (token.location.line - 1 != line) {
        return false;
    }
    return token.location.column - 1 < character;
}

std::vector<Token> signature_tokens_before_cursor(const Document& doc, int line, int character) {
    std::vector<Token> out;
    for (const Token& token : lex_source(doc.text, doc.path)) {
        if (signature_token_before_cursor(token, line, character)) {
            out.push_back(token);
        }
    }
    return out;
}

std::string call_name_before_open_paren(const std::vector<Token>& tokens, size_t open_index) {
    if (open_index == 0) {
        return {};
    }
    size_t name_end = open_index - 1;
    if (tokens[name_end].kind == TokenKind::RBracket) {
        int bracket_depth = 0;
        std::optional<size_t> template_open;
        for (size_t index = name_end + 1; index-- > 0;) {
            if (tokens[index].kind == TokenKind::RBracket) {
                ++bracket_depth;
            } else if (tokens[index].kind == TokenKind::LBracket) {
                --bracket_depth;
                if (bracket_depth == 0) {
                    template_open = index;
                    break;
                }
            }
        }
        if (!template_open || *template_open == 0) {
            return {};
        }
        name_end = *template_open - 1;
    }
    if (tokens[name_end].kind != TokenKind::Identifier) {
        return {};
    }
    size_t start = name_end;
    while (start >= 2 && tokens[start - 1].kind == TokenKind::Dot &&
           tokens[start - 2].kind == TokenKind::Identifier) {
        start -= 2;
    }
    std::string out;
    for (size_t i = start; i <= name_end; ++i) {
        if (tokens[i].kind != TokenKind::Identifier && tokens[i].kind != TokenKind::Dot) {
            return {};
        }
        out += tokens[i].text;
    }
    return out;
}

CallSite call_site_at(const Document& doc, const Json* params) {
    const LspPosition position = lsp_position(params);
    const std::vector<Token> tokens =
        signature_tokens_before_cursor(doc, position.line, position.character);
    int depth = 0;
    int parameter = 0;
    for (size_t index = tokens.size(); index-- > 0;) {
        const Token& token = tokens[index];
        if (token.kind == TokenKind::RParen) {
            ++depth;
            continue;
        }
        if (token.kind == TokenKind::LParen) {
            if (depth > 0) {
                --depth;
                continue;
            }
            return {.name = call_name_before_open_paren(tokens, index), .parameter = parameter};
        }
        if (token.kind == TokenKind::Comma && depth == 0) {
            ++parameter;
        }
    }
    return {};
}

} // namespace

std::string completion_json(const Document* doc, const Json* params) {
    const ProjectIndex* index = nullptr;
    const ModuleAst* current = nullptr;
    if (doc != nullptr) {
        index = completion_index(*doc);
        if (index != nullptr) {
            current = &index->visible_unit_for_path(doc->path);
        }
    }
    if (doc != nullptr) {
        if (const std::optional<std::string> member_target =
                member_completion_target(*doc, params)) {
            if (index != nullptr && current != nullptr) {
                return member_completion_json(*index, *current, *member_target, params);
            }
            return "[]";
        }
    }
    std::ostringstream out;
    out << "[";
    bool first = true;
    const auto add = [&](std::string_view label, int kind, std::string_view detail,
                         std::string_view documentation = {}) {
        if (!first) {
            out << ",";
        }
        first = false;
        write_completion_item(out, label, kind, detail, documentation);
    };
    const auto add_snippet = [&](std::string_view label, std::string_view detail,
                                 std::string_view insert_text) {
        if (!first) {
            out << ",";
        }
        first = false;
        out << "{\"label\":\"" << json_escape(label) << "\",\"kind\":15,\"detail\":\""
            << json_escape(detail) << "\",\"insertText\":\"" << json_escape(insert_text)
            << "\",\"insertTextFormat\":2}";
    };
    for (std::string_view keyword :
         {"class", "def", "enum", "import", "return", "for", "if", "elif", "else", "while", "try",
          "except", "True", "False", "None"}) {
        add(keyword, 14, "keyword");
    }
    add_snippet("def", "snippet", "def ${1:name}(${2:args}) -> ${3:i32}:\n    ${0:return 0}");
    add_snippet("class", "snippet", "class ${1:Name}:\n    ${0:field: i32}");
    add_snippet("if", "snippet", "if ${1:condition}:\n    ${0:pass}");
    add_snippet("for", "snippet", "for ${1:item} in ${2:items}:\n    ${0:pass}");
    add_snippet("while", "snippet", "while ${1:condition}:\n    ${0:pass}");
    add_snippet("enum", "snippet", "enum ${1:Name}:\n    ${0:VALUE}");
    add_snippet("import", "snippet", "import ${1:module}");
    add_snippet("from", "snippet", "from ${1:module} import ${2:symbol}");
    add_snippet("try", "snippet",
                "try:\n    ${1:pass}\nexcept ${2:Exception} as ${3:error}:\n    ${0:pass}");
    add_snippet("except", "snippet", "except ${1:Exception} as ${2:error}:\n    ${0:pass}");
    for (std::string_view type : {"bool", "char", "i8", "i16", "i32", "i64", "u8", "u16", "u32",
                                  "u64", "isize", "usize", "f32", "f64", "str", "cstr"}) {
        add(type, 25, "type");
    }
    if (doc != nullptr && current != nullptr) {
        for (const auto& [name, type_ref] : local_type_refs_before_cursor(*current, params)) {
            add(name, 6, name + ": " + substitute_type_ref_text(type_ref, {}));
        }
        for (const Symbol& symbol : symbols_for_module(*current, true)) {
            add(symbol.name, completion_kind(symbol.kind), symbol.detail, symbol.doc_comment);
        }
    }
    out << "]";
    return out.str();
}

std::string completion_resolve_json(const Json* params) {
    const std::string label =
        params == nullptr ? std::string{} : string_value(params->get("label"));
    const int kind = optional_int_value(params == nullptr ? nullptr : params->get("kind"));
    const std::string detail =
        params == nullptr ? std::string{} : string_value(params->get("detail"));
    std::ostringstream out;
    out << "{\"label\":\"" << json_escape(label) << "\",\"kind\":" << kind;
    if (!detail.empty()) {
        out << ",\"detail\":\"" << json_escape(detail) << "\"";
    }
    if (const Json* insert_text = params == nullptr ? nullptr : params->get("insertText");
        insert_text != nullptr && insert_text->string() != nullptr) {
        out << ",\"insertText\":\"" << json_escape(*insert_text->string()) << "\"";
    }
    if (const int insert_format =
            optional_int_value(params == nullptr ? nullptr : params->get("insertTextFormat"));
        insert_format != 0) {
        out << ",\"insertTextFormat\":" << insert_format;
    }
    if (const Json* documentation = params == nullptr ? nullptr : params->get("documentation");
        documentation != nullptr && documentation->object() != nullptr) {
        if (const Json* value = documentation->get("value");
            value != nullptr && value->string() != nullptr && !value->string()->empty()) {
            out << ",\"documentation\":{\"kind\":\"markdown\",\"value\":\""
                << json_escape(*value->string()) << "\"}";
        } else {
            out << ",\"documentation\":{\"kind\":\"markdown\",\"value\":\""
                << json_escape(completion_documentation(label, detail)) << "\"}";
        }
    } else {
        out << ",\"documentation\":{\"kind\":\"markdown\",\"value\":\""
            << json_escape(completion_documentation(label, detail)) << "\"}";
    }
    out << "}";
    return out.str();
}

std::string signature_help_json(const Document* doc, const Json* params) {
    if (doc == nullptr) {
        return "{\"signatures\":[],\"activeSignature\":0,\"activeParameter\":0}";
    }
    const CallSite call = call_site_at(*doc, params);
    if (call.name.empty()) {
        return "{\"signatures\":[],\"activeSignature\":0,\"activeParameter\":0}";
    }
    std::vector<SignatureCandidate> signatures;
    if (const ProjectIndex* index = completion_index(*doc)) {
        const ModuleAst& current = index->visible_unit_for_path(doc->path);
        add_member_signature_candidates(signatures, *index, current, call, params);
        add_constructor_signature_candidates(signatures, current, call.name);
        for (const Symbol& symbol : symbols_for_module(current, true)) {
            if (symbol_matches(symbol.name, call.name) &&
                (symbol.kind == lsp_symbol_kind::Function ||
                 symbol.kind == lsp_symbol_kind::Method)) {
                signatures.push_back({.label = symbol.detail, .documentation = symbol.doc_comment});
            }
        }
    }
    std::ostringstream out;
    out << "{\"signatures\":[";
    for (size_t i = 0; i < signatures.size(); ++i) {
        if (i > 0) {
            out << ",";
        }
        out << "{\"label\":\"" << json_escape(signatures[i].label) << "\"";
        write_documentation(out, signatures[i].documentation);
        out << "}";
    }
    out << "],\"activeSignature\":0,\"activeParameter\":" << call.parameter << "}";
    return out.str();
}

} // namespace dudu
