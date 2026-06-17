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

TypeRef normalize_tuple_element(const TypeRef& type) {
    if (type.kind == TypeKind::Reference && type.children.size() == 1) {
        TypeRef out = type;
        out.children[0] = normalize_tuple_element(type.children.front());
        out.text = substitute_type_ref_text(out, {});
        return out;
    }
    if (type.kind != TypeKind::Template || type.name != "__tuple_element_t" ||
        type.children.size() != 2 || type.children[0].kind != TypeKind::Value) {
        TypeRef out = type;
        for (TypeRef& child : out.children) {
            child = normalize_tuple_element(child);
        }
        out.text = substitute_type_ref_text(out, {});
        return out;
    }
    const TypeRef& tuple_type = type.children[1];
    if (tuple_type.kind != TypeKind::Template ||
        (tuple_type.name != "std.tuple" && tuple_type.name != "tuple")) {
        return type;
    }
    const std::string index_text = trim_copy(type.children[0].value);
    if (index_text.empty() || index_text.find_first_not_of("0123456789") != std::string::npos) {
        return type;
    }
    const size_t index = static_cast<size_t>(std::stoull(index_text));
    if (index >= tuple_type.children.size()) {
        return type;
    }
    return normalize_tuple_element(tuple_type.children[index]);
}

bool nonarray_template_name(const std::string& name) {
    return name == "_NonArray" || name.ends_with("._NonArray") || name.ends_with("::_NonArray");
}

std::string normalize_nonarray_templates(const TypeRef& type) {
    if (type.kind != TypeKind::Template) {
        return substitute_type_ref_text(type, {});
    }
    if ((type.name == "basic_string" || type.name == "std.basic_string" ||
         type.name == "std::basic_string") &&
        !type.children.empty() && substitute_type_ref_text(type.children.front(), {}) == "char") {
        return "std.string";
    }
    if (nonarray_template_name(type.name) && type.children.size() == 1) {
        return normalize_nonarray_templates(type.children.front());
    }
    std::string out = type.name + "[";
    for (size_t i = 0; i < type.children.size(); ++i) {
        if (i > 0) {
            out += ", ";
        }
        out += normalize_nonarray_templates(type.children[i]);
    }
    out += "]";
    return out;
}

std::string strip_cpp_pointer_cv_artifacts(std::string type) {
    for (const std::string_view marker : {"* const[", "& const[", "* volatile[", "& volatile["}) {
        size_t pos = type.find(marker);
        while (pos != std::string::npos) {
            type.erase(pos + 1, 1);
            pos = type.find(marker, pos + 1);
        }
    }
    return type;
}

} // namespace

std::string normalize_cpp_type_artifacts(const TypeRef& type) {
    const TypeRef tuple_normalized = normalize_tuple_element(type);
    return strip_cpp_pointer_cv_artifacts(normalize_nonarray_templates(tuple_normalized));
}

std::string normalize_cpp_type_artifacts(std::string type) {
    type = normalize_type_traits(std::move(type));
    const TypeRef parsed = parse_type_text(type);
    if (parsed.kind == TypeKind::Unknown) {
        return strip_cpp_pointer_cv_artifacts(type);
    }
    return normalize_cpp_type_artifacts(parsed);
}

} // namespace dudu
