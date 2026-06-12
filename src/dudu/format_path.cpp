#include "dudu/format_path.hpp"

#include "dudu/format.hpp"

#include <fstream>
#include <iostream>
#include <stdexcept>

namespace dudu {
namespace {

std::string read_text_file(const std::filesystem::path& path) {
    std::ifstream file(path);
    if (!file) {
        throw std::runtime_error("could not open " + path.string());
    }
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

void write_text_file(const std::filesystem::path& path, const std::string& text) {
    std::ofstream out(path);
    if (!out) {
        throw std::runtime_error("could not open output " + path.string());
    }
    out << text;
}

bool is_dudu_file(const std::filesystem::directory_entry& entry) {
    return entry.is_regular_file() && entry.path().extension() == ".dd";
}

void format_file_in_place(const std::filesystem::path& path) {
    write_text_file(path, format_source(read_text_file(path)));
}

bool check_formatted_file(const std::filesystem::path& path) {
    const std::string source = read_text_file(path);
    if (format_source(source) == source) {
        return true;
    }
    std::cerr << path.string() << ": would reformat\n";
    return false;
}

} // namespace

bool check_formatted_path(const std::filesystem::path& path) {
    if (!std::filesystem::is_directory(path)) {
        return check_formatted_file(path);
    }
    bool ok = true;
    for (const std::filesystem::directory_entry& entry :
         std::filesystem::recursive_directory_iterator(path)) {
        if (is_dudu_file(entry)) {
            ok = check_formatted_file(entry.path()) && ok;
        }
    }
    return ok;
}

void format_path(const std::filesystem::path& path,
                 const std::optional<std::filesystem::path>& output, std::ostream& stream) {
    if (!std::filesystem::is_directory(path)) {
        const std::string formatted = format_source(read_text_file(path));
        if (!output.has_value() || output->empty()) {
            stream << formatted;
        } else {
            write_text_file(*output, formatted);
        }
        return;
    }
    if (output.has_value() && !output->empty()) {
        throw std::runtime_error("cannot format a directory to one output file");
    }
    for (const std::filesystem::directory_entry& entry :
         std::filesystem::recursive_directory_iterator(path)) {
        if (is_dudu_file(entry)) {
            format_file_in_place(entry.path());
        }
    }
}

} // namespace dudu
