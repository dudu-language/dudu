#include "dudu/ast_parse_utils.hpp"

#include <string>
#include <vector>

namespace dudu {
std::vector<TypeRef> parse_type_list(std::string_view text, SourceLocation location) {
    std::vector<TypeRef> out;
    for (const std::string_view part : split_top_level_commas(text)) {
        if (!trim_view(part).empty()) {
            const size_t offset = static_cast<size_t>(part.data() - text.data());
            out.push_back(parse_type_text(part, advance_columns(location, offset)));
        }
    }
    return out;
}
TypeRef parse_type_text(std::string_view text, SourceLocation location) {
    text = trim_view(text);
    if (text.empty()) {
        return make_type(TypeKind::Unknown, text, location);
    }
    const size_t arrow = find_top_level_arrow(text);
    if (arrow != std::string_view::npos) {
        TypeRef type = make_type(TypeKind::Function, text, location);
        type.children.push_back(
            parse_type_text(text.substr(arrow + 2), advance_columns(location, arrow + 2)));
        std::string_view params = trim_view(text.substr(0, arrow));
        size_t params_offset = 0;
        if (params.starts_with("fn(") && params.ends_with(")")) {
            params_offset = 3;
            params = params.substr(3, params.size() - 4);
        } else if (enclosed_by_outer_pair(params, '(', ')')) {
            params_offset = 1;
            params = params.substr(1, params.size() - 2);
        }
        std::vector<TypeRef> parsed_params =
            parse_type_list(params, advance_columns(location, params_offset));
        type.children.insert(type.children.end(), parsed_params.begin(), parsed_params.end());
        return type;
    }
    if (text.starts_with("fn(") && text.ends_with(")")) {
        TypeRef type = make_type(TypeKind::Function, text, location);
        type.children.push_back(parse_type_text("void", location));
        std::string_view params = text.substr(3, text.size() - 4);
        std::vector<TypeRef> parsed_params = parse_type_list(params, advance_columns(location, 3));
        type.children.insert(type.children.end(), parsed_params.begin(), parsed_params.end());
        return type;
    }
    if (text.front() == '*') {
        TypeRef type = make_type(TypeKind::Pointer, text, location);
        type.children.push_back(parse_type_text(text.substr(1), advance_columns(location, 1)));
        return type;
    }
    if (text.front() == '&') {
        TypeRef type = make_type(TypeKind::Reference, text, location);
        type.children.push_back(parse_type_text(text.substr(1), advance_columns(location, 1)));
        return type;
    }
    if (is_integer_literal(text)) {
        TypeRef type = make_type(TypeKind::Value, text, location);
        type.value = std::string(text);
        return type;
    }
    if (text.ends_with("]")) {
        const size_t open = find_matching_open(text, text.size() - 1, '[', ']');
        if (open != std::string_view::npos && open > 0) {
            const std::string_view head = trim_view(text.substr(0, open));
            const std::string_view inner = text.substr(open + 1, text.size() - open - 2);
            const TypeKind wrapper = wrapper_type_kind(head);
            if (wrapper != TypeKind::Unknown) {
                TypeRef type = make_type(wrapper, text, location);
                type.children.push_back(
                    parse_type_text(inner, advance_columns(location, open + 1)));
                return type;
            }
            if (head.ends_with("]")) {
                TypeRef type = make_type(TypeKind::FixedArray, text, location);
                type.children.push_back(parse_type_text(head, location));
                type.value = trim_string(inner);
                return type;
            }
            TypeRef type = make_type(TypeKind::Template, text, location);
            type.name = trim_string(head);
            type.children = parse_type_list(inner, advance_columns(location, open + 1));
            return type;
        }
    }
    if (text.find('.') != std::string_view::npos) {
        TypeRef type = make_type(TypeKind::Qualified, text, location);
        type.name = std::string(text);
        return type;
    }
    if (is_identifier(text)) {
        TypeRef type = make_type(TypeKind::Named, text, location);
        type.name = std::string(text);
        return type;
    }
    return make_type(TypeKind::Unknown, text, location);
}

} // namespace dudu
