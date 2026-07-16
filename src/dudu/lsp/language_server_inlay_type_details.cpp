#include "dudu/lsp/language_server_inlay_type_details.hpp"

#include "dudu/core/ast_type.hpp"
#include "dudu/lsp/language_server_json.hpp"
#include "dudu/lsp/language_server_markdown.hpp"
#include "dudu/lsp/language_server_navigation.hpp"
#include "dudu/lsp/language_server_type_hover.hpp"
#include "dudu/lsp/language_server_type_layout.hpp"

#include <algorithm>
#include <cctype>
#include <optional>
#include <sstream>

namespace dudu {
namespace {

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
                             .tooltip = type_name_hover_markdown(symbols, token),
                             .location = type_name_definition_location(symbols, token)});
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
