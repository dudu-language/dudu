#include "dudu/lsp/language_server_signature_help.hpp"

#include "dudu/core/ast_type.hpp"
#include "dudu/lsp/language_server_call_site.hpp"
#include "dudu/lsp/language_server_class_members.hpp"
#include "dudu/lsp/language_server_json.hpp"
#include "dudu/lsp/language_server_local_context.hpp"
#include "dudu/lsp/language_server_macros.hpp"
#include "dudu/lsp/language_server_navigation.hpp"
#include "dudu/lsp/language_server_support.hpp"
#include "dudu/lsp/language_server_symbols.hpp"
#include "dudu/sema/sema_context.hpp"
#include "dudu/sema/sema_function_type.hpp"

#include <exception>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace dudu {
namespace {

struct SignatureCandidate {
    std::string label;
    std::string documentation;
};

const ProjectIndex* signature_index(const Document& doc) {
    try {
        return &project_index_for_document(doc, true);
    } catch (const std::exception&) {
        return nullptr;
    }
}

void add_constructor_candidates(std::vector<SignatureCandidate>& signatures,
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
            signatures.push_back({.label = symbol.detail, .documentation = symbol.doc_comment});
        }
    }
    const TypeRef type_ref = local_type_ref_before_cursor(current, receiver, params);
    if (!has_type_ref(type_ref)) {
        return;
    }
    const std::set<std::string> candidate_types = member_candidate_types(current, type_ref);
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
    add_from_classes(current.classes);
    add_from_classes(current.native_classes);
}

} // namespace

std::string signature_help_json(const Document* doc, const Json* params) {
    if (doc == nullptr) {
        return "{\"signatures\":[],\"activeSignature\":0,\"activeParameter\":0}";
    }
    const LspCallSite call = lsp_call_site_at(*doc, params);
    if (call.name.empty()) {
        return "{\"signatures\":[],\"activeSignature\":0,\"activeParameter\":0}";
    }
    std::vector<SignatureCandidate> signatures;
    if (const ProjectIndex* index = signature_index(*doc)) {
        const ModuleAst& current = index->visible_unit_for_path(doc->path);
        const std::optional<MacroEditorCall> macro =
            macro_call_for_reference(*index, current, call.name);
        if (macro) {
            signatures.push_back(
                {.label = macro->signature, .documentation = macro->documentation});
        }
        const Symbols semantic_symbols = collect_symbols(current);
        add_member_candidates(signatures, current, call, params);
        add_constructor_candidates(signatures, current, call.name);
        for (const Symbol& symbol : symbols_for_module(current, true)) {
            if (!macro && symbol_matches(symbol.name, call.name) &&
                (symbol.kind == lsp_symbol_kind::Function ||
                 symbol.kind == lsp_symbol_kind::Method)) {
                signatures.push_back({.label = symbol.detail, .documentation = symbol.doc_comment});
            }
        }
        for (const NativeValueDecl& value : current.native_values) {
            if (!symbol_matches(value.name, call.name)) {
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
            signatures.push_back({.label = std::move(label), .documentation = value.doc_comment});
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
        out << "}";
    }
    out << "],\"activeSignature\":0,\"activeParameter\":" << call.parameter << "}";
    return out.str();
}

} // namespace dudu
