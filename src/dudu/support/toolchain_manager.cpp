#include "dudu/support/toolchain_manager.hpp"

#include <cstdlib>
#include <filesystem>
#include <stdexcept>
#include <string>

namespace dudu {
namespace {

std::string shell_quote(const std::string& value) {
    std::string out = "'";
    for (const char c : value) {
        out += c == '\'' ? "'\\''" : std::string(1, c);
    }
    out += "'";
    return out;
}

std::filesystem::path manager_path(const std::filesystem::path& executable) {
    if (const char* override_path = std::getenv("DUDU_TOOLCHAIN_MANAGER")) {
        if (*override_path != '\0') {
            return override_path;
        }
    }

    std::error_code error;
    std::filesystem::path resolved = std::filesystem::canonical(executable, error);
    if (error) {
        resolved = std::filesystem::absolute(executable, error);
    }
    return resolved.parent_path().parent_path() / "libexec" / "dudu" /
           "dudu-toolchain-manager";
}

} // namespace

int run_toolchain_manager(const std::filesystem::path& executable,
                          const std::string& operation,
                          const std::vector<std::string>& arguments) {
    const std::filesystem::path manager = manager_path(executable);
    if (!std::filesystem::exists(manager)) {
        throw std::runtime_error(
            "toolchain manager is not installed; install Dudu with install.sh or update it "
            "through the package manager that owns this installation");
    }

    std::string command = shell_quote(manager.string()) + " --" + operation;
    for (const std::string& argument : arguments) {
        command += " " + shell_quote(argument);
    }
    const int status = std::system(command.c_str());
    return status == 0 ? 0 : 1;
}

} // namespace dudu
