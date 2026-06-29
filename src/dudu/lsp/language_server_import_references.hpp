#pragma once

#include "dudu/lsp/language_server_types.hpp"

#include <optional>
#include <string>

namespace dudu {

struct ImportReferenceTarget {
    std::string source_key;
    std::string member_name;
};

std::optional<std::string> module_import_target_key(const Document& doc,
                                                    const std::string& dotted_query);
std::optional<ImportReferenceTarget> selective_import_target(const Document& doc,
                                                             const std::string& query);
bool same_import_reference_target(const std::optional<ImportReferenceTarget>& lhs,
                                  const std::optional<ImportReferenceTarget>& rhs);

} // namespace dudu
