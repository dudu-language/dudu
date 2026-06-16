#include "dudu/cmake_emit.hpp"

#include "dudu/module_loader.hpp"

#include <sstream>
#include <string_view>

namespace dudu {
namespace {

std::string cmake_quote(const std::string& value) {
    std::string out = "\"";
    for (const char c : value) {
        if (c == '"' || c == '\\') {
            out.push_back('\\');
        }
        out.push_back(c);
    }
    out.push_back('"');
    return out;
}

std::filesystem::path project_relative_path(const std::filesystem::path& project_dir,
                                            const std::string& value) {
    const std::filesystem::path path = value;
    return path.is_absolute() ? path : std::filesystem::absolute(project_dir / path);
}

void emit_cmake_list_values(std::ostringstream& out, std::string_view prefix,
                            const std::vector<std::string>& values,
                            const std::filesystem::path* project_dir = nullptr) {
    if (values.empty()) {
        return;
    }
    out << prefix;
    for (const std::string& value : values) {
        const std::string emitted =
            project_dir == nullptr ? value : project_relative_path(*project_dir, value).string();
        out << ' ' << cmake_quote(emitted);
    }
    out << ")\n";
}

void emit_cmake_depends(std::ostringstream& out, const std::vector<std::filesystem::path>& files) {
    out << "    DEPENDS";
    for (const std::filesystem::path& file : files) {
        out << ' ' << cmake_quote(file.string());
    }
    out << "\n";
}

void emit_cmake_sources(std::ostringstream& out, const ProjectConfig& config,
                        const std::filesystem::path& project_dir) {
    for (const std::string& source : config.cpp_sources) {
        out << "    " << cmake_quote(project_relative_path(project_dir, source).string()) << "\n";
    }
    for (const std::string& source : config.c_sources) {
        out << "    " << cmake_quote(project_relative_path(project_dir, source).string()) << "\n";
    }
}

std::string cmake_cpp_standard(const std::string& cpp_std) {
    if (cpp_std.size() > 3 && cpp_std.substr(0, 3) == "c++") {
        return cpp_std.substr(3);
    }
    return "20";
}

std::filesystem::path project_dir_for_input(const std::filesystem::path& input) {
    return parse_project_config(find_project_config(input)).project_dir;
}

std::string source_path_for_project(const std::filesystem::path& project_dir,
                                    const std::filesystem::path& input) {
    return std::filesystem::relative(std::filesystem::absolute(input), project_dir).string();
}

void emit_cmake_target(std::ostringstream& out, const ProjectConfig& config,
                       const std::filesystem::path& project_dir, const std::string& target) {
    if (config.target_kind == "library") {
        out << "add_library(" << target << " STATIC\n";
    } else if (config.target_kind == "shared_library") {
        out << "add_library(" << target << " SHARED\n";
    } else {
        out << "add_executable(" << target << "\n";
    }
    out << "    ${DUDU_GENERATED}\n";
    emit_cmake_sources(out, config, project_dir);
    out << ")\n";
}

void emit_pkg_config(std::ostringstream& out, const ProjectConfig& config) {
    if (config.pkg_config_packages.empty()) {
        return;
    }
    out << "find_package(PkgConfig REQUIRED)\n"
        << "pkg_check_modules(DUDU_PKG REQUIRED IMPORTED_TARGET";
    for (const std::string& package : config.pkg_config_packages) {
        out << ' ' << package;
    }
    out << ")\n\n";
}

} // namespace

std::string emit_cmake_project(const ProjectConfig& config, const std::filesystem::path& input) {
    const std::string target = config.name.empty() ? input.stem().string() : config.name;
    const std::filesystem::path project_dir = project_dir_for_input(input);
    const std::string source_path = source_path_for_project(project_dir, input);
    std::ostringstream out;
    out << "cmake_minimum_required(VERSION 3.20)\n\n"
        << "project(" << target << " LANGUAGES C CXX)\n\n"
        << "set(CMAKE_CXX_STANDARD " << cmake_cpp_standard(config.cpp_std) << ")\n"
        << "set(CMAKE_CXX_STANDARD_REQUIRED ON)\n"
        << "set(DUDU_EXECUTABLE duc CACHE FILEPATH \"Path to the duc compiler\")\n"
        << "set(DUDU_PROJECT_DIR " << cmake_quote(project_dir.string()) << ")\n"
        << "set(DUDU_SOURCE " << cmake_quote(source_path) << ")\n"
        << "set(DUDU_GENERATED ${CMAKE_CURRENT_BINARY_DIR}/generated/" << target << ".cpp)\n\n"
        << "add_custom_command(\n"
        << "    OUTPUT ${DUDU_GENERATED}\n"
        << "    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_CURRENT_BINARY_DIR}/generated\n"
        << "    COMMAND ${DUDU_EXECUTABLE} emit ${DUDU_PROJECT_DIR}/${DUDU_SOURCE} -o "
           "${DUDU_GENERATED}\n";
    emit_cmake_depends(out, source_tree_files(input));
    out << "    VERBATIM\n"
        << "    WORKING_DIRECTORY ${DUDU_PROJECT_DIR}\n"
        << ")\n\n";
    emit_pkg_config(out, config);
    emit_cmake_target(out, config, project_dir, target);
    emit_cmake_list_values(out, "target_include_directories(" + target + " PRIVATE",
                           config.include_dirs, &project_dir);
    emit_cmake_list_values(out, "target_compile_definitions(" + target + " PRIVATE",
                           config.defines);
    emit_cmake_list_values(out, "target_compile_options(" + target + " PRIVATE", config.flags);
    emit_cmake_list_values(out, "target_link_directories(" + target + " PRIVATE", config.lib_dirs,
                           &project_dir);
    emit_cmake_list_values(out, "target_link_libraries(" + target + " PRIVATE", config.libs);
    if (!config.pkg_config_packages.empty()) {
        out << "target_link_libraries(" << target << " PRIVATE PkgConfig::DUDU_PKG)\n";
    }
    emit_cmake_list_values(out, "target_link_options(" + target + " PRIVATE", config.link_flags);
    return out.str();
}

std::string emit_cmake_cpp_project(const ProjectConfig& config, const std::string& target,
                                   const std::filesystem::path& cpp_source) {
    const std::filesystem::path project_dir =
        config.project_dir.empty() ? std::filesystem::current_path() : config.project_dir;
    std::ostringstream out;
    out << "cmake_minimum_required(VERSION 3.20)\n\n"
        << "project(" << target << " LANGUAGES C CXX)\n\n"
        << "set(CMAKE_CXX_STANDARD " << cmake_cpp_standard(config.cpp_std) << ")\n"
        << "set(CMAKE_CXX_STANDARD_REQUIRED ON)\n\n";
    emit_pkg_config(out, config);
    if (config.target_kind == "library") {
        out << "add_library(" << target << " STATIC\n";
    } else if (config.target_kind == "shared_library") {
        out << "add_library(" << target << " SHARED\n";
    } else {
        out << "add_executable(" << target << "\n";
    }
    out << "    " << cmake_quote(cpp_source.string()) << "\n";
    emit_cmake_sources(out, config, project_dir);
    out << ")\n";
    emit_cmake_list_values(out, "target_include_directories(" + target + " PRIVATE",
                           config.include_dirs, &project_dir);
    emit_cmake_list_values(out, "target_compile_definitions(" + target + " PRIVATE",
                           config.defines);
    emit_cmake_list_values(out, "target_compile_options(" + target + " PRIVATE", config.flags);
    emit_cmake_list_values(out, "target_link_directories(" + target + " PRIVATE", config.lib_dirs,
                           &project_dir);
    emit_cmake_list_values(out, "target_link_libraries(" + target + " PRIVATE", config.libs);
    if (!config.pkg_config_packages.empty()) {
        out << "target_link_libraries(" << target << " PRIVATE PkgConfig::DUDU_PKG)\n";
    }
    emit_cmake_list_values(out, "target_link_options(" + target + " PRIVATE", config.link_flags);
    return out.str();
}

} // namespace dudu
