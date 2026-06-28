#include "dudu/build_backend_select.hpp"

namespace dudu {

ProjectConfig select_build_backend(ProjectConfig config, const std::filesystem::path& input,
                                   bool project_driver) {
    (void)input;
    (void)project_driver;
    return config;
}

} // namespace dudu
