#include "dudu/lsp/language_server_type_hover.hpp"

#include "dudu/core/ast_type.hpp"
#include "dudu/core/decorators.hpp"
#include "dudu/lsp/language_server_json.hpp"
#include "dudu/lsp/language_server_navigation.hpp"
#include "dudu/lsp/language_server_symbols.hpp"
#include "dudu/lsp/language_server_type_layout.hpp"
#include "dudu/sema/sema_context.hpp"

#include <sstream>
#include <string>

namespace dudu {
namespace {

std::string generic_params_label(const std::vector<std::string>& params) {
    if (params.empty()) {
        return {};
    }
    std::ostringstream out;
    out << "[";
    for (size_t i = 0; i < params.size(); ++i) {
        if (i > 0) {
            out << ", ";
        }
        out << params[i];
    }
    out << "]";
    return out.str();
}

std::string class_header(const ClassDecl& klass, bool native) {
    return std::string(native ? "native class " : "class ") + klass.name +
           generic_params_label(klass.generic_params);
}

std::string class_definition_preview(const ClassDecl& klass, bool native) {
    std::ostringstream out;
    out << class_header(klass, native) << ":";
    constexpr size_t max_fields = 5;
    size_t shown = 0;
    for (const FieldDecl& field : klass.fields) {
        if (shown >= max_fields) {
            break;
        }
        out << "\n    " << field.name << ": " << type_ref_text(field.type_ref);
        ++shown;
    }
    const size_t remaining = klass.fields.size() > shown ? klass.fields.size() - shown : 0;
    if (remaining > 0) {
        out << "\n    # ... " << remaining << " more field" << (remaining == 1 ? "" : "s");
    }
    if (klass.fields.empty()) {
        out << "\n    pass";
    }
    return out.str();
}

std::string fenced_code(std::string_view language, const std::string& code) {
    return "```" + std::string(language) + "\n" + code + "\n```";
}

} // namespace

std::string class_hover_json(const ModuleAst& module, const ClassDecl& klass, bool native) {
    std::string markdown =
        fenced_code(native ? "cpp" : "dudu", class_definition_preview(klass, native));
    const Symbols symbols = collect_symbols(module);
    if (const std::optional<TypeLayout> layout = resolved_class_layout(symbols, klass)) {
        markdown += "\n\nsize = " + std::to_string(layout->size) +
                    " bytes, align = " + std::to_string(layout->alignment) + " bytes";
    }
    if (!klass.doc_comment.empty()) {
        markdown += "\n\n" + klass.doc_comment;
    }
    return "{\"contents\":{\"kind\":\"markdown\",\"value\":\"" + json_escape(markdown) +
           "\"},\"range\":" + range_json(klass.location) + "}";
}

} // namespace dudu
