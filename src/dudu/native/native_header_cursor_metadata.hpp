#pragma once

#include "dudu/core/ast.hpp"

#include <clang-c/Index.h>

namespace dudu {

NativeDeclarationMetadata native_cursor_metadata(CXCursor cursor);

} // namespace dudu
