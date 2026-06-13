#include "dudu/native_headers.hpp"
#include "dudu/cpp_lower.hpp"
#include "dudu/native_build.hpp"
#include "dudu/native_header_cache.hpp"
#include "dudu/native_header_merge.hpp"
#include "dudu/native_header_scope.hpp"
#include "dudu/native_header_types.hpp"
#include <chrono>
#include <cctype>
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
bool is_simd_vector_type_name(std::string_view name) {
    return name == "__m64" || starts_with(name, "__m128") || starts_with(name, "__m256") ||
           starts_with(name, "__m512");
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
std::filesystem::path temp_base(const std::filesystem::path& source_dir) {
    const auto ticks = std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    return source_dir / (".dudu_native_headers_" + ticks);
}
bool requires_scan_success(const ImportDecl& import) {
    const std::filesystem::path header = unquoted(import.module_path);
    const std::string text = header.string();
    return header.is_absolute() || starts_with(text, ".") || starts_with(text, "/");
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
    const size_t branch = line.find("|-");
    const size_t last = line.find("`-");
    if (branch == std::string::npos) return last == std::string::npos ? 0 : static_cast<int>(last / 2);
    if (last == std::string::npos) return static_cast<int>(branch / 2);
    return static_cast<int>((branch < last ? branch : last) / 2);
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
std::string method_key(const FunctionDecl& fn) {
    std::string key = fn.name + "(";
    for (const ParamDecl& param : fn.params) key += param.type + ",";
    return key + ")->" + fn.return_type;
}
void merge_class(ClassDecl& target, const ClassDecl& source) {
    std::set<std::string> bases(target.base_classes.begin(), target.base_classes.end());
    for (const std::string& base : source.base_classes)
        if (bases.insert(base).second) target.base_classes.push_back(base);
    std::set<std::string> fields;
    for (const FieldDecl& field : target.fields) fields.insert(field.name);
    for (const FieldDecl& field : source.fields)
        if (fields.insert(field.name).second) target.fields.push_back(field);
    std::set<std::string> methods;
    for (const FunctionDecl& method : target.methods) methods.insert(method_key(method));
    for (const FunctionDecl& method : source.methods)
        if (methods.insert(method_key(method)).second) target.methods.push_back(method);
}
void add_unique_class(std::vector<ClassDecl>& out, std::set<std::string>& seen, ClassDecl value) {
    if (seen.insert(value.name).second) {
        out.push_back(std::move(value));
        return;
    }
    for (ClassDecl& klass : out)
        if (klass.name == value.name) merge_class(klass, value);
}
void parse_ast_line(NativeHeaderScan& scan, const std::string& line,
                    std::vector<std::pair<int, std::string>>& namespaces,
                    std::vector<std::pair<int, size_t>>& classes,
                    const SourceLocation& location) {
    static const std::regex typedef_decl(
        R"((TypedefDecl|TypeAliasDecl).*\b([A-Za-z_][A-Za-z0-9_]*) '([^']*)')");
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
    static const std::regex base_decl(R"(\b(public|protected|private) '([^']+)')");
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
    if (line.find("NamespaceDecl") != std::string::npos &&
        std::regex_search(line, match, ns_decl)) {
        const std::string name = match[1].str();
        if (starts_with(name, "__")) {
            return;
        }
        namespaces.push_back({depth, name});
        scan.namespaces.push_back({.name = name, .location = location});
    } else if ((line.find("TypedefDecl") != std::string::npos ||
                line.find("TypeAliasDecl") != std::string::npos) &&
               std::regex_search(line, match, typedef_decl)) {
        const std::string name = match[2].str();
        const std::string raw_type = match[3].str();
        const std::string lowered_type = dudu_type(raw_type);
        const bool useful_alias = lowered_type != raw_type && lowered_type != name;
        if (!starts_with(name, "__") || is_simd_vector_type_name(name) || useful_alias) {
            scan.types.push_back({.name = name,
                                  .type = useful_alias ? lowered_type : "",
                                  .location = location});
        }
    } else if ((line.find("RecordDecl") != std::string::npos ||
                line.find("CXXRecordDecl") != std::string::npos) &&
               std::regex_search(line, match, record_decl)) {
        const std::string raw_name = match[3].str();
        const std::string name = class_name(scan, namespaces, classes, raw_name);
        if (!starts_with(raw_name, "__")) {
            scan.types.push_back({.name = name, .type = "", .location = location});
            if (line.find(" definition") != std::string::npos) {
                ClassDecl klass;
                klass.name = name;
                klass.location = location;
                scan.classes.push_back(std::move(klass));
                classes.push_back({depth, scan.classes.size() - 1});
            }
        }
    } else if (line.find("EnumDecl") != std::string::npos &&
               std::regex_search(line, match, enum_decl)) {
        const std::string name = match[1].str();
        if (!starts_with(name, "__")) {
            scan.types.push_back({.name = name, .type = "", .location = location});
        }
    } else if (line.find("FunctionDecl") != std::string::npos &&
               std::regex_search(line, match, fn_decl)) {
        const std::string name = join_scope(namespaces, match[1].str());
        if (starts_with(name, "__")) {
            return;
        }
        const std::string signature = match[2].str();
        scan.functions.push_back({.name = name,
                                  .params = qualify_scoped_types(
                                      scan, namespaces, signature_params(signature)),
                                  .return_type = qualify_scoped_type(
                                      scan, namespaces, signature_return_type(signature)),
                                  .variadic = signature.find("...") != std::string::npos,
                                  .location = location});
    } else if (!classes.empty() && line.find("CXXMethodDecl") != std::string::npos &&
               std::regex_search(line, match, method_decl)) {
        FunctionDecl method;
        method.name = match[1].str();
        method.return_type =
            qualify_scoped_type(scan, namespaces, signature_return_type(match[2].str()));
        for (const std::string& param : signature_params(match[2].str())) {
            ParamDecl decl;
            decl.name = "arg" + std::to_string(method.params.size());
            decl.type = qualify_scoped_type(scan, namespaces, param);
            decl.location = location;
            method.params.push_back(std::move(decl));
        }
        method.location = location;
        scan.classes[classes.back().second].methods.push_back(std::move(method));
    } else if (!classes.empty() && line.find("CXXConstructorDecl") != std::string::npos &&
               std::regex_search(line, match, ctor_decl)) {
        const std::vector<std::string> params =
            qualify_scoped_types(scan, namespaces, signature_params(match[2].str()));
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
    } else if (!classes.empty() && line.find("FieldDecl") != std::string::npos &&
               std::regex_search(line, match, field_decl)) {
        scan.classes[classes.back().second].fields.push_back(
            {.name = match[1].str(),
             .type = qualify_scoped_type(scan, namespaces, dudu_type(match[2].str())),
             .location = location});
    } else if (!classes.empty() &&
               (line.find("public '") != std::string::npos ||
                line.find("protected '") != std::string::npos ||
                line.find("private '") != std::string::npos) &&
               std::regex_search(line, match, base_decl)) {
        scan.classes[classes.back().second].base_classes.push_back(
            qualify_scoped_type(scan, namespaces, dudu_type(match[2].str())));
    } else if (line.find("EnumConstantDecl") != std::string::npos &&
               std::regex_search(line, match, enum_value_decl)) {
        const std::string name = match[1].str();
        if (!starts_with(name, "__")) {
            scan.values.push_back({.name = name, .type = "i32", .location = location});
        }
    } else if (line.find("VarDecl") != std::string::npos &&
               line.find("ParmVarDecl") == std::string::npos &&
               std::regex_search(line, match, var_decl)) {
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
struct MacroParams { int arity = 0; bool variadic = false; };
MacroParams macro_params(std::string args) {
    args = trim_copy(std::move(args));
    if (args.empty()) return {};
    MacroParams out;
    for (std::string part : split_top_level_args(args)) {
        part = trim_copy(std::move(part));
        if (part == "..." || part.find("...") != std::string::npos || part == "__VA_ARGS__")
            out.variadic = true;
        else
            ++out.arity;
    }
    return out;
}
void parse_macro_dump(NativeHeaderScan& scan, const std::string& dump,
                      const SourceLocation& location) {
    static const std::regex function_macro(
        R"(^#define ([A-Za-z_][A-Za-z0-9_]*)\(([^)]*)\))");
    static const std::regex simd_function_macro(R"(^#define ((_mm|_MM_)[A-Za-z0-9_]*)\(([^)]*)\))");
    static const std::regex object_macro(R"(^#define ([A-Z_][A-Z0-9_]*)(\s|$))");
    std::istringstream in(dump);
    std::string line;
    while (std::getline(in, line)) {
        std::smatch match;
        bool is_function_macro = std::regex_search(line, match, function_macro);
        const bool is_simd_macro =
            is_function_macro ? false : std::regex_search(line, match, simd_function_macro);
        if (is_function_macro || is_simd_macro) {
            const std::string name = match[1].str();
            if (starts_with(name, "_") && !starts_with(name, "_mm") && !starts_with(name, "_MM_")) {
                continue;
            }
            const MacroParams params = macro_params(match[is_simd_macro ? 3 : 2].str());
            scan.macros.push_back({.name = name,
                                   .arity = params.arity,
                                   .function_like = true,
                                   .location = location});
            scan.functions.push_back({.name = name,
                                      .params = std::vector<std::string>(
                                          static_cast<size_t>(params.arity), "auto"),
                                      .return_type = "auto",
                                      .variadic = params.variadic,
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
bool public_direct_macro_name(const std::string& name) {
    return !name.empty() && (std::isupper(static_cast<unsigned char>(name.front())) != 0 ||
                             name.front() == '_');
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
    for (auto item : scan.classes) add_unique_class(out.classes, classes, std::move(item));
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
    const auto cleanup = [&]() {
        std::filesystem::remove(cpp);
        std::filesystem::remove(ast);
        std::filesystem::remove(macros);
        std::filesystem::remove(err);
    };
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
    if (ast_dump.empty() && macro_dump.empty()) {
        if (!requires_scan_success(import)) {
            cleanup();
            return {};
        }
        const std::string detail = read_text(err);
        cleanup();
        throw CompileError(import.location, "could not scan native header " +
                                                unquoted(import.module_path) +
                                                (detail.empty() ? "" : "\n" + detail));
    }
    parse_macro_dump(scan, macro_dump, import.location);
    store_native_header_raw_cache(raw_cache, ast_dump, macro_dump);
    cleanup();
    cache[key] = dedupe_scan(std::move(scan));
    return cache[key];
}
template <typename T>
void append_unique(std::vector<T>& target, const std::vector<T>& source) {
    std::set<std::string> seen;
    for (const T& item : target) seen.insert(item.name);
    for (const T& item : source)
        if (seen.insert(item.name).second) target.push_back(item);
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
std::vector<NativeMacroDecl> direct_macros(const std::vector<NativeMacroDecl>& source) {
    std::vector<NativeMacroDecl> out;
    for (const NativeMacroDecl& item : source) if (public_direct_macro_name(item.name)) out.push_back(item);
    return out;
}
} // namespace
NativeHeaderScan scan_native_headers(const ModuleAst& module, const NativeHeaderOptions& options) {
    const std::string flags = scanner_flags(options);
    NativeHeaderScan out;
    for (const ImportDecl& import : module.imports) {
        if (!is_foreign(import)) {
            continue;
        }
        NativeHeaderScan scan = scan_one_header(import, options, flags);
        if (direct_import(import)) {
            append_unique(out.types, scan.types);
            append_unique(out.classes, scan.classes);
            append_unique(out.values, scan.values);
            append_unique_native_functions(out.functions, scan.functions);
            append_unique(out.macros, direct_macros(scan.macros));
            append_unique(out.namespaces, scan.namespaces);
        } else {
            append_unique(out.types, scan.types);
            append_unique(out.classes, scan.classes);
            append_unique(out.types, prefixed_names(scan.types, import.alias));
            append_unique(out.classes, prefixed_names(scan.classes, import.alias));
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
