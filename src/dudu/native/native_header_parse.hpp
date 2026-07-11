#pragma once

#include "dudu/native/native_header_usr.hpp"
#include "dudu/native/native_headers.hpp"

#include <string>

namespace dudu {

void parse_ast_dump(NativeHeaderScan& scan, const std::string& dump,
                    const SourceLocation& location);
void parse_ast_dump(NativeHeaderScan& scan, const std::string& dump, const SourceLocation& location,
                    const NativeCursorIdentityIndex& identities);
void parse_macro_dump(NativeHeaderScan& scan, const std::string& dump,
                      const SourceLocation& location);
NativeHeaderScan dedupe_scan(NativeHeaderScan scan);

} // namespace dudu
