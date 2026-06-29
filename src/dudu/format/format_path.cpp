#include "dudu/format/format_path.hpp"

#include "dudu/core/file_io.hpp"
#include "dudu/format/format.hpp"

#include <algorithm>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace dudu {
namespace {

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

std::filesystem::path normalized_existing_path(const std::filesystem::path& path) {
    std::error_code error;
    const std::filesystem::path canonical = std::filesystem::weakly_canonical(path, error);
    if (!error) {
        return canonical.lexically_normal();
    }
    return std::filesystem::absolute(path, error).lexically_normal();
}

std::vector<std::filesystem::path> normalized_excludes(const FormatPathOptions& options) {
    std::vector<std::filesystem::path> out;
    out.reserve(options.excluded_dirs.size());
    for (const std::filesystem::path& path : options.excluded_dirs) {
        if (!path.empty()) {
            out.push_back(normalized_existing_path(path));
        }
    }
    return out;
}

bool path_is_or_is_inside(const std::filesystem::path& path, const std::filesystem::path& root) {
    auto path_it = path.begin();
    auto root_it = root.begin();
    for (; root_it != root.end(); ++root_it, ++path_it) {
        if (path_it == path.end() || *path_it != *root_it) {
            return false;
        }
    }
    return true;
}

bool excluded_dir(const std::filesystem::path& path,
                  const std::vector<std::filesystem::path>& excluded_dirs) {
    const std::filesystem::path normalized = normalized_existing_path(path);
    return std::any_of(excluded_dirs.begin(), excluded_dirs.end(),
                       [&](const std::filesystem::path& excluded) {
                           return path_is_or_is_inside(normalized, excluded);
                       });
}

void format_file_in_place(const std::filesystem::path& path) {
    write_text_file(path, format_source(read_required_text_file(path)));
}

bool check_formatted_file(const std::filesystem::path& path) {
    const std::string source = read_required_text_file(path);
    if (format_source(source) == source) {
        return true;
    }
    std::cerr << path.string() << ": would reformat\n";
    return false;
}

} // namespace

bool check_formatted_path(const std::filesystem::path& path, const FormatPathOptions& options) {
    if (!std::filesystem::is_directory(path)) {
        return check_formatted_file(path);
    }
    bool ok = true;
    const std::vector<std::filesystem::path> excluded_dirs = normalized_excludes(options);
    for (auto it = std::filesystem::recursive_directory_iterator(path);
         it != std::filesystem::recursive_directory_iterator(); ++it) {
        const std::filesystem::directory_entry& entry = *it;
        if (entry.is_directory() && excluded_dir(entry.path(), excluded_dirs)) {
            it.disable_recursion_pending();
            continue;
        }
        if (is_dudu_file(entry)) {
            ok = check_formatted_file(entry.path()) && ok;
        }
    }
    return ok;
}

void format_path_in_place(const std::filesystem::path& path, const FormatPathOptions& options) {
    if (!std::filesystem::is_directory(path)) {
        format_file_in_place(path);
        return;
    }
    const std::vector<std::filesystem::path> excluded_dirs = normalized_excludes(options);
    for (auto it = std::filesystem::recursive_directory_iterator(path);
         it != std::filesystem::recursive_directory_iterator(); ++it) {
        const std::filesystem::directory_entry& entry = *it;
        if (entry.is_directory() && excluded_dir(entry.path(), excluded_dirs)) {
            it.disable_recursion_pending();
            continue;
        }
        if (is_dudu_file(entry)) {
            format_file_in_place(entry.path());
        }
    }
}

void format_path(const std::filesystem::path& path,
                 const std::optional<std::filesystem::path>& output, std::ostream& stream,
                 const FormatPathOptions& options) {
    if (!std::filesystem::is_directory(path)) {
        const std::string formatted = format_source(read_required_text_file(path));
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
    format_path_in_place(path, options);
}

} // namespace dudu
