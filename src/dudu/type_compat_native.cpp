#include "dudu/type_compat_native.hpp"

#include "dudu/ast_type.hpp"
#include "dudu/cpp_lower.hpp"

#include <optional>
#include <string_view>
#include <vector>

namespace dudu {
namespace {

size_t matching_angle(const std::string& text, const size_t open) {
    int depth = 0;
    for (size_t i = open; i < text.size(); ++i) {
        if (text[i] == '<') {
            ++depth;
        } else if (text[i] == '>') {
            --depth;
            if (depth == 0) {
                return i;
            }
        }
    }
    return std::string::npos;
}

std::string normalize_type_traits(std::string type) {
    type = trim_copy(std::move(type));
    const std::vector<std::string> prefixes = {"typename __decay_and_strip<",
                                               "typename std.remove_reference<",
                                               "typename std::remove_reference<"};
    size_t search_start = 0;
    while (search_start < type.size()) {
        size_t pos = std::string::npos;
        std::string prefix;
        for (const std::string& candidate : prefixes) {
            const size_t candidate_pos = type.find(candidate, search_start);
            if (candidate_pos != std::string::npos &&
                (pos == std::string::npos || candidate_pos < pos)) {
                pos = candidate_pos;
                prefix = candidate;
            }
        }
        if (pos == std::string::npos) {
            break;
        }
        const size_t open = pos + prefix.size() - 1;
        const size_t close = matching_angle(type, open);
        if (close == std::string::npos) {
            return type;
        }
        size_t suffix_end = close + 1;
        std::string suffix;
        if (type.compare(suffix_end, 7, ".__type") == 0) {
            suffix = ".__type";
        } else if (type.compare(suffix_end, 8, "::__type") == 0) {
            suffix = "::__type";
        } else if (type.compare(suffix_end, 5, ".type") == 0) {
            suffix = ".type";
        } else if (type.compare(suffix_end, 6, "::type") == 0) {
            suffix = "::type";
        } else {
            search_start = close + 1;
            continue;
        }
        suffix_end += suffix.size();
        const std::string inner = trim_copy(type.substr(pos + prefix.size(), close - open - 1));
        type.replace(pos, suffix_end - pos, inner);
        search_start = pos + inner.size();
    }
    return trim_copy(type);
}

std::string normalize_tuple_element(std::string type) {
    type = trim_copy(std::move(type));
    const TypeRef parsed = parse_type_text(type);
    if (parsed.kind == TypeKind::Reference && parsed.children.size() == 1) {
        return "&" + normalize_tuple_element(parsed.children.front().text);
    }
    if (parsed.kind != TypeKind::Template || parsed.name != "__tuple_element_t" ||
        parsed.children.size() != 2 || parsed.children[0].kind != TypeKind::Value) {
        return type;
    }
    const TypeRef& tuple_type = parsed.children[1];
    if (tuple_type.kind != TypeKind::Template ||
        (tuple_type.name != "std.tuple" && tuple_type.name != "tuple")) {
        return type;
    }
    const std::string index_text = trim_copy(parsed.children[0].value);
    if (index_text.empty() || index_text.find_first_not_of("0123456789") != std::string::npos) {
        return type;
    }
    const size_t index = static_cast<size_t>(std::stoull(index_text));
    if (index >= tuple_type.children.size()) {
        return type;
    }
    return trim_copy(tuple_type.children[index].text);
}

} // namespace

std::string normalize_cpp_type_artifacts(std::string type) {
    type = normalize_tuple_element(normalize_type_traits(std::move(type)));
    for (const std::string_view marker : {"* const[", "& const[", "* volatile[", "& volatile["}) {
        size_t pos = type.find(marker);
        while (pos != std::string::npos) {
            type.erase(pos + 1, 1);
            pos = type.find(marker, pos + 1);
        }
    }
    return type;
}

} // namespace dudu
