#pragma once

#include "dudu/ast.hpp"
#include "dudu/project_config.hpp"

#include <filesystem>
#include <vector>

namespace dudu {

struct NativeHeaderOptions {
    ProjectConfig config;
    std::filesystem::path source_dir;
};

std::vector<NativeTypeDecl> scan_native_header_types(const ModuleAst& module,
                                                     const NativeHeaderOptions& options);
void merge_native_header_types(ModuleAst& module, const NativeHeaderOptions& options);

} // namespace dudu
