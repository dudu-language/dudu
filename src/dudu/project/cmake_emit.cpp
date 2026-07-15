#include "dudu/project/cmake_emit.hpp"

#include "dudu/codegen/cpp_emit_modules.hpp"
#include "dudu/core/file_io.hpp"
#include "dudu/project/project_index_cache.hpp"

#include <sstream>
#include <string_view>

namespace dudu {
namespace {

ProjectIndexCache cmake_project_index_cache;

const ProjectIndex& load_cmake_project_index(const ProjectConfig& config,
                                             const std::filesystem::path& input) {
    const std::string source = read_required_text_file(input);
    ProjectIndexOptions options;
    options.entry_path = input;
    options.entry_source = source;
    options.config = config;
    options.source_dir = config.project_dir.empty() ? input.parent_path() : config.project_dir;
    options.force_module_tree = true;
    options.include_native_headers = false;
    options.check_semantics = false;
    return cmake_project_index_cache.get(options);
}

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
    out << "    DEPENDS ${DUDU_EXECUTABLE}";
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

std::string source_path_for_project(const std::filesystem::path& project_dir,
                                    const std::filesystem::path& input) {
    return std::filesystem::relative(std::filesystem::absolute(input), project_dir).string();
}

void emit_cmake_target(std::ostringstream& out, const ProjectConfig& config,
                       const std::filesystem::path& project_dir, const std::string& target,
                       std::string_view generated_var = "DUDU_GENERATED") {
    if (config.target_kind == "library") {
        out << "add_library(" << target << " STATIC\n";
    } else if (config.target_kind == "shared_library") {
        out << "add_library(" << target << " SHARED\n";
    } else {
        out << "add_executable(" << target << "\n";
    }
    out << "    ${" << generated_var << "}\n";
    emit_cmake_sources(out, config, project_dir);
    out << ")\n";
}

void emit_target_artifact_manifest(std::ostringstream& out, const std::string& target) {
    out << "file(GENERATE OUTPUT \"${CMAKE_CURRENT_BINARY_DIR}/dudu-target-artifact.txt\"\n"
        << "    CONTENT \"$<TARGET_FILE:" << target << ">\")\n";
}

std::vector<std::filesystem::path> generated_module_sources(const ModuleAst& module,
                                                            bool test_source = false) {
    return test_source ? cpp_test_module_source_paths(module) : cpp_module_source_paths(module);
}

std::vector<std::filesystem::path> generated_module_artifacts(const ModuleAst& module,
                                                              bool test_source = false) {
    return test_source ? cpp_test_module_artifact_paths(module)
                       : cpp_module_artifact_paths(module);
}

std::vector<std::filesystem::path> dudu_emit_dependencies(const ProjectConfig& config,
                                                          const ProjectIndex& index) {
    std::vector<std::filesystem::path> files = index.source_files();
    if (!config.manifest_path.empty() && std::filesystem::exists(config.manifest_path)) {
        files.push_back(config.manifest_path);
        const std::filesystem::path lock = config.manifest_path.parent_path() / "dudu.lock";
        if (std::filesystem::exists(lock)) {
            files.push_back(lock);
        }
    }
    return files;
}

void emit_generated_module_list(std::ostringstream& out, std::string_view name,
                                const std::filesystem::path& generated_dir,
                                const std::vector<std::filesystem::path>& paths) {
    out << "set(" << name << "\n";
    for (const std::filesystem::path& path : paths) {
        out << "    " << cmake_quote((generated_dir / path).string()) << "\n";
    }
    out << ")\n\n";
}

void emit_pkg_config(std::ostringstream& out, const ProjectConfig& config) {
    if (config.pkg_config_packages.empty()) {
        return;
    }
    if (!config.pkg_config_paths.empty()) {
        std::ostringstream paths;
        for (size_t i = 0; i < config.pkg_config_paths.size(); ++i) {
            if (i > 0) {
                paths << ':';
            }
            paths << project_relative_path(config.project_dir, config.pkg_config_paths[i]).string();
        }
        out << "if(DEFINED ENV{PKG_CONFIG_PATH} AND NOT \"$ENV{PKG_CONFIG_PATH}\" STREQUAL \"\")\n"
            << "    set(ENV{PKG_CONFIG_PATH} "
            << cmake_quote(paths.str() + ":$ENV{PKG_CONFIG_PATH}") << ")\n"
            << "else()\n"
            << "    set(ENV{PKG_CONFIG_PATH} " << cmake_quote(paths.str()) << ")\n"
            << "endif()\n";
    }
    out << "find_package(PkgConfig REQUIRED)\n"
        << "pkg_check_modules(DUDU_PKG REQUIRED IMPORTED_TARGET";
    for (const std::string& package : config.pkg_config_packages) {
        out << ' ' << package;
    }
    out << ")\n\n";
}

void emit_target_mode_options(std::ostringstream& out, const std::string& target,
                              const ProjectConfig& config) {
    if (config.target_mode == "freestanding" || config.target_mode == "embedded") {
        out << "target_compile_options(" << target
            << " PRIVATE \"-ffreestanding\" \"-fno-exceptions\" \"-fno-rtti\")\n";
    }
}

} // namespace

std::string emit_cmake_project(const ProjectConfig& config, const std::filesystem::path& input) {
    const std::string target = config.name.empty() ? input.stem().string() : config.name;
    return emit_cmake_project(config, input, target);
}

std::string emit_cmake_project(const ProjectConfig& config, const std::filesystem::path& input,
                               const std::string& target) {
    const std::filesystem::path project_dir =
        config.project_dir.empty() ? input.parent_path() : config.project_dir;
    const std::string source_path = source_path_for_project(project_dir, input);
    const std::filesystem::path generated_dir =
        std::filesystem::path("${CMAKE_CURRENT_BINARY_DIR}") / "generated";
    const ProjectIndex index = load_cmake_project_index(config, input);
    const ModuleAst& module = index.merged_module();
    const std::vector<std::filesystem::path> generated_sources = generated_module_sources(module);
    const std::vector<std::filesystem::path> generated_artifacts =
        generated_module_artifacts(module);
    std::ostringstream out;
    out << "cmake_minimum_required(VERSION 3.20)\n\n"
        << "project(" << target << " LANGUAGES C CXX)\n\n"
        << "set(CMAKE_CXX_STANDARD " << cmake_cpp_standard(config.cpp_std) << ")\n"
        << "set(CMAKE_CXX_STANDARD_REQUIRED ON)\n"
        << "set(DUDU_EXECUTABLE duc CACHE FILEPATH \"Path to the duc compiler\")\n"
        << "set(DUDU_PROJECT_DIR " << cmake_quote(project_dir.string()) << ")\n"
        << "set(DUDU_SOURCE " << cmake_quote(source_path) << ")\n"
        << "set(DUDU_GENERATED_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated)\n"
        << "set(DUDU_GENERATED_STAMP ${DUDU_GENERATED_DIR}/.dudu_emit.stamp)\n"
        << "set(DUDU_SOURCE_STAMP ${DUDU_GENERATED_DIR}/.dudu_sources.stamp)\n"
        << "set(DUDU_TIMING_ARGS)\n"
        << "if(DUDU_TIMINGS)\n"
        << "    set(DUDU_TIMING_ARGS --timings)\n"
        << "endif()\n\n";
    emit_generated_module_list(out, "DUDU_GENERATED", "${DUDU_GENERATED_DIR}", generated_sources);
    emit_generated_module_list(out, "DUDU_GENERATED_ARTIFACTS", "${DUDU_GENERATED_DIR}",
                               generated_artifacts);
    out << "add_custom_target(" << target << "_dudu_generate\n"
        << "    BYPRODUCTS ${DUDU_GENERATED_STAMP} ${DUDU_GENERATED_ARTIFACTS} "
           "${DUDU_SOURCE_STAMP}\n"
        << "    COMMAND ${CMAKE_COMMAND} -E make_directory ${DUDU_GENERATED_DIR}\n"
        << "    COMMAND ${DUDU_EXECUTABLE} emit-modules ${DUDU_TIMING_ARGS} "
           "${DUDU_PROJECT_DIR}/${DUDU_SOURCE} -o "
           "${DUDU_GENERATED_DIR}\n"
        << "    COMMAND ${CMAKE_COMMAND} -E touch ${DUDU_GENERATED_STAMP}\n";
    emit_cmake_depends(out, dudu_emit_dependencies(config, index));
    out << "    COMMENT \"Dudu emit modules\"\n"
        << "    VERBATIM\n"
        << "    WORKING_DIRECTORY ${DUDU_PROJECT_DIR}\n"
        << ")\n"
        << "set_source_files_properties(${DUDU_GENERATED} PROPERTIES GENERATED TRUE)\n\n";
    emit_pkg_config(out, config);
    emit_cmake_target(out, config, project_dir, target);
    emit_target_artifact_manifest(out, target);
    out << "add_dependencies(" << target << ' ' << target << "_dudu_generate)\n";
    out << "target_include_directories(" << target << " PRIVATE ${DUDU_GENERATED_DIR})\n";
    out << "target_precompile_headers(" << target
        << " PRIVATE \"$<$<COMPILE_LANGUAGE:CXX>:${DUDU_GENERATED_DIR}/dudu_runtime.hpp>\")\n";
    emit_cmake_list_values(out, "target_include_directories(" + target + " PRIVATE",
                           config.include_dirs, &project_dir);
    emit_cmake_list_values(out, "target_compile_definitions(" + target + " PRIVATE",
                           config.defines);
    emit_cmake_list_values(out, "target_compile_options(" + target + " PRIVATE", config.flags);
    emit_target_mode_options(out, target, config);
    emit_cmake_list_values(out, "target_link_directories(" + target + " PRIVATE", config.lib_dirs,
                           &project_dir);
    emit_cmake_list_values(out, "target_link_libraries(" + target + " PRIVATE", config.libs);
    if (!config.pkg_config_packages.empty()) {
        out << "target_link_libraries(" << target << " PRIVATE PkgConfig::DUDU_PKG)\n";
    }
    emit_cmake_list_values(out, "target_link_options(" + target + " PRIVATE", config.link_flags);
    return out.str();
}

std::string emit_cmake_test_project(const ProjectConfig& config, const std::filesystem::path& input,
                                    const std::string& target, const std::string& filter,
                                    bool capture_output) {
    const std::filesystem::path project_dir =
        config.project_dir.empty() ? input.parent_path() : config.project_dir;
    const std::string source_path = source_path_for_project(project_dir, input);
    const std::filesystem::path generated_dir =
        std::filesystem::path("${CMAKE_CURRENT_BINARY_DIR}") / "generated";
    const ProjectIndex index = load_cmake_project_index(config, input);
    const ModuleAst& module = index.merged_module();
    const std::vector<std::filesystem::path> generated_sources =
        generated_module_sources(module, true);
    const std::vector<std::filesystem::path> generated_artifacts =
        generated_module_artifacts(module, true);
    std::ostringstream out;
    out << "cmake_minimum_required(VERSION 3.20)\n\n"
        << "project(" << target << " LANGUAGES C CXX)\n\n"
        << "set(CMAKE_CXX_STANDARD " << cmake_cpp_standard(config.cpp_std) << ")\n"
        << "set(CMAKE_CXX_STANDARD_REQUIRED ON)\n"
        << "set(DUDU_EXECUTABLE duc CACHE FILEPATH \"Path to the duc compiler\")\n"
        << "set(DUDU_PROJECT_DIR " << cmake_quote(project_dir.string()) << ")\n"
        << "set(DUDU_SOURCE " << cmake_quote(source_path) << ")\n"
        << "set(DUDU_GENERATED_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated)\n"
        << "set(DUDU_GENERATED_STAMP ${DUDU_GENERATED_DIR}/.dudu_emit.stamp)\n"
        << "set(DUDU_SOURCE_STAMP ${DUDU_GENERATED_DIR}/.dudu_test_sources.stamp)\n"
        << "set(DUDU_TIMING_ARGS)\n"
        << "if(DUDU_TIMINGS)\n"
        << "    set(DUDU_TIMING_ARGS --timings)\n"
        << "endif()\n\n";
    emit_generated_module_list(out, "DUDU_GENERATED", "${DUDU_GENERATED_DIR}", generated_sources);
    emit_generated_module_list(out, "DUDU_GENERATED_ARTIFACTS", "${DUDU_GENERATED_DIR}",
                               generated_artifacts);
    out << "add_custom_target(" << target << "_dudu_generate\n"
        << "    BYPRODUCTS ${DUDU_GENERATED_STAMP} ${DUDU_GENERATED_ARTIFACTS} "
           "${DUDU_SOURCE_STAMP}\n"
        << "    COMMAND ${CMAKE_COMMAND} -E make_directory ${DUDU_GENERATED_DIR}\n"
        << "    COMMAND ${DUDU_EXECUTABLE} emit-test-modules ${DUDU_TIMING_ARGS} "
           "${DUDU_PROJECT_DIR}/${DUDU_SOURCE} "
           "-o ${DUDU_GENERATED_DIR}";
    if (!filter.empty()) {
        out << " --filter " << cmake_quote(filter);
    }
    if (!capture_output) {
        out << " --no-capture";
    }
    out << "\n"
        << "    COMMAND ${CMAKE_COMMAND} -E touch ${DUDU_GENERATED_STAMP}\n";
    emit_cmake_depends(out, dudu_emit_dependencies(config, index));
    out << "    COMMENT \"Dudu emit test modules\"\n"
        << "    VERBATIM\n"
        << "    WORKING_DIRECTORY ${DUDU_PROJECT_DIR}\n"
        << ")\n"
        << "set_source_files_properties(${DUDU_GENERATED} PROPERTIES GENERATED TRUE)\n\n";
    emit_pkg_config(out, config);
    out << "add_executable(" << target << "\n"
        << "    ${DUDU_GENERATED}\n";
    emit_cmake_sources(out, config, project_dir);
    out << ")\n";
    emit_target_artifact_manifest(out, target);
    out << "add_dependencies(" << target << ' ' << target << "_dudu_generate)\n";
    out << "target_include_directories(" << target << " PRIVATE ${DUDU_GENERATED_DIR})\n";
    out << "target_precompile_headers(" << target
        << " PRIVATE \"$<$<COMPILE_LANGUAGE:CXX>:${DUDU_GENERATED_DIR}/dudu_runtime.hpp>\")\n";
    emit_cmake_list_values(out, "target_include_directories(" + target + " PRIVATE",
                           config.include_dirs, &project_dir);
    emit_cmake_list_values(out, "target_compile_definitions(" + target + " PRIVATE",
                           config.defines);
    emit_cmake_list_values(out, "target_compile_options(" + target + " PRIVATE", config.flags);
    emit_target_mode_options(out, target, config);
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
