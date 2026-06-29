#include "dudu/native/native_header_scan_command.hpp"

#include "dudu/codegen/cpp_lower.hpp"
#include "dudu/core/file_io.hpp"
#include "dudu/native/native_build.hpp"
#include "dudu/project/project_driver.hpp"

#include <chrono>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace dudu {
namespace {

void append_include_flag(std::string& flags, const std::filesystem::path& path) {
    flags += " " + shell_quote_arg("-I" + path.lexically_normal().string());
}

std::string capture_pkg_config_cflags(const std::vector<std::string>& packages) {
    if (packages.empty()) {
        return {};
    }
    const char* pkg_config = std::getenv("PKG_CONFIG");
    std::string command =
        shell_quote_arg(pkg_config == nullptr ? "pkg-config" : std::string(pkg_config)) +
        " --cflags";
    for (const std::string& package : packages) {
        command += " " + shell_quote_arg(package);
    }
    command += " 2>/dev/null";
    FILE* pipe = popen(command.c_str(), "r");
    if (pipe == nullptr) {
        return {};
    }
    std::string output;
    char buffer[256];
    while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
        output += buffer;
    }
    if (pclose(pipe) != 0) {
        return {};
    }
    return output;
}

std::string header_stamp(const ImportDecl& import, const NativeHeaderOptions& options) {
    const std::filesystem::path header = native_header_unquoted(import.module_path);
    const std::filesystem::path path =
        header.is_absolute() ? header : (options.source_dir / header).lexically_normal();
    std::error_code error;
    if (!std::filesystem::exists(path, error) || error) {
        return {};
    }
    const auto size = std::filesystem::file_size(path, error);
    if (error) {
        return {};
    }
    const auto mtime = std::filesystem::last_write_time(path, error);
    if (error) {
        return {};
    }
    return "|" + path.string() + "|" + std::to_string(size) + "|" +
           std::to_string(mtime.time_since_epoch().count());
}

std::string include_line(const std::string& header) {
    return "#include \"" + header + "\"\n";
}

} // namespace

std::string native_header_unquoted(std::string value) {
    if (value.size() >= 2 && value.front() == '"' && value.back() == '"') {
        return value.substr(1, value.size() - 2);
    }
    return value;
}

std::string native_header_read_text(const std::filesystem::path& path) {
    return try_read_text_file(path).value_or("");
}

void native_header_write_text(const std::filesystem::path& path, const std::string& text) {
    std::ofstream out(path);
    if (!out) {
        throw std::runtime_error("could not write " + path.string());
    }
    out << text;
}

std::string native_header_scanner_flags(const NativeHeaderOptions& options,
                                        bool include_source_dir) {
    std::string flags;
    if (include_source_dir) {
        append_include_flag(flags, options.source_dir);
    }
    for (const std::string& include_dir : options.config.include_dirs) {
        append_include_flag(flags, project_path(options.config, include_dir));
    }
    for (const std::string& define : options.config.defines) {
        flags += " " + shell_quote_arg("-D" + define);
    }
    for (const std::string& flag : options.config.flags) {
        flags += " " + shell_quote_arg(flag);
    }
    const std::string pkg_flags =
        trim_copy(capture_pkg_config_cflags(options.config.pkg_config_packages));
    return pkg_flags.empty() ? flags : flags + " " + pkg_flags;
}

std::filesystem::path native_header_temp_base(const std::filesystem::path& source_dir) {
    const auto ticks = std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    std::error_code error;
    const std::filesystem::path dir =
        std::filesystem::temp_directory_path(error) / "dudu-native-headers";
    if (!error) {
        std::filesystem::create_directories(dir, error);
        if (!error) {
            return dir / ("dudu_native_headers_" + ticks);
        }
    }
    return source_dir / (".dudu_native_headers_" + ticks);
}

std::string native_header_scan_key(const ImportDecl& import, const NativeHeaderOptions& options,
                                   const std::string& flags) {
    return "v3|" + native_header_unquoted(import.module_path) + "|" +
           native_header_clangxx_command() + "|" + options.config.cpp_std + "|" + flags +
           header_stamp(import, options);
}

std::string native_header_run_capture(const std::string& command,
                                      const std::filesystem::path& output,
                                      const std::filesystem::path& error) {
    const int status = std::system(
        (command + " >" + shell_quote_path(output) + " 2>" + shell_quote_path(error)).c_str());
    if (status != 0) {
        return {};
    }
    return native_header_read_text(output);
}

std::string native_header_scanner_source_for_header(const ImportDecl& import, bool with_c_prelude) {
    std::string source;
    if (with_c_prelude) {
        source += "#include <stddef.h>\n#include <stdio.h>\n";
    }
    source += include_line(native_header_unquoted(import.module_path));
    source += "int dudu_probe = 0;\n";
    return source;
}

std::string native_header_clangxx_command() {
    const char* clang_env = std::getenv("CLANGXX");
    return clang_env == nullptr ? "clang++" : std::string(clang_env);
}

std::string native_header_clang_base_command(const NativeHeaderOptions& options,
                                             const std::filesystem::path& cpp, bool ast_dump,
                                             const std::string& flags) {
    const std::string clang = native_header_clangxx_command();
    std::string command = shell_quote_arg(clang) +
                          " -std=" + shell_quote_arg(options.config.cpp_std) +
                          " -x c++ -fsyntax-only -fno-color-diagnostics ";
    if (ast_dump) {
        command += "-Xclang -ast-dump ";
    }
    return command + shell_quote_path(cpp) + flags;
}

std::string native_header_scan_error_message(const ImportDecl& import, std::string detail,
                                             const std::string& clang) {
    std::ostringstream out;
    out << "could not scan native header " << native_header_unquoted(import.module_path);
    detail = trim_copy(std::move(detail));
    if (!detail.empty()) {
        out << "\n" << detail;
    }
    if (!clang.empty() && detail.find(clang) != std::string::npos) {
        out << "\nhint: native header awareness requires clang++; install clang or set CLANGXX";
    } else if (detail.find("not found") != std::string::npos ||
               detail.find("No such file") != std::string::npos) {
        out << "\nhint: add the header directory to [include].paths or the package to [pkg].libs";
    } else {
        out << "\nhint: native header awareness requires clang++; install clang or set CLANGXX";
    }
    return out.str();
}

} // namespace dudu
