#include "dudu/language_server_completion.hpp"

#include "dudu/ast_type.hpp"
#include "dudu/language_server_json.hpp"
#include "dudu/language_server_local_context.hpp"
#include "dudu/language_server_navigation.hpp"
#include "dudu/language_server_support.hpp"
#include "dudu/language_server_symbols.hpp"
#include "dudu/lexer.hpp"
#include "dudu/native_headers.hpp"
#include "dudu/parser.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
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

std::optional<std::string> module_completion_json(const Document& doc, const std::string& target) {
    ModuleAst module;
    try {
        module = parse_source(doc.text, doc.path);
    } catch (const std::exception&) {
        return std::nullopt;
    }
    for (const ImportDecl& import : module.imports) {
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
        const std::filesystem::path file =
            module_path_to_file(doc.path.parent_path(), import.module_path);
        std::ifstream input(file);
        if (!input) {
            return "[]";
        }
        const std::string text{std::istreambuf_iterator<char>(input),
                               std::istreambuf_iterator<char>()};
        const Document imported{
            .uri = file_uri(file),
            .path = file,
            .text = text,
        };
        std::ostringstream out;
        out << "[";
        bool first = true;
        for (const Symbol& symbol : symbols_for_document(imported, false)) {
            if (!first) {
                out << ",";
            }
            first = false;
            out << "{\"label\":\"" << json_escape(symbol.name)
                << "\",\"kind\":" << completion_kind(symbol.kind) << ",\"detail\":\""
                << json_escape(symbol.detail) << "\"}";
        }
        out << "]";
        return out.str();
    }
    return std::nullopt;
}

std::string member_completion_json(const Document& doc, const std::string& target,
                                   const Json* params) {
    if (const std::optional<std::string> module_result = module_completion_json(doc, target)) {
        return *module_result;
    }
    const TypeRef type_ref = local_type_ref_before_cursor(doc, target, params);
    if (!has_type_ref(type_ref)) {
        return "[]";
    }
    std::ostringstream out;
    out << "[";
    bool first = true;
    const auto add = [&](std::string_view label, int kind, std::string_view detail) {
        if (!first) {
            out << ",";
        }
        first = false;
        out << "{\"label\":\"" << json_escape(label) << "\",\"kind\":" << kind << ",\"detail\":\""
            << json_escape(detail) << "\"}";
    };
    try {
        ModuleAst module = parse_source(doc.text, doc.path);
        const ProjectConfig config = config_for_file(doc.path);
        merge_native_header_types(module, {.config = config, .source_dir = doc.path.parent_path()});
        const std::set<std::string> candidate_types = member_candidate_types(module, type_ref);
        for (const ClassDecl& klass : module.classes) {
            if (!candidate_types.contains(klass.name)) {
                continue;
            }
            for (const FieldDecl& field : klass.fields) {
                add(field.name, 5, field.name + ": " + type_ref_text(field.type_ref));
            }
            for (const ConstDecl& constant : klass.constants) {
                add(constant.name, 21, constant.name + ": " + type_ref_text(constant.type_ref));
            }
            for (const ConstDecl& field : klass.static_fields) {
                add(field.name, 5, field.name + ": " + type_ref_text(field.type_ref));
            }
            for (const FunctionDecl& method : klass.methods) {
                add(method.name, is_constructor_method_name(method.name) ? 4 : 2,
                    function_detail(method));
            }
            break;
        }
        for (const ClassDecl& klass : module.native_classes) {
            if (!candidate_types.contains(klass.name)) {
                continue;
            }
            for (const FieldDecl& field : klass.fields) {
                add(field.name, 5, field.name + ": " + type_ref_text(field.type_ref));
            }
            for (const FunctionDecl& method : klass.methods) {
                add(method.name, is_constructor_method_name(method.name) ? 4 : 2,
                    function_detail(method));
            }
            break;
        }
    } catch (const std::exception&) {
    }
    out << "]";
    return out.str();
}

struct CallSite {
    std::string name;
    int parameter = 0;
};

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
    if (open_index == 0 || tokens[open_index - 1].kind != TokenKind::Identifier) {
        return {};
    }
    size_t start = open_index - 1;
    while (start >= 2 && tokens[start - 1].kind == TokenKind::Dot &&
           tokens[start - 2].kind == TokenKind::Identifier) {
        start -= 2;
    }
    std::string out;
    for (size_t i = start; i < open_index; ++i) {
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
    if (doc != nullptr) {
        if (const std::optional<std::string> member_target =
                member_completion_target(*doc, params)) {
            return member_completion_json(*doc, *member_target, params);
        }
    }
    std::ostringstream out;
    out << "[";
    bool first = true;
    const auto add = [&](std::string_view label, int kind, std::string_view detail) {
        if (!first) {
            out << ",";
        }
        first = false;
        out << "{\"label\":\"" << json_escape(label) << "\",\"kind\":" << kind << ",\"detail\":\""
            << json_escape(detail) << "\"}";
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
    for (std::string_view type : {"bool", "i8", "i16", "i32", "i64", "u8", "u16", "u32", "u64",
                                  "isize", "usize", "f32", "f64", "str", "cstr"}) {
        add(type, 25, "type");
    }
    if (doc != nullptr) {
        for (const auto& [name, type_ref] : local_type_refs_before_cursor(*doc, params)) {
            add(name, 6, name + ": " + substitute_type_ref_text(type_ref, {}));
        }
        for (const Symbol& symbol : symbols_for_document(*doc)) {
            add(symbol.name, completion_kind(symbol.kind), symbol.detail);
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
    out << ",\"documentation\":{\"kind\":\"markdown\",\"value\":\""
        << json_escape(completion_documentation(label, detail)) << "\"}}";
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
    std::vector<std::string> signatures;
    for (const Symbol& symbol : symbols_for_document(*doc)) {
        if (symbol_matches(symbol.name, call.name) && (symbol.kind == 12 || symbol.kind == 6)) {
            signatures.push_back(symbol.detail);
        }
    }
    std::ostringstream out;
    out << "{\"signatures\":[";
    for (size_t i = 0; i < signatures.size(); ++i) {
        if (i > 0) {
            out << ",";
        }
        out << "{\"label\":\"" << json_escape(signatures[i]) << "\"}";
    }
    out << "],\"activeSignature\":0,\"activeParameter\":" << call.parameter << "}";
    return out.str();
}

} // namespace dudu
