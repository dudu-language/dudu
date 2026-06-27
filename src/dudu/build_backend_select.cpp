#include "dudu/build_backend_select.hpp"

#include "dudu/module_loader.hpp"

namespace dudu {

ProjectConfig select_build_backend(ProjectConfig config, const std::filesystem::path& input,
                                   bool project_driver) {
    if (config.build_backend_explicit || config.build_backend != "cmake" || project_driver) {
        return config;
    }
    if (!input.empty() && source_tree_files(input).size() <= 1) {
        config.build_backend = "direct";
    }
    return config;
}

} // namespace dudu
