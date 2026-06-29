#pragma once

#include "dudu/core/ast.hpp"

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
std::vector<CppModuleArtifact>
emit_cpp_module_artifacts(const ModuleAst& module, const std::vector<std::string>& module_paths);
std::vector<CppModuleArtifact> emit_cpp_test_module_artifacts(const ModuleAst& module,
                                                              const std::string& filter = {},
                                                              bool capture_output = true);
std::vector<CppModuleArtifact>
emit_cpp_test_module_artifacts(const ModuleAst& module,
                               const std::vector<std::string>& module_paths,
                               const std::string& filter = {}, bool capture_output = true);
std::vector<std::filesystem::path> cpp_module_source_paths(const ModuleAst& module);
std::vector<std::filesystem::path> cpp_test_module_source_paths(const ModuleAst& module);
std::vector<std::filesystem::path> cpp_module_artifact_paths(const ModuleAst& module);
std::vector<std::filesystem::path> cpp_test_module_artifact_paths(const ModuleAst& module);
void write_cpp_artifacts(const std::filesystem::path& dir,
                         const std::vector<CppModuleArtifact>& artifacts);
void write_cpp_module_artifacts(const std::filesystem::path& dir, const ModuleAst& module);

} // namespace dudu
