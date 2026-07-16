#pragma once

#include "dudu/native/native_headers.hpp"

namespace dudu {

void attach_native_macro_definition_locations(NativeHeaderScan& scan, const ImportDecl& import,
                                              const NativeHeaderOptions& options);
NativeHeaderScan direct_native_import_view(const NativeHeaderScan& scan);
NativeHeaderScan aliased_native_import_view(const NativeHeaderScan& scan, const ImportDecl& import);

} // namespace dudu
