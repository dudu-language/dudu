#include "dudu/lsp/language_server_type_hover.hpp"

#include "dudu/core/ast_type.hpp"
#include "dudu/core/decorators.hpp"
#include "dudu/lsp/language_server_json.hpp"
#include "dudu/lsp/language_server_navigation.hpp"
#include "dudu/lsp/language_server_symbols.hpp"

#include <algorithm>
#include <optional>
#include <sstream>
#include <string>

namespace dudu {
namespace {

struct LayoutInfo {
    size_t size = 0;
    size_t align = 1;
};

size_t align_up(size_t value, size_t align) {
    return align == 0 ? value : ((value + align - 1) / align) * align;
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
    if (type.kind == TypeKind::Const || type.kind == TypeKind::Volatile ||
        type.kind == TypeKind::Static || type.kind == TypeKind::Device ||
        type.kind == TypeKind::Storage || type.kind == TypeKind::Shared) {
        if (type.children.size() != 1) {
            return std::nullopt;
        }
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
    (void)module;
    std::string markdown = fenced_code(native ? "cpp" : "dudu", class_definition_preview(klass, native));
    if (!native) {
        if (const std::optional<LayoutInfo> layout = class_layout(klass)) {
            markdown += "\n\nsize = " + std::to_string(layout->size) + " bytes, align = " +
                        std::to_string(layout->align) + " bytes";
        }
    }
    if (!klass.doc_comment.empty()) {
        markdown += "\n\n" + klass.doc_comment;
    }
    return "{\"contents\":{\"kind\":\"markdown\",\"value\":\"" + json_escape(markdown) +
           "\"},\"range\":" + range_json(klass.location) + "}";
}

} // namespace dudu
