#include "dudu/file_io.hpp"

#include <fstream>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace dudu {

std::optional<std::string> try_read_text_file(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) {
        return std::nullopt;
    }

    const std::ifstream::pos_type end = file.tellg();
    if (end < 0) {
        file.clear();
        file.seekg(0, std::ios::beg);
        std::ostringstream out;
        out << file.rdbuf();
        return out.str();
    }

    std::string text(static_cast<size_t>(end), '\0');
    file.seekg(0, std::ios::beg);
    if (!text.empty()) {
        file.read(text.data(), static_cast<std::streamsize>(text.size()));
    }
    if (!file && !file.eof()) {
        return std::nullopt;
    }
    return text;
}

std::string read_required_text_file(const std::filesystem::path& path) {
    if (std::optional<std::string> text = try_read_text_file(path)) {
        return std::move(*text);
    }
    throw std::runtime_error("could not open " + path.string());
}

} // namespace dudu
