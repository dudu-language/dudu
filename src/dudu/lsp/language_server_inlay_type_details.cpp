#include "dudu/lsp/language_server_inlay_type_details.hpp"

#include "dudu/core/ast_type.hpp"
#include "dudu/lsp/language_server_json.hpp"
#include "dudu/lsp/language_server_navigation.hpp"
#include "dudu/lsp/language_server_type_layout.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <optional>
#include <sstream>

namespace dudu {
namespace {

std::optional<std::string> builtin_type_tooltip(const std::string& name) {
    if (name == "slice") {
        return "`slice`\n\nIndex slice value produced by `start:stop[:step]` inside `[]`.";
    }
    if (name == "ellipsis") {
        return "`ellipsis`\n\nIndex item produced by `...` inside `[]`.";
    }
    if (name == "new_axis") {
        return "`new_axis`\n\nIndex item produced by `None` inside `[]`.";
    }
    if (name == "scalar_index") {
        return "`scalar_index`\n\nIndex-category type for scalar integer indices.";
    }
    if (name == "basic_index") {
        return "`basic_index`\n\nIndex-category type for scalar indices, slices, ellipsis, "
               "and new-axis items.";
    }
    if (name == "array_view") {
        return "`array_view[T]`\n\nRank-independent non-owning view produced by fixed-array "
               "slicing.";
    }
    return std::nullopt;
}

std::string fenced_code(std::string_view language, const std::string& code) {
    return "```" + std::string(language) + "\n" + code + "\n```";
}

std::string class_preview(const ClassDecl& klass, bool native) {
    std::ostringstream out;
    out << (native ? "native class " : "class ") << klass.name << ":";
    constexpr size_t max_fields = 5;
    size_t shown = 0;
    for (const FieldDecl& field : klass.fields) {
        if (shown >= max_fields) {
            break;
        }
        out << "\n    " << field.name << ": " << type_ref_text(field.type_ref);
        ++shown;
    }
    if (klass.fields.size() > shown) {
        out << "\n    # ... " << klass.fields.size() - shown << " more";
    } else if (klass.fields.empty()) {
        out << "\n    pass";
    }
    return out.str();
}

const ClassDecl* class_for_type_name(const Symbols& symbols, const std::string& name,
                                     bool& native) {
    if (const auto found = symbols.classes.find(name); found != symbols.classes.end()) {
        native = native_class_decl_for_binding(symbols, name) != nullptr &&
                 std::filesystem::path(found->second->location.file.str()).extension() != ".dd";
        return found->second;
    }
    return nullptr;
}

const NativeTypeDecl* native_type_for_name(const Symbols& symbols, const std::string& name) {
    return native_type_decl_for_binding(symbols, name);
}

std::string type_token_tooltip(const Symbols& symbols, const std::string& name) {
    bool native = false;
    const ClassDecl* klass = class_for_type_name(symbols, name, native);
    if (klass == nullptr) {
        if (const NativeTypeDecl* type = native_type_for_name(symbols, name)) {
            std::string markdown = fenced_code("cpp", type->native_spelling.empty()
                                                          ? std::string("native type ") + type->name
                                                          : "native type " + type->name + " = " +
                                                                native_type_alias_type_text(*type));
            if (type->layout) {
                markdown += "\n\nsize = " + std::to_string(type->layout->size) +
                            " bytes, align = " + std::to_string(type->layout->alignment) + " bytes";
            }
            if (!type->doc_comment.empty()) {
                markdown += "\n\n" + type->doc_comment;
            }
            return markdown;
        }
        if (const std::optional<TypeLayout> layout = primitive_type_layout(name)) {
            return "`" + name + "`\n\nsize = " + std::to_string(layout->size) +
                   " bytes, align = " + std::to_string(layout->alignment) + " bytes";
        }
        if (const std::optional<std::string> builtin = builtin_type_tooltip(name)) {
            return *builtin;
        }
        return {};
    }
    std::string markdown = fenced_code(native ? "cpp" : "dudu", class_preview(*klass, native));
    if (const std::optional<TypeLayout> layout = resolved_class_layout(symbols, *klass)) {
        markdown += "\n\nsize = " + std::to_string(layout->size) +
                    " bytes, align = " + std::to_string(layout->alignment) + " bytes";
    }
    if (!klass->doc_comment.empty()) {
        markdown += "\n\n" + klass->doc_comment;
    }
    return markdown;
}

SourceLocation type_token_location(const Symbols& symbols, const std::string& name) {
    bool native = false;
    const ClassDecl* klass = class_for_type_name(symbols, name, native);
    if (klass != nullptr) {
        return klass->location;
    }
    if (const NativeTypeDecl* type = native_type_for_name(symbols, name)) {
        return type->location;
    }
    return {};
}

bool token_char(char ch) {
    return std::isalnum(static_cast<unsigned char>(ch)) != 0 || ch == '_' || ch == '.';
}

std::vector<InlayLabelPart> label_parts_for_type(const Document&, const Symbols& symbols,
                                                 const std::string& label) {
    std::vector<InlayLabelPart> parts;
    size_t pos = 0;
    while (pos < label.size()) {
        if (!token_char(label[pos]) || std::isdigit(static_cast<unsigned char>(label[pos])) != 0) {
            parts.push_back({.value = std::string(1, label[pos]), .tooltip = {}, .location = {}});
            ++pos;
            continue;
        }
        const size_t start = pos;
        while (pos < label.size() && token_char(label[pos])) {
            ++pos;
        }
        std::string token = label.substr(start, pos - start);
        while (!token.empty() && token.back() == '.') {
            --pos;
            token.pop_back();
        }
        if (!token.empty()) {
            parts.push_back({.value = token,
                             .tooltip = type_token_tooltip(symbols, token),
                             .location = type_token_location(symbols, token)});
        }
    }
    return parts;
}

bool has_part_metadata(const std::vector<InlayLabelPart>& parts) {
    return std::ranges::any_of(parts, [](const InlayLabelPart& part) {
        return !part.tooltip.empty() || !part.location.file.empty();
    });
}

} // namespace

InlayTypeDetail inlay_type_detail(const Document& doc, const Symbols& symbols, const TypeRef& type,
                                  std::string prefix) {
    InlayTypeDetail detail;
    detail.label = std::move(prefix) + type_ref_text(type);
    detail.tooltip_markdown = fenced_code("dudu", type_ref_text(type));
    if (const std::optional<TypeLayout> layout = resolved_type_layout(symbols, type)) {
        detail.tooltip_markdown += "\n\nsize = " + std::to_string(layout->size) +
                                   " bytes, align = " + std::to_string(layout->alignment) +
                                   " bytes";
    }
    detail.label_parts = label_parts_for_type(doc, symbols, detail.label);
    if (!has_part_metadata(detail.label_parts)) {
        detail.label_parts.clear();
    }
    return detail;
}

std::string inlay_label_json(const InlayTypeDetail& detail, const Document& doc) {
    if (detail.label_parts.empty()) {
        return "\"" + json_escape(detail.label) + "\"";
    }
    std::ostringstream out;
    out << "[";
    for (size_t i = 0; i < detail.label_parts.size(); ++i) {
        const InlayLabelPart& part = detail.label_parts[i];
        if (i > 0) {
            out << ",";
        }
        out << "{\"value\":\"" << json_escape(part.value) << "\"";
        if (!part.tooltip.empty()) {
            out << ",\"tooltip\":{\"kind\":\"markdown\",\"value\":\"" << json_escape(part.tooltip)
                << "\"}";
        }
        if (!part.location.file.empty()) {
            out << ",\"location\":"
                << location_json(uri_for_location(part.location, doc), range_json(part.location));
        }
        out << "}";
    }
    out << "]";
    return out.str();
}

std::string inlay_tooltip_json(const InlayTypeDetail& detail) {
    if (detail.tooltip_markdown.empty()) {
        return {};
    }
    return "{\"kind\":\"markdown\",\"value\":\"" + json_escape(detail.tooltip_markdown) + "\"}";
}

} // namespace dudu
