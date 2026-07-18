#pragma once

#include "dudu/native/native_header_usr.hpp"
#include "dudu/native/native_headers.hpp"

#include <string>
#include <vector>

namespace dudu {

void parse_ast_dump(NativeHeaderScan& scan, const std::string& dump,
                    const SourceLocation& location);
void parse_ast_dump(NativeHeaderScan& scan, const std::string& dump, const SourceLocation& location,
                    const NativeCursorIdentityIndex& identities);
void parse_macro_dump(NativeHeaderScan& scan, const std::string& dump,
                      const SourceLocation& location);
NativeHeaderScan dedupe_scan(NativeHeaderScan scan);

namespace native_ast_parse {

TypeRef parse_native_type_text(std::string text, const SourceLocation& location);
TypeRef parse_native_type_text(std::string text, const SourceLocation& location,
                               const std::vector<std::string>& dependent_type_names);

} // namespace native_ast_parse

} // namespace dudu
