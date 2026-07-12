#include "dudu/lsp/language_server_text_sync.hpp"

#include "dudu/lsp/language_server_json.hpp"

#include <cstddef>
#include <stdexcept>
#include <string_view>

namespace dudu {
namespace {

size_t utf8_sequence_length(unsigned char lead) {
    if (lead < 0x80) {
        return 1;
    }
    if ((lead & 0xe0) == 0xc0) {
        return 2;
    }
    if ((lead & 0xf0) == 0xe0) {
        return 3;
    }
    if ((lead & 0xf8) == 0xf0) {
        return 4;
    }
    throw std::runtime_error("invalid UTF-8 in open document");
}

int utf16_code_units(std::string_view bytes) {
    return bytes.size() == 4 ? 2 : 1;
}

size_t line_start_offset(std::string_view text, int target_line) {
    if (target_line < 0) {
        throw std::runtime_error("negative LSP line position");
    }
    size_t offset = 0;
    for (int line = 0; line < target_line; ++line) {
        const size_t newline = text.find('\n', offset);
        if (newline == std::string_view::npos) {
            throw std::runtime_error("LSP line position is outside the document");
        }
        offset = newline + 1;
    }
    return offset;
}

size_t byte_offset(std::string_view text, int line, int character) {
    if (character < 0) {
        throw std::runtime_error("negative LSP character position");
    }
    size_t offset = line_start_offset(text, line);
    int utf16_offset = 0;
    while (utf16_offset < character) {
        if (offset >= text.size() || text[offset] == '\n' || text[offset] == '\r') {
            throw std::runtime_error("LSP character position is outside the line");
        }
        const size_t length = utf8_sequence_length(static_cast<unsigned char>(text[offset]));
        if (offset + length > text.size()) {
            throw std::runtime_error("truncated UTF-8 in open document");
        }
        for (size_t index = 1; index < length; ++index) {
            if ((static_cast<unsigned char>(text[offset + index]) & 0xc0) != 0x80) {
                throw std::runtime_error("invalid UTF-8 continuation byte in open document");
            }
        }
        const int units = utf16_code_units(text.substr(offset, length));
        if (utf16_offset + units > character) {
            throw std::runtime_error("LSP position splits a UTF-16 surrogate pair");
        }
        utf16_offset += units;
        offset += length;
    }
    return offset;
}

int position_component(const Json* position, std::string_view name) {
    if (position == nullptr) {
        throw std::runtime_error("LSP text edit is missing a position");
    }
    return required_int_value(position->get(name), name);
}

size_t position_offset(std::string_view text, const Json* position) {
    return byte_offset(text, position_component(position, "line"),
                       position_component(position, "character"));
}

void apply_change(std::string& text, const Json& change) {
    const Json* replacement = change.get("text");
    if (replacement == nullptr || replacement->string() == nullptr) {
        throw std::runtime_error("LSP content change is missing text");
    }
    const Json* range = change.get("range");
    if (range == nullptr) {
        text = *replacement->string();
        return;
    }
    const size_t start = position_offset(text, range->get("start"));
    const size_t end = position_offset(text, range->get("end"));
    if (end < start) {
        throw std::runtime_error("LSP content change has a reversed range");
    }
    text.replace(start, end - start, *replacement->string());
}

} // namespace

void apply_lsp_content_changes(std::string& text, const JsonArray& changes) {
    std::string updated = text;
    for (const Json& change : changes) {
        apply_change(updated, change);
    }
    text = std::move(updated);
}

} // namespace dudu
