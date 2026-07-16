#pragma once

#include "dudu/native/native_headers.hpp"

#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace dudu {

struct NativeClassScope {
    int depth = 0;
    std::string name;
    std::optional<size_t> declaration_index;
    std::vector<std::string> specialization_source_args;
};

std::string join_scope(const std::vector<std::pair<int, std::string>>& namespaces,
                       const std::string& name);
std::string class_name(const NativeHeaderScan& scan,
                       const std::vector<std::pair<int, std::string>>& namespaces,
                       const std::vector<NativeClassScope>& classes, const std::string& name);
std::string qualify_scoped_type(const NativeHeaderScan& scan,
                                const std::vector<std::pair<int, std::string>>& namespaces,
                                std::string type);
std::string qualify_scoped_type(const NativeHeaderScan& scan,
                                const std::vector<std::pair<int, std::string>>& namespaces,
                                const std::vector<NativeClassScope>& classes,
                                std::string type);
std::vector<std::string>
qualify_scoped_types(const NativeHeaderScan& scan,
                     const std::vector<std::pair<int, std::string>>& namespaces,
                     std::vector<std::string> types);
std::vector<std::string> qualify_scoped_types(
    const NativeHeaderScan& scan, const std::vector<std::pair<int, std::string>>& namespaces,
    const std::vector<NativeClassScope>& classes, std::vector<std::string> types);
void qualify_completed_native_scan(NativeHeaderScan& scan);

} // namespace dudu
