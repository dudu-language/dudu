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

struct NativeHeaderScan {
    std::vector<NativeTypeDecl> types;
    std::vector<NativeValueDecl> values;
    std::vector<NativeFunctionDecl> functions;
    std::vector<NativeMacroDecl> macros;
    std::vector<NativeNamespaceDecl> namespaces;
    std::vector<ClassDecl> classes;
};

NativeHeaderScan scan_native_headers(const ModuleAst& module, const NativeHeaderOptions& options);
void merge_native_headers(ModuleAst& module, const NativeHeaderOptions& options);

std::vector<NativeTypeDecl> scan_native_header_types(const ModuleAst& module,
                                                     const NativeHeaderOptions& options);
void merge_native_header_types(ModuleAst& module, const NativeHeaderOptions& options);

} // namespace dudu
