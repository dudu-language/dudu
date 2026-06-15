#include "dudu/cpp_pointer_members.hpp"

#include "dudu/ast_type.hpp"
#include "dudu/cpp_lower.hpp"

#include <cctype>

namespace dudu {
namespace {

bool is_pointer_type(const std::string& type) {
    return parse_type_text(type).kind == TypeKind::Pointer;
}

bool is_pointer_list_type(std::string type) {
    type = trim_copy(std::move(type));
    const TypeRef parsed = parse_type_text(type);
    if (parsed.kind != TypeKind::Template || parsed.name != "list" || parsed.children.size() != 1) {
        return false;
    }
    return parsed.children.front().kind == TypeKind::Pointer;
}

size_t matching_bracket(const std::string& text, size_t open) {
    int depth = 1;
    for (size_t i = open + 1; i < text.size(); ++i) {
        if (text[i] == '[') {
            ++depth;
        } else if (text[i] == ']') {
            --depth;
            if (depth == 0) {
                return i;
            }
        }
    }
    return std::string::npos;
}

void rewrite_pointer_local(std::string& expr, const std::string& name) {
    const std::string marker = name + ".";
    size_t pos = expr.find(marker);
    while (pos != std::string::npos) {
        const bool left_ok =
            pos == 0 ||
            (std::isalnum(static_cast<unsigned char>(expr[pos - 1])) == 0 && expr[pos - 1] != '_');
        if (left_ok) {
            expr.replace(pos, marker.size(), name + "->");
            pos = expr.find(marker, pos + name.size() + 2);
            continue;
        }
        pos = expr.find(marker, pos + marker.size());
    }
}

void rewrite_pointer_list(std::string& expr, const std::string& name) {
    const std::string marker = name + "[";
    size_t pos = expr.find(marker);
    while (pos != std::string::npos) {
        const size_t close = matching_bracket(expr, pos + name.size());
        if (close != std::string::npos && close + 1 < expr.size() && expr[close + 1] == '.') {
            expr.replace(close + 1, 1, "->");
            pos = expr.find(marker, close + 2);
            continue;
        }
        pos = expr.find(marker, pos + marker.size());
    }
}

} // namespace

std::string rewrite_pointer_members(std::string expr,
                                    const std::map<std::string, std::string>& locals) {
    for (const auto& [name, type] : locals) {
        if (is_pointer_type(type)) {
            rewrite_pointer_local(expr, name);
        }
        if (is_pointer_list_type(type)) {
            rewrite_pointer_list(expr, name);
        }
    }
    return expr;
}

} // namespace dudu
