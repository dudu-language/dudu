#pragma once

#include "dudu/ast.hpp"

#include <iosfwd>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace dudu {

std::string native_cache_join_strings(const std::vector<std::string>& values);
std::vector<std::string> native_cache_split_strings(const std::string& text);
std::string cached_type_text(const TypeRef& type);
TypeRef cached_type_ref(const std::string& text, const SourceLocation& location);
std::vector<std::string> cached_type_texts(const std::vector<TypeRef>& types);
std::vector<TypeRef> cached_type_refs(const std::string& text, const SourceLocation& location);
void write_record(std::ostream& out, std::string_view tag,
                  const std::vector<std::string>& fields);
std::optional<std::pair<std::string, std::vector<std::string>>>
parse_record(const std::string& line);

} // namespace dudu
