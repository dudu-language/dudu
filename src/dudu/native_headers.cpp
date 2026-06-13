#include "dudu/native_headers.hpp"

#include "dudu/native_build.hpp"
#include "dudu/source.hpp"

#include <chrono>
#include <cstdlib>
#include <fstream>
#include <regex>
#include <set>
#include <stdexcept>

namespace dudu {
namespace {

bool is_foreign(const ImportDecl& import) {
    return import.kind == ImportKind::ForeignC || import.kind == ImportKind::ForeignCpp;
}

std::string unquoted(std::string value) {
    if (value.size() >= 2 && value.front() == '"' && value.back() == '"') {
        return value.substr(1, value.size() - 2);
    }
    return value;
}

std::string read_text(const std::filesystem::path& path) {
    std::ifstream in(path);
    if (!in) {
        return {};
    }
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

void write_text(const std::filesystem::path& path, const std::string& text) {
    std::ofstream out(path);
    if (!out) {
        throw std::runtime_error("could not write " + path.string());
    }
    out << text;
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

std::filesystem::path absolute_from(const std::filesystem::path& base,
                                    const std::filesystem::path& path) {
    return path.is_absolute() ? path : base / path;
}

std::vector<std::filesystem::path> env_include_dirs() {
    const char* raw = std::getenv("CPLUS_INCLUDE_PATH");
    if (raw == nullptr) {
        return {};
    }
    std::vector<std::filesystem::path> out;
    std::string text = raw;
    size_t start = 0;
    while (start <= text.size()) {
        const size_t end = text.find(':', start);
        std::string item = text.substr(start, end == std::string::npos ? end : end - start);
        if (!item.empty()) {
            out.emplace_back(std::move(item));
        }
        if (end == std::string::npos) {
            break;
        }
        start = end + 1;
    }
    return out;
}

bool can_resolve_header(const ImportDecl& import, const NativeHeaderOptions& options) {
    const std::filesystem::path header = unquoted(import.module_path);
    if (std::filesystem::exists(absolute_from(options.source_dir, header))) {
        return true;
    }
    for (const std::string& include_dir : options.config.include_dirs) {
        if (std::filesystem::exists(absolute_from(options.source_dir, include_dir) / header)) {
            return true;
        }
    }
    for (const std::filesystem::path& include_dir : env_include_dirs()) {
        if (std::filesystem::exists(absolute_from(options.source_dir, include_dir) / header)) {
            return true;
        }
    }
    return false;
}

std::string scanner_flags(const NativeHeaderOptions& options) {
    std::string flags = " -I" + shell_quote_arg(options.source_dir.string());
    for (const std::string& include_dir : options.config.include_dirs) {
        flags +=
            " " + shell_quote_arg("-I" + absolute_from(options.source_dir, include_dir).string());
    }
    for (const std::string& define : options.config.defines) {
        flags += " " + shell_quote_arg("-D" + define);
    }
    for (const std::string& flag : options.config.flags) {
        flags += " " + shell_quote_arg(flag);
    }
    const std::string pkg_flags = capture_pkg_config_cflags(options.config.pkg_config_packages);
    if (!pkg_flags.empty()) {
        flags += " " + pkg_flags;
    }
    return flags;
}

void add_type(std::vector<NativeTypeDecl>& out, std::set<std::string>& seen,
              const std::string& name, const SourceLocation& location) {
    if (name.empty() || !seen.insert(name).second) {
        return;
    }
    out.push_back({.name = name, .location = location});
}

std::vector<NativeTypeDecl> parse_ast_dump(const std::string& dump,
                                           const SourceLocation& location) {
    static const std::regex typedef_decl(
        R"((TypedefDecl|TypeAliasDecl).*\b([A-Za-z_][A-Za-z0-9_]*) ')");
    static const std::regex record_decl(
        R"((RecordDecl|CXXRecordDecl).*\b(struct|class|union) ([A-Za-z_][A-Za-z0-9_]*)\b)");
    static const std::regex enum_decl(R"(EnumDecl.*\b([A-Za-z_][A-Za-z0-9_]*)\b)");
    std::vector<NativeTypeDecl> out;
    std::set<std::string> seen;
    size_t start = 0;
    while (start < dump.size()) {
        const size_t end = dump.find('\n', start);
        const std::string line = dump.substr(start, end == std::string::npos ? end : end - start);
        std::smatch match;
        if (std::regex_search(line, match, typedef_decl)) {
            add_type(out, seen, match[2].str(), location);
        } else if (std::regex_search(line, match, record_decl)) {
            add_type(out, seen, match[3].str(), location);
        } else if (std::regex_search(line, match, enum_decl)) {
            add_type(out, seen, match[1].str(), location);
        }
        if (end == std::string::npos) {
            break;
        }
        start = end + 1;
    }
    return out;
}

std::filesystem::path temp_base(const std::filesystem::path& source_dir) {
    const auto ticks = std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    return source_dir / (".dudu_native_headers_" + ticks);
}

} // namespace

std::vector<NativeTypeDecl> scan_native_header_types(const ModuleAst& module,
                                                     const NativeHeaderOptions& options) {
    std::vector<const ImportDecl*> imports;
    for (const ImportDecl& import : module.imports) {
        if (is_foreign(import) && can_resolve_header(import, options)) {
            imports.push_back(&import);
        }
    }
    if (imports.empty()) {
        return {};
    }

    std::string source;
    for (const ImportDecl* import : imports) {
        source += "#include \"" + unquoted(import->module_path) + "\"\n";
    }
    source += "int dudu_native_header_probe = 0;\n";

    const std::filesystem::path base = temp_base(options.source_dir);
    const std::filesystem::path cpp = base.string() + ".cpp";
    const std::filesystem::path ast = base.string() + ".ast";
    const std::filesystem::path err = base.string() + ".err";
    write_text(cpp, source);

    const char* clang_env = std::getenv("CLANGXX");
    const std::string clang = clang_env == nullptr ? "clang++" : clang_env;
    const std::string command =
        shell_quote_arg(clang) + " -std=" + shell_quote_arg(options.config.cpp_std) +
        " -x c++ -fsyntax-only -fno-color-diagnostics -Xclang -ast-dump " + shell_quote_path(cpp) +
        scanner_flags(options) + " >" + shell_quote_path(ast) + " 2>" + shell_quote_path(err);
    const SourceLocation location = imports.front()->location;
    const int status = std::system(command.c_str());
    if (status != 0) {
        std::filesystem::remove(cpp);
        std::filesystem::remove(ast);
        std::filesystem::remove(err);
        return {};
    }
    std::vector<NativeTypeDecl> out = parse_ast_dump(read_text(ast), location);
    std::filesystem::remove(cpp);
    std::filesystem::remove(ast);
    std::filesystem::remove(err);
    return out;
}

void merge_native_header_types(ModuleAst& module, const NativeHeaderOptions& options) {
    std::set<std::string> existing;
    for (const NativeTypeDecl& type : module.native_types) {
        existing.insert(type.name);
    }
    for (NativeTypeDecl type : scan_native_header_types(module, options)) {
        if (existing.insert(type.name).second) {
            module.native_types.push_back(std::move(type));
        }
    }
}

} // namespace dudu
