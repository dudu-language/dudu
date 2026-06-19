#include "dudu/language_server_diagnostics.hpp"

#include "dudu/cpp_lower.hpp"
#include "dudu/language_server_ast_lints.hpp"
#include "dudu/language_server_json.hpp"
#include "dudu/language_server_support.hpp"
#include "dudu/module_loader.hpp"
#include "dudu/native_build.hpp"
#include "dudu/native_headers.hpp"
#include "dudu/parser.hpp"
#include "dudu/sema.hpp"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <optional>
#include <sstream>
#include <string_view>

namespace dudu {
namespace {

std::string diagnostic_source(std::string_view code) {
    if (code == "dudu.native_header.scan_failed") {
        return "dudu/native-header";
    }
    if (code.starts_with("dudu.sema.")) {
        return "dudu/sema";
    }
    if (code.starts_with("dudu.parser.") || code.starts_with("dudu.lexer.")) {
        return "dudu/parser";
    }
    return "dudu/compiler";
}

std::optional<std::string> missing_pkg_config_package(const ProjectConfig& config) {
    const char* pkg_config = std::getenv("PKG_CONFIG");
    const std::string executable = pkg_config == nullptr ? "pkg-config" : std::string(pkg_config);
    for (const std::string& package : config.pkg_config_packages) {
        const std::string command =
            shell_quote_arg(executable) + " --exists " + shell_quote_arg(package) + " 2>/dev/null";
        if (std::system(command.c_str()) != 0) {
            return package;
        }
    }
    return std::nullopt;
}

bool has_module_imports(const ModuleAst& module) {
    return std::any_of(module.imports.begin(), module.imports.end(), [](const ImportDecl& import) {
        return import.kind == ImportKind::Module || import.kind == ImportKind::From;
    });
}

} // namespace

std::vector<Diagnostic> diagnostics_for_document(const Document& doc) {
    try {
        ProjectConfig config;
        try {
            config = config_for_file(doc.path);
        } catch (const std::exception& error) {
            return {{.location = {.file = doc.path, .line = 1, .column = 1},
                     .message = error.what(),
                     .source = "dudu/build-config",
                     .severity = 1,
                     .code = "",
                     .data_name = ""}};
        }
        if (const std::optional<std::string> missing = missing_pkg_config_package(config)) {
            return {{.location = {.file = doc.path, .line = 1, .column = 1},
                     .message = "missing pkg-config package: " + *missing,
                     .source = "dudu/build-config",
                     .severity = 1,
                     .code = "",
                     .data_name = ""}};
        }
        ModuleAst parsed = parse_source(doc.text, doc.path);
        const bool saved_tree =
            std::filesystem::exists(doc.path) && source_tree_files(doc.path).size() > 1;
        const bool project_tree = saved_tree || has_module_imports(parsed);
        ModuleAst module = project_tree && std::filesystem::exists(doc.path)
                               ? load_source_tree(doc.path, doc.text)
                               : std::move(parsed);
        module.build_values = config.build_values;
        module.build_values["TARGET_KIND"] = '"' + config.target_kind + '"';
        module.build_values["TARGET_MODE"] = '"' + config.target_mode + '"';
        module.target_mode_explicit = config.target_mode_explicit;
        const NativeHeaderOptions native_options{.config = config,
                                                 .source_dir = doc.path.parent_path()};
        merge_native_header_types(module, native_options);
        for (ModuleAst& unit : module.module_units) {
            unit.build_values = module.build_values;
            unit.target_mode_explicit = module.target_mode_explicit;
            merge_native_header_types(unit, native_options);
        }
        if (project_tree || config.build_backend == "cmake") {
            analyze_module_tree(module, {.check_bodies = true});
        } else {
            analyze_module(module, {.check_bodies = true});
        }
        return ast_lint_diagnostics(module, doc);
    } catch (const CompileError& error) {
        return {{.location = error.location(),
                 .message = error.what(),
                 .source = diagnostic_source(error.code()),
                 .severity = 1,
                 .code = error.code(),
                 .data_name = error.data_name()}};
    } catch (const std::exception& error) {
        return {{.location = {.file = doc.path, .line = 1, .column = 1},
                 .message = error.what(),
                 .source = "dudu/lsp",
                 .severity = 1,
                 .code = "",
                 .data_name = ""}};
    }
}

std::string diagnostic_json(const Diagnostic& diagnostic) {
    const int line = std::max(0, diagnostic.location.line - 1);
    const int column = std::max(0, diagnostic.location.column - 1);
    std::ostringstream out;
    out << "{\"range\":{\"start\":{\"line\":" << line << ",\"character\":" << column
        << "},\"end\":{\"line\":" << line << ",\"character\":" << (column + 1)
        << "}},\"severity\":" << diagnostic.severity << ",\"source\":\""
        << json_escape(diagnostic.source) << "\"";
    if (!diagnostic.code.empty()) {
        out << ",\"code\":\"" << json_escape(diagnostic.code) << "\"";
    }
    if (!diagnostic.data_name.empty()) {
        out << ",\"data\":{\"name\":\"" << json_escape(diagnostic.data_name) << "\"}";
    }
    out << ",\"message\":\"" << json_escape(diagnostic.message) << "\"}";
    return out.str();
}

} // namespace dudu
