#pragma once

#include "dudu/macro/macro_worker_build.hpp"
#include "dudu/project/project_config.hpp"

#include <filesystem>
#include <string>
#include <vector>

namespace dudu::macro {

struct RuntimeLayout {
    std::vector<std::filesystem::path> include_dirs;
    std::filesystem::path library;
};

RuntimeLayout find_runtime_layout();
WorkerBuildOptions worker_build_options(const ProjectConfig& config,
                                        const RuntimeLayout& runtime,
                                        const std::filesystem::path& cache_dir,
                                        std::string package,
                                        std::vector<std::string> capabilities);

} // namespace dudu::macro
