#pragma once

#include "dudu/ast.hpp"

#include <filesystem>
#include <string>
#include <vector>

namespace dudu {

enum class CppModuleArtifactKind {
    Header,
    Source,
};

struct CppModuleArtifact {
    std::filesystem::path path;
    std::string module_path;
    CppModuleArtifactKind kind = CppModuleArtifactKind::Source;
    std::string content;
};

std::vector<CppModuleArtifact> emit_cpp_module_artifacts(const ModuleAst& module);
void write_cpp_module_artifacts(const std::filesystem::path& dir, const ModuleAst& module);

} // namespace dudu
