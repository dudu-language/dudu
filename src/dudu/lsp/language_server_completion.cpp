#include "dudu/lsp/language_server_completion.hpp"

#include "dudu/core/ast_type.hpp"
#include "dudu/lsp/language_server_call_site.hpp"
#include "dudu/lsp/language_server_class_members.hpp"
#include "dudu/lsp/language_server_json.hpp"
#include "dudu/lsp/language_server_local_context.hpp"
#include "dudu/lsp/language_server_macros.hpp"
#include "dudu/lsp/language_server_navigation.hpp"
#include "dudu/lsp/language_server_support.hpp"
#include "dudu/lsp/language_server_symbols.hpp"
#include "dudu/project/module_names.hpp"

#include <algorithm>
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
    case lsp_symbol_kind::Module:
    case lsp_symbol_kind::Namespace:
        return 9;
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
    case lsp_symbol_kind::EnumMember:
        return 20;
    case lsp_symbol_kind::Struct:
        return 22;
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

void write_completion_item(std::ostringstream& out, std::string_view label, int kind,
                           std::string_view detail, std::string_view documentation = {}) {
    out << "{\"label\":\"" << json_escape(label) << "\",\"kind\":" << kind;
    if (!detail.empty()) {
        out << ",\"detail\":\"" << json_escape(detail) << "\"";
    }
    write_markdown_documentation(out, documentation);
    out << "}";
}

struct CompletionAccumulator {
    std::ostringstream out;
    bool first = true;
    std::set<std::string> labels;

    CompletionAccumulator() {
        out << "[";
    }

    void add(std::string_view label, int kind, std::string_view detail,
             std::string_view documentation = {}) {
        if (!labels.insert(std::string(label)).second) {
            return;
        }
        if (!first) {
            out << ",";
        }
        first = false;
        write_completion_item(out, label, kind, detail, documentation);
    }

    std::string json() {
        out << "]";
        return out.str();
    }
};

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
        write_markdown_documentation(out, symbol.doc_comment);
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

std::string enum_value_completion_json(const ModuleAst& module, const std::string& target) {
    const TypeRef target_ref = named_type_ref(target);
    const std::set<std::string> candidate_types = member_candidate_types(module, target_ref);
    std::set<std::string> qualified_candidates;
    for (const std::string& candidate : candidate_types) {
        if (candidate.find('.') != std::string::npos) {
            qualified_candidates.insert(candidate);
        }
    }
    CompletionAccumulator completions;
    const auto add_enum_values = [&](const EnumDecl& en) {
        const std::string qualified =
            en.origin_module.empty() ? en.name : en.origin_module + "." + en.name;
        for (const EnumValueDecl& value : en.values) {
            completions.add(value.name, 20, "enum variant " + qualified + "." + value.name,
                            value.doc_comment);
        }
    };
    for (const EnumDecl& en : module.enums) {
        const std::string qualified =
            en.origin_module.empty() ? en.name : en.origin_module + "." + en.name;
        if (qualified_candidates.contains(qualified)) {
            add_enum_values(en);
        }
    }
    if (!completions.labels.empty() || !qualified_candidates.empty()) {
        return completions.json();
    }
    for (const EnumDecl& en : module.enums) {
        if (candidate_types.contains(en.name)) {
            add_enum_values(en);
        }
    }
    return completions.json();
}

ProjectIndexSnapshot completion_index(const Document& doc) {
    try {
        return project_index_for_document(doc, true);
    } catch (const std::exception&) {
    }
    return {};
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
    if (const std::string enum_values = enum_value_completion_json(index.merged_module(), target);
        enum_values != "[]") {
        return enum_values;
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
    const ModuleAst& module = current;
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

} // namespace

std::string completion_json(const Document* doc, const Json* params) {
    ProjectIndexSnapshot index;
    const ModuleAst* current = nullptr;
    if (doc != nullptr) {
        index = completion_index(*doc);
        if (index != nullptr) {
            current = &index->visible_unit_for_path(doc->path);
        }
    }
    if (doc != nullptr) {
        const LspCallSite call = lsp_call_site_at(*doc, params);
        if (index != nullptr && current != nullptr && !call.name.empty()) {
            if (const std::optional<MacroEditorCall> macro =
                    macro_call_for_reference(*index, *current, call.name)) {
                std::ostringstream macro_items;
                macro_items << "[";
                for (size_t i = 0; i < macro->options.size(); ++i) {
                    if (i > 0)
                        macro_items << ",";
                    const MacroEditorOption& option = macro->options[i];
                    macro_items << "{\"label\":\"" << json_escape(option.name)
                                << "\",\"kind\":5,\"detail\":\""
                                << json_escape(option.name + ": " + option.type)
                                << "\",\"insertText\":\"" << json_escape(option.name + "=${1}")
                                << "\",\"insertTextFormat\":2";
                    write_markdown_documentation(macro_items, option.documentation);
                    macro_items << "}";
                }
                macro_items << "]";
                return macro_items.str();
            }
        }
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

} // namespace dudu
