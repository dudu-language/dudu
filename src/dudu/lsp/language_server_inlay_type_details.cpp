#include "dudu/lsp/language_server_inlay_type_details.hpp"

#include "dudu/core/ast_type.hpp"
#include "dudu/core/decorators.hpp"
#include "dudu/lsp/language_server_json.hpp"
#include "dudu/lsp/language_server_navigation.hpp"

#include <algorithm>
#include <cctype>
#include <optional>
#include <sstream>

namespace dudu {
namespace {

struct LayoutInfo {
    size_t size = 0;
    size_t align = 1;
};

size_t align_up(size_t value, size_t align) {
    return align == 0 ? value : ((value + align - 1) / align) * align;
}

std::optional<LayoutInfo> primitive_layout(const std::string& name) {
    if (name == "bool" || name == "i8" || name == "u8") {
        return LayoutInfo{.size = 1, .align = 1};
    }
    if (name == "i16" || name == "u16") {
        return LayoutInfo{.size = 2, .align = 2};
    }
    if (name == "i32" || name == "u32" || name == "f32") {
        return LayoutInfo{.size = 4, .align = 4};
    }
    if (name == "i64" || name == "u64" || name == "isize" || name == "usize" || name == "f64") {
        return LayoutInfo{.size = 8, .align = 8};
    }
    return std::nullopt;
}

std::optional<size_t> align_decorator_value(const ClassDecl& klass) {
    for (const Decorator& decorator : klass.decorators) {
        const std::optional<std::string> value = decorator_first_arg_display(decorator, "align");
        if (!value) {
            continue;
        }
        try {
            const size_t parsed = static_cast<size_t>(std::stoull(*value));
            return parsed == 0 ? std::nullopt : std::optional<size_t>{parsed};
        } catch (const std::exception&) {
            return std::nullopt;
        }
    }
    return std::nullopt;
}

std::optional<LayoutInfo> type_layout(const TypeRef& type) {
    if (!has_type_ref(type)) {
        return std::nullopt;
    }
    if (type.kind == TypeKind::Named || type.kind == TypeKind::Qualified) {
        return primitive_layout(type_ref_text(type));
    }
    if (type.kind == TypeKind::Pointer || type.kind == TypeKind::Reference) {
        return LayoutInfo{.size = 8, .align = 8};
    }
    if ((type.kind == TypeKind::Const || type.kind == TypeKind::Volatile ||
         type.kind == TypeKind::Static || type.kind == TypeKind::Device ||
         type.kind == TypeKind::Storage || type.kind == TypeKind::Shared) &&
        type.children.size() == 1) {
        return type_layout(type.children.front());
    }
    if (type.kind == TypeKind::FixedArray && type.children.size() == 2) {
        const std::optional<LayoutInfo> element = type_layout(type.children.front());
        if (!element || type.children[1].value.empty()) {
            return std::nullopt;
        }
        try {
            const size_t count = static_cast<size_t>(std::stoull(type.children[1].value));
            return LayoutInfo{.size = element->size * count, .align = element->align};
        } catch (const std::exception&) {
            return std::nullopt;
        }
    }
    return std::nullopt;
}

std::optional<LayoutInfo> class_layout(const ClassDecl& klass) {
    size_t size = 0;
    size_t align = 1;
    for (const FieldDecl& field : klass.fields) {
        const std::optional<LayoutInfo> field_layout = type_layout(field.type_ref);
        if (!field_layout) {
            return std::nullopt;
        }
        size = align_up(size, field_layout->align);
        size += field_layout->size;
        align = std::max(align, field_layout->align);
    }
    if (const std::optional<size_t> explicit_align = align_decorator_value(klass)) {
        align = std::max(align, *explicit_align);
    }
    return LayoutInfo{.size = align_up(std::max<size_t>(size, 1), align), .align = align};
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
        native = false;
        return found->second;
    }
    if (const auto found = symbols.native_classes.find(name);
        found != symbols.native_classes.end()) {
        native = true;
        return &found->second;
    }
    return nullptr;
}

std::string type_token_tooltip(const Symbols& symbols, const std::string& name) {
    bool native = false;
    const ClassDecl* klass = class_for_type_name(symbols, name, native);
    if (klass == nullptr) {
        if (const std::optional<LayoutInfo> layout = primitive_layout(name)) {
            return "`" + name + "`\n\nsize = " + std::to_string(layout->size) +
                   " bytes, align = " + std::to_string(layout->align) + " bytes";
        }
        return {};
    }
    std::string markdown = fenced_code(native ? "cpp" : "dudu", class_preview(*klass, native));
    if (!native) {
        if (const std::optional<LayoutInfo> layout = class_layout(*klass)) {
            markdown += "\n\nsize = " + std::to_string(layout->size) +
                        " bytes, align = " + std::to_string(layout->align) + " bytes";
        }
    }
    if (!klass->doc_comment.empty()) {
        markdown += "\n\n" + klass->doc_comment;
    }
    return markdown;
}

SourceLocation type_token_location(const Symbols& symbols, const std::string& name) {
    bool native = false;
    const ClassDecl* klass = class_for_type_name(symbols, name, native);
    return klass == nullptr ? SourceLocation{} : klass->location;
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
    if (const std::optional<LayoutInfo> layout = type_layout(type)) {
        detail.tooltip_markdown += "\n\nsize = " + std::to_string(layout->size) +
                                   " bytes, align = " + std::to_string(layout->align) + " bytes";
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
