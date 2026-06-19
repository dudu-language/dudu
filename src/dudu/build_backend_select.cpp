#include "dudu/build_backend_select.hpp"

#include "dudu/module_loader.hpp"

namespace dudu {

ProjectConfig select_build_backend(ProjectConfig config, const std::filesystem::path& input) {
    if (!config.build_backend_explicit && config.build_backend == "direct" && !input.empty() &&
        source_tree_files(input).size() > 1) {
        config.build_backend = "cmake";
    }
    return config;
}

} // namespace dudu
