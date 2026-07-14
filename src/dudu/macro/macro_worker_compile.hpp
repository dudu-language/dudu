#pragma once

#include "dudu/codegen/cpp_emit_modules.hpp"
#include "dudu/macro/macro_worker_build.hpp"

#include <filesystem>
#include <vector>

namespace dudu::macro {

WorkerBinary::Timings compile_worker(const std::filesystem::path& dir,
                                     const std::vector<CppModuleArtifact>& artifacts,
                                     const WorkerBuildOptions& options);

} // namespace dudu::macro
