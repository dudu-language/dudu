#include "dudu/core/file_io.hpp"
#include "dudu/project/project_index.hpp"
#include "dudu/project/project_index_internal.hpp"

#include <fstream>
#include <map>
#include <sstream>
#include <stdexcept>
#include <vector>

namespace dudu {
namespace {

using project_index_internal::file_mtime;
using project_index_internal::mtime_stamp;
using project_index_internal::path_key;

struct SourceStamp {
    std::string module_path;
    std::string mtime;
    std::string source_key;
};

std::vector<SourceStamp> parse_source_stamp_file(const std::filesystem::path& path) {
    const std::optional<std::string> text = try_read_text_file(path);
    if (!text.has_value()) {
        return {};
    }
    std::vector<SourceStamp> out;
    std::istringstream lines(*text);
    std::string line;
    while (std::getline(lines, line)) {
        if (line.empty()) {
            continue;
        }
        const size_t first = line.find('\t');
        const size_t second =
            first == std::string::npos ? std::string::npos : line.find('\t', first + 1);
        if (first == std::string::npos || second == std::string::npos) {
            continue;
        }
        out.push_back({.module_path = line.substr(0, first),
                       .mtime = line.substr(first + 1, second - first - 1),
                       .source_key = line.substr(second + 1)});
    }
    return out;
}

} // namespace

std::vector<std::filesystem::path>
ProjectIndex::changed_sources_since_stamp_file(const std::filesystem::path& path) const {
    const std::vector<SourceStamp> previous_stamps = parse_source_stamp_file(path);
    std::map<std::string, SourceStamp> previous_by_module;
    for (const SourceStamp& stamp : previous_stamps) {
        previous_by_module[stamp.module_path] = stamp;
    }

    std::vector<std::filesystem::path> changed;
    for (const ProjectModuleSummary& summary : modules_) {
        if (summary.source_path.empty()) {
            continue;
        }
        const auto previous = previous_by_module.find(summary.module_path);
        if (previous == previous_by_module.end() ||
            previous->second.source_key != path_key(summary.source_path) ||
            previous->second.mtime != mtime_stamp(summary.source_mtime)) {
            changed.push_back(summary.source_path);
        }
    }
    return changed;
}

void ProjectIndex::write_source_stamp_file(const std::filesystem::path& path) const {
    if (path.empty()) {
        return;
    }
    std::filesystem::create_directories(path.parent_path().empty() ? "." : path.parent_path());
    std::ofstream out(path, std::ios::binary);
    if (!out) {
        throw std::runtime_error("could not open source stamp file " + path.string());
    }
    for (const ProjectModuleSummary& summary : modules_) {
        if (summary.source_path.empty()) {
            continue;
        }
        out << summary.module_path << '\t' << mtime_stamp(summary.source_mtime) << '\t'
            << path_key(summary.source_path) << '\n';
    }
}

bool ProjectIndex::source_stamps_current() const {
    for (const ProjectModuleSummary& summary : modules_) {
        if (summary.source_path.empty() || !summary.source_mtime.has_value()) {
            continue;
        }
        if (file_mtime(summary.source_path) != summary.source_mtime) {
            return false;
        }
    }
    return true;
}

bool source_stamp_file_current(const std::filesystem::path& path) {
    const std::vector<SourceStamp> stamps = parse_source_stamp_file(path);
    if (stamps.empty()) {
        return false;
    }
    for (const SourceStamp& stamp : stamps) {
        if (stamp.source_key.empty() || stamp.mtime.empty() ||
            mtime_stamp(file_mtime(std::filesystem::path(stamp.source_key))) != stamp.mtime) {
            return false;
        }
    }
    return true;
}

bool source_stamp_file_current_for_entry(const std::filesystem::path& path,
                                         const std::filesystem::path& entry_path) {
    const std::vector<SourceStamp> stamps = parse_source_stamp_file(path);
    if (stamps.empty()) {
        return false;
    }
    const std::string entry_key = path_key(entry_path);
    bool found_entry = false;
    for (const SourceStamp& stamp : stamps) {
        if (stamp.source_key.empty() || stamp.mtime.empty() ||
            mtime_stamp(file_mtime(std::filesystem::path(stamp.source_key))) != stamp.mtime) {
            return false;
        }
        found_entry = found_entry || stamp.source_key == entry_key;
    }
    return found_entry;
}

} // namespace dudu
