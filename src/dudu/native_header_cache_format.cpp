#include "dudu/native_header_cache_format.hpp"

#include "dudu/ast_type.hpp"

#include <ostream>

namespace dudu {
namespace {

constexpr char kListSeparator = '\x1f';

void append_number(std::string& out, const size_t value) {
    out += std::to_string(value);
    out.push_back(';');
}

void append_string(std::string& out, const std::string& value) {
    out += std::to_string(value.size());
    out.push_back(':');
    out += value;
}

void append_type_ref(std::string& out, const TypeRef& type) {
    append_number(out, static_cast<size_t>(type.kind));
    append_number(out, type.malformed ? 1 : 0);
    append_string(out, type.name);
    append_string(out, type.value);
    append_number(out, type.children.size());
    for (const TypeRef& child : type.children) {
        append_type_ref(out, child);
    }
}

std::optional<size_t> read_number(std::string_view text, size_t& offset) {
    const size_t end = text.find(';', offset);
    if (end == std::string_view::npos) {
        return std::nullopt;
    }
    const size_t value =
        static_cast<size_t>(std::stoull(std::string(text.substr(offset, end - offset))));
    offset = end + 1;
    return value;
}

std::optional<std::string> read_string(std::string_view text, size_t& offset) {
    const size_t colon = text.find(':', offset);
    if (colon == std::string_view::npos) {
        return std::nullopt;
    }
    const size_t size =
        static_cast<size_t>(std::stoull(std::string(text.substr(offset, colon - offset))));
    const size_t data = colon + 1;
    if (data + size > text.size()) {
        return std::nullopt;
    }
    offset = data + size;
    return std::string(text.substr(data, size));
}

std::optional<TypeRef> read_type_ref(std::string_view text, size_t& offset,
                                     const SourceLocation& location, const bool root) {
    const std::optional<size_t> kind = read_number(text, offset);
    const std::optional<size_t> malformed = read_number(text, offset);
    const std::optional<std::string> name = read_string(text, offset);
    const std::optional<std::string> value = read_string(text, offset);
    const std::optional<size_t> child_count = read_number(text, offset);
    if (!kind || !malformed || !name || !value || !child_count) {
        return std::nullopt;
    }
    TypeRef type;
    type.kind = static_cast<TypeKind>(*kind);
    type.malformed = *malformed != 0;
    type.name = *name;
    type.value = *value;
    if (root) {
        type.location = location;
        type.range.start = location;
        type.range.end = location;
    }
    for (size_t i = 0; i < *child_count; ++i) {
        const std::optional<TypeRef> child = read_type_ref(text, offset, location, false);
        if (!child) {
            return std::nullopt;
        }
        type.children.push_back(*child);
    }
    return type;
}

} // namespace

std::string native_cache_join_strings(const std::vector<std::string>& values) {
    std::string out;
    for (size_t i = 0; i < values.size(); ++i) {
        if (i > 0) {
            out.push_back(kListSeparator);
        }
        out += values[i];
    }
    return out;
}

std::vector<std::string> native_cache_split_strings(const std::string& text) {
    std::vector<std::string> out;
    if (text.empty()) {
        return out;
    }
    size_t start = 0;
    while (start <= text.size()) {
        const size_t next = text.find(kListSeparator, start);
        out.push_back(
            text.substr(start, next == std::string::npos ? std::string::npos : next - start));
        if (next == std::string::npos) {
            break;
        }
        start = next + 1;
    }
    return out;
}

std::string cached_type_text(const TypeRef& type) {
    std::string out;
    append_type_ref(out, type);
    return out;
}

TypeRef cached_type_ref(const std::string& text, const SourceLocation& location) {
    size_t offset = 0;
    const std::optional<TypeRef> type = read_type_ref(text, offset, location, true);
    if (!type || offset != text.size()) {
        return parse_type_text(text, location);
    }
    return *type;
}

std::vector<std::string> cached_type_texts(const std::vector<TypeRef>& types) {
    std::vector<std::string> out;
    out.reserve(types.size());
    for (const TypeRef& type : types) {
        out.push_back(cached_type_text(type));
    }
    return out;
}

std::vector<TypeRef> cached_type_refs(const std::string& text, const SourceLocation& location) {
    std::vector<TypeRef> out;
    for (const std::string& item : native_cache_split_strings(text)) {
        out.push_back(cached_type_ref(item, location));
    }
    return out;
}

void write_record(std::ostream& out, std::string_view tag,
                  const std::vector<std::string>& fields) {
    out << tag;
    for (const std::string& field : fields) {
        out << '\t' << field.size() << ':' << field;
    }
    out << '\n';
}

std::optional<std::pair<std::string, std::vector<std::string>>>
parse_record(const std::string& line) {
    const size_t first_tab = line.find('\t');
    const std::string tag = line.substr(0, first_tab);
    std::vector<std::string> fields;
    size_t offset = first_tab == std::string::npos ? line.size() : first_tab + 1;
    while (offset < line.size()) {
        const size_t colon = line.find(':', offset);
        if (colon == std::string::npos) {
            return std::nullopt;
        }
        const size_t size = static_cast<size_t>(std::stoull(line.substr(offset, colon - offset)));
        const size_t data = colon + 1;
        if (data + size > line.size()) {
            return std::nullopt;
        }
        fields.push_back(line.substr(data, size));
        offset = data + size;
        if (offset < line.size()) {
            if (line[offset] != '\t') {
                return std::nullopt;
            }
            ++offset;
        }
    }
    return std::make_pair(tag, fields);
}

} // namespace dudu
