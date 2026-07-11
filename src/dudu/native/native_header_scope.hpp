#pragma once

#include "dudu/native/native_headers.hpp"

#include <string>
#include <utility>
#include <vector>

namespace dudu {

std::string join_scope(const std::vector<std::pair<int, std::string>>& namespaces,
                       const std::string& name);
std::string class_name(const NativeHeaderScan& scan,
                       const std::vector<std::pair<int, std::string>>& namespaces,
                       const std::vector<std::pair<int, size_t>>& classes, const std::string& name);
std::string qualify_scoped_type(const NativeHeaderScan& scan,
                                const std::vector<std::pair<int, std::string>>& namespaces,
                                std::string type);
std::string qualify_scoped_type(const NativeHeaderScan& scan,
                                const std::vector<std::pair<int, std::string>>& namespaces,
                                const std::vector<std::pair<int, size_t>>& classes,
                                std::string type);
std::vector<std::string>
qualify_scoped_types(const NativeHeaderScan& scan,
                     const std::vector<std::pair<int, std::string>>& namespaces,
                     std::vector<std::string> types);
std::vector<std::string> qualify_scoped_types(
    const NativeHeaderScan& scan, const std::vector<std::pair<int, std::string>>& namespaces,
    const std::vector<std::pair<int, size_t>>& classes, std::vector<std::string> types);

} // namespace dudu
