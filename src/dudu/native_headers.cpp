#include "dudu/native_headers.hpp"
#include "dudu/cpp_lower.hpp"
#include "dudu/native_build.hpp"
#include "dudu/native_header_cache.hpp"
#include "dudu/native_header_merge.hpp"
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <map>
#include <regex>
#include <set>
#include <sstream>
#include <stdexcept>
namespace dudu {
namespace {
bool is_foreign(const ImportDecl& import) {
    return import.kind == ImportKind::ForeignC || import.kind == ImportKind::ForeignCpp;
}
bool direct_import(const ImportDecl& import) {
    return import.alias.empty();
}
std::string unquoted(std::string value) {
    if (value.size() >= 2 && value.front() == '"' && value.back() == '"') {
        return value.substr(1, value.size() - 2);
    }
    return value;
}
std::filesystem::path absolute_from(const std::filesystem::path& base,
                                    const std::filesystem::path& path) {
    return path.is_absolute() ? path : base / path;
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
    return pkg_flags.empty() ? flags : flags + " " + pkg_flags;
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
std::filesystem::path temp_base(const std::filesystem::path& source_dir) {
    const auto ticks = std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    return source_dir / (".dudu_native_headers_" + ticks);
}
std::string run_capture(const std::string& command, const std::filesystem::path& output,
                        const std::filesystem::path& error) {
    const int status = std::system((command + " >" + shell_quote_path(output) + " 2>" +
                                    shell_quote_path(error))
                                       .c_str());
    if (status != 0) {
        return {};
    }
    return read_text(output);
}
std::string clang_base_command(const NativeHeaderOptions& options, const std::filesystem::path& cpp,
                               bool ast_dump) {
    const char* clang_env = std::getenv("CLANGXX");
    const std::string clang = clang_env == nullptr ? "clang++" : clang_env;
    std::string command = shell_quote_arg(clang) + " -std=" + shell_quote_arg(options.config.cpp_std) +
                          " -x c++ -fsyntax-only -fno-color-diagnostics ";
    if (ast_dump) {
        command += "-Xclang -ast-dump ";
    }
    return command + shell_quote_path(cpp) + scanner_flags(options);
}
int ast_depth(const std::string& line) {
    int depth = 0;
    for (size_t i = 0; i + 1 < line.size(); i += 2) {
        if (line[i] != '|' || line[i + 1] != ' ') {
            break;
        }
        ++depth;
    }
    return depth;
}
std::string join_scope(const std::vector<std::pair<int, std::string>>& namespaces,
                       const std::string& name) {
    std::string out;
    for (const auto& [depth, ns] : namespaces) {
        (void)depth;
        out += ns + ".";
    }
    return out + name;
}
std::string dudu_type(std::string type) {
    type = trim_copy(std::move(type));
    while (starts_with(type, "const ")) {
        type = trim_copy(type.substr(6));
    }
    if (type == "bool") return "bool";
    if (type == "void") return "void";
    if (type == "float") return "f32";
    if (type == "double") return "f64";
    if (type == "char *" || type == "const char *") return "cstr";
    if (type == "int8_t" || type == "Sint8" || type == "signed char") return "i8";
    if (type == "uint8_t" || type == "Uint8" || type == "unsigned char") return "u8";
    if (type == "int16_t" || type == "Sint16" || type == "short") return "i16";
    if (type == "uint16_t" || type == "Uint16" || type == "unsigned short") return "u16";
    if (type == "int" || type == "int32_t" || type == "Sint32") return "i32";
    if (type == "unsigned int" || type == "uint32_t" || type == "Uint32") return "u32";
    if (type == "long" || type == "long long" || type == "int64_t" || type == "Sint64") return "i64";
    if (type == "unsigned long" || type == "unsigned long long" || type == "uint64_t" || type == "Uint64") return "u64";
    if (type.find('*') != std::string::npos) return "auto";
    return type.empty() ? "auto" : type;
}
std::vector<std::string> signature_params(const std::string& signature) {
    const size_t open = signature.find('(');
    const size_t close = signature.rfind(')');
    if (open == std::string::npos || close == std::string::npos || close <= open + 1) return {};
    std::vector<std::string> out;
    for (std::string part : split_top_level_args(signature.substr(open + 1, close - open - 1))) {
        if (part == "...") continue;
        out.push_back(dudu_type(std::move(part)));
    }
    return out;
}
std::string signature_return_type(const std::string& signature) {
    const size_t open = signature.find('(');
    return dudu_type(open == std::string::npos ? signature : signature.substr(0, open));
}
template <typename T>
void add_unique(std::vector<T>& out, std::set<std::string>& seen, T value) {
    if (seen.insert(value.name).second) {
        out.push_back(std::move(value));
    }
}
void add_unique_function(std::vector<NativeFunctionDecl>& out, std::set<std::string>& seen,
                         NativeFunctionDecl value) {
    if (seen.insert(native_function_key(value)).second) out.push_back(std::move(value));
}
void parse_ast_line(NativeHeaderScan& scan, const std::string& line,
                    std::vector<std::pair<int, std::string>>& namespaces,
                    std::vector<std::pair<int, size_t>>& classes,
                    const SourceLocation& location) {
    static const std::regex typedef_decl(R"((TypedefDecl|TypeAliasDecl).*\b([A-Za-z_][A-Za-z0-9_]*) ')");
    static const std::regex record_decl(
        R"((RecordDecl|CXXRecordDecl).*\b(struct|class|union) ([A-Za-z_][A-Za-z0-9_]*)\b)");
    static const std::regex enum_decl(R"(EnumDecl.*\b([A-Za-z_][A-Za-z0-9_]*)\b)");
    static const std::regex ns_decl(R"(NamespaceDecl.*\b([A-Za-z_][A-Za-z0-9_]*)$)");
    static const std::regex fn_decl(R"(FunctionDecl.*\b([A-Za-z_][A-Za-z0-9_]*) '([^']*)')");
    static const std::regex method_decl(
        R"(CXXMethodDecl.*\b([A-Za-z_][A-Za-z0-9_]*) '([^']*)')");
    static const std::regex ctor_decl(
        R"(CXXConstructorDecl.*\b([A-Za-z_][A-Za-z0-9_]*) '([^']*)')");
    static const std::regex field_decl(R"(FieldDecl.*\b([A-Za-z_][A-Za-z0-9_]*) '([^']*)')");
    static const std::regex enum_value_decl(R"(EnumConstantDecl.*\b([A-Za-z_][A-Za-z0-9_]*) ')");
    static const std::regex var_decl(R"(VarDecl.*\b([A-Za-z_][A-Za-z0-9_]*) '([^']*)')");
    const int depth = ast_depth(line);
    while (!namespaces.empty() && namespaces.back().first >= depth) {
        namespaces.pop_back();
    }
    while (!classes.empty() && classes.back().first >= depth) {
        classes.pop_back();
    }
    std::smatch match;
    if (std::regex_search(line, match, ns_decl)) {
        const std::string name = match[1].str();
        if (starts_with(name, "__")) {
            return;
        }
        namespaces.push_back({depth, name});
        scan.namespaces.push_back({.name = name, .location = location});
    } else if (std::regex_search(line, match, typedef_decl)) {
        const std::string name = match[2].str();
        if (!starts_with(name, "__")) {
            scan.types.push_back({.name = name, .location = location});
        }
    } else if (std::regex_search(line, match, record_decl)) {
        const std::string name = match[3].str();
        if (!starts_with(name, "__")) {
            scan.types.push_back({.name = name, .location = location});
            if (line.find(" definition") != std::string::npos) {
                ClassDecl klass;
                klass.name = name;
                klass.location = location;
                scan.classes.push_back(std::move(klass));
                classes.push_back({depth, scan.classes.size() - 1});
            }
        }
    } else if (std::regex_search(line, match, enum_decl)) {
        const std::string name = match[1].str();
        if (!starts_with(name, "__")) {
            scan.types.push_back({.name = name, .location = location});
        }
    } else if (std::regex_search(line, match, fn_decl)) {
        const std::string name = join_scope(namespaces, match[1].str());
        if (starts_with(name, "__")) {
            return;
        }
        const std::string signature = match[2].str();
        scan.functions.push_back({.name = name,
                                  .params = signature_params(signature),
                                  .return_type = signature_return_type(signature),
                                  .variadic = signature.find("...") != std::string::npos,
                                  .location = location});
    } else if (!classes.empty() && std::regex_search(line, match, method_decl)) {
        FunctionDecl method;
        method.name = match[1].str();
        method.return_type = signature_return_type(match[2].str());
        for (const std::string& param : signature_params(match[2].str())) {
            ParamDecl decl;
            decl.name = "arg" + std::to_string(method.params.size());
            decl.type = param;
            decl.location = location;
            method.params.push_back(std::move(decl));
        }
        method.location = location;
        scan.classes[classes.back().second].methods.push_back(std::move(method));
    } else if (!classes.empty() && std::regex_search(line, match, ctor_decl)) {
        const std::vector<std::string> params = signature_params(match[2].str());
        if (!params.empty()) {
            FunctionDecl ctor;
            ctor.name = "__init__";
            for (const std::string& param : params) {
                ParamDecl decl;
                decl.name = "arg" + std::to_string(ctor.params.size());
                decl.type = param;
                decl.location = location;
                ctor.params.push_back(std::move(decl));
            }
            ctor.location = location;
            scan.classes[classes.back().second].methods.push_back(std::move(ctor));
        }
    } else if (!classes.empty() && std::regex_search(line, match, field_decl)) {
        scan.classes[classes.back().second].fields.push_back(
            {.name = match[1].str(), .type = dudu_type(match[2].str()), .location = location});
    } else if (std::regex_search(line, match, enum_value_decl)) {
        const std::string name = match[1].str();
        if (!starts_with(name, "__")) {
            scan.values.push_back({.name = name, .type = "i32", .location = location});
        }
    } else if (line.find("ParmVarDecl") == std::string::npos && std::regex_search(line, match, var_decl)) {
        const std::string name = match[1].str();
        if (!starts_with(name, "__")) {
            scan.values.push_back(
                {.name = name, .type = dudu_type(match[2].str()), .location = location});
        }
    }
}
void parse_ast_dump(NativeHeaderScan& scan, const std::string& dump,
                    const SourceLocation& location) {
    std::vector<std::pair<int, std::string>> namespaces;
    std::vector<std::pair<int, size_t>> classes;
    std::istringstream in(dump);
    std::string line;
    while (std::getline(in, line)) {
        parse_ast_line(scan, line, namespaces, classes, location);
    }
}
int macro_arity(std::string args) {
    args = trim_copy(std::move(args));
    if (args.empty()) {
        return 0;
    }
    return static_cast<int>(split_top_level_args(args).size());
}
void parse_macro_dump(NativeHeaderScan& scan, const std::string& dump,
                      const SourceLocation& location) {
    static const std::regex function_macro(R"(^#define ([A-Z_][A-Z0-9_]*)\(([^)]*)\))");
    static const std::regex object_macro(R"(^#define ([A-Z_][A-Z0-9_]*)(\s|$))");
    std::istringstream in(dump);
    std::string line;
    while (std::getline(in, line)) {
        std::smatch match;
        if (std::regex_search(line, match, function_macro)) {
            const std::string name = match[1].str();
            if (starts_with(name, "_")) {
                continue;
            }
            scan.macros.push_back({.name = match[1].str(),
                                   .arity = macro_arity(match[2].str()),
                                   .function_like = true,
                                   .location = location});
            scan.functions.push_back({.name = name,
                                      .params = std::vector<std::string>(
                                          static_cast<size_t>(macro_arity(match[2].str())),
                                          "auto"),
                                      .return_type = "auto",
                                      .location = location});
        } else if (std::regex_search(line, match, object_macro)) {
            const std::string name = match[1].str();
            if (starts_with(name, "_")) {
                continue;
            }
            scan.macros.push_back({.name = name, .function_like = false, .location = location});
            scan.values.push_back({.name = name, .type = "auto", .location = location});
        }
    }
}
NativeHeaderScan dedupe_scan(NativeHeaderScan scan) {
    NativeHeaderScan out;
    std::set<std::string> types;
    std::set<std::string> values;
    std::set<std::string> functions;
    std::set<std::string> macros;
    std::set<std::string> namespaces;
    std::set<std::string> classes;
    for (auto item : scan.types) add_unique(out.types, types, std::move(item));
    for (auto item : scan.values) add_unique(out.values, values, std::move(item));
    for (auto item : scan.functions) add_unique_function(out.functions, functions, std::move(item));
    for (auto item : scan.macros) add_unique(out.macros, macros, std::move(item));
    for (auto item : scan.namespaces) add_unique(out.namespaces, namespaces, std::move(item));
    for (auto item : scan.classes) add_unique(out.classes, classes, std::move(item));
    return out;
}
std::string scan_key(const ImportDecl& import, const NativeHeaderOptions& options,
                     const std::string& flags) {
    return unquoted(import.module_path) + "|" + options.config.cpp_std + "|" + flags;
}
NativeHeaderScan scan_one_header(const ImportDecl& import, const NativeHeaderOptions& options,
                                 const std::string& flags) {
    static std::map<std::string, NativeHeaderScan> cache;
    const std::string key = scan_key(import, options, flags);
    if (const auto found = cache.find(key); found != cache.end()) {
        return found->second;
    }
    NativeHeaderRawCache raw_cache = load_native_header_raw_cache(options, key);
    if (raw_cache.hit) {
        NativeHeaderScan scan;
        parse_ast_dump(scan, raw_cache.ast_dump, import.location);
        parse_macro_dump(scan, raw_cache.macro_dump, import.location);
        cache[key] = dedupe_scan(std::move(scan));
        return cache[key];
    }
    const std::filesystem::path base = temp_base(options.source_dir);
    const std::filesystem::path cpp = base.string() + ".cpp";
    const std::filesystem::path ast = base.string() + ".ast";
    const std::filesystem::path macros = base.string() + ".macros";
    const std::filesystem::path err = base.string() + ".err";
    write_text(cpp, "#include \"" + unquoted(import.module_path) + "\"\nint dudu_probe = 0;\n");

    NativeHeaderScan scan;
    const std::string ast_dump = run_capture(clang_base_command(options, cpp, true), ast, err);
    if (!ast_dump.empty()) {
        parse_ast_dump(scan, ast_dump, import.location);
    }
    const char* clang_env = std::getenv("CLANGXX");
    const std::string clang = clang_env == nullptr ? "clang++" : clang_env;
    const std::string macro_cmd = shell_quote_arg(clang) + " -std=" +
                                  shell_quote_arg(options.config.cpp_std) + " -x c++ -dM -E " +
                                  shell_quote_path(cpp) + flags;
    const std::string macro_dump = run_capture(macro_cmd, macros, err);
    parse_macro_dump(scan, macro_dump, import.location);
    store_native_header_raw_cache(raw_cache, ast_dump, macro_dump);
    std::filesystem::remove(cpp);
    std::filesystem::remove(ast);
    std::filesystem::remove(macros);
    std::filesystem::remove(err);
    cache[key] = dedupe_scan(std::move(scan));
    return cache[key];
}
template <typename T>
void append_unique(std::vector<T>& target, const std::vector<T>& source) {
    std::set<std::string> seen;
    for (const T& item : target) {
        seen.insert(item.name);
    }
    for (const T& item : source) {
        if (seen.insert(item.name).second) {
            target.push_back(item);
        }
    }
}
template <typename T>
std::vector<T> prefixed_names(const std::vector<T>& source, const std::string& prefix) {
    std::vector<T> out;
    out.reserve(source.size());
    for (T item : source) {
        item.name = prefix + "." + item.name;
        out.push_back(std::move(item));
    }
    return out;
}
} // namespace
NativeHeaderScan scan_native_headers(const ModuleAst& module, const NativeHeaderOptions& options) {
    const std::string flags = scanner_flags(options);
    NativeHeaderScan out;
    for (const ImportDecl& import : module.imports) {
        if (!is_foreign(import) || !can_resolve_header(import, options)) {
            continue;
        }
        NativeHeaderScan scan = scan_one_header(import, options, flags);
        append_unique(out.types, scan.types);
        append_unique(out.classes, scan.classes);
        if (direct_import(import)) {
            append_unique(out.values, scan.values);
            append_unique_native_functions(out.functions, scan.functions);
            append_unique(out.macros, scan.macros);
            append_unique(out.namespaces, scan.namespaces);
        } else {
            append_unique(out.values, prefixed_names(scan.values, import.alias));
            append_unique_native_functions(out.functions, prefixed_names(scan.functions, import.alias));
            append_unique(out.macros, prefixed_names(scan.macros, import.alias));
        }
    }
    return out;
}
void merge_native_headers(ModuleAst& module, const NativeHeaderOptions& options) {
    NativeHeaderScan scan = scan_native_headers(module, options);
    append_unique(module.native_types, scan.types);
    append_unique(module.native_values, scan.values);
    append_unique_native_functions(module.native_functions, scan.functions);
    append_unique(module.native_macros, scan.macros);
    append_unique(module.native_namespaces, scan.namespaces);
    append_unique(module.native_classes, scan.classes);
}
std::vector<NativeTypeDecl> scan_native_header_types(const ModuleAst& module,
                                                     const NativeHeaderOptions& options) {
    return scan_native_headers(module, options).types;
}

void merge_native_header_types(ModuleAst& module, const NativeHeaderOptions& options) {
    merge_native_headers(module, options);
}

} // namespace dudu
