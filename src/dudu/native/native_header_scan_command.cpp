#include "dudu/native/native_header_scan_command.hpp"

#include "dudu/core/file_io.hpp"
#include "dudu/core/text.hpp"
#include "dudu/native/native_build.hpp"
#include "dudu/project/project_driver.hpp"
#include "dudu/support/executable.hpp"

#include <cctype>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <sstream>
#include <stdexcept>

namespace dudu {
namespace {

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

std::vector<std::string> split_command_words(std::string_view text) {
    std::vector<std::string> out;
    std::string current;
    char quote = '\0';
    bool escaped = false;
    for (const char ch : text) {
        if (escaped) {
            current.push_back(ch);
            escaped = false;
            continue;
        }
        if (ch == '\\') {
            escaped = true;
            continue;
        }
        if (quote != '\0') {
            if (ch == quote) {
                quote = '\0';
            } else {
                current.push_back(ch);
            }
            continue;
        }
        if (ch == '\'' || ch == '"') {
            quote = ch;
            continue;
        }
        if (std::isspace(static_cast<unsigned char>(ch)) != 0) {
            if (!current.empty()) {
                out.push_back(std::move(current));
                current.clear();
            }
            continue;
        }
        current.push_back(ch);
    }
    if (escaped) {
        current.push_back('\\');
    }
    if (!current.empty()) {
        out.push_back(std::move(current));
    }
    return out;
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
    return "|" + path.string() + "|" + std::to_string(size) + "|" + file_time_stamp(mtime);
}

std::string compiler_identity(const std::string& command) {
    std::filesystem::path executable =
        find_executable(command).value_or(std::filesystem::path(command));

    std::error_code error;
    const std::filesystem::path canonical = std::filesystem::weakly_canonical(executable, error);
    if (!error) {
        executable = canonical;
    }
    const auto size = std::filesystem::file_size(executable, error);
    if (error) {
        return command;
    }
    const auto mtime = std::filesystem::last_write_time(executable, error);
    if (error) {
        return command;
    }
    return executable.string() + "|" + std::to_string(size) + "|" + file_time_stamp(mtime);
}

std::string include_line(const ImportDecl& import) {
    if (import.native_include_style == NativeIncludeStyle::System) {
        return "#include <" + native_header_unquoted(import.module_path) + ">\n";
    }
    return "#include \"" + native_header_unquoted(import.module_path) + "\"\n";
}

std::string scanner_environment_identity() {
    std::string identity;
    for (const char* name :
         {"CPATH", "CPLUS_INCLUDE_PATH", "C_INCLUDE_PATH", "SDKROOT", "MACOSX_DEPLOYMENT_TARGET"}) {
        const char* value = std::getenv(name);
        identity += "|";
        identity += name;
        identity += "=";
        if (value != nullptr) {
            identity += value;
        }
    }
    return identity;
}

} // namespace

std::string native_header_unquoted(std::string value) {
    if (value.size() >= 2 && value.front() == '"' && value.back() == '"') {
        return value.substr(1, value.size() - 2);
    }
    return value;
}

std::optional<std::filesystem::path>
resolve_existing_native_header_path(const ImportDecl& import, const NativeHeaderOptions& options) {
    const std::filesystem::path header = native_header_unquoted(import.module_path);
    const std::filesystem::path path =
        header.is_absolute() ? header : (options.source_dir / header).lexically_normal();
    std::error_code error;
    if (!std::filesystem::exists(path, error) || error) {
        return std::nullopt;
    }
    return path.lexically_normal();
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
    for (const std::string& arg : native_header_scanner_arguments(options, include_source_dir)) {
        flags += " " + shell_quote_arg(arg);
    }
    return flags;
}

std::vector<std::string> native_header_scanner_arguments(const NativeHeaderOptions& options,
                                                         bool include_source_dir) {
    std::vector<std::string> args;
    if (include_source_dir) {
        args.push_back("-I" + options.source_dir.lexically_normal().string());
    }
    for (const std::string& include_dir : options.config.include_dirs) {
        args.push_back("-I" +
                       project_path(options.config, include_dir).lexically_normal().string());
    }
    for (const std::string& define : options.config.defines) {
        args.push_back("-D" + define);
    }
    for (const std::string& flag : options.config.flags) {
        args.push_back(flag);
    }
    std::vector<std::string> pkg_args =
        split_command_words(capture_pkg_config_cflags(options.config.pkg_config_packages));
    args.insert(args.end(), std::make_move_iterator(pkg_args.begin()),
                std::make_move_iterator(pkg_args.end()));
    return args;
}

std::filesystem::path native_header_temp_base(const std::filesystem::path& source_dir) {
    const auto ticks = std::to_string(std::chrono::duration_cast<std::chrono::nanoseconds>(
                                          std::chrono::steady_clock::now().time_since_epoch())
                                          .count());
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

std::string native_header_compiler_identity(const std::string& command) {
    return compiler_identity(command);
}

std::string native_header_scan_key(const ImportDecl& import, const NativeHeaderOptions& options,
                                   const std::string& flags) {
    return native_header_scan_key(std::span<const ImportDecl>(&import, 1), options, flags);
}

std::string native_header_scan_key(std::span<const ImportDecl> imports,
                                   const NativeHeaderOptions& options, const std::string& flags) {
    std::string key = "v12-rich-metadata|" +
                      native_header_compiler_identity(native_header_clangxx_command()) + "|" +
                      options.config.cpp_std + "|" + flags + scanner_environment_identity();
    for (const ImportDecl& import : imports) {
        key += "|";
        key += import.native_include_style == NativeIncludeStyle::System ? "system|" : "path|";
        key += native_header_unquoted(import.module_path);
        key += header_stamp(import, options);
    }
    return key;
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
    return native_header_scanner_source_for_headers(std::span<const ImportDecl>(&import, 1),
                                                    with_c_prelude);
}

std::string native_header_scanner_source_for_headers(std::span<const ImportDecl> imports,
                                                     bool with_c_prelude) {
    std::string source;
    if (with_c_prelude) {
        source += "#include <stddef.h>\n#include <stdio.h>\n";
    }
    for (const ImportDecl& import : imports) {
        source += include_line(import);
    }
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
        command += "-fparse-all-comments -Xclang -ast-dump ";
    }
    return command + shell_quote_path(cpp) + flags;
}

std::string native_header_scan_error_message(const ImportDecl& import, std::string detail,
                                             const std::string& clang) {
    std::ostringstream out;
    out << "could not scan native header " << native_header_unquoted(import.module_path);
    detail = trim_string(std::move(detail));
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
